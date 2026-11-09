// The Lantern Keeper (examples/lantern_keeper), played from its real project
// files: the whole story from the first line to the ending, through the same
// game rules, content and behaviours the player program uses.

#include "support/TempFolder.hpp"
#include "KeeperBehaviours.hpp"

#include "rolo/audio/AudioEngine.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/content/Notation.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/GameModule.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>

using namespace rolo;

namespace {

const std::filesystem::path kKeeper = LANTERN_KEEPER_PROJECT_DIR;

struct KeeperGame {
    Logger log;
    std::vector<std::string> errors;   // errors and warnings
    std::vector<std::string> infos;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    AssetDatabase assets{kKeeper, AssetDatabase::Access::ReadOnly};
    ContentLibrary content;
    FolderSaveStore saves{rolo::test::uniqueTempFolder("keeper-saves")};
    AudioEngine audio{log, {.device = false}};   // decodes the game's sounds for real, without a sound card
    std::unique_ptr<Game> game;

    KeeperGame()
    {
        log.addSink([this](const LogMessage& m) {
            (m.level >= LogLevel::Warning ? errors : infos).push_back(m.text);
        });
        registerGameplayComponents(registry);
        registerGameModules(registry, log);
        for (const FileIssue& issue : assets.refresh()) {
            errors.push_back(issue.toString());
        }
        std::vector<FileIssue> issues;
        content = ContentLibrary::load(assets, &issues);
        for (FileIssue& issue : content.check()) {
            issues.push_back(std::move(issue));
        }
        for (const FileIssue& issue : issues) {
            errors.push_back(issue.toString());
        }
        GameSetup setup;
        setup.assets = &assets;
        setup.content = &content;
        setup.saves = &saves;
        audio.setAssets(&assets);
        setup.sound = &audio;
        setup.gameName = "The Lantern Keeper";
        setup.loadScene = [this](AssetId id, std::string& error) { return load(id, error); };
        game = std::make_unique<Game>(log, std::move(setup));
    }
    ~KeeperGame()
    {
        std::error_code ec;
        std::filesystem::remove_all(saves.folder(), ec);
    }

    std::optional<Scene> load(AssetId id, std::string& error) const
    {
        const AssetRecord* record = assets.find(id);
        if (record == nullptr || record->type != AssetType::Scene) {
            error = "not a scene in this project";
            return std::nullopt;
        }
        SceneLoadResult loaded = loadSceneFile(assets.absolutePath(*record), registry);
        if (!loaded.ok()) {
            error = "could not be read";
            return std::nullopt;
        }
        return std::move(*loaded.scene);
    }

    void start()
    {
        const Project project = *Project::open(kKeeper);
        const AssetRecord* first = assets.findByPath(project.settings().startScene);
        REQUIRE(first != nullptr);
        std::string error;
        auto scene = load(first->id, error);
        REQUIRE(scene);
        game->start(*scene, first->id);
        frames(1);
    }

