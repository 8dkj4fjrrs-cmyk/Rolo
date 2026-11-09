#include "rolo/physics/PhysicsWorld.hpp"

#include "box2d/box2d.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace rolo {

namespace {

constexpr float kPpm = PhysicsWorld::kPointsPerMeter;
constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

b2Vec2 meters(Vec2 points)
{
    return {points.x / kPpm, points.y / kPpm};
}

Vec2 points(b2Vec2 meters)
{
    return {meters.x * kPpm, meters.y * kPpm};
}

// Rolo turns clockwise (Transform::rotation); Box2D counter-clockwise.
// (Exact sine and cosine: b2MakeRot approximates them, about 0.1° off, and an
// object placed at 10° should come back at 10°.)
b2Rot rotation(float clockwiseDegrees)
{
    const float radians = -clockwiseDegrees / kDegrees;
    return b2Rot{std::cos(radians), std::sin(radians)};
}

b2BodyId bodyId(BodyHandle body)
{
    return b2LoadBodyId(body.value);
}

b2BodyType bodyType(BodyType type)
{
    switch (type) {
    case BodyType::Dynamic: return b2_dynamicBody;
    case BodyType::Kinematic: return b2_kinematicBody;
    case BodyType::Static: return b2_staticBody;
    }
    return b2_dynamicBody;
}

} // namespace

struct PhysicsWorld::Impl {
    b2WorldId world = b2_nullWorldId;
    // Box2D keeps a void* per body; a table keeps whole 64-bit values on
    // every computer instead.
    std::unordered_map<std::uint64_t, std::uint64_t> users;

    bool valid(BodyHandle body) const { return !body.isNull() && b2Body_IsValid(bodyId(body)); }
};

PhysicsWorld::PhysicsWorld(Vec2 gravity)
    : m(std::make_unique<Impl>())
{
    b2WorldDef def = b2DefaultWorldDef();
    def.gravity = meters(gravity);
    // Box2D's own threads stay off (workerCount 1): the game's worlds are small,
    // and one thread gives the same result every time.
    m->world = b2CreateWorld(&def);
}

PhysicsWorld::~PhysicsWorld()
{
    b2DestroyWorld(m->world);
}

Vec2 PhysicsWorld::gravity() const
{
    return points(b2World_GetGravity(m->world));
}

void PhysicsWorld::setGravity(Vec2 gravity)
{
    b2World_SetGravity(m->world, meters(gravity));
    // Resting bodies sleep (Box2D skips them until something touches them):
    // wake them, or they would hang in the air under the new gravity.
    for (const auto& [value, user] : m->users) {
        if (b2Body_IsValid(b2LoadBodyId(value))) {
            b2Body_SetAwake(b2LoadBodyId(value), true);
        }
    }
}

BodyHandle PhysicsWorld::createBody(const BodySettings& s)
{
    b2BodyDef def = b2DefaultBodyDef();
    def.type = bodyType(s.type);
    def.position = b2ToPos(meters(s.position));
    def.rotation = rotation(s.angle);
    def.gravityScale = s.gravityScale;
    def.linearDamping = std::max(0.0f, s.drag);
    def.motionLocks.angularZ = s.lockRotation;
    def.isBullet = s.fast;
    const b2BodyId id = b2CreateBody(m->world, &def);
    const BodyHandle handle{b2StoreBodyId(id)};
    m->users[handle.value] = s.user;
    return handle;
}

void PhysicsWorld::destroyBody(BodyHandle body)
{
    if (m->valid(body)) {
        b2DestroyBody(bodyId(body));
    }
    m->users.erase(body.value);
}

bool PhysicsWorld::isValid(BodyHandle body) const
{
    return m->valid(body);
}

void PhysicsWorld::setType(BodyHandle body, BodyType type)
{
    if (m->valid(body)) {
        b2Body_SetType(bodyId(body), bodyType(type));
    }
}

std::uint64_t PhysicsWorld::user(BodyHandle body) const
{
    const auto found = m->users.find(body.value);
    return found != m->users.end() ? found->second : 0;
}

