// The ready-made behaviours (rolo/gameplay/BuiltinBehaviours.hpp) that let
// a game be made without C++: walking, doors with arrival points, camera bounds.

#include "support/TempFolder.hpp"

#include "rolo/assets/Animation.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/BuiltinBehaviours.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rolo;

namespace {

struct Captured {
    Logger log;
    std::vector<std::string> warnings;
    Captured()
    {
        log.addSink([this](const LogMessage& m) {
            if (m.level >= LogLevel::Warning) {
                warnings.push_back(std::string(m.text));
            }
        });
    }
};

// A player: a Collider, an Interactor and a TopDownController.
EntityId addPlayer(Scene& scene, Vec2 at)
{
    const EntityId id = scene.create("Player")->id();
    scene.setProperty(id, "rolo.Transform", "position", at);
    auto collider = std::make_unique<Collider>();
    collider->size = {20, 20};
    scene.addComponent(id, std::move(collider));
    scene.addComponent(id, std::make_unique<Interactor>());
    scene.addComponent(id, std::make_unique<TopDownController>());
    return id;
}

void frames(Game& game, int n)
{
    for (int i = 0; i < n; ++i) {
        game.update(1.0f / 60.0f);
    }
}

} // namespace

TEST_CASE("The built-in behaviours are registered with the gameplay components", "[gameplay][builtin]")
{
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameplayComponents(registry);
    for (const char* id : {"rolo.TopDownController", "rolo.SceneDoor", "rolo.CameraBounds"}) {
        CHECK(registry.find(id) != nullptr);
    }
}

TEST_CASE("The top-down controller walks, stops at walls and faces the way it goes", "[gameplay][builtin]")
{
    Captured c;
    Scene scene;
    const EntityId player = addPlayer(scene, {0, 0});
    scene.addComponent(player, std::make_unique<Sprite>());
    const EntityId wall = scene.create("Wall")->id();
    scene.setProperty(wall, "rolo.Transform", "position", Vec2{-100, 0});
    scene.addComponent(wall, std::make_unique<Collider>());   // 32 x 32, solid

    Game game(c.log, nullptr);
    game.start(scene);
    game.input().press(Key::Left);
    frames(game, 60);   // 150 pt/s for a second, but the wall is in the way
    const Box box = *game.colliderBox(game.find("Player"));
    CHECK(box.min.x == Catch::Approx(-84.0f));   // wall's right edge: -100 + 16
    CHECK(game.scene().find(game.find("Player"))->get<Sprite>()->flipX);   // no clips: flips to face left
    game.input().release(Key::Left);
    game.input().press(Key::Right);
    frames(game, 2);
    CHECK_FALSE(game.scene().find(game.find("Player"))->get<Sprite>()->flipX);
}

TEST_CASE("The top-down controller plays walk and idle clips when the animations have them", "[gameplay][builtin]")
{
    const auto folder = test::uniqueTempFolder("builtin-anim");
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "project.rolo") << R"({"format": "rolo.project", "version": 1, "name": "T"})";
    {
        AssetDatabase db(folder);
        const AssetId image = *db.import(std::filesystem::path(ROLO_TEST_DATA_DIR) / "tiles_16px.png");
        const AssetId set = *db.createAnimationSet(image, {16, 16}, "Hero");
        AnimationSet clips = *db.find(set)->animation;
        clips.clips = {AnimationClip{"idle_down", {0}, 1.0f, true}, AnimationClip{"walk_up", {1, 2}, 8.0f, true},
                       AnimationClip{"idle_up", {1}, 1.0f, true}};
        REQUIRE(db.writeText(set, serializeAnimationSet(clips)));

        Captured c;
        Scene scene;
        const EntityId player = addPlayer(scene, {0, 0});
        auto animator = std::make_unique<Animator>();
        animator->animations = set;
        scene.addComponent(player, std::move(animator));
        Game game(c.log, &db);
        game.start(scene);
        frames(game, 1);
        CHECK(game.currentClip(player) == "idle_down");
        game.input().press(Key::Up);
        frames(game, 5);
        CHECK(game.currentClip(player) == "walk_up");
        game.input().release(Key::Up);
        frames(game, 1);
        CHECK(game.currentClip(player) == "idle_up");
        game.input().press(Key::Left);   // the set has no walk_left: the clip stays
        frames(game, 2);
        CHECK(game.currentClip(player) == "idle_up");
    }
    std::filesystem::remove_all(folder);
}

