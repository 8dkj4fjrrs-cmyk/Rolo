#include "rolo/gameplay/Behaviour.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/GameModule.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace rolo;

namespace {

// Counts its calls and moves right while the Right key is down.
struct Walker : BehaviourBase<Walker> {
    int starts = 0;
    int updates = 0;
    float speed = 10.0f;

    void start(Game&) override { ++starts; }
    void update(Game& game, float dt) override
    {
        ++updates;
        if (game.input().isDown(Key::Right)) {
            game.move(entity(), {speed * dt, 0.0f});
        }
    }
    static const ComponentType& componentType()
    {
        static const ComponentType type = ComponentTypeBuilder<Walker>("test.Walker", "Walker")
                                              .category("Gameplay")
                                              .property("speed", "Speed", &Walker::speed)
                                              .build();
        return type;
    }
};

// Fails on its second update.
struct Fragile : BehaviourBase<Fragile> {
    int updates = 0;
    void update(Game&, float) override
    {
        if (++updates == 2) {
            throw std::runtime_error("the bridge collapsed");
        }
    }
    static const ComponentType& componentType()
    {
        static const ComponentType type = ComponentTypeBuilder<Fragile>("test.Fragile", "Fragile").build();
        return type;
    }
};

// Removes itself, or changes scene, when Space is pressed.
struct Trigger : BehaviourBase<Trigger> {
    AssetId next;
    void update(Game& game, float) override
    {
        if (!game.input().wasPressed(Key::Space)) {
            return;
        }
        if (next.isNull()) {
            game.destroy(entity());
        } else {
            game.changeScene(next);
        }
    }
    static const ComponentType& componentType()
    {
        static const ComponentType type =
            ComponentTypeBuilder<Trigger>("test.Trigger", "Trigger").property("next", "Next", &Trigger::next).build();
        return type;
    }
};

// On its first update, changes the scene at once (not at the end of the
// frame, as Game::destroy does): removes `victim` and gives `host` a Walker.
struct Meddler : BehaviourBase<Meddler> {
    EntityId victim;
    EntityId host;
    bool done = false;
    void update(Game& game, float) override
    {
        if (!done) {
            done = true;
            game.scene().remove(victim);
            game.scene().addComponent(host, std::make_unique<Walker>());
        }
    }
    static const ComponentType& componentType()
    {
        static const ComponentType type = ComponentTypeBuilder<Meddler>("test.Meddler", "Meddler").build();
        return type;
    }
};

struct Captured {
    Logger log;
    std::vector<std::string> errors;
    std::vector<std::string> infos;
    Captured()
    {
        log.addSink([this](const LogMessage& r) {
            (r.level == LogLevel::Error ? errors : infos).push_back(std::string(r.text));
        });
    }
};

template <class T>
const T& behaviourOf(const Game& game, EntityId id)
{
    return *game.scene().find(id)->get<T>();
}

} // namespace

TEST_CASE("Behaviours start once, update every frame, on a copy of the scene", "[gameplay][game]")
{
    Captured c;
    Scene edited("Level");
    const EntityId hero = edited.create("Hero")->id();
    edited.addComponent(hero, std::make_unique<Walker>());

    Game game(c.log, nullptr);
    game.start(edited);
    REQUIRE(game.isRunning());
    game.input().press(Key::Right);
    game.update(0.05f);
    game.update(0.05f);
    CHECK(behaviourOf<Walker>(game, hero).starts == 1);
    CHECK(behaviourOf<Walker>(game, hero).updates == 2);
    CHECK(game.scene().find(hero)->transform().position.x == Catch::Approx(1.0f));   // 10 pt/s for 0.1 s
    CHECK(game.time() == Catch::Approx(0.1));
    CHECK(game.frameCount() == 2);

    // The scene being edited never changed.
    CHECK(edited.find(hero)->transform().position.x == 0.0f);
    CHECK(edited.find(hero)->get<Walker>()->updates == 0);

    game.stop();
    CHECK_FALSE(game.isRunning());
    CHECK(game.sharedScene() == nullptr);
    CHECK(c.errors.empty());
}

