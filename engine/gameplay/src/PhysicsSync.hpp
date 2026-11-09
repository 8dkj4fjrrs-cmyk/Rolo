#pragma once

// PhysicsSync: keeps a PhysicsWorld in step with the scene a game plays.
//
// Physics runs only in a scene with at least one RigidBody: other games are
// not touched at all. In such a scene:
//   * an object with a RigidBody and a Collider gets a body of the RigidBody's
//     type, shaped like the Collider;
//   * an object with a solid Collider but no RigidBody gets a static body. If
//     code moves it (Game::move, a script's setPosition), the body becomes
//     kinematic and follows its Transform, pushing dynamic bodies out of the
//     way, like a player shoving a crate. (Not kinematic from the start:
//     boxes resting on a kinematic floor slowly slip on it.)
//   * the solid cells of tilemaps become one static body of boxes, merged into
//     long rows and blocks so bodies slide along floors and walls without
//     catching on the seams between tiles.
//
// Each frame, before behaviours run, bodies are made, changed or removed to
// match the objects (sync). After behaviours, the world steps at a fixed
// 60 steps a second (so the result does not depend on the frame rate) and
// dynamic and kinematic RigidBodies write their positions back into their
// Transforms. A RigidBody moved by code is put where the code put it.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/physics/PhysicsWorld.hpp"
#include "rolo/scene/Scene.hpp"

#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace rolo {

class PhysicsSync {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    static constexpr int kMaxStepsPerFrame = 4;

    explicit PhysicsSync(const AssetDatabase* assets);

    /// Forgets everything (a new scene starts).
    void reset();
    /// Makes, changes and removes bodies to match the scene. Starts physics
    /// the first time the scene has a RigidBody.
    void sync(Scene& scene);
    /// Steps the world for `dt` seconds of game time and moves the objects.
    void step(Scene& scene, float dt);
    /// Pairs of objects that started touching in the last step().
    const std::vector<std::pair<EntityId, EntityId>>& hits() const noexcept { return m_hits; }

    bool isRunning() const noexcept { return m_world != nullptr; }
    PhysicsWorld* world() noexcept { return m_world.get(); }
    const PhysicsWorld* world() const noexcept { return m_world.get(); }
    /// The body of an object (null if it has none).
    BodyHandle bodyOf(EntityId id) const;

private:
    struct Tracked {
        BodyHandle body;
        std::size_t signature = 0;   // what the body was made from; a change remakes it
        Vec2 position;               // the world position it was last placed at
        float angle = 0.0f;
        bool follows = false;        // no RigidBody: a body following the Transform
        bool moving = false;         // ...that code has moved: kinematic since
        bool writesBack = false;     // dynamic or kinematic RigidBody: physics moves the object
    };

    void buildTiles(const Scene& scene);
    void writeBack(Scene& scene);

    const AssetDatabase* m_assets;
    std::unique_ptr<PhysicsWorld> m_world;
    std::map<EntityId, Tracked> m_bodies;
    float m_accumulator = 0.0f;
    std::vector<std::pair<EntityId, EntityId>> m_hits;
};

} // namespace rolo