TEST_CASE("Walking into a scene door arrives at the named object", "[gameplay][builtin]")
{
    Captured c;
    const AssetId village = AssetId::parse("1000000000000001").value();
    const AssetId woods = AssetId::parse("1000000000000002").value();
    const auto makeScene = [&](AssetId id) {
        Scene s(id == village ? "Village" : "Woods");
        const EntityId door = s.create("Door")->id();
        s.setProperty(door, "rolo.Transform", "position", Vec2{500, 0});
        auto area = std::make_unique<Collider>();
        area->solid = false;
        s.addComponent(door, std::move(area));
        auto sceneDoor = std::make_unique<SceneDoor>();
        sceneDoor->scene = id == village ? woods : village;
        sceneDoor->arriveAt = id == village ? "From Village" : "Nowhere";
        s.addComponent(door, std::move(sceneDoor));
        const EntityId spot = s.create("From Village")->id();
        s.setProperty(spot, "rolo.Transform", "position", Vec2{-300, 40});
        addPlayer(s, {0, 0});
        return s;
    };
    GameSetup setup;
    setup.loadScene = [&](AssetId id, std::string&) -> std::optional<Scene> { return makeScene(id); };
    Game game(c.log, std::move(setup));
    game.start(makeScene(village), village);
    game.transform(game.find("Player"))->position = {500, 0};   // into the doorway
    frames(game, 2);
    REQUIRE(game.scene().name() == "Woods");
    CHECK(game.worldPosition(game.find("Player")).x == Catch::Approx(-300.0f));
    CHECK(game.worldPosition(game.find("Player")).y == Catch::Approx(40.0f));

    // An arrival point the next scene lacks: a warning, and the scene's own start.
    game.transform(game.find("Player"))->position = {500, 0};
    frames(game, 2);
    REQUIRE(game.scene().name() == "Village");
    CHECK(game.worldPosition(game.find("Player")).x == Catch::Approx(0.0f));
    REQUIRE(c.warnings.size() == 1);
    CHECK(c.warnings[0].find("no object named 'Nowhere'") != std::string::npos);
}

TEST_CASE("Camera bounds keep the view inside the map", "[gameplay][builtin]")
{
    Captured c;
    Scene scene;
    const EntityId player = addPlayer(scene, {100, 100});
    const EntityId cam = scene.create("Camera", player)->id();
    scene.addComponent(cam, std::make_unique<Camera>());
    auto bounds = std::make_unique<CameraBounds>();
    bounds->min = {0, 0};
    bounds->max = {2000, 1000};
    scene.addComponent(cam, std::move(bounds));

    Game game(c.log, nullptr);
    game.start(scene);
    frames(game, 1);
    CHECK(game.worldPosition(game.find("Camera")).x == Catch::Approx(640.0f));   // half the view from the left edge
    CHECK(game.worldPosition(game.find("Camera")).y == Catch::Approx(360.0f));
    game.transform(player)->position = {1000, 500};
    frames(game, 1);
    CHECK(game.worldPosition(game.find("Camera")).x == Catch::Approx(1000.0f));   // free to follow in the middle
    game.transform(player)->position = {1990, 990};
    frames(game, 1);
    CHECK(game.worldPosition(game.find("Camera")).x == Catch::Approx(1360.0f));
    CHECK(game.worldPosition(game.find("Camera")).y == Catch::Approx(640.0f));

    // A screen that shows more than the game's size (a project that fills
    // screens of other shapes): the view is wider, so the camera stops sooner.
    game.setViewSize({1700.0f, 720.0f});
    frames(game, 1);
    CHECK(game.worldPosition(game.find("Camera")).x == Catch::Approx(2000.0f - 850.0f));
    CHECK(game.worldPosition(game.find("Camera")).y == Catch::Approx(640.0f));
    CHECK(c.warnings.empty());
}
