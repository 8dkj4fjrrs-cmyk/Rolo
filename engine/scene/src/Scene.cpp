#include "rolo/scene/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace rolo {

// --- Entity ----------------------------------------------------------------------------

const Component* Entity::component(std::string_view typeId) const
{
    // An unknown component (see UnknownComponent) answers to the type it had
    // in the file. All of them share one ComponentType, so that is a pointer
    // comparison, not a dynamic_cast for every component looked at.
    const ComponentType& unknownType = UnknownComponent::unknownType();
    for (const auto& c : m_components) {
        const ComponentType& type = c->type();
        if (&type == &unknownType) {
            if (static_cast<const UnknownComponent&>(*c).originalTypeId() == typeId || type.id == typeId) {
                return c.get();
            }
        } else if (type.id == typeId) {
            return c.get();
        }
    }
    return nullptr;
}

Component* Entity::component(std::string_view typeId)
{
    return const_cast<Component*>(static_cast<const Entity&>(*this).component(typeId));
}

// --- Scene: construction ---------------------------------------------------------------

Scene::Scene(std::string name)
    : m_name(std::move(name))
{
}

Scene::Scene(std::string name, EntityIdGenerator generator)
    : m_name(std::move(name))
    , m_ids(std::move(generator))
{
}

Scene::Scene(const Scene& other)
    : m_name(other.m_name)
    , m_gravity(other.m_gravity)
    , m_roots(other.m_roots)
    , m_ids(other.m_ids)
    , m_revision(other.m_revision)
    , m_contentRevision(other.m_contentRevision)
    , m_structureRevision(other.m_structureRevision)
{
    for (const auto& [id, entity] : other.m_entities) {
        auto copy = std::make_unique<Entity>();
        copy->m_id = entity->m_id;
        copy->m_name = entity->m_name;
        copy->m_active = entity->m_active;
        copy->m_parent = entity->m_parent;
        copy->m_children = entity->m_children;
        for (const auto& c : entity->m_components) {
            copy->m_components.push_back(c->clone());
        }
        m_entities.emplace(id, std::move(copy));
    }
}

Scene& Scene::operator=(const Scene& other)
{
    if (this != &other) {
        Scene copy(other);
        *this = std::move(copy);
    }
    return *this;
}

void Scene::setName(std::string name)
{
    m_name = std::move(name);
    markChanged();
}

void Scene::setGravity(Vec2 gravity)
{
    if (std::isfinite(gravity.x) && std::isfinite(gravity.y) && gravity != m_gravity) {
        m_gravity = gravity;
        markChanged();
    }
}

// --- Reading ---------------------------------------------------------------------------

const Entity* Scene::find(EntityId id) const
{
    const auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : it->second.get();
}

Entity* Scene::findMutable(EntityId id)
{
    const auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : it->second.get();
}

const std::vector<EntityId>& Scene::childrenOf(EntityId parent) const
{
    if (parent.isNull()) {
        return m_roots;
    }
    static const std::vector<EntityId> none;
    const Entity* p = find(parent);
    return p != nullptr ? p->m_children : none;
}

std::vector<EntityId>* Scene::siblingsOf(EntityId parent)
{
    if (parent.isNull()) {
        return &m_roots;
    }
    Entity* p = findMutable(parent);
    return p != nullptr ? &p->m_children : nullptr;
}

std::optional<std::size_t> Scene::indexOf(EntityId id) const
{
    const Entity* e = find(id);
    if (e == nullptr) {
        return std::nullopt;
    }
    const auto& siblings = childrenOf(e->m_parent);
    const auto it = std::find(siblings.begin(), siblings.end(), id);
    if (it == siblings.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - siblings.begin());
}

bool Scene::isAncestorOrSelf(EntityId ancestor, EntityId id) const
{
    for (const Entity* e = find(id); e != nullptr; e = find(e->m_parent)) {
        if (e->m_id == ancestor) {
            return true;
        }
    }
    return false;
}