TEST_CASE("Long frames are cut short so things cannot jump through walls", "[gameplay][game]")
{
    Captured c;
    Scene scene;
    const EntityId hero = scene.create("Hero")->id();
    scene.addComponent(hero, std::make_unique<Walker>());
    Game game(c.log, nullptr);
    game.start(scene);
    game.input().press(Key::Right);
    game.update(5.0f);   // e.g. the program was stuck for five seconds
    CHECK(game.deltaTime() == Catch::Approx(Game::kMaxStep));
    CHECK(game.scene().find(hero)->transform().position.x == Catch::Approx(10.0f * Game::kMaxStep));
}

TEST_CASE("Inactive objects' behaviours do not run", "[gameplay][game]")
{
    Captured c;
    Scene scene;
    const EntityId parent = scene.create("Parent")->id();
    const EntityId hero = scene.create("Hero", parent)->id();
    scene.addComponent(hero, std::make_unique<Walker>());
    scene.setActive(parent, false);
    Game game(c.log, nullptr);
    game.start(scene);
    game.update(0.1f);
    CHECK(behaviourOf<Walker>(game, hero).starts == 0);
    game.scene().setActive(parent, true);
    game.update(0.1f);
    CHECK(behaviourOf<Walker>(game, hero).starts == 1);
}

TEST_CASE("A failing behaviour is reported and switched off; the rest keeps running", "[gameplay][game]")
{
    Captured c;
    Scene scene;
    const EntityId bridge = scene.create("Bridge")->id();
    scene.addComponent(bridge, std::make_unique<Fragile>());
    const EntityId hero = scene.create("Hero")->id();
    scene.addComponent(hero, std::make_unique<Walker>());

    Game game(c.log, nullptr);
    game.start(scene);
    for (int i = 0; i < 4; ++i) {
        game.update(0.1f);
    }
    REQUIRE(c.errors.size() == 1);
    CHECK(c.errors[0].find("Fragile on 'Bridge' failed in update(): the bridge collapsed") != std::string::npos);
    CHECK(behaviourOf<Fragile>(game, bridge).updates == 2);   // not called again
    CHECK(behaviourOf<Walker>(game, hero).updates == 4);

    // Playing again starts fresh.
    game.start(scene);
    game.update(0.1f);
    CHECK(behaviourOf<Fragile>(game, bridge).updates == 1);
}

TEST_CASE("Objects are removed at the end of the frame", "[gameplay][game]")
{
    Captured c;
    Scene scene;
    const EntityId gem = scene.create("Gem")->id();
    scene.addComponent(gem, std::make_unique<Trigger>());
    scene.create("Shine", gem);
    Game game(c.log, nullptr);
    game.start(scene);
    game.update(0.1f);
    CHECK(game.scene().contains(gem));
    game.input().press(Key::Space);
    game.update(0.1f);
    CHECK_FALSE(game.scene().contains(gem));
    CHECK(game.scene().empty());   // with its child
}

TEST_CASE("Scene changes load the next scene at the end of the frame", "[gameplay][game]")
{
    Captured c;
    const AssetId caveId = *AssetId::parse("00000000000000ca");
    Scene meadow("Meadow");
    const EntityId door = meadow.create("Door")->id();
    auto trigger = std::make_unique<Trigger>();
    trigger->next = caveId;
    meadow.addComponent(door, std::move(trigger));
    Scene cave("Cave");
    const EntityId bat = cave.create("Bat")->id();
    cave.addComponent(bat, std::make_unique<Walker>());

    std::vector<AssetId> requested;
    Game game(c.log, nullptr, [&](AssetId id, std::string& error) -> std::optional<Scene> {
        requested.push_back(id);
        if (id == caveId) {
            return cave;
        }
        error = "no such scene";
        return std::nullopt;
    });
    game.start(meadow);
    const auto before = game.sharedScene();
    game.input().press(Key::Space);
    game.update(0.1f);
    CHECK(requested == std::vector<AssetId>{caveId});
    CHECK(game.scene().name() == "Cave");
    CHECK(game.sharedScene() != before);   // the renderer sees a new scene
    CHECK(behaviourOf<Walker>(game, bat).starts == 0);   // starts next frame
    game.update(0.1f);
    CHECK(behaviourOf<Walker>(game, bat).starts == 1);

    // A scene that cannot be loaded: an error, and the game stays where it is.
    game.start(meadow);
    game.scene().edit<Trigger>(door)->next = *AssetId::parse("00000000000000ff");
    game.input().press(Key::Space);
    game.update(0.1f);
    CHECK(game.scene().name() == "Meadow");
    REQUIRE(c.errors.size() == 1);
    CHECK(c.errors[0].find("no such scene") != std::string::npos);
}

