#pragma once

// Scene: a tree of entities, each with an ID, a name, an "active" switch and
// a list of components (always including a Transform).
//
// All changes go through Scene's methods. Each change increases revision(),
// which is how the renderer and the editor notice that something changed.
//
// Children are drawn after (on top of) their parent, and siblings in order:
// the last child is drawn on top.

#include "rolo/scene/Component.hpp"
#include "rolo/scene/Components.hpp"
#include "rolo/scene/EntityId.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rolo {

class Entity {
public:
    EntityId id() const noexcept { return m_id; }
    const std::string& name() const noexcept { return m_name; }
    /// Inactive entities (and everything inside them) are not drawn.
    bool isActive() const noexcept { return m_active; }
    EntityId parent() const noexcept { return m_parent; }
    const std::vector<EntityId>& children() const noexcept { return m_children; }

    const std::vector<std::unique_ptr<Component>>& components() const noexcept { return m_components; }
    const Component* component(std::string_view typeId) const;
    Component* component(std::string_view typeId);

    template <class T>
    const T* get() const
    {
        return static_cast<const T*>(component(T::componentType().id));
    }
    template <class T>
    T* get()
    {
        return static_cast<T*>(component(T::componentType().id));
    }

    const Transform& transform() const { return *get<Transform>(); }

private:
    friend class Scene;
    EntityId m_id;
    std::string m_name;
    bool m_active = true;
    EntityId m_parent;
    std::vector<EntityId> m_children;
    std::vector<std::unique_ptr<Component>> m_components;
};

/// A copy of an entity and everything inside it, detached from any scene.
/// Used by undo (to put back a deleted entity with the same IDs) and by
/// duplicate (with new IDs).
struct EntitySnapshot {
    struct Item {
        EntityId id;
        EntityId parent;   // for the first item: the parent it was taken from
        std::string name;
        bool active = true;
        std::vector<std::unique_ptr<Component>> components;
    };
    std::vector<Item> items;   // depth-first: the entity, then its descendants
    std::size_t index = 0;     // position of the entity among its siblings

    bool empty() const noexcept { return items.empty(); }
    EntityId rootId() const { return items.empty() ? EntityId{} : items.front().id; }
};

class Scene {
public:
    explicit Scene(std::string name = "Untitled");
    /// Uses `generator` for new IDs (tests pass a seeded one for repeatable IDs).
    Scene(std::string name, EntityIdGenerator generator);
    Scene(const Scene& other);
    Scene& operator=(const Scene& other);
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    ~Scene() = default;

    const std::string& name() const noexcept { return m_name; }
    void setName(std::string name);

    /// The pull of gravity on RigidBodies, in points per second per second
    /// (y up). Platformers keep the default; top-down games set {0, 0}.
    static constexpr Vec2 kDefaultGravity{0.0f, -980.0f};
    Vec2 gravity() const noexcept { return m_gravity; }
    void setGravity(Vec2 gravity);

    /// Increases with every change.
    std::uint64_t revision() const noexcept { return m_revision; }
    /// Increases with every change except moving, rotating or scaling
    /// (Transform changes). When only revision() went up, everything is
    /// where it was, just placed differently: the renderer then moves its
    /// nodes instead of building them again, which matters when a game moves
    /// things every frame.
    std::uint64_t contentRevision() const noexcept { return m_contentRevision; }
    /// Call after changing components directly (not through Scene's methods).
    void markChanged() noexcept
    {
        ++m_revision;
        ++m_contentRevision;
    }
    /// Call after changing only Transforms directly.
    void markMoved() noexcept { ++m_revision; }

    // --- Reading --------------------------------------------------------------------------
    std::size_t size() const noexcept { return m_entities.size(); }
    bool empty() const noexcept { return m_entities.empty(); }
    const Entity* find(EntityId id) const;
    bool contains(EntityId id) const { return find(id) != nullptr; }
    /// Top-level entities, in drawing order.
    const std::vector<EntityId>& roots() const noexcept { return m_roots; }
    /// Children of `parent`, or the top-level entities for the null ID.
    const std::vector<EntityId>& childrenOf(EntityId parent) const;
    /// Position of `id` among its siblings.
    std::optional<std::size_t> indexOf(EntityId id) const;
    /// True if `ancestor` is `id` or contains it (at any depth).
    bool isAncestorOrSelf(EntityId ancestor, EntityId id) const;
    /// Every entity, parents before children, siblings in order.
    void forEach(const std::function<void(const Entity&, int depth)>& visit) const;