void Scene::forEach(const std::function<void(const Entity&, int depth)>& visit) const
{
    // Iterative depth-first walk (no recursion limit for deep trees).
    std::vector<std::pair<EntityId, int>> stack;
    for (auto it = m_roots.rbegin(); it != m_roots.rend(); ++it) {
        stack.emplace_back(*it, 0);
    }
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        const Entity* e = find(id);
        if (e == nullptr) {
            continue;
        }
        visit(*e, depth);
        for (auto it = e->m_children.rbegin(); it != e->m_children.rend(); ++it) {
            stack.emplace_back(*it, depth + 1);
        }
    }
}

// --- Finding by kind or name -------------------------------------------------------------

const Scene::Index& Scene::index() const
{
    if (m_index.structureRevision == m_structureRevision) {
        return m_index;
    }
    Index& made = m_index;
    made.order.clear();
    made.byName.clear();
    for (auto& [type, list] : made.byType) {
        list.clear();   // kept (emptied), so asking for a type that is gone gives an empty list
    }
    made.order.reserve(m_entities.size());
    const ComponentType& unknownType = UnknownComponent::unknownType();
    forEach([&](const Entity& entity, int) {
        made.order.push_back(entity.id());
        made.byName.try_emplace(entity.name(), entity.id());   // the first with a name wins
        for (const auto& c : entity.components()) {
            const ComponentType& type = c->type();
            const std::string& id = &type == &unknownType
                                        ? static_cast<const UnknownComponent&>(*c).originalTypeId()
                                        : type.id;
            auto found = made.byType.find(id);
            if (found == made.byType.end()) {
                found = made.byType.emplace(id, std::vector<EntityId>{}).first;
            }
            found->second.push_back(entity.id());
        }
    });
    made.structureRevision = m_structureRevision;
    return made;
}

const std::vector<EntityId>& Scene::entitiesWith(std::string_view componentType) const
{
    static const std::vector<EntityId> kNone;
    const Index& made = index();
    const auto found = made.byType.find(componentType);
    return found != made.byType.end() ? found->second : kNone;
}

const std::vector<EntityId>& Scene::inOrder() const
{
    return index().order;
}

EntityId Scene::findByName(std::string_view name) const
{
    const Index& made = index();
    const auto found = made.byName.find(name);
    return found != made.byName.end() ? found->second : EntityId{};
}

// --- Changing ---------------------------------------------------------------------------

EntityId Scene::newId()
{
    EntityId id = m_ids.next();
    while (contains(id)) {
        id = m_ids.next();
    }
    return id;
}

Entity* Scene::create(std::string name, EntityId parent, std::optional<std::size_t> index, EntityId id)
{
    std::vector<EntityId>* siblings = siblingsOf(parent);
    if (siblings == nullptr) {
        return nullptr;
    }
    if (id.isNull()) {
        id = newId();
    } else if (contains(id)) {
        return nullptr;
    }
    auto entity = std::make_unique<Entity>();
    entity->m_id = id;
    entity->m_name = std::move(name);
    entity->m_parent = parent;
    entity->m_components.push_back(std::make_unique<Transform>());
    Entity* raw = entity.get();
    m_entities.emplace(id, std::move(entity));
    const std::size_t at = std::min(index.value_or(siblings->size()), siblings->size());
    siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(at), id);
    markStructure();
    return raw;
}

void Scene::eraseSubtree(EntityId id)
{
    std::vector<EntityId> pending{id};
    while (!pending.empty()) {
        const EntityId current = pending.back();
        pending.pop_back();
        if (const Entity* e = find(current)) {
            pending.insert(pending.end(), e->m_children.begin(), e->m_children.end());
        }
        m_entities.erase(current);
    }
}

bool Scene::remove(EntityId id)
{
    const Entity* e = find(id);
    if (e == nullptr) {
        return false;
    }
    if (std::vector<EntityId>* siblings = siblingsOf(e->m_parent)) {
        siblings->erase(std::remove(siblings->begin(), siblings->end(), id), siblings->end());
    }
    eraseSubtree(id);
    markStructure();
    return true;
}

