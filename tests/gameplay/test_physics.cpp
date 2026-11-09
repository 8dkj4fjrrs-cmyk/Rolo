// Physics in games: RigidBody and Collider in a running Game (PhysicsSync),
// the physics part of the Lua API, and how scenes save gravity, bodies and
// collider shapes.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Behaviour.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Components.hpp"
#include "rolo/scene/SceneFile.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rolo;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// RigidBody has a base class, so it cannot be written with designated
// initialisers; these settings can.
struct BodyOptions {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float friction = 0.4f;
    float bounciness = 0.0f;
    float drag = 0.0f;
    bool lockRotation = false;
};

RigidBody makeBody(BodyOptions o)
{
    RigidBody b;
    b.type = o.type;
    b.mass = o.mass;
    b.friction = o.friction;
    b.bounciness = o.bounciness;
    b.drag = o.drag;
    b.lockRotation = o.lockRotation;
    return b;
}

EntityId addObject(Scene& scene, const std::string& name, Vec2 at, Vec2 size, std::optional<RigidBody> body,
                   ColliderShape shape = ColliderShape::Box, EntityId parent = {})
{
    const EntityId id = scene.create(name, parent)->id();
    scene.setProperty(id, "rolo.Transform", "position", at);
    auto collider = std::make_unique<Collider>();
    collider->size = size;
    collider->shape = shape;
    scene.addComponent(id, std::move(collider));
    if (body) {
        scene.addComponent(id, std::make_unique<RigidBody>(*body));
    }
    return id;
}

// A floor 2000 wide whose top is at y = 0, with no RigidBody (a plain solid collider).
EntityId addFloor(Scene& scene)
{
    return addObject(scene, "Floor", {0, -50}, {2000, 100}, std::nullopt);
}

void frames(Game& game, int n)
{
    for (int i = 0; i < n; ++i) {
        game.update(1.0f / 60.0f);
    }
}

Vec2 positionOf(const Game& game, EntityId id)
{
    return game.scene().find(id)->transform().position;
}

// Remembers who it collided with.
struct Bumper : BehaviourBase<Bumper> {
    std::vector<EntityId> hits;
    void collide(Game&, EntityId other) override { hits.push_back(other); }
    static const ComponentType& componentType()
    {
        static const ComponentType type = ComponentTypeBuilder<Bumper>("test.Bumper", "Bumper").build();
        return type;
    }
};

// A project folder for scripts.
struct ScriptFolder {
    fs::path folder = test::uniqueTempFolder("physics-scripts");
    AssetDatabase assets{folder};
    Logger log;
    std::vector<std::string> printed;
    std::vector<std::string> problems;
    ScriptFolder()
    {
        fs::create_directories(folder / "scripts");
        log.addSink([this](const LogMessage& m) {
            (m.level >= LogLevel::Warning ? problems : printed).push_back(m.text);
        });
    }
    ~ScriptFolder()
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }
    AssetId write(const std::string& code)
    {
        std::ofstream(folder / "scripts" / "test.lua", std::ios::binary) << code;
        assets.refresh();
        return assets.findByPath("scripts/test.lua")->id;
    }
};

} // namespace

TEST_CASE("Scenes without a RigidBody have no physics: nothing changes", "[gameplay][physics]")
{
    Scene scene;
    addFloor(scene);
    const EntityId box = addObject(scene, "Box", {0, 200}, {40, 40}, std::nullopt);
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 60);
    CHECK_FALSE(game.hasPhysics());
    CHECK(positionOf(game, box) == Vec2{0, 200});   // a collider alone does not fall
    CHECK(game.velocity(box) == Vec2{});
}

TEST_CASE("A RigidBody falls onto a floor and rests there; its Transform follows", "[gameplay][physics]")
{
    Scene scene;
    addFloor(scene);
    const EntityId crate = addObject(scene, "Crate", {0, 300}, {40, 40}, makeBody({}));
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 1);
    CHECK(game.hasPhysics());
    frames(game, 30);
    CHECK(positionOf(game, crate).y < 290.0f);   // falling
    CHECK(game.velocity(crate).y < -100.0f);
    CHECK_FALSE(game.isOnGround(crate));
    frames(game, 150);
    CHECK(positionOf(game, crate).y == Approx(20.0f).margin(1.0f));
    CHECK(game.isOnGround(crate));
    const auto touching = game.touching(crate);
    REQUIRE(touching.size() == 1);
    CHECK(game.scene().find(touching[0])->name() == "Floor");
}

