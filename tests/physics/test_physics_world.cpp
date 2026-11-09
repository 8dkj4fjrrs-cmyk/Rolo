// PhysicsWorld (engine/physics): Box2D in Rolo's units. These check the
// numbers a game relies on: how far things fall, that they land and rest,
// bounce, are pushed by the right amount, and turn the right way.

#include "rolo/physics/PhysicsWorld.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>

using namespace rolo;
using Catch::Approx;

namespace {

constexpr float kStep = 1.0f / 60.0f;

void run(PhysicsWorld& world, float seconds)
{
    const int steps = static_cast<int>(std::lround(seconds / kStep));
    for (int i = 0; i < steps; ++i) {
        world.step(kStep);
    }
}

// A floor 2000 points wide whose top is at y = 0.
BodyHandle addFloor(PhysicsWorld& world, std::uint64_t user = 99)
{
    const BodyHandle floor = world.createBody({.type = BodyType::Static, .position = {0, -50}, .user = user});
    world.addShape(floor, {.size = {2000, 100}, .friction = 0.6f});
    return floor;
}

BodyHandle addBox(PhysicsWorld& world, Vec2 at, BodySettings settings = {}, ShapeSettings shape = {})
{
    settings.position = at;
    const BodyHandle body = world.createBody(settings);
    world.addShape(body, shape);
    return body;
}

} // namespace

TEST_CASE("Things fall with gravity, in points per second per second", "[physics]")
{
    PhysicsWorld world({0, -980});
    CHECK(world.gravity().y == Approx(-980.0f));
    const BodyHandle box = addBox(world, {0, 1000});
    run(world, 1.0f);
    // Half of g times t squared: 490 points in a second (stepping adds a little).
    const float fallen = 1000.0f - world.position(box).y;
    CHECK(fallen == Approx(490.0f).epsilon(0.03));
    CHECK(world.velocity(box).y == Approx(-980.0f).epsilon(0.01));
}