bool Scene::rename(EntityId id, std::string name)
{
    Entity* e = findMutable(id);
    if (e == nullptr) {
        return false;
    }
    e->m_name = std::move(name);
    markStructure();
    return true;
}

bool Scene::setActive(EntityId id, bool active)
{
    Entity* e = findMutable(id);
    if (e == nullptr) {
        return false;
    }
    e->m_active = active;
    markChanged();
    return true;
}

bool Scene::move(EntityId id, EntityId newParent, std::size_t index)
{
    Entity* e = findMutable(id);
    if (e == nullptr || (!newParent.isNull() && isAncestorOrSelf(id, newParent))) {
        return false;
    }
    std::vector<EntityId>* target = siblingsOf(newParent);
    if (target == nullptr) {
        return false;
    }
    std::vector<EntityId>* source = siblingsOf(e->m_parent);
    if (source != nullptr) {
        source->erase(std::remove(source->begin(), source->end(), id), source->end());
    }
    e->m_parent = newParent;
    const std::size_t at = std::min(index, target->size());
    target->insert(target->begin() + static_cast<std::ptrdiff_t>(at), id);
    markStructure();
    return true;
}

bool Scene::setProperty(EntityId id, std::string_view componentType, std::string_view key, const PropertyValue& value)
{
    Entity* e = findMutable(id);
    if (e == nullptr) {
        return false;
    }
    Component* c = e->component(componentType);
    if (c == nullptr || !c->set(key, value)) {
        return false;
    }
    if (componentType == Transform::componentType().id) {
        markMoved();   // only where things are: the renderer can just move nodes
    } else {
        markChanged();
    }
    return true;
}

Component* Scene::editComponent(EntityId id, std::string_view componentType)
{
    Entity* e = findMutable(id);
    Component* c = e != nullptr ? e->component(componentType) : nullptr;
    if (c == nullptr) {
        return nullptr;
    }
    if (componentType == Transform::componentType().id) {
        markMoved();
    } else {
        markChanged();
    }
    return c;
}

Animator* Scene::editAnimatorPlayback(EntityId id)
{
    Entity* e = findMutable(id);
    auto* animator = e != nullptr ? static_cast<Animator*>(e->component(Animator::componentType().id)) : nullptr;
    if (animator != nullptr) {
        markMoved();
    }
    return animator;
}

bool Scene::modifyComponent(EntityId id, std::string_view componentType,
                            const std::function<bool(Component&)>& change)
{
    Entity* e = findMutable(id);
    Component* c = e != nullptr ? e->component(componentType) : nullptr;
    if (c == nullptr || !change(*c)) {
        return false;
    }
    markChanged();
    return true;
}

Component* Scene::addComponent(EntityId id, std::unique_ptr<Component> component, std::optional<std::size_t> index)
{
    Entity* e = findMutable(id);
    if (e == nullptr || component == nullptr) {
        return nullptr;
    }
    const std::string& typeId = dynamic_cast<const UnknownComponent*>(component.get()) != nullptr
                                    ? static_cast<const UnknownComponent&>(*component).originalTypeId()
                                    : component->type().id;
    if (e->component(typeId) != nullptr) {
        return nullptr;
    }
    Component* raw = component.get();
    const std::size_t at = std::min(index.value_or(e->m_components.size()), e->m_components.size());
    e->m_components.insert(e->m_components.begin() + static_cast<std::ptrdiff_t>(at), std::move(component));
    markStructure();
    return raw;
}

std::unique_ptr<Component> Scene::removeComponent(EntityId id, std::string_view componentType)
{
    Entity* e = findMutable(id);
    if (e == nullptr) {
        return nullptr;
    }
    for (auto it = e->m_components.begin(); it != e->m_components.end(); ++it) {
        const Component& c = **it;
        const auto* unknown = dynamic_cast<const UnknownComponent*>(&c);
        const bool match = unknown != nullptr ? unknown->originalTypeId() == componentType : c.type().id == componentType;
        if (!match) {
            continue;
        }
        if (c.type().required) {
            return nullptr;
        }
        std::unique_ptr<Component> removed = std::move(*it);
        e->m_components.erase(it);
        markStructure();
        return removed;
    }
    return nullptr;
}