    // --- Changing -----------------------------------------------------------------------------
    /// Creates an entity with a Transform. `index` = position among the
    /// siblings (default: last). A null `id` gets a fresh one. Returns nothing
    /// if the parent does not exist or the ID is already used.
    Entity* create(std::string name, EntityId parent = {}, std::optional<std::size_t> index = {},
                   EntityId id = {});
    /// Removes the entity and everything inside it.
    bool remove(EntityId id);
    bool rename(EntityId id, std::string name);
    bool setActive(EntityId id, bool active);
    /// Moves an entity to a new parent and position. Refuses to move an entity
    /// inside itself.
    bool move(EntityId id, EntityId newParent, std::size_t index);

    /// Sets a component property (the value is coerced, see PropertyInfo::coerce).
    bool setProperty(EntityId id, std::string_view componentType, std::string_view key, const PropertyValue& value);
    /// A component to change directly, e.g. by gameplay code; null if the
    /// entity or component is missing. Counts as a change (for a Transform,
    /// as a move only), so take it again for each frame's changes.
    Component* editComponent(EntityId id, std::string_view componentType);
    template <class T>
    T* edit(EntityId id)
    {
        return static_cast<T*>(editComponent(id, T::componentType().id));
    }
    /// An object's Animator, to switch clips (null if it has none). Counts
    /// as a move, not a content change: the renderer reads the clip, speed,
    /// playing and startTime again every frame, so nothing needs rebuilding.
    /// To give it a different animation set, use edit<Animator>() instead.
    Animator* editAnimatorPlayback(EntityId id);
    /// Changes a component in ways properties cannot express (e.g. painting
    /// tiles). `change` returns true if it changed anything.
    bool modifyComponent(EntityId id, std::string_view componentType, const std::function<bool(Component&)>& change);
    /// Adds a component; refuses a second component of the same type.
    /// `index` = position in the entity's component list (default: last).
    Component* addComponent(EntityId id, std::unique_ptr<Component> component,
                            std::optional<std::size_t> index = {});
    /// Removes and returns a component. Refuses required ones (Transform).
    std::unique_ptr<Component> removeComponent(EntityId id, std::string_view componentType);
    /// Position of a component in the entity's list.
    std::optional<std::size_t> componentIndex(EntityId id, std::string_view componentType) const;

    // --- Snapshots (undo, duplicate) -------------------------------------------------------
    EntitySnapshot capture(EntityId id) const;
    /// Puts a snapshot back (same IDs) under `parent` at `index`. Fails if any
    /// of its IDs is already in the scene or the parent is missing.
    bool restore(const EntitySnapshot& snapshot, EntityId parent, std::size_t index);
    /// A copy of `snapshot` with fresh IDs (for duplicating).
    EntitySnapshot withNewIds(const EntitySnapshot& snapshot);

    // --- Geometry --------------------------------------------------------------------------
    /// The entity's transform in world coordinates (parents included).
    Affine2 worldTransform(EntityId id) const;
    /// The four world-space corners of the entity's shape (bottom-left,
    /// bottom-right, top-right, top-left), if it has a Shape.
    std::optional<std::array<Vec2, 4>> worldCorners(EntityId id) const;
    /// The top-most active entity whose shape contains `worldPoint`.
    EntityId hitTest(Vec2 worldPoint) const;

    /// A new ID not used in this scene.
    EntityId newId();

private:
    Entity* findMutable(EntityId id);
    std::vector<EntityId>* siblingsOf(EntityId parent);
    void eraseSubtree(EntityId id);
    void captureInto(const Entity& entity, EntitySnapshot& out) const;
    bool isEffectivelyActive(const Entity& entity) const;

    std::string m_name;
    Vec2 m_gravity = kDefaultGravity;
    std::unordered_map<EntityId, std::unique_ptr<Entity>> m_entities;
    std::vector<EntityId> m_roots;
    EntityIdGenerator m_ids;
    std::uint64_t m_revision = 1;
    std::uint64_t m_contentRevision = 1;
};

/// The scene a new Rolo project starts with: Rolo, the engine's mascot (a
/// smiling orange ball), drawn entirely with Shape components, centred in a
/// world of the given size.
Scene makeStartScene(float worldWidth = 1280.0f, float worldHeight = 720.0f);

} // namespace rolo
