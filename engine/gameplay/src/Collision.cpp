#include "rolo/gameplay/Collision.hpp"

#include <algorithm>
#include <cmath>

namespace rolo {

namespace {

// How far apart counts as "touching" rather than "overlapping", in points.
// After a move ends exactly against a wall, rounding can leave the boxes a
// hair inside each other; without this tolerance the wall would then count
// as "already overlapping" and stop blocking.
constexpr float kTouch = 1e-3f;

bool overlapsDeeply(const Box& a, const Box& b)
{
    return a.min.x < b.max.x - kTouch && b.min.x < a.max.x - kTouch && a.min.y < b.max.y - kTouch
           && b.min.y < a.max.y - kTouch;
}

bool isActiveInScene(const Scene& scene, const Entity& entity)
{
    for (const Entity* e = &entity; e != nullptr; e = scene.find(e->parent())) {
        if (!e->isActive()) {
            return false;
        }
    }
    return true;
}

Box boundsOf(const std::array<Vec2, 4>& corners)
{
    Box b{corners[0], corners[0]};
    for (const Vec2& c : corners) {
        b.min = {std::min(b.min.x, c.x), std::min(b.min.y, c.y)};
        b.max = {std::max(b.max.x, c.x), std::max(b.max.y, c.y)};
    }
    return b;
}

Box unite(const Box& a, const Box& b)
{
    return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y)}, {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y)}};
}

// Something that blocks movement.
struct Solid {
    Box box;
    EntityId entity;   // null for tiles
};

std::vector<Solid> solidsIn(const Scene& scene, const AssetDatabase* assets, const Box& area, EntityId ignore)
{
    std::vector<Solid> out;
    scene.forEach([&](const Entity& entity, int) {
        const Collider* collider = entity.get<Collider>();
        if (collider == nullptr || !collider->solid || (!ignore.isNull() && scene.isAncestorOrSelf(ignore, entity.id()))) {
            return;
        }
        if (const auto box = colliderBox(scene, entity.id()); box && box->overlaps(area)) {
            out.push_back({*box, entity.id()});
        }
    });
    for (const Box& tile : solidTiles(scene, assets, area)) {
        out.push_back({tile, {}});
    }
    return out;
}

// One axis of a sweep: how far `box` can go along `axis` (0 = x, 1 = y).
float sweepAxis(const Box& box, float delta, int axis, const std::vector<Solid>& solids, EntityId& hit, bool& blocked)
{
    const auto lo = [axis](const Box& b) { return axis == 0 ? b.min.x : b.min.y; };
    const auto hi = [axis](const Box& b) { return axis == 0 ? b.max.x : b.max.y; };
    const auto otherLo = [axis](const Box& b) { return axis == 0 ? b.min.y : b.min.x; };
    const auto otherHi = [axis](const Box& b) { return axis == 0 ? b.max.y : b.max.x; };

    float allowed = delta;
    for (const Solid& s : solids) {
        if (overlapsDeeply(box, s.box)) {
            continue; // already inside it: let it get out
        }
        // Side by side on the other axis (touching does not count, so it
        // can slide along a floor or wall).
        if (!(otherLo(box) < otherHi(s.box) - kTouch && otherLo(s.box) < otherHi(box) - kTouch)) {
            continue;
        }
        if (delta > 0.0f && lo(s.box) >= hi(box) - kTouch) {
            const float gap = std::max(0.0f, lo(s.box) - hi(box));
            if (gap < allowed) {
                allowed = gap;
                hit = s.entity;
                blocked = true;
            }
        } else if (delta < 0.0f && hi(s.box) <= lo(box) + kTouch) {
            const float gap = std::min(0.0f, hi(s.box) - lo(box));
            if (gap > allowed) {
                allowed = gap;
                hit = s.entity;
                blocked = true;
            }
        }
    }
    return allowed;
}

} // namespace

std::optional<Box> colliderBox(const Scene& scene, EntityId id)
{
    const Entity* e = scene.find(id);
    const Collider* c = e != nullptr ? e->get<Collider>() : nullptr;
    if (c == nullptr || !isActiveInScene(scene, *e)) {
        return std::nullopt;
    }
    const Affine2 world = scene.worldTransform(id);
    float hw = c->size.x * 0.5f;
    float hh = c->size.y * 0.5f;
    if (c->shape == ColliderShape::Circle) {
        hw = hh = std::min(hw, hh);   // the square around the circle
    }
    const Vec2 o = c->offset;
    return boundsOf({world.apply({o.x - hw, o.y - hh}), world.apply({o.x + hw, o.y - hh}),
                     world.apply({o.x + hw, o.y + hh}), world.apply({o.x - hw, o.y + hh})});
}