    void frames(int n)
    {
        for (int i = 0; i < n; ++i) {
            game->update(1.0f / 60.0f);
        }
    }
    void tap(Key key)
    {
        game->input().press(key);
        frames(1);
        game->input().release(key);
        frames(1);
    }
    EntityId player() const { return game->find("Player"); }
    /// Puts the player's feet (its collider) on a point, as if it walked there.
    void stand(Vec2 at)
    {
        const Vec2 offset = at - game->colliderBox(player())->center();
        game->transform(player())->position = game->scene().find(player())->transform().position + offset;
        frames(1);
    }
    /// The area to stand in to use an object: its child with a Trigger (a
    /// talk area, a lock), or the object itself.
    Vec2 useSpot(std::string_view name) const
    {
        const EntityId id = game->find(name);
        REQUIRE_FALSE(id.isNull());
        for (const EntityId child : game->scene().childrenOf(id)) {
            if (game->scene().find(child)->get<Trigger>() != nullptr) {
                return game->colliderBox(child)->center();
            }
        }
        return game->colliderBox(id)->center();
    }
    std::string text(std::string_view name) const
    {
        return game->scene().find(game->find(name))->get<Text>()->text;
    }
    /// Reads a dialogue to its end, taking the first answer each time.
    std::vector<std::string> readDialogue()
    {
        std::vector<std::string> lines;
        for (int guard = 0; guard < 40 && game->isInDialogue(); ++guard) {
            lines.push_back(text("Line"));
            tap(Key::E);
        }
        CHECK_FALSE(game->isInDialogue());
        return lines;
    }
    /// Walks up to something, presses E and reads what follows.
    std::vector<std::string> use(std::string_view name)
    {
        stand(useSpot(name));
        tap(Key::E);
        REQUIRE(game->isInDialogue());
        return readDialogue();
    }
    /// Walks into a door and waits for the next scene.
    void goThrough(std::string_view door, std::string_view nextScene)
    {
        stand(game->colliderBox(game->find(door))->center());
        frames(2);
        REQUIRE(game->scene().name() == nextScene);
    }
    template <class T>
    T variable(std::string_view id) const
    {
        return std::get<T>(*game->content().state().variable(id));
    }
    QuestState quest(std::string_view path) const
    {
        return game->content().state().questState(assets.findByPath(path)->id);
    }
    bool active(std::string_view name) const { return game->scene().find(game->find(name))->isActive(); }
};

bool contains(const std::vector<std::string>& lines, std::string_view text)
{
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

std::string all(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) {
        out += l + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("The Lantern Keeper opens cleanly", "[gameplay][keeper]")
{
    KeeperGame k;
    INFO(all(k.errors));
    CHECK(k.errors.empty());
    for (const char* path : {"scenes/village.lscene", "scenes/woods.lscene", "scenes/mine.lscene"}) {
        INFO(path);
        SceneLoadResult loaded = loadSceneFile(kKeeper / path, k.registry);
        REQUIRE(loaded.ok());
        CHECK(loaded.issues.empty());
        CHECK(checkAssetReferences(*loaded.scene, k.assets).empty());
        std::vector<FileIssue> issues;
        loaded.scene->forEach([&](const Entity& e, int) {
            if (const Trigger* t = e.get<Trigger>()) {
                if (t->reaction.condition) {
                    k.content.checkCondition(*t->reaction.condition, path, issues);
                }
                k.content.checkActions(t->reaction.actions, path, issues);
            }
            if (const Animator* a = e.get<Animator>()) {
                const AssetRecord* set = k.assets.find(a->animations);
                REQUIRE(set != nullptr);
                REQUIRE(set->animation.has_value());
                CHECK(set->animation->findClip(a->clip) != nullptr);
            }
        });
        INFO(issues.size());
        CHECK(issues.empty());
    }
}

TEST_CASE("The Lantern Keeper can be played from the first line to the ending", "[gameplay][keeper]")
{
    KeeperGame k;
    k.start();
    Game& game = *k.game;
    REQUIRE(game.scene().name() == "Village");
    const auto soundFile = [&](const char* name) { return k.assets.findByPath(std::string("assets/sounds/") + name)->id; };
    CHECK(k.audio.music() == soundFile("night.ogg"));

    // The story opens by itself.
    REQUIRE(game.isInDialogue());
    const auto intro = k.readDialogue();
    CHECK(intro.front() == "Emberwick, the night after the great storm.");
    CHECK(k.active("Night"));
    CHECK(k.text("Flame Count") == "Flames  0 / 3");

    // The camera stays inside the map: the player starts near its left edge.
    const Vec2 camera = game.worldPosition(game.find("Camera"));
    CHECK(camera.x == Catch::Approx(640.0f));
    CHECK(game.worldPosition(k.player()).x < 640.0f);

    // Keeper Maren asks for the flames; asking where they went comes back to the question.
    const auto maren = k.use("Keeper Maren");
    CHECK(contains(maren, "blew out the great lantern"));
    CHECK(k.quest("content/lantern.lquest") == QuestState::Active);
    CHECK(k.text("Quest Log").find("The Dark Lantern") != std::string::npos);

    // Wren has lost her cat; Nell saw where it went.
    CHECK(contains(k.use("Wren"), "Have you seen Biscuit?"));
    CHECK(k.quest("content/biscuit.lquest") == QuestState::Active);
    CHECK(contains(k.use("Nell"), "Biscuit ran into the woods"));

    // To Whisperwood, arriving by the road.
    k.goThrough("Road to Whisperwood", "Whisperwood");
    CHECK(k.audio.music() == soundFile("night.ogg"));   // the same music carries on
    const Vec2 arrived = game.worldPosition(k.player());
    CHECK(arrived.x == Catch::Approx(game.worldPosition(game.find("From Village")).x));

    // The mine gate is locked.
    CHECK(contains(k.use("Mine Gate"), "locked tight"));
    CHECK_FALSE(game.find("Mine Gate").isNull());

    // Biscuit, found in the clearing, runs home.
    CHECK(contains(k.use("Biscuit"), "It's Biscuit!"));
    CHECK(k.variable<bool>("cat_found"));
    k.frames(2);
    CHECK(game.find("Biscuit").isNull());

    // The first lost flame, by the pond (with a sparkle).
    const std::size_t sounds = k.audio.playingCount();
    k.stand(game.colliderBox(game.find("Lost Flame"))->center());
    CHECK(k.audio.playingCount() == sounds + 1);
    CHECK(contains(k.readDialogue(), "You found a lost flame! It's warm"));
    CHECK(k.variable<int>("flames") == 1);
    CHECK(k.text("Flame Count") == "Flames  1 / 3");

    // Home again: Biscuit is back, and Wren gives Pip the mine key.
    k.goThrough("Road to Emberwick", "Village");
    CHECK(k.active("Biscuit"));
    CHECK(contains(k.use("Wren"), "the key to the old mine"));
    CHECK(k.quest("content/biscuit.lquest") == QuestState::Completed);
    CHECK(k.variable<bool>("has_key"));

    // Rest at the wayshrine: the game is saved.
    CHECK(contains(k.use("Wayshrine"), "Your journey is saved"));
    CHECK(k.saves.read("quick").has_value());

    // The gate opens with the key, for good, and the way into the mine is clear.
    k.goThrough("Road to Whisperwood", "Whisperwood");
    CHECK(contains(k.use("Mine Gate"), "The gate creaks open"));
    k.frames(2);
    CHECK(game.find("Mine Gate").isNull());
    const Box door = *game.colliderBox(game.find("Into the Mine"));
    k.stand({door.min.x - 200.0f, door.center().y});
    game.input().press(Key::Right);   // walk through where the gate was
    k.frames(120);
    game.input().release(Key::Right);
    REQUIRE(game.scene().name() == "Old Mine");
    CHECK(k.audio.music() == soundFile("mine.ogg"));

    // Two flames in the mine.
    k.stand(game.colliderBox(game.find("Lost Flame North"))->center());
    k.readDialogue();
    k.stand(game.colliderBox(game.find("Lost Flame East"))->center());
    CHECK(contains(k.readDialogue(), "That's the third flame!"));
    CHECK(k.variable<int>("flames") == 3);
    CHECK(contains(k.use("Miners' Sign"), "follow the rails"));

    // Back out and home; the gate stays open.
    k.goThrough("Way Out", "Whisperwood");
    CHECK(k.audio.music() == soundFile("night.ogg"));
    CHECK(game.find("Mine Gate").isNull());
    k.goThrough("Road to Emberwick", "Village");
    CHECK(contains(k.use("Keeper Maren"), "You found all three!"));

    // Light the lantern: the night lifts and the story ends.
    const auto lighting = k.use("Lantern");
    CHECK(contains(lighting, "warm light spills across Emberwick"));
    k.frames(2);
    CHECK(k.variable<bool>("lantern_lit"));
    CHECK(k.active("Lantern Light"));
    CHECK_FALSE(k.active("Night"));
    CHECK(k.quest("content/lantern.lquest") == QuestState::Completed);
    CHECK(k.text("Quest Log").empty());
    k.frames(120);
    CHECK(k.active("The End"));
    CHECK(contains(k.use("Nell"), "I can see my shadow"));

    // Loading the save goes back to the wayshrine: one flame, the key, still dark.
    game.load("quick");
    k.frames(2);
    CHECK(game.scene().name() == "Village");
    CHECK(k.variable<int>("flames") == 1);
    CHECK(k.variable<bool>("has_key"));
    CHECK_FALSE(k.variable<bool>("lantern_lit"));
    CHECK(k.active("Night"));
    CHECK_FALSE(k.active("Lantern Light"));
    CHECK_FALSE(game.isInDialogue());   // the opening only plays once per playthrough

    INFO(all(k.errors));
    CHECK(k.errors.empty());
}

TEST_CASE("Walking into a doorway arrives at the matching spawn point", "[gameplay][keeper]")
{
    KeeperGame k;
    k.start();
    Game& game = *k.game;
    k.readDialogue();
    k.goThrough("Road to Whisperwood", "Whisperwood");
    k.goThrough("Road to Emberwick", "Village");
    // Arrived by the road, not at the village's starting point, and the
    // arrival is used up (a later save or load does not move the player).
    CHECK(game.worldPosition(k.player()).x == Catch::Approx(game.worldPosition(game.find("From Woods")).x));
    CHECK(k.variable<std::string>(keeper::kArrivalVariable).empty());
    // Not standing in the doorway again (no bouncing back and forth).
    k.frames(30);
    CHECK(game.scene().name() == "Village");
}

TEST_CASE("The guide's conditions and actions are written as the editor reads them", "[gameplay][keeper]")
{
    // docs/making-the-lantern-keeper.md types these into the editor. Names
    // resolve to anything here: only the written form is being checked.
    NotationNames names;
    names.findAsset = [](std::string_view, std::string_view, std::string&) -> std::optional<AssetId> {
        return AssetId::parse("4b45455000000001");
    };
    names.findEntity = [](std::string_view, std::string&) -> std::optional<EntityId> {
        return EntityId::parse("4b45010000000001");
    };
    for (const char* condition : {"met_maren or flames >= 1", "flames >= 3", "lantern_lit", "met_wren or cat_found",
                                  "cat_found", "not lantern_lit", "quest lantern is active and flames >= 3"}) {
        std::string error;
        const bool ok = parseConditionNotation(condition, names, &error).has_value();
        INFO(condition << ": " << error);
        CHECK(ok);
    }
    for (const char* action : {"set has_key true", "startDialogue maren", "set met_maren true",
                               "completeObjective biscuit tell", "set cat_found true", "set lantern_lit true",
                               "emit gate_open", "saveGame quick", "setActive Biscuit on",
                               "setActive \"Lantern Light\" on", "setActive Night off", "wait 1.5",
                               "setActive \"The End\" on", "destroy Biscuit", "destroy \"Mine Gate\"",
                               "add flames 1", "destroy this"}) {
        std::string error;
        const bool ok = parseActionNotation(action, names, &error).has_value();
        INFO(action << ": " << error);
        CHECK(ok);
    }
}