TEST_CASE("Solid tiles are floors and walls for physics", "[gameplay][physics]")
{
    Scene scene;
    const EntityId mapId = scene.create("Map")->id();
    auto map = std::make_unique<Tilemap>();
    map->cellSize = {32, 32};
    for (int x = -10; x <= 10; ++x) {
        map->setSolid({x, -1}, true);   // a floor: cells just below y = 0
    }
    map->setSolid({3, 0}, true);        // a wall block on it, from x = 96
    scene.addComponent(mapId, std::move(map));
    const EntityId ball = addObject(scene, "Ball", {0, 100}, {20, 20}, makeBody({}), ColliderShape::Circle);
    const EntityId slider = addObject(scene, "Slider", {40, 10}, {20, 20}, makeBody({.friction = 0.0f}));
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 120);
    CHECK(positionOf(game, ball).y == Approx(10.0f).margin(1.0f));
    CHECK(game.isOnGround(ball));
    game.setVelocity(slider, {300, 0});
    frames(game, 60);
    CHECK(positionOf(game, slider).x == Approx(86.0f).margin(1.5f));   // stopped by the wall (96 - 10)
}

TEST_CASE("Gravity is the scene's: zero for top-down games, and changeable while playing", "[gameplay][physics]")
{
    Scene scene;
    scene.setGravity({0, 0});
    const EntityId ball = addObject(scene, "Ball", {0, 0}, {20, 20}, makeBody({}), ColliderShape::Circle);
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 60);
    CHECK(positionOf(game, ball) == Vec2{0, 0});   // floats
    CHECK_FALSE(game.isOnGround(ball));
    game.setGravity({300, 0});
    CHECK(game.gravity().x == 300.0f);
    frames(game, 60);
    CHECK(positionOf(game, ball).x == Approx(150.0f).epsilon(0.05));   // falls sideways
    // The scene being edited is untouched.
    CHECK(scene.gravity() == Vec2{0, 0});
}

TEST_CASE("Velocity, impulses and spin move bodies; code can still place them", "[gameplay][physics]")
{
    Scene scene;
    scene.setGravity({0, 0});
    const EntityId ship = addObject(scene, "Ship", {0, 0}, {20, 20}, makeBody({.mass = 2.0f}));
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 1);
    game.applyImpulse(ship, {200, 0});   // 2 kg: +100 points/s
    CHECK(game.velocity(ship).x == Approx(100.0f));
    frames(game, 60);
    CHECK(positionOf(game, ship).x == Approx(100.0f).epsilon(0.03));
    game.setSpin(ship, 90.0f);
    frames(game, 60);
    CHECK(game.scene().find(ship)->transform().rotation == Approx(90.0f).margin(2.0f));

    // A script or behaviour moving the Transform teleports the body there.
    game.transform(ship)->position = {500, 500};
    frames(game, 1);
    CHECK(positionOf(game, ship).x == Approx(500.0f + 100.0f / 60.0f).margin(0.5f));
    CHECK(positionOf(game, ship).y == Approx(500.0f).margin(0.5f));
}

TEST_CASE("A player moved by code (no RigidBody) pushes crates", "[gameplay][physics]")
{
    Scene scene;
    scene.setGravity({0, 0});   // top-down
    const EntityId player = addObject(scene, "Player", {0, 0}, {30, 30}, std::nullopt);
    const EntityId crate = addObject(scene, "Crate", {40, 0}, {30, 30}, makeBody({.drag = 5.0f}));
    Logger log;
    Game game(log);
    game.start(scene);
    for (int i = 0; i < 60; ++i) {
        game.transform(player)->position.x += 2.0f;   // as a behaviour or script would
        frames(game, 1);
    }
    CHECK(positionOf(game, player).x == Approx(120.0f));
    CHECK(positionOf(game, crate).x > 140.0f);
}

TEST_CASE("Bodies of children follow their parents' placement", "[gameplay][physics]")
{
    Scene scene;
    addFloor(scene);
    const EntityId holder = scene.create("Holder")->id();
    scene.setProperty(holder, "rolo.Transform", "position", Vec2{100, 0});
    scene.setProperty(holder, "rolo.Transform", "scale", Vec2{2, 2});
    const EntityId box = addObject(scene, "Box", {0, 100}, {20, 20}, makeBody({}), ColliderShape::Box, holder);
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 180);
    // In the world: x 100, resting on the floor with its scaled 40-point height.
    CHECK(game.worldPosition(box).x == Approx(100.0f).margin(0.5f));
    CHECK(game.worldPosition(box).y == Approx(20.0f).margin(1.0f));
    // In its parent's coordinates (halved by the parent's scale).
    CHECK(positionOf(game, box).y == Approx(10.0f).margin(0.5f));
}