void PhysicsWorld::addShape(BodyHandle body, const ShapeSettings& s)
{
    if (!m->valid(body) || !(s.size.x > 0.0f) || !(s.size.y > 0.0f)) {
        return;
    }
    const b2BodyId id = bodyId(body);
    const b2Vec2 half = meters({s.size.x * 0.5f, s.size.y * 0.5f});
    const b2Vec2 centre = meters(s.offset);
    const b2Rot turn = rotation(s.angle);

    // The body's mass is spread over the shape's area (Box2D wants a density).
    float area = 4.0f * half.x * half.y;
    float radius = std::min(half.x, half.y);
    if (s.shape == ColliderShape::Circle) {
        area = std::numbers::pi_v<float> * radius * radius;
    } else if (s.shape == ColliderShape::Capsule) {
        const float length = 2.0f * (std::max(half.x, half.y) - radius);
        area = std::numbers::pi_v<float> * radius * radius + 2.0f * radius * length;
    }

    b2ShapeDef def = b2DefaultShapeDef();
    def.density = area > 0.0f ? std::max(0.0f, s.mass) / area : 1.0f;
    def.material.friction = std::max(0.0f, s.friction);
    def.material.restitution = std::clamp(s.bounciness, 0.0f, 1.0f);
    def.isSensor = s.sensor;
    def.enableContactEvents = !s.sensor;

    switch (s.shape) {
    case ColliderShape::Box: {
        const b2Polygon box = b2MakeOffsetBox(half.x, half.y, centre, turn);
        b2CreatePolygonShape(id, &def, &box);
        break;
    }
    case ColliderShape::Circle: {
        const b2Circle circle{centre, radius};
        b2CreateCircleShape(id, &def, &circle);
        break;
    }
    case ColliderShape::Capsule: {
        // Two end centres along the long side of the box, turned with it.
        const float reach = std::max(half.x, half.y) - radius;
        const b2Vec2 along = half.y >= half.x ? b2Vec2{0.0f, reach} : b2Vec2{reach, 0.0f};
        const b2Vec2 turned = b2RotateVector(turn, along);
        if (reach <= 0.0001f) {   // a square box: the capsule is a circle
            const b2Circle circle{centre, radius};
            b2CreateCircleShape(id, &def, &circle);
        } else {
            const b2Capsule capsule{b2Sub(centre, turned), b2Add(centre, turned), radius};
            b2CreateCapsuleShape(id, &def, &capsule);
        }
        break;
    }
    }
}

void PhysicsWorld::step(float seconds)
{
    m_hits.clear();
    if (!(seconds > 0.0f)) {
        return;
    }
    b2World_Step(m->world, seconds, kSubSteps);
    const b2ContactEvents events = b2World_GetContactEvents(m->world);
    for (int i = 0; i < events.beginCount; ++i) {
        const b2ContactBeginTouchEvent& e = events.beginEvents[i];
        if (!b2Shape_IsValid(e.shapeIdA) || !b2Shape_IsValid(e.shapeIdB)) {
            continue;
        }
        const BodyHandle a{b2StoreBodyId(b2Shape_GetBody(e.shapeIdA))};
        const BodyHandle b{b2StoreBodyId(b2Shape_GetBody(e.shapeIdB))};
        m_hits.push_back({user(a), user(b)});
    }
}

Vec2 PhysicsWorld::position(BodyHandle body) const
{
    if (!m->valid(body)) {
        return {};
    }
    const b2Pos p = b2Body_GetPosition(bodyId(body));
    return {static_cast<float>(p.x) * kPpm, static_cast<float>(p.y) * kPpm};
}

float PhysicsWorld::angle(BodyHandle body) const
{
    return m->valid(body) ? -b2Rot_GetAngle(b2Body_GetRotation(bodyId(body))) * kDegrees : 0.0f;
}

void PhysicsWorld::setTransform(BodyHandle body, Vec2 position, float angle)
{
    if (m->valid(body)) {
        b2Body_SetTransform(bodyId(body), b2ToPos(meters(position)), rotation(angle));
    }
}