TEST_CASE("A box lands on the floor and comes to rest on it", "[physics]")
{
    PhysicsWorld world({0, -980});
    addFloor(world);
    const BodyHandle box = addBox(world, {0, 200}, {.user = 7}, {.size = {40, 40}});
    run(world, 3.0f);
    CHECK(world.position(box).y == Approx(20.0f).margin(1.0f));   // half its height above the floor
    CHECK(std::abs(world.velocity(box).y) < 1.0f);

    // It touches the floor, which is below it: the normal points up, from the floor to the box.
    const auto contacts = world.contacts(box);
    REQUIRE(contacts.size() == 1);
    CHECK(contacts[0].other == 99);
    CHECK(contacts[0].normal.y == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Landing is reported once, as a hit between the two bodies", "[physics]")
{
    PhysicsWorld world({0, -980});
    addFloor(world, 1);
    addBox(world, {0, 100}, {.user = 2});
    int hits = 0;
    for (int i = 0; i < 120; ++i) {
        world.step(kStep);
        for (const BodyHit& hit : world.hits()) {
            CHECK(((hit.a == 1 && hit.b == 2) || (hit.a == 2 && hit.b == 1)));
            ++hits;
        }
    }
    CHECK(hits == 1);
}

TEST_CASE("Every shape is as heavy as its mass, whatever its size", "[physics]")
{
    PhysicsWorld world({0, 0});
    const ColliderShape shape = GENERATE(ColliderShape::Box, ColliderShape::Circle, ColliderShape::Capsule);
    const Vec2 size = GENERATE(Vec2{32, 32}, Vec2{20, 60}, Vec2{80, 30});
    const BodyHandle body = addBox(world, {}, {}, {.shape = shape, .size = size, .mass = 2.5f});
    CHECK(world.mass(body) == Approx(2.5f).epsilon(0.001));
}

TEST_CASE("An impulse changes speed by impulse divided by mass", "[physics]")
{
    PhysicsWorld world({0, 0});
    const BodyHandle light = addBox(world, {0, 0}, {}, {.mass = 1.0f});
    const BodyHandle heavy = addBox(world, {500, 0}, {}, {.mass = 4.0f});
    world.applyImpulse(light, {0, 500});
    world.applyImpulse(heavy, {0, 500});
    CHECK(world.velocity(light).y == Approx(500.0f));
    CHECK(world.velocity(heavy).y == Approx(125.0f));
    run(world, 1.0f);
    CHECK(world.position(light).y == Approx(500.0f).epsilon(0.01));

    // A steady force speeds it up: 100 kg·points/s² on 1 kg for a second gives 100 points/s.
    const BodyHandle pushed = addBox(world, {0, -500}, {}, {.mass = 1.0f});
    for (int i = 0; i < 60; ++i) {
        world.applyForce(pushed, {100, 0});
        world.step(kStep);
    }
    CHECK(world.velocity(pushed).x == Approx(100.0f).epsilon(0.02));
}

TEST_CASE("Bounciness 1 bounces back about as high; 0 thuds", "[physics]")
{
    PhysicsWorld world({0, -980});
    addFloor(world);
    const BodyHandle ball = addBox(world, {0, 300}, {}, {.shape = ColliderShape::Circle, .size = {20, 20}, .bounciness = 1.0f});
    const BodyHandle lump = addBox(world, {200, 300}, {}, {.shape = ColliderShape::Circle, .size = {20, 20}, .bounciness = 0.0f});
    run(world, 0.85f);   // past the first landing (about 0.78 s)
    float highest = 0.0f;
    float lumpHighest = 0.0f;
    for (int i = 0; i < 60; ++i) {
        world.step(kStep);
        highest = std::max(highest, world.position(ball).y);
        lumpHighest = std::max(lumpHighest, world.position(lump).y);
    }
    CHECK(highest > 250.0f);
    CHECK(lumpHighest < 20.0f);
}

TEST_CASE("Angles turn clockwise, like Transform rotation", "[physics]")
{
    PhysicsWorld world({0, 0});
    const BodyHandle wheel = addBox(world, {}, {.angle = 10.0f});
    CHECK(world.angle(wheel) == Approx(10.0f).margin(0.001f));
    world.setAngularVelocity(wheel, 45.0f);
    CHECK(world.angularVelocity(wheel) == Approx(45.0f).margin(0.01f));
    run(world, 1.0f);
    CHECK(world.angle(wheel) == Approx(55.0f).margin(0.5f));
}

TEST_CASE("Lock Rotation keeps a body upright when it is knocked", "[physics]")
{
    PhysicsWorld world({0, -980});
    addFloor(world);
    const BodyHandle free = addBox(world, {0, 20.5f}, {}, {.size = {40, 40}});
    const BodyHandle locked = addBox(world, {300, 20.5f}, {.lockRotation = true}, {.size = {40, 40}});
    // A ball flies into the top corner of each.
    for (const float x : {-60.0f, 240.0f}) {
        const BodyHandle ball =
            addBox(world, {x, 38}, {.fast = true}, {.shape = ColliderShape::Circle, .size = {10, 10}, .mass = 3.0f});
        world.setVelocity(ball, {900, 0});
    }
    run(world, 0.5f);
    CHECK(std::abs(world.angle(free)) > 5.0f);
    CHECK(world.angle(locked) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("A kinematic body moved with moveTo pushes dynamic ones", "[physics]")
{
    PhysicsWorld world({0, 0});
    const BodyHandle pusher = addBox(world, {0, 0}, {.type = BodyType::Kinematic});
    const BodyHandle crate = addBox(world, {40, 0});
    for (int i = 1; i <= 60; ++i) {
        world.moveTo(pusher, {static_cast<float>(i) * 2.0f, 0}, 0.0f, kStep);
        world.step(kStep);
    }
    CHECK(world.position(pusher).x == Approx(120.0f).margin(0.5f));
    CHECK(world.position(crate).x > 140.0f);   // shoved ahead of it
}

TEST_CASE("Bodies can be teleported and removed", "[physics]")
{
    PhysicsWorld world({0, -980});
    const BodyHandle box = addBox(world, {0, 0}, {.user = 5});
    CHECK(world.isValid(box));
    CHECK(world.user(box) == 5);
    world.setVelocity(box, {0, 100});
    world.setTransform(box, {300, 400}, 90.0f);
    CHECK(world.position(box).x == Approx(300.0f));
    CHECK(world.angle(box) == Approx(90.0f).margin(0.01f));
    CHECK(world.velocity(box).y == Approx(100.0f));   // it keeps its speed
    CHECK(world.bodyCount() == 1);
    world.destroyBody(box);
    CHECK_FALSE(world.isValid(box));
    CHECK(world.bodyCount() == 0);
    // Using a removed body does nothing.
    world.setVelocity(box, {1, 1});
    CHECK(world.velocity(box).x == 0.0f);
    CHECK(world.contacts(box).empty());
}

TEST_CASE("The same simulation gives the same result every time", "[physics]")
{
    const auto simulate = [] {
        PhysicsWorld world({0, -980});
        addFloor(world);
        std::vector<BodyHandle> boxes;
        for (int i = 0; i < 20; ++i) {
            boxes.push_back(addBox(world, {static_cast<float>(i % 5) * 33.0f, 50.0f + static_cast<float>(i) * 35.0f},
                                   {.angle = static_cast<float>(i) * 7.0f}, {.size = {30, 30}}));
        }
        run(world, 2.0f);
        std::vector<float> out;
        for (const BodyHandle b : boxes) {
            out.push_back(world.position(b).x);
            out.push_back(world.position(b).y);
            out.push_back(world.angle(b));
        }
        return out;
    };
    CHECK(simulate() == simulate());
}