TEST_CASE("Moving a child converts world movement into its parent's space", "[gameplay][game]")
{
    Captured c;
    Scene scene;
    const EntityId parent = scene.create("Parent")->id();
    scene.setProperty(parent, "rolo.Transform", "scale", Vec2{2, 2});
    const EntityId child = scene.create("Child", parent)->id();
    Game game(c.log, nullptr);
    game.start(scene);
    game.move(child, {10, 0});
    CHECK(game.worldPosition(child).x == Catch::Approx(10.0f));
    CHECK(game.scene().find(child)->transform().position.x == Catch::Approx(5.0f));
}

TEST_CASE("Game modules register their behaviours", "[gameplay][module]")
{
    Captured c;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    {
        GameModuleRegistry module(registry, c.log, "test_game");
        module.add(Walker::componentType());
        module.add(Walker::componentType());   // twice: reported, not fatal
    }
    CHECK(registry.find("test.Walker") != nullptr);
    REQUIRE(c.errors.size() == 1);
    CHECK(c.errors[0].find("Game module 'test_game' could not add 'test.Walker'") != std::string::npos);
    CHECK(registry.find("rolo.Collider") != nullptr);
}

TEST_CASE("Behaviours may add and remove objects and components in the middle of a frame", "[gameplay][game]")
{
    // The game keeps its list of behaviours from frame to frame; a change in
    // the middle of a frame must not leave it running something removed.
    Captured c;
    Scene edited("Level");
    const EntityId first = edited.create("First")->id();
    const EntityId meddler = edited.create("Meddler")->id();
    const EntityId victim = edited.create("Victim")->id();
    const EntityId last = edited.create("Last")->id();
    const EntityId host = edited.create("Host")->id();
    edited.addComponent(first, std::make_unique<Walker>());
    auto m = std::make_unique<Meddler>();
    m->victim = victim;
    m->host = host;
    edited.addComponent(meddler, std::move(m));
    edited.addComponent(victim, std::make_unique<Walker>());   // listed after the meddler: must not run
    edited.addComponent(last, std::make_unique<Walker>());

    Game game(c.log, nullptr);
    game.start(edited);
    game.update(0.05f);
    CHECK_FALSE(game.scene().contains(victim));
    CHECK(behaviourOf<Walker>(game, first).updates == 1);
    CHECK(behaviourOf<Walker>(game, last).updates == 1);    // those after the change still run
    CHECK(behaviourOf<Walker>(game, host).updates == 0);    // new this frame: starts next frame
    game.update(0.05f);
    game.update(0.05f);
    CHECK(behaviourOf<Walker>(game, first).updates == 3);
    CHECK(behaviourOf<Walker>(game, last).updates == 3);
    CHECK(behaviourOf<Walker>(game, host).starts == 1);
    CHECK(behaviourOf<Walker>(game, host).updates == 2);
    CHECK(c.errors.empty());

    // game.find follows renames and removals.
    CHECK(game.find("Last") == last);
    game.scene().rename(last, "Renamed");
    CHECK(game.find("Last").isNull());
    CHECK(game.find("Renamed") == last);
    CHECK(game.find("Victim").isNull());
}