TEST_CASE("Removed or switched-off objects leave physics; switched on, they return", "[gameplay][physics]")
{
    Scene scene;
    addFloor(scene);
    const EntityId wall = addObject(scene, "Wall", {0, 100}, {200, 20}, makeBody({.type = BodyType::Static}));
    const EntityId box = addObject(scene, "Box", {0, 200}, {20, 20}, makeBody({}));
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 120);
    CHECK(positionOf(game, box).y == Approx(120.0f).margin(1.0f));   // on the wall
    game.destroy(wall);
    frames(game, 120);
    CHECK(positionOf(game, box).y == Approx(10.0f).margin(1.0f));    // fell to the floor
    CHECK(game.velocity(wall) == Vec2{});
}

TEST_CASE("Behaviours hear about collisions", "[gameplay][physics]")
{
    Scene scene;
    const EntityId floor = addFloor(scene);
    const EntityId box = addObject(scene, "Box", {0, 50}, {20, 20}, makeBody({}));
    scene.addComponent(box, std::make_unique<Bumper>());
    Logger log;
    Game game(log);
    game.start(scene);
    frames(game, 60);
    const auto& hits = game.scene().find(box)->get<Bumper>()->hits;
    REQUIRE(hits.size() == 1);
    CHECK(hits[0] == floor);
}

TEST_CASE("Scripts push bodies, ask if they are on the ground, and hear collisions", "[gameplay][physics][script]")
{
    ScriptFolder p;
    const AssetId script = p.write(R"(
        function start(self)
            self.jumps = 0
        end
        function update(self, dt)
            local vx, vy = self.object:velocity()
            if self.object:isOnGround() and self.jumps == 0 then
                self.jumps = 1
                self.object:applyImpulse(0, 400)
                print("jump from " .. #self.object:contacts() .. " contact")
            end
            if self.jumps == 1 and vy < 0 then
                self.jumps = 2
                print("falling again")
                local gx, gy = game.gravity()
                print("gravity " .. gx .. " " .. gy)
                self.object:setVelocity(50, vy)
                self.object:setSpin(0)
                assert(self.object:spin() == 0)
            end
        end
        function collide(self, other)
            print("hit " .. other:name())
        end
    )");
    Scene scene;
    addFloor(scene);
    const EntityId hero = addObject(scene, "Hero", {0, 20}, {20, 40}, makeBody({.lockRotation = true}),
                                    ColliderShape::Capsule);
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(hero, std::move(component));
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 120);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    CHECK(p.problems.empty());
    const std::vector<std::string> expected{"scripts/test.lua: hit Floor", "scripts/test.lua: jump from 1 contact",
                                            "scripts/test.lua: falling again", "scripts/test.lua: gravity 0.0 -980.0",
                                            "scripts/test.lua: hit Floor"};
    std::vector<std::string> script_lines;
    for (const std::string& line : p.printed) {
        if (line.rfind("scripts/", 0) == 0) {
            script_lines.push_back(line);
        }
    }
    CHECK(script_lines == expected);
    CHECK(positionOf(game, hero).x > 10.0f);   // moved sideways by setVelocity
}

TEST_CASE("Scripts read gamepads", "[gameplay][gamepad][script]")
{
    ScriptFolder p;
    const AssetId script = p.write(R"(
        function update(self, dt)
            local x, y = input.stick("left")
            print("pad " .. tostring(input.gamepad()) .. " " .. tostring(input.gamepad(2))
                  .. " a " .. tostring(input.isDown("pad a")) .. " " .. tostring(input.wasPressed("Gamepad A", 1))
                  .. " start " .. tostring(input.isDown("pad start"))
                  .. " stick " .. string.format("%.2f %.2f", x, y)
                  .. " trigger " .. string.format("%.2f", input.trigger("right")))
        end
    )");
    Scene scene;
    const EntityId thing = scene.create("Thing")->id();
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(thing, std::move(component));
    Game game(p.log, &p.assets);
    game.start(scene);
    GamepadState pad;
    pad.connected = true;
    pad.buttons[static_cast<std::size_t>(GamepadButton::A)] = true;
    pad.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = 0.6f;
    pad.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = 0.5f;
    game.input().setGamepad(0, pad);
    frames(game, 1);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    CHECK(p.problems.empty());
    CHECK(std::find(p.printed.begin(), p.printed.end(),
                    "scripts/test.lua: pad true false a true true start false stick 0.00 0.50 trigger 0.50")
          != p.printed.end());

    for (const char* bad : {"input.isDown('pad z')", "input.isDown('pad a', 5)", "input.stick('middle')"}) {
        INFO(bad);
        ScriptFolder q;
        const AssetId wrong = q.write(std::string("function start(self) ") + bad + " end");
        Scene s2;
        const EntityId t = s2.create("T")->id();
        auto c = std::make_unique<Script>();
        c->script = wrong;
        s2.addComponent(t, std::move(c));
        Game g(q.log, &q.assets);
        g.start(s2);
        frames(g, 1);
        CHECK(q.problems.size() == 1);
    }
}

