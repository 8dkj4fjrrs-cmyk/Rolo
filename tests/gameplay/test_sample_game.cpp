// The sample game (examples/sample_game), played from its real project files:
// the module, the scenes, the content, and a whole playthrough.

#include "support/TempFolder.hpp"
#include "SampleBehaviours.hpp"

#include "rolo/content/ContentLibrary.hpp"
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

const std::filesystem::path kSample = ROLO_SAMPLE_PROJECT_DIR;

// Everything the player needs to play the sample, read-only like a game.
struct SampleProject {
    Logger log;
    std::vector<std::string> errors;   // errors and warnings
    std::vector<std::string> infos;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    AssetDatabase assets{kSample, AssetDatabase::Access::ReadOnly};
    ContentLibrary content;
    FolderSaveStore saves{rolo::test::uniqueTempFolder("sample-saves")};
    std::unique_ptr<Game> game;

    SampleProject()
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
        std::filesystem::remove_all(saves.folder());
        GameSetup setup;
        setup.assets = &assets;
        setup.content = &content;
        setup.saves = &saves;
        setup.gameName = "Rolo Sample";
        setup.loadScene = [this](AssetId id, std::string& error) { return load(id, error); };
        game = std::make_unique<Game>(log, std::move(setup));
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

    void startMeadow()
    {
        std::string error;
        const AssetRecord* meadow = assets.findByPath("scenes/meadow.lscene");
        REQUIRE(meadow != nullptr);
        auto scene = load(meadow->id, error);
        REQUIRE(scene);
        game->start(*scene, meadow->id);
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
    /// Puts the player's feet (its collider) on a point, as if it walked there.
    void stand(Vec2 at)
    {
        const EntityId player = game->find("Player");
        const Vec2 offset = at - game->colliderBox(player)->center();
        game->transform(player)->position = game->scene().find(player)->transform().position + offset;
        frames(1);
    }
    int gems() const { return std::get<int>(*game->content().state().variable("gems")); }
    std::string text(std::string_view name) { return game->scene().find(game->find(name))->get<Text>()->text; }
    bool logged(std::string_view line) const { return std::find(infos.begin(), infos.end(), line) != infos.end(); }
};

std::string all(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) {
        out += l + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("The sample game module is linked and registers its behaviours", "[gameplay][sample]")
{
    Logger log;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameModules(registry, log);
    REQUIRE(linkedGameModules().size() == 2);   // with The Lantern Keeper's
    CHECK(linkedGameModules()[0] == "sample_game");
    CHECK(linkedGameModules()[1] == "keeper");
    for (const char* id : {"sample.PlayerController", "sample.SceneDoor", "sample.Collectible", "sample.Spinner"}) {
        const ComponentType* type = registry.find(id);
        REQUIRE(type != nullptr);
        CHECK(type->category == "Gameplay");
    }
}

TEST_CASE("The sample project opens cleanly: scenes, assets, content and behaviours", "[gameplay][sample]")
{
    SampleProject p;
    INFO(all(p.errors));
    CHECK(p.errors.empty());
    std::string error;
    const auto project = Project::open(kSample, &error);
    REQUIRE(project);
    for (const char* path : {"scenes/meadow.lscene", "scenes/cave.lscene"}) {
        SceneLoadResult loaded = loadSceneFile(kSample / path, p.registry);
        REQUIRE(loaded.ok());
        CHECK(loaded.issues.empty());   // no unknown components: the modules are linked
        CHECK(checkAssetReferences(*loaded.scene, p.assets).empty());
        // Every trigger's condition and actions fit the content.
        std::vector<FileIssue> issues;
        loaded.scene->forEach([&](const Entity& e, int) {
            if (const Trigger* t = e.get<Trigger>()) {
                if (t->reaction.condition) {
                    p.content.checkCondition(*t->reaction.condition, path, issues);
                }
                p.content.checkActions(t->reaction.actions, path, issues);
            }
        });
        CHECK(issues.empty());
        // Every Animator plays a clip its animations have.
        loaded.scene->forEach([&](const Entity& e, int) {
            if (const Animator* a = e.get<Animator>()) {
                const AssetRecord* set = p.assets.find(a->animations);
                REQUIRE(set != nullptr);
                REQUIRE(set->animation.has_value());
                INFO(e.name() << " plays " << a->clip);
                CHECK(set->animation->findClip(a->clip) != nullptr);
            }
        });
    }
    CHECK(project->settings().startScene == "scenes/meadow.lscene");
    CHECK(p.content.variables().size() == 3);
}

TEST_CASE("Playing the sample: walls stop the player, the boulder blocks the cave", "[gameplay][sample]")
{
    SampleProject p;
    p.startMeadow();
    Game& game = *p.game;
    const EntityId player = game.find("Player");
    const float startX = game.worldPosition(player).x;

    // Walking left: stopped by the wall around the meadow (cell 0 is 0..32).
    CHECK(game.currentClip(player) == "idle_down");
    game.input().press(Key::Left);
    p.frames(120);
    CHECK(game.currentClip(player) == "walk_left");   // the hero's walk animation
    game.input().release(Key::Left);
    p.frames(1);
    CHECK(game.currentClip(player) == "idle_left");   // standing, still facing left
    CHECK(game.colliderBox(player)->min.x == Catch::Approx(32.0f).margin(0.01f));
    CHECK(game.worldPosition(player).x < startX);

    // Walking right along the path: the boulder stops the player.
    game.input().press(Key::Right);
    p.frames(900);
    CHECK(game.scene().name() == "Meadow");
    const EntityId boulder = game.find("Boulder");
    REQUIRE_FALSE(boulder.isNull());
    CHECK(game.colliderBox(player)->max.x == Catch::Approx(game.colliderBox(boulder)->min.x).margin(0.01f));

    // Once the cave is open, the boulder rolls aside and the path leads on.
    game.content().state().setVariable("cave_open", true);
    p.frames(300);
    CHECK(p.logged("The boulder rolls aside."));
    CHECK(game.scene().name() == "Cave");
    INFO(all(p.errors));
    CHECK(p.errors.empty());
}

TEST_CASE("Playing the sample all the way: elder, gems, quest, boulder, cave, saving", "[gameplay][sample]")
{
    SampleProject p;
    p.startMeadow();
    Game& game = *p.game;
    const auto at = [&](std::string_view name) { return game.worldPosition(game.find(name)); };

    CHECK(p.text("Gem Count") == "Gems: 0");
    CHECK_FALSE(game.scene().find(game.find("Dialogue Box"))->isActive());

    // Talk to the elder and take the quest.
    const Vec2 elder = at("Elder");
    p.stand({elder.x + 26.0f, elder.y - 18.0f});
    CHECK(p.text("Prompt") == "{icon:key:E} Talk");   // drawn as a picture of the E key
    p.tap(Key::E);
    REQUIRE(game.isInDialogue());
    CHECK(p.text("Speaker") == "Elder Maren");
    CHECK(p.text("Line") == "Ah, a traveller! Welcome to the meadow.");
    p.tap(Key::E);
    CHECK(p.text("Choices") == "> I'll find them.\n  Maybe later.");
    p.tap(Key::E);
    p.tap(Key::E);
    CHECK_FALSE(game.isInDialogue());
    CHECK(p.text("Quest Log") == "Gems for the Elder\n  [ ] Find 3 gems (0/3)\n  [ ] Bring them to Elder Maren");

    // Moving does nothing while talking (the player controller waits).
    // Collect the gems.
    for (const char* gem : {"Gem 1", "Gem 2", "Gem 3"}) {
        p.stand(at(gem));
        p.frames(1);
    }
    CHECK(p.gems() == 3);
    CHECK(p.text("Gem Count") == "Gems: 3");
    CHECK(p.text("Quest Log") == "Gems for the Elder\n  [x] Find 3 gems (3/3)\n  [ ] Bring them to Elder Maren");

    // Save here, by the last gem.
    REQUIRE(game.save("quick"));

    // Back to the elder: the quest completes, the boulder rolls away.
    p.stand({elder.x + 26.0f, elder.y - 18.0f});
    p.tap(Key::E);
    CHECK(p.text("Line") == "Three gems! Listen... the boulder is moving.");
    p.tap(Key::E);
    CHECK(p.text("Line") == "The way to the cave is open. Follow the path east.");
    p.tap(Key::E);
    CHECK(game.content().state().questState(p.assets.findByPath("content/gems.lquest")->id) == QuestState::Completed);
    CHECK(p.text("Quest Log").empty());
    p.frames(2);
    CHECK(game.find("Boulder").isNull());

    // On to the cave.
    p.stand({at("Door to Cave").x - 40.0f, at("Door to Cave").y});
    game.input().press(Key::Right);
    p.frames(60);
    game.input().release(Key::Right);
    CHECK(game.scene().name() == "Cave");
    CHECK(p.text("Gem Count") == "Gems: 3");   // progress travels with the player

    // The cave's gem uses the C++ Collectible: it adds to the same variable.
    p.stand(at("Gem 4"));
    p.frames(1);
    CHECK(p.gems() == 4);

    // Load the save: back in the meadow, three gems, quest not yet handed in.
    game.load("quick");
    p.frames(1);
    CHECK(game.scene().name() == "Meadow");
    CHECK(p.gems() == 3);
    CHECK(game.find("Gem 1").isNull());          // still collected
    CHECK_FALSE(game.find("Boulder").isNull());  // not yet moved
    CHECK(p.text("Gem Count") == "Gems: 3");
    INFO(all(p.errors));
    CHECK(p.errors.empty());
}