void PhysicsWorld::moveTo(BodyHandle body, Vec2 position, float angle, float seconds)
{
    if (m->valid(body) && seconds > 0.0f) {
        // Already there: hold still. (Steering to where it already is leaves
        // rounding errors as a tiny spin, and on a long floor even a tiny
        // spin moves its far end enough to nudge what stands on it.)
        const b2BodyId id = bodyId(body);
        const b2Pos now = b2Body_GetPosition(id);
        const b2Vec2 there = meters(position);
        const float turn = std::abs(-b2Rot_GetAngle(b2Body_GetRotation(id)) * kDegrees - angle);
        if (std::abs(static_cast<float>(now.x) - there.x) < 1e-5f && std::abs(static_cast<float>(now.y) - there.y) < 1e-5f
            && (turn < 1e-3f || std::abs(turn - 360.0f) < 1e-3f)) {
            b2Body_SetLinearVelocity(id, b2Vec2_zero);
            b2Body_SetAngularVelocity(id, 0.0f);
            return;
        }
        b2WorldTransform target;
        target.p = b2ToPos(meters(position));
        target.q = rotation(angle);
        b2Body_SetTargetTransform(bodyId(body), target, seconds, true);
    }
}

Vec2 PhysicsWorld::velocity(BodyHandle body) const
{
    return m->valid(body) ? points(b2Body_GetLinearVelocity(bodyId(body))) : Vec2{};
}

void PhysicsWorld::setVelocity(BodyHandle body, Vec2 velocity)
{
    if (m->valid(body)) {
        b2Body_SetLinearVelocity(bodyId(body), meters(velocity));
    }
}

float PhysicsWorld::angularVelocity(BodyHandle body) const
{
    return m->valid(body) ? -b2Body_GetAngularVelocity(bodyId(body)) * kDegrees : 0.0f;
}

void PhysicsWorld::setAngularVelocity(BodyHandle body, float degreesPerSecond)
{
    if (m->valid(body)) {
        b2Body_SetAngularVelocity(bodyId(body), -degreesPerSecond / kDegrees);
    }
}

void PhysicsWorld::applyImpulse(BodyHandle body, Vec2 impulse)
{
    if (m->valid(body)) {
        b2Body_ApplyLinearImpulseToCenter(bodyId(body), meters(impulse), true);
    }
}

void PhysicsWorld::applyForce(BodyHandle body, Vec2 force)
{
    if (m->valid(body)) {
        b2Body_ApplyForceToCenter(bodyId(body), meters(force), true);
    }
}

float PhysicsWorld::mass(BodyHandle body) const
{
    return m->valid(body) ? b2Body_GetMass(bodyId(body)) : 0.0f;
}

std::vector<BodyContact> PhysicsWorld::contacts(BodyHandle body) const
{
    std::vector<BodyContact> out;
    if (!m->valid(body)) {
        return out;
    }
    const b2BodyId id = bodyId(body);
    std::vector<b2ContactData> data(static_cast<std::size_t>(b2Body_GetContactCapacity(id)));
    const int count = data.empty() ? 0 : b2Body_GetContactData(id, data.data(), static_cast<int>(data.size()));
    for (int i = 0; i < count; ++i) {
        const b2ContactData& c = data[static_cast<std::size_t>(i)];
        if (c.manifold.pointCount == 0) {
            continue;   // only close: not touching yet
        }
        const b2BodyId a = b2Shape_GetBody(c.shapeIdA);
        const bool weAreA = b2StoreBodyId(a) == body.value;
        const b2BodyId other = weAreA ? b2Shape_GetBody(c.shapeIdB) : a;
        // The manifold's normal points from A to B; ours points from the other body to this one.
        const b2Vec2 n = weAreA ? b2Neg(c.manifold.normal) : c.manifold.normal;
        out.push_back({user(BodyHandle{b2StoreBodyId(other)}), {n.x, n.y}});
    }
    return out;
}

std::size_t PhysicsWorld::bodyCount() const noexcept
{
    return m->users.size();
}

} // namespace rolo