TEST_CASE("Physics in scripts needs a RigidBody, and says so", "[gameplay][physics][script]")
{
    ScriptFolder p;
    const AssetId script = p.write("function start(self) self.object:setVelocity(1, 2) end");
    Scene scene;
    const EntityId thing = scene.create("Thing")->id();
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(thing, std::move(component));
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.problems.size() == 1);
    CHECK(p.problems[0].find("this object has no Rigid Body component") != std::string::npos);
}

TEST_CASE("Physics plays the same way every time", "[gameplay][physics]")
{
    Scene scene;
    addFloor(scene);
    for (int i = 0; i < 12; ++i) {
        addObject(scene, "Box", {static_cast<float>(i % 4) * 25.0f, 40.0f + static_cast<float>(i) * 30.0f}, {24, 24},
                  makeBody({.bounciness = 0.3f}));
    }
    const auto play = [&] {
        Logger log;
        Game game(log);
        game.start(scene);
        frames(game, 240);
        std::vector<float> out;
        game.scene().forEach([&](const Entity& e, int) {
            out.push_back(e.transform().position.x);
            out.push_back(e.transform().position.y);
            out.push_back(e.transform().rotation);
        });
        return out;
    };
    CHECK(play() == play());
}

TEST_CASE("Scenes save gravity (when changed), rigid bodies and collider shapes", "[scene][physics]")
{
    Scene scene("Level");
    const EntityId ball = addObject(scene, "Ball", {0, 0}, {20, 20}, makeBody({.mass = 3.0f, .bounciness = 0.5f}),
                                    ColliderShape::Circle);
    std::string json = serializeScene(scene);
    CHECK(json.find("\"gravity\"") == std::string::npos);   // the default is not written
    CHECK(json.find("\"shape\": \"circle\"") != std::string::npos);

    scene.setGravity({0, 0});
    json = serializeScene(scene);
    CHECK(json.find("\"gravity\": [0, 0]") != std::string::npos);

    const ComponentRegistry registry = ComponentRegistry::withBuiltins();
    SceneLoadResult loaded = parseScene(json, registry);
    REQUIRE(loaded.ok());
    CHECK(loaded.issues.empty());
    CHECK(loaded.scene->gravity() == Vec2{0, 0});
    const Entity* read = loaded.scene->find(ball);
    REQUIRE(read != nullptr);
    REQUIRE(read->get<RigidBody>() != nullptr);
    CHECK(read->get<RigidBody>()->mass == 3.0f);
    CHECK(read->get<RigidBody>()->bounciness == 0.5f);
    CHECK(read->get<Collider>()->shape == ColliderShape::Circle);

    // A scene from before physics: no gravity, colliders without a shape.
    const std::string old = R"({"format": "rolo.scene", "version": 1, "name": "Old", "entities": [
        {"id": "0000000000000001", "name": "Wall", "active": true, "components": [
            {"type": "rolo.Transform", "position": [0, 0], "rotation": 0, "scale": [1, 1]},
            {"type": "rolo.Collider", "size": [10, 10], "offset": [0, 0], "solid": true}]}]})";
    SceneLoadResult before = parseScene(old, registry);
    REQUIRE(before.ok());
    CHECK(before.issues.empty());
    CHECK(before.scene->gravity() == Scene::kDefaultGravity);
    CHECK(before.scene->find(EntityId{1})->get<Collider>()->shape == ColliderShape::Box);

    // Bad gravity: a warning, and the default.
    const std::string bad = R"({"format": "rolo.scene", "version": 1, "name": "Bad", "gravity": "down", "entities": []})";
    SceneLoadResult badLoaded = parseScene(bad, registry);
    REQUIRE(badLoaded.ok());
    REQUIRE(badLoaded.issues.size() == 1);
    CHECK(badLoaded.scene->gravity() == Scene::kDefaultGravity);
}

namespace {
struct Bad : ComponentBase<Bad> {
    int type = 0;
    static const ComponentType& componentType()
    {
        static const ComponentType type = ComponentTypeBuilder<Bad>("test.Bad", "Bad").build();
        return type;
    }
};
} // namespace

TEST_CASE("A property cannot take the names scene files use for components", "[scene]")
{
    CHECK_THROWS_AS(ComponentTypeBuilder<Bad>("test.Bad", "Bad").property("type", "Type", &Bad::type), std::logic_error);
    CHECK_THROWS_AS(ComponentTypeBuilder<Bad>("test.Bad", "Bad").property("data", "Data", &Bad::type), std::logic_error);
}