std::vector<EntityId> overlappingColliders(const Scene& scene, const Box& box, EntityId ignore)
{
    std::vector<EntityId> out;
    scene.forEach([&](const Entity& entity, int) {
        if (entity.get<Collider>() == nullptr || (!ignore.isNull() && scene.isAncestorOrSelf(ignore, entity.id()))) {
            return;
        }
        if (const auto b = colliderBox(scene, entity.id()); b && overlapsDeeply(*b, box)) {
            out.push_back(entity.id());
        }
    });
    return out;
}

std::vector<Box> solidTiles(const Scene& scene, const AssetDatabase* assets, const Box& area)
{
    std::vector<Box> out;
    scene.forEach([&](const Entity& entity, int) {
        const Tilemap* map = entity.get<Tilemap>();
        if (map == nullptr || !isActiveInScene(scene, entity)) {
            return;
        }
        const auto used = map->usedCells();
        if (!used) {
            return;
        }
        // Tilemaps are assumed unrotated: their origin and scale place the grid.
        const Affine2 world = scene.worldTransform(entity.id());
        const Vec2 origin{world.tx, world.ty};
        const float cw = map->cellSize.x * world.a;
        const float ch = map->cellSize.y * world.d;
        if (std::abs(cw) < 1e-6f || std::abs(ch) < 1e-6f) {
            return;
        }
        const AssetRecord* record = assets != nullptr ? assets->find(map->tileset) : nullptr;
        const Tileset* tileset = record != nullptr && record->tileset ? &*record->tileset : nullptr;

        // The cells the area covers (either way round, for negative scales),
        // limited to the cells the map uses.
        const float x0 = (area.min.x - origin.x) / cw;
        const float x1 = (area.max.x - origin.x) / cw;
        const float y0 = (area.min.y - origin.y) / ch;
        const float y1 = (area.max.y - origin.y) / ch;
        const auto cellLo = [](float a, float b, int limit) {
            return std::max(limit, static_cast<int>(std::floor(std::max(-1e9f, std::min(a, b)))));
        };
        const auto cellHi = [](float a, float b, int limit) {
            return std::min(limit, static_cast<int>(std::floor(std::min(1e9f, std::max(a, b)))));
        };
        const int cx0 = cellLo(x0, x1, used->min.x);
        const int cx1 = cellHi(x0, x1, used->max.x);
        const int cy0 = cellLo(y0, y1, used->min.y);
        const int cy1 = cellHi(y0, y1, used->max.y);
        for (int y = cy0; y <= cy1; ++y) {
            for (int x = cx0; x <= cx1; ++x) {
                if (!isTileSolid(*map, tileset, {x, y})) {
                    continue;
                }
                const float ax = origin.x + static_cast<float>(x) * cw;
                const float ay = origin.y + static_cast<float>(y) * ch;
                const Box cell{{std::min(ax, ax + cw), std::min(ay, ay + ch)}, {std::max(ax, ax + cw), std::max(ay, ay + ch)}};
                if (cell.overlaps(area)) {
                    out.push_back(cell);
                }
            }
        }
    });
    return out;
}

bool isBlocked(const Scene& scene, const AssetDatabase* assets, const Box& box, EntityId ignore)
{
    for (const Solid& s : solidsIn(scene, assets, box, ignore)) {
        if (overlapsDeeply(s.box, box)) {
            return true;
        }
    }
    return false;
}

MoveResult sweepBox(const Scene& scene, const AssetDatabase* assets, const Box& box, Vec2 delta, EntityId ignore)
{
    MoveResult result;
    if (delta.x == 0.0f && delta.y == 0.0f) {
        return result;
    }
    // Everything solid anywhere the box could reach this move.
    const std::vector<Solid> solids = solidsIn(scene, assets, unite(box, box.moved(delta)), ignore);

    Box b = box;
    if (delta.x != 0.0f) {
        result.moved.x = sweepAxis(b, delta.x, 0, solids, result.hit, result.blockedX);
        b = b.moved({result.moved.x, 0.0f});
    }
    if (delta.y != 0.0f) {
        result.moved.y = sweepAxis(b, delta.y, 1, solids, result.hit, result.blockedY);
    }
    return result;
}

} // namespace rolo
