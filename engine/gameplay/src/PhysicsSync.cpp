#include "PhysicsSync.hpp"

#include "rolo/gameplay/Collision.hpp"
#include "rolo/scene/Components.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace rolo {

namespace {

constexpr float kDegrees = 57.29577951f;
// Floors, walls and colliders without a RigidBody: a little grip, no bounce.
constexpr float kGroundFriction = 0.6f;

bool activeInScene(const Scene& scene, const Entity& entity)
{
    for (const Entity* e = &entity; e != nullptr; e = e->parent().isNull() ? nullptr : scene.find(e->parent())) {
        if (!e->isActive()) {
            return false;
        }
    }
    return true;
}

// A world transform taken apart: where, turned how far (clockwise), how large.
struct Placement {
    Vec2 position;
    float angle = 0.0f;
    Vec2 scale{1.0f, 1.0f};
};

Placement placementOf(const Affine2& world)
{
    Placement p;
    p.position = {world.tx, world.ty};
    p.angle = -std::atan2(world.b, world.a) * kDegrees;
    p.scale = {std::hypot(world.a, world.b), std::hypot(world.c, world.d)};
    return p;
}

void mix(std::size_t& seed, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    seed ^= std::hash<std::uint32_t>{}(bits) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
}

// What a body is made from: when any of it changes, the body is made again.
std::size_t signatureOf(const Collider& c, const RigidBody* rb, Vec2 scale)
{
    std::size_t seed = 0;
    for (const float v : {c.size.x, c.size.y, c.offset.x, c.offset.y, c.solid ? 1.0f : 0.0f,
                          static_cast<float>(c.shape), scale.x, scale.y}) {
        mix(seed, v);
    }
    if (rb != nullptr) {
        for (const float v : {static_cast<float>(rb->type), rb->mass, rb->gravityScale, rb->friction, rb->bounciness,
                              rb->drag, rb->lockRotation ? 1.0f : 0.0f, rb->fast ? 1.0f : 0.0f}) {
            mix(seed, v);
        }
    } else {
        mix(seed, -1.0f);
    }
    return seed;
}

bool near(Vec2 a, Vec2 b)
{
    return std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f;
}

// Joins boxes that share a whole edge: first along rows, then rows of the
// same width into blocks. Fewer, larger boxes mean no seams to catch on.
std::vector<Box> mergeBoxes(std::vector<Box> cells)
{
    const auto same = [](float a, float b) { return std::abs(a - b) < 0.01f; };
    std::sort(cells.begin(), cells.end(), [](const Box& l, const Box& r) {
        return l.min.y != r.min.y ? l.min.y < r.min.y : l.min.x < r.min.x;
    });
    std::vector<Box> rows;
    for (const Box& cell : cells) {
        if (!rows.empty() && same(rows.back().min.y, cell.min.y) && same(rows.back().max.y, cell.max.y)
            && same(rows.back().max.x, cell.min.x)) {
            rows.back().max.x = cell.max.x;
        } else {
            rows.push_back(cell);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Box& l, const Box& r) {
        return l.min.x != r.min.x ? l.min.x < r.min.x : (l.max.x != r.max.x ? l.max.x < r.max.x : l.min.y < r.min.y);
    });
    std::vector<Box> blocks;
    for (const Box& row : rows) {
        if (!blocks.empty() && same(blocks.back().min.x, row.min.x) && same(blocks.back().max.x, row.max.x)
            && same(blocks.back().max.y, row.min.y)) {
            blocks.back().max.y = row.max.y;
        } else {
            blocks.push_back(row);
        }
    }
    return blocks;
}

} // namespace

PhysicsSync::PhysicsSync(const AssetDatabase* assets)
    : m_assets(assets)
{
}

void PhysicsSync::reset()
{
    m_world.reset();
    m_bodies.clear();
    m_accumulator = 0.0f;
    m_hits.clear();
}

BodyHandle PhysicsSync::bodyOf(EntityId id) const
{
    const auto found = m_bodies.find(id);
    return found != m_bodies.end() ? found->second.body : BodyHandle{};
}

void PhysicsSync::buildTiles(const Scene& scene)
{
    const float far = 1.0e8f;
    const std::vector<Box> blocks = mergeBoxes(solidTiles(scene, m_assets, Box{{-far, -far}, {far, far}}));
    if (blocks.empty()) {
        return;
    }
    const BodyHandle tiles = m_world->createBody({.type = BodyType::Static});
    for (const Box& block : blocks) {
        m_world->addShape(tiles, {.shape = ColliderShape::Box, .size = block.size(), .offset = block.center(),
                                  .friction = kGroundFriction});
    }
}