std::optional<std::size_t> Scene::componentIndex(EntityId id, std::string_view componentType) const
{
    const Entity* e = find(id);
    if (e == nullptr) {
        return std::nullopt;
    }
    const Component* c = e->component(componentType);
    for (std::size_t i = 0; i < e->m_components.size(); ++i) {
        if (e->m_components[i].get() == c && c != nullptr) {
            return i;
        }
    }
    return std::nullopt;
}

// --- Snapshots ---------------------------------------------------------------------------

void Scene::captureInto(const Entity& entity, EntitySnapshot& out) const
{
    EntitySnapshot::Item item;
    item.id = entity.m_id;
    item.parent = entity.m_parent;
    item.name = entity.m_name;
    item.active = entity.m_active;
    for (const auto& c : entity.m_components) {
        item.components.push_back(c->clone());
    }
    out.items.push_back(std::move(item));
    for (EntityId child : entity.m_children) {
        if (const Entity* c = find(child)) {
            captureInto(*c, out);
        }
    }
}

EntitySnapshot Scene::capture(EntityId id) const
{
    EntitySnapshot snapshot;
    if (const Entity* e = find(id)) {
        captureInto(*e, snapshot);
        snapshot.index = indexOf(id).value_or(0);
    }
    return snapshot;
}

bool Scene::restore(const EntitySnapshot& snapshot, EntityId parent, std::size_t index)
{
    if (snapshot.empty() || siblingsOf(parent) == nullptr) {
        return false;
    }
    for (const auto& item : snapshot.items) {
        if (contains(item.id)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < snapshot.items.size(); ++i) {
        const auto& item = snapshot.items[i];
        auto entity = std::make_unique<Entity>();
        entity->m_id = item.id;
        entity->m_name = item.name;
        entity->m_active = item.active;
        entity->m_parent = i == 0 ? parent : item.parent;
        for (const auto& c : item.components) {
            entity->m_components.push_back(c->clone());
        }
        if (entity->get<Transform>() == nullptr) {
            entity->m_components.insert(entity->m_components.begin(), std::make_unique<Transform>());
        }
        const EntityId myParent = entity->m_parent;
        m_entities.emplace(item.id, std::move(entity));
        std::vector<EntityId>* siblings = siblingsOf(myParent);
        if (siblings == nullptr) {
            siblings = &m_roots; // a broken snapshot: keep the entity rather than lose it
            findMutable(item.id)->m_parent = {};
        }
        const std::size_t at = i == 0 ? std::min(index, siblings->size()) : siblings->size();
        siblings->insert(siblings->begin() + static_cast<std::ptrdiff_t>(at), item.id);
    }
    markStructure();
    return true;
}

EntitySnapshot Scene::withNewIds(const EntitySnapshot& snapshot)
{
    EntitySnapshot copy;
    copy.index = snapshot.index;
    std::unordered_map<EntityId, EntityId> remap;
    for (const auto& item : snapshot.items) {
        EntityId fresh = newId();
        // Also avoid IDs given out earlier in this copy.
        while (std::any_of(remap.begin(), remap.end(), [&](const auto& kv) { return kv.second == fresh; })) {
            fresh = newId();
        }
        remap[item.id] = fresh;
    }
    for (const auto& item : snapshot.items) {
        EntitySnapshot::Item c;
        c.id = remap[item.id];
        const auto parent = remap.find(item.parent);
        c.parent = parent != remap.end() ? parent->second : item.parent;
        c.name = item.name;
        c.active = item.active;
        for (const auto& comp : item.components) {
            c.components.push_back(comp->clone());
        }
        copy.items.push_back(std::move(c));
    }
    return copy;
}

// --- Geometry ------------------------------------------------------------------------------

Affine2 Scene::worldTransform(EntityId id) const
{
    Affine2 result;
    for (const Entity* e = find(id); e != nullptr; e = find(e->m_parent)) {
        if (const Transform* t = e->get<Transform>()) {
            result = t->local() * result;
        }
    }
    return result;
}

std::optional<LocalBounds> localBounds(const Entity& entity)
{
    if (const Shape* shape = entity.get<Shape>()) {
        return LocalBounds{{}, shape->size, shape->kind == ShapeKind::Ellipse};
    }
    if (const Sprite* sprite = entity.get<Sprite>()) {
        return LocalBounds{{}, sprite->size, false};
    }
    if (const Tilemap* map = entity.get<Tilemap>()) {
        if (const auto used = map->usedCells()) {
            const Vec2 lo{static_cast<float>(used->min.x) * map->cellSize.x, static_cast<float>(used->min.y) * map->cellSize.y};
            const Vec2 hi{static_cast<float>(used->max.x + 1) * map->cellSize.x,
                          static_cast<float>(used->max.y + 1) * map->cellSize.y};
            return LocalBounds{{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f}, hi - lo, false};
        }
    }
    if (const Text* text = entity.get<Text>()) {
        // Roughly the first line, so text can be clicked in the editor.
        const float w = text->width > 0.0f ? text->width
                                           : std::max(text->size, static_cast<float>(text->text.size()) * text->size * 0.55f);
        const float x = text->align == TextAlign::Left ? w * 0.5f : text->align == TextAlign::Right ? -w * 0.5f : 0.0f;
        return LocalBounds{{x, -text->size * 0.6f}, {w, text->size * 1.2f}, false};
    }
    if (const Collider* collider = entity.get<Collider>()) {
        // An object with only a collider (e.g. a door area) can still be
        // clicked and outlined in the editor.
        return LocalBounds{collider->offset, collider->size, false};
    }
    return std::nullopt;
}

std::optional<std::array<Vec2, 4>> Scene::worldCorners(EntityId id) const
{
    const Entity* e = find(id);
    const auto bounds = e != nullptr ? localBounds(*e) : std::nullopt;
    if (!bounds) {
        return std::nullopt;
    }
    const Affine2 world = worldTransform(id);
    const Vec2 c = bounds->center;
    const float hw = bounds->size.x * 0.5f;
    const float hh = bounds->size.y * 0.5f;
    return std::array<Vec2, 4>{world.apply({c.x - hw, c.y - hh}), world.apply({c.x + hw, c.y - hh}),
                               world.apply({c.x + hw, c.y + hh}), world.apply({c.x - hw, c.y + hh})};
}

bool Scene::isEffectivelyActive(const Entity& entity) const
{
    for (const Entity* e = &entity; e != nullptr; e = find(e->m_parent)) {
        if (!e->m_active) {
            return false;
        }
    }
    return true;
}

EntityId Scene::hitTest(Vec2 worldPoint) const
{
    // Everything in drawing order; the last hit is the one on top.
    EntityId hit;
    forEach([&](const Entity& e, int) {
        const auto bounds = localBounds(e);
        if (!bounds || bounds->size.x <= 0.0f || bounds->size.y <= 0.0f || !isEffectivelyActive(e)) {
            return;
        }
        const auto inverse = worldTransform(e.m_id).inverse();
        if (!inverse) {
            return;
        }
        const Vec2 p = inverse->apply(worldPoint) - bounds->center;
        const float hw = bounds->size.x * 0.5f;
        const float hh = bounds->size.y * 0.5f;
        bool inside = false;
        if (!bounds->ellipse) {
            inside = std::abs(p.x) <= hw && std::abs(p.y) <= hh;
        } else {
            const float nx = p.x / hw;
            const float ny = p.y / hh;
            inside = nx * nx + ny * ny <= 1.0f;
        }
        if (inside) {
            hit = e.m_id;
        }
    });
    return hit;
}

// --- Start scene -------------------------------------------------------------------------------

namespace {

Entity* shapeEntity(Scene& scene, const char* name, EntityId parent, Vec2 position, ShapeKind kind, Vec2 size,
                    Color color, bool filled = true, float lineWidth = 2.0f)
{
    Entity* e = scene.create(name, parent);
    scene.setProperty(e->id(), "rolo.Transform", "position", position);
    auto shape = std::make_unique<Shape>();
    shape->kind = kind;
    shape->size = size;
    shape->color = color;
    shape->filled = filled;
    shape->lineWidth = lineWidth;
    scene.addComponent(e->id(), std::move(shape));
    return e;
}

} // namespace

Scene makeStartScene(float worldWidth, float worldHeight)
{
    // Rolo, the engine's mascot, drawn with Shape components only (later
    // shapes are drawn on top). y goes up, so positive y is above the centre.
    Scene scene("Start Scene");
    const Color red{214, 52, 43, 255};
    const Color shadow{122, 15, 11, 110};
    const Color ink{20, 20, 20, 255};
    const Color orange{255, 147, 38, 255};
    const Color shine{255, 243, 214, 140};
    const Color white{255, 255, 255, 255};
    const Color cheek{255, 90, 95, 150};
    const Color mouth{90, 23, 8, 255};
    const Color tongue{255, 111, 111, 255};
    const Color leaf{76, 194, 90, 255};

    shapeEntity(scene, "Background", {}, {worldWidth * 0.5f, worldHeight * 0.5f}, ShapeKind::Rectangle,
                {worldWidth, worldHeight}, red);

    Entity* rolo = scene.create("Rolo");
    const EntityId r = rolo->id();
    scene.setProperty(r, "rolo.Transform", "position", Vec2{worldWidth * 0.5f, worldHeight * 0.5f - 10.0f});

    shapeEntity(scene, "Shadow", r, {0.0f, -170.0f}, ShapeKind::Ellipse, {240.0f, 30.0f}, shadow);
    // The sprout: a stem and a leaf.
    shapeEntity(scene, "Stem", r, {4.0f, 160.0f}, ShapeKind::Rectangle, {12.0f, 40.0f}, ink);
    shapeEntity(scene, "Leaf", r, {38.0f, 176.0f}, ShapeKind::Ellipse, {60.0f, 26.0f}, leaf);
    // The body: an outline circle, the orange ball and a shine.
    shapeEntity(scene, "Outline", r, {}, ShapeKind::Ellipse, {312.0f, 312.0f}, ink);
    shapeEntity(scene, "Body", r, {}, ShapeKind::Ellipse, {288.0f, 288.0f}, orange);
    shapeEntity(scene, "Shine", r, {-58.0f, 78.0f}, ShapeKind::Ellipse, {84.0f, 44.0f}, shine);
    // Big eyes looking a little inward, with a glint each.
    for (const float side : {-1.0f, 1.0f}) {
        const char* label = side < 0.0f ? "Left Eye" : "Right Eye";
        Entity* eye = shapeEntity(scene, label, r, {side * 52.0f, 22.0f}, ShapeKind::Ellipse, {86.0f, 102.0f}, ink);
        shapeEntity(scene, "White", eye->id(), {}, ShapeKind::Ellipse, {68.0f, 84.0f}, white);
        Entity* pupil = shapeEntity(scene, "Pupil", eye->id(), {-side * 10.0f, -12.0f}, ShapeKind::Ellipse,
                                    {40.0f, 40.0f}, ink);
        shapeEntity(scene, "Glint", pupil->id(), {7.0f, 9.0f}, ShapeKind::Ellipse, {14.0f, 14.0f}, white);
        shapeEntity(scene, side < 0.0f ? "Left Cheek" : "Right Cheek", r, {side * 92.0f, -46.0f},
                    ShapeKind::Ellipse, {44.0f, 26.0f}, cheek);
    }
    // The smile: the lower half of a dark ellipse (an orange bar hides the
    // upper half), with a tongue.
    shapeEntity(scene, "Mouth", r, {0.0f, -52.0f}, ShapeKind::Ellipse, {110.0f, 76.0f}, mouth);
    shapeEntity(scene, "Mouth Top", r, {0.0f, -33.0f}, ShapeKind::Rectangle, {120.0f, 38.0f}, orange);
    shapeEntity(scene, "Tongue", r, {0.0f, -78.0f}, ShapeKind::Ellipse, {54.0f, 22.0f}, tongue);
    return scene;
}

} // namespace rolo
