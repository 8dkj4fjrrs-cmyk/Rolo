#pragma once

// PhysicsWorld: 2D rigid-body physics, in Rolo's units.
//
// A thin layer over Box2D (third_party/box2d, ADR 0016), so the rest of Rolo
// never sees Box2D's types and works in the units of its scenes:
//   * lengths in points, y up (Box2D works in metres: kPointsPerMeter);
//   * angles in degrees, clockwise, like Transform::rotation;
//   * velocities in points per second, gravity in points per second per second;
//   * mass in kilograms, impulses in kg·points/s, forces in kg·points/s².
//
// The game (rolo::Game) keeps a world for each scene with RigidBodies: it
// makes a body for every Collider, steps the world at a fixed rate and writes
// the bodies' positions back into the objects' Transforms (gameplay/src/
// PhysicsSync.cpp). Tests use the world directly.
//
// Box2D is MIT-licensed; nothing here needs anything else.

#include "rolo/scene/Components.hpp"
#include "rolo/scene/Types.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace rolo {

/// A body in a PhysicsWorld. Copyable; it becomes invalid when the body is destroyed.
struct BodyHandle {
    std::uint64_t value = 0;   // Box2D's body id, packed; 0: none
    bool isNull() const noexcept { return value == 0; }
    friend bool operator==(const BodyHandle&, const BodyHandle&) = default;
};

struct BodySettings {
    BodyType type = BodyType::Dynamic;
    Vec2 position;               // points
    float angle = 0.0f;          // degrees
    float gravityScale = 1.0f;
    float drag = 0.0f;
    bool lockRotation = false;
    bool fast = false;           // continuous collision against moving bodies too (bullets)
    std::uint64_t user = 0;      // anything the caller wants back in contacts (the game: an EntityId)
};

/// One shape on a body, in the body's own coordinates (points).
struct ShapeSettings {
    ColliderShape shape = ColliderShape::Box;
    Vec2 size{32.0f, 32.0f};     // the box the shape fits in (see ColliderShape)
    Vec2 offset;                 // its centre, from the body's origin
    float angle = 0.0f;          // its turn on the body, degrees (boxes and capsules)
    float mass = 1.0f;           // kg; spread over the shape's area
    float friction = 0.4f;
    float bounciness = 0.0f;
    bool sensor = false;         // overlaps others without pushing them
};

/// Something a body touches now.
struct BodyContact {
    std::uint64_t other = 0;     // the other body's `user`
    Vec2 normal;                 // unit vector pointing from the other body towards this one
};

/// Two bodies that started touching during the last step.
struct BodyHit {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
};

class PhysicsWorld {
public:
    /// Box2D is tuned for objects from about 0.1 to 10 metres. A 48-point
    /// character is then 0.75 m: comfortably inside.
    static constexpr float kPointsPerMeter = 64.0f;
    /// Each step is split into this many sub-steps (Box2D's recommendation).
    static constexpr int kSubSteps = 4;

    explicit PhysicsWorld(Vec2 gravity);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    Vec2 gravity() const;
    void setGravity(Vec2 gravity);

    BodyHandle createBody(const BodySettings& settings);
    void destroyBody(BodyHandle body);
    bool isValid(BodyHandle body) const;
    /// Adds a shape. Sizes of zero or less are ignored (nothing to collide with).
    void addShape(BodyHandle body, const ShapeSettings& shape);
    /// Changes a body's type (e.g. a static wall that code starts moving).
    void setType(BodyHandle body, BodyType type);
    /// The `user` value given when the body was made.
    std::uint64_t user(BodyHandle body) const;

    /// Advances the simulation by `seconds` (the game calls it with a fixed 1/60 s).
    void step(float seconds);

    Vec2 position(BodyHandle body) const;
    float angle(BodyHandle body) const;
    /// Puts the body somewhere at once (it keeps its velocity).
    void setTransform(BodyHandle body, Vec2 position, float angle);
    /// Kinematic bodies: sets the velocity that brings it to `position` and
    /// `angle` over the next step of `seconds`, so it pushes what is in its way.
    void moveTo(BodyHandle body, Vec2 position, float angle, float seconds);

    Vec2 velocity(BodyHandle body) const;
    void setVelocity(BodyHandle body, Vec2 velocity);
    float angularVelocity(BodyHandle body) const;   // degrees per second
    void setAngularVelocity(BodyHandle body, float degreesPerSecond);
    void applyImpulse(BodyHandle body, Vec2 impulse);
    void applyForce(BodyHandle body, Vec2 force);
    float mass(BodyHandle body) const;

    /// What the body touches now (contacts with at least one point).
    std::vector<BodyContact> contacts(BodyHandle body) const;
    /// Pairs of bodies that started touching during the last step().
    const std::vector<BodyHit>& hits() const noexcept { return m_hits; }

    std::size_t bodyCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m;
    std::vector<BodyHit> m_hits;
};

} // namespace rolo