void PhysicsSync::sync(Scene& scene)
{
    if (!m_world) {
        // Physics starts with the first RigidBody (a scene may get one later).
        if (scene.all<RigidBody>().empty()) {   // the scene's own list: no walk through every object
            return;
        }
        m_world = std::make_unique<PhysicsWorld>(scene.gravity());
        buildTiles(scene);
    }
    if (!near(m_world->gravity(), scene.gravity())) {
        m_world->setGravity(scene.gravity());
    }

    std::map<EntityId, Tracked> kept;
    // Only the objects with a Collider (the scene keeps that list), not every object.
    for (const EntityId colliderId : scene.all<Collider>()) {
        const Entity& entity = *scene.find(colliderId);
        const Collider* collider = entity.get<Collider>();
        const RigidBody* rb = entity.get<RigidBody>();
        if (collider == nullptr || (rb == nullptr && !collider->solid) || !activeInScene(scene, entity)) {
            continue;
        }
        const Placement at = placementOf(scene.worldTransform(entity.id()));
        const std::size_t signature = signatureOf(*collider, rb, at.scale);
        Tracked tracked;
        Vec2 keepVelocity;
        float keepSpin = 0.0f;
        if (const auto old = m_bodies.find(entity.id()); old != m_bodies.end()) {
            tracked = old->second;
            m_bodies.erase(old);
            if (tracked.signature != signature) {
                // Changed (bigger, another shape, heavier...): made again, moving as it was.
                keepVelocity = m_world->velocity(tracked.body);
                keepSpin = m_world->angularVelocity(tracked.body);
                m_world->destroyBody(tracked.body);
                tracked.body = {};
            }
        }
        if (tracked.body.isNull()) {
            BodySettings body;
            body.type = rb != nullptr ? rb->type : BodyType::Static;
            body.position = at.position;
            body.angle = at.angle;
            body.user = entity.id().value();
            if (rb != nullptr) {
                body.gravityScale = rb->gravityScale;
                body.drag = rb->drag;
                body.lockRotation = rb->lockRotation;
                body.fast = rb->fast;
            }
            tracked.body = m_world->createBody(body);
            ShapeSettings shape;
            shape.shape = collider->shape;
            shape.size = {collider->size.x * at.scale.x, collider->size.y * at.scale.y};
            shape.offset = {collider->offset.x * at.scale.x, collider->offset.y * at.scale.y};
            shape.mass = rb != nullptr ? rb->mass : 1.0f;
            shape.friction = rb != nullptr ? rb->friction : kGroundFriction;
            shape.bounciness = rb != nullptr ? rb->bounciness : 0.0f;
            shape.sensor = !collider->solid;
            m_world->addShape(tracked.body, shape);
            m_world->setVelocity(tracked.body, keepVelocity);
            m_world->setAngularVelocity(tracked.body, keepSpin);
            tracked.signature = signature;
            tracked.follows = rb == nullptr;
            tracked.moving = false;
            tracked.position = at.position;   // where it starts: moving away from here is "moved by code"
            tracked.angle = at.angle;
            tracked.writesBack = rb != nullptr && rb->type != BodyType::Static;
        } else if (!near(at.position, tracked.position) || std::abs(at.angle - tracked.angle) > 0.01f) {
            // Moved by code since physics last placed it.
            if (!tracked.follows) {
                m_world->setTransform(tracked.body, at.position, at.angle);   // put where the code put it
            } else if (!tracked.moving) {
                // Code moves it: from now on it is steered there in step().
                m_world->setType(tracked.body, BodyType::Kinematic);
                tracked.moving = true;
            }
        }
        if (!tracked.follows) {
            tracked.position = at.position;
            tracked.angle = at.angle;
        }
        kept.emplace(entity.id(), tracked);
    }
    // Objects removed, switched off, or without their Collider: their bodies go.
    for (const auto& [id, tracked] : m_bodies) {
        m_world->destroyBody(tracked.body);
    }
    m_bodies = std::move(kept);
}

void PhysicsSync::step(Scene& scene, float dt)
{
    m_hits.clear();
    if (!m_world) {
        return;
    }
    sync(scene);   // objects behaviours added, moved or removed this frame
    m_accumulator += std::max(0.0f, dt);
    int steps = 0;
    while (m_accumulator >= kStep && steps < kMaxStepsPerFrame) {
        // Bodies following their Transform glide there during the step, so
        // they push what is in the way instead of jumping into it.
        for (auto& [id, tracked] : m_bodies) {
            if (tracked.follows && tracked.moving) {
                const Placement at = placementOf(scene.worldTransform(id));
                m_world->moveTo(tracked.body, at.position, at.angle, kStep);
                tracked.position = at.position;
                tracked.angle = at.angle;
            }
        }
        m_world->step(kStep);
        for (const BodyHit& hit : m_world->hits()) {
            if (hit.a != 0 && hit.b != 0) {
                m_hits.emplace_back(EntityId{hit.a}, EntityId{hit.b});
            }
        }
        m_accumulator -= kStep;
        ++steps;
    }
    if (steps == kMaxStepsPerFrame) {
        m_accumulator = 0.0f;   // far behind (a long frame): drop the rest rather than run ever slower
    }
    if (steps > 0) {
        writeBack(scene);
    }
}

void PhysicsSync::writeBack(Scene& scene)
{
    for (auto& [id, tracked] : m_bodies) {
        if (!tracked.writesBack) {
            continue;
        }
        const Entity* entity = scene.find(id);
        if (entity == nullptr) {
            continue;
        }
        const Vec2 position = m_world->position(tracked.body);
        const float angle = m_world->angle(tracked.body);
        // The Transform is relative to the parent, if there is one.
        Vec2 local = position;
        float localAngle = angle;
        if (const Entity* parent = scene.find(entity->parent())) {
            const Affine2 parentWorld = scene.worldTransform(parent->id());
            if (const auto inverse = parentWorld.inverse()) {
                local = inverse->apply(position);
            }
            localAngle = angle - placementOf(parentWorld).angle;
        }
        // Only a body that moved changes its Transform (resting bodies cost nothing to draw).
        const Transform* now = entity->get<Transform>();
        if (now != nullptr && (!near(now->position, local) || std::abs(now->rotation - localAngle) > 0.001f)) {
            Transform* transform = scene.edit<Transform>(id);
            transform->position = local;
            transform->rotation = localAngle;
        }
        tracked.position = position;
        tracked.angle = angle;
    }
}

} // namespace rolo
