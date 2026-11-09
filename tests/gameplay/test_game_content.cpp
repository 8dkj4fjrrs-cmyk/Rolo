// Game + RPG content: triggers, interaction, the dialogue box, texts,
// persistence within a playthrough, and saving and loading.

#include "support/TempFolder.hpp"
#include "../content/ContentFixture.hpp"

#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace rolo;
using namespace rolo::test;

namespace {

const AssetId kSceneA{0x5001};
const AssetId kSceneB{0x5002};

EntityId addBox(Scene& scene, const char* name, Vec2 at, Vec2 size, EntityId parent = {})
{
    const EntityId id = scene.create(name, parent)->id();
    scene.setProperty(id, "rolo.Transform", "position", at);
    auto collider = std::make_unique<Collider>();
    collider->size = size;
    collider->solid = false;
    scene.addComponent(id, std::move(collider));
    return id;
}

EntityId addTrigger(Scene& scene, const char* name, Vec2 at, TriggerOn on, std::vector<Action> actions,
                    std::optional<Condition> condition = std::nullopt)
{
    const EntityId id = addBox(scene, name, at, {40, 40});
    auto trigger = std::make_unique<Trigger>();
    trigger->on = on;
    trigger->reaction.actions = std::move(actions);
    trigger->reaction.condition = std::move(condition);
    scene.addComponent(id, std::move(trigger));
    return id;
}

Action startDialogue(AssetId id)
{
    Action a;
    a.kind = Action::Kind::StartDialogue;
    a.asset = id;
    return a;
}

Action destroySelf()
{
    Action a;
    a.kind = Action::Kind::Destroy;
    return a;
}

Text* textIn(Game& game, std::string_view name)
{
    return game.get<Text>(game.find(name));
}

// A scene with a player (at x) and the user interface texts.
struct World {
    Scene scene{"A"};
    EntityId player;
    EntityId box;
    explicit World(float playerX = 200.0f)
    {
        player = addBox(scene, "Player", {playerX, 0}, {10, 10});
        scene.addComponent(player, std::make_unique<Interactor>());
        box = scene.create("Dialogue Box")->id();
        scene.addComponent(box, std::make_unique<DialogueView>());
        const auto addText = [&](const char* name, TextSource source, std::string written = {}) {
            const EntityId id = scene.create(name, source == TextSource::Text || source == TextSource::InteractionPrompt
                                                       || source == TextSource::QuestLog
                                                   ? EntityId{}
                                                   : box)
                                    ->id();
            auto text = std::make_unique<Text>();
            text->source = source;
            text->text = std::move(written);
            scene.addComponent(id, std::move(text));
        };
        addText("Speaker", TextSource::DialogueSpeaker);
        addText("Line", TextSource::DialogueLine);
        addText("Choices", TextSource::DialogueChoices);
        addText("Prompt", TextSource::InteractionPrompt);
        addText("Gems", TextSource::Text, "Gems: {gems}");
        addText("Quests", TextSource::QuestLog);
    }
};

struct Run {
    ContentLibrary lib = makeLibrary();
    Logger log;
    std::vector<std::string> lines;
    std::map<AssetId, Scene> scenes;
    std::unique_ptr<FolderSaveStore> saves;
    std::unique_ptr<Game> game;

    Run()
    {
        log.addSink([this](const LogMessage& m) { lines.push_back(m.text); });
        lib.addScene(kSceneA, "a.lscene");
        lib.addScene(kSceneB, "b.lscene");
        saves = std::make_unique<FolderSaveStore>(rolo::test::uniqueTempFolder("saves"));
        std::filesystem::remove_all(saves->folder());
        GameSetup setup;
        setup.content = &lib;
        setup.saves = saves.get();
        setup.gameName = "Test";
        setup.loadScene = [this](AssetId id, std::string& error) -> std::optional<Scene> {
            const auto it = scenes.find(id);
            if (it == scenes.end()) {
                error = "unknown";
                return std::nullopt;
            }
            return it->second;
        };
        game = std::make_unique<Game>(log, std::move(setup));
    }
    void frames(int n, Key hold = Key::Unknown)
    {
        for (int i = 0; i < n; ++i) {
            if (hold != Key::Unknown) {
                game->input().press(hold);
            }
            game->update(1.0f / 60.0f);
        }
    }
    void tap(Key key)
    {
        game->input().press(key);
        game->update(1.0f / 60.0f);
        game->input().release(key);
        game->update(1.0f / 60.0f);
    }
};

} // namespace

TEST_CASE("Talking to someone: prompt, dialogue box, lines, choices", "[gameplay][content]")
{
    Run r;
    World w(5.0f);   // the player stands in the elder's area
    addTrigger(w.scene, "Elder", {0, 0}, TriggerOn::Interact, {startDialogue(kElder)});
    r.game->start(w.scene, kSceneA);
    Game& g = *r.game;
    CHECK_FALSE(g.scene().find(w.box)->isActive());   // the dialogue box starts hidden
    CHECK(textIn(g, "Gems")->text == "Gems: 0");

    r.frames(1);
    CHECK(g.interactionPrompt() == "[E] Talk");
    CHECK(textIn(g, "Prompt")->text == "{icon:key:E} Talk");   // drawn as a picture of the E key

    r.tap(Key::E);
    REQUIRE(g.isInDialogue());
    CHECK(g.scene().find(w.box)->isActive());
    CHECK(textIn(g, "Speaker")->text == "Elder");
    CHECK(textIn(g, "Speaker")->color == Color{242, 169, 59, 255});   // the speaker's colour
    CHECK(textIn(g, "Line")->text == "Hello, Ana.");
    CHECK(textIn(g, "Prompt")->text.empty());   // no prompt while talking

    r.tap(Key::Space);   // go on
    CHECK(textIn(g, "Line")->text == "Will you find 3 gems?");
    CHECK(textIn(g, "Choices")->text == "> Yes.\n  No.");
    r.tap(Key::Down);
    CHECK(textIn(g, "Choices")->text == "  Yes.\n> No.");
    r.tap(Key::Up);
    r.tap(Key::E);   // "Yes": starts the quest
    CHECK(g.content().state().questState(kGemsQuest) == QuestState::Active);
    CHECK(textIn(g, "Line")->text == "Thank you!");
    CHECK(textIn(g, "Quests")->text == "Gems for the Elder\n  [ ] Find 3 gems (0/3)\n  [ ] Bring them back\n"
                                       "  [ ] Find the hidden gem (optional)");
    r.tap(Key::E);
    CHECK_FALSE(g.isInDialogue());
    CHECK_FALSE(g.scene().find(w.box)->isActive());

    // The key press that ends a dialogue does not start it again at once.
    CHECK_FALSE(g.isInDialogue());
}

TEST_CASE("Area, scene start, timer, event and condition triggers", "[gameplay][content]")
{
    Run r;
    World w(200.0f);
    Action emit;
    emit.kind = Action::Kind::Emit;
    emit.text = "bell";
    addTrigger(w.scene, "Start", {500, 500}, TriggerOn::SceneStart, {addVar("gems", 100)});
    const EntityId door = addTrigger(w.scene, "Door", {0, 0}, TriggerOn::EnterArea, {addVar("gems", 1), emit});
    addTrigger(w.scene, "Leave", {0, 0}, TriggerOn::LeaveArea, {addVar("gems", 10)});
    const EntityId timer = addTrigger(w.scene, "Timer", {500, 500}, TriggerOn::Timer, {setVar("hero", std::string("Tick"))});
    w.scene.edit<Trigger>(timer)->delay = 0.5f;
    const EntityId bell = addTrigger(w.scene, "Bell", {500, 500}, TriggerOn::GameEvent, {setVar("met_elder", true)});
    w.scene.edit<Trigger>(bell)->event = "bell";
    addTrigger(w.scene, "Rich", {500, 500}, TriggerOn::ConditionMet, {setVar("rewarded", true)},
               compare("gems", CompareOp::GreaterEqual, 111));
    r.game->start(w.scene, kSceneA);
    Game& g = *r.game;
    const auto gems = [&] { return std::get<int>(*g.content().state().variable("gems")); };

    r.frames(1);
    CHECK(gems() == 100);   // scene start, once
    r.frames(5);
    CHECK(gems() == 100);
    CHECK(std::get<std::string>(*g.content().state().variable("hero")) == "Ana");
    r.frames(30);
    CHECK(std::get<std::string>(*g.content().state().variable("hero")) == "Tick");   // after half a second

    // Walk into the door's area, then out.
    g.transform(w.player)->position = {5, 0};
    r.frames(1);
    CHECK(gems() == 101);
    CHECK_FALSE(std::get<bool>(*g.content().state().variable("met_elder")));
    r.frames(1);   // the event arrives a frame later
    CHECK(std::get<bool>(*g.content().state().variable("met_elder")));
    r.frames(3);
    CHECK(gems() == 101);   // only when entering
    g.transform(w.player)->position = {200, 0};
    r.frames(1);
    CHECK(gems() == 111);
    r.frames(1);
    CHECK(std::get<bool>(*g.content().state().variable("rewarded")));   // gems >= 111 came true
    static_cast<void>(door);
}

TEST_CASE("Removed objects and one-time triggers stay that way in a playthrough", "[gameplay][content]")
{
    Run r;
    World a(5.0f);
    const EntityId gem = addTrigger(a.scene, "Gem", {0, 0}, TriggerOn::EnterArea, {addVar("gems", 1), destroySelf()});
    const EntityId hello = addTrigger(a.scene, "Hello", {500, 500}, TriggerOn::SceneStart, {addVar("gems", 10)});
    a.scene.edit<Trigger>(hello)->once = true;
    Action toB;
    toB.kind = Action::Kind::ChangeScene;
    toB.asset = kSceneB;
    addTrigger(a.scene, "Go", {1000, 0}, TriggerOn::GameEvent, {toB});
    a.scene.edit<Trigger>(a.scene.roots().back())->event = "go";
    World b(0.0f);
    Action toA = toB;
    toA.asset = kSceneA;
    addTrigger(b.scene, "Back", {1000, 0}, TriggerOn::GameEvent, {toA});
    b.scene.edit<Trigger>(b.scene.roots().back())->event = "back";
    r.scenes[kSceneA] = a.scene;
    r.scenes[kSceneB] = b.scene;

    r.game->start(a.scene, kSceneA);
    Game& g = *r.game;
    r.frames(2);
    CHECK_FALSE(g.scene().contains(gem));   // collected
    CHECK(std::get<int>(*g.content().state().variable("gems")) == 11);

    g.emit("go");
    r.frames(2);
    CHECK(g.scene().name() == "A");   // b's name is "A" too (World), so check the id
    CHECK(g.sceneId() == kSceneB);
    g.emit("back");
    r.frames(2);
    CHECK(g.sceneId() == kSceneA);
    CHECK_FALSE(g.scene().contains(gem));   // still collected
    CHECK(std::get<int>(*g.content().state().variable("gems")) == 11);   // and "Hello" did not run again
}

TEST_CASE("Saving and loading a slot brings back progress, scene and position", "[gameplay][content]")
{
    Run r;
    World a(5.0f);
    const EntityId gem = addTrigger(a.scene, "Gem", {0, 0}, TriggerOn::EnterArea, {addVar("gems", 1), destroySelf()});
    World b(0.0f);
    r.scenes[kSceneA] = a.scene;
    r.scenes[kSceneB] = b.scene;
    r.game->start(a.scene, kSceneA);
    Game& g = *r.game;
    r.frames(2);
    g.content().startQuest(kGemsQuest);
    g.transform(a.player)->position = {300, 40};
    REQUIRE(g.save("slot1"));
    CHECK(std::filesystem::exists(r.saves->pathOf("slot1")));

    // Play on: more gems, another scene.
    g.content().run({addVar("gems", 5)});
    g.changeScene(kSceneB);
    r.frames(1);
    CHECK(g.sceneId() == kSceneB);

    g.load("slot1");
    r.frames(1);
    CHECK(g.sceneId() == kSceneA);
    CHECK(std::get<int>(*g.content().state().variable("gems")) == 1);
    CHECK(g.content().state().questState(kGemsQuest) == QuestState::Active);
    CHECK_FALSE(g.scene().contains(gem));
    CHECK(g.worldPosition(g.find("Player")).x == 300.0f);
    CHECK(g.worldPosition(g.find("Player")).y == 40.0f);

    // Problems are reported, not crashed on.
    g.load("nothing_here");
    r.frames(1);
    CHECK(std::find(r.lines.begin(), r.lines.end(), "There is no save in slot 'nothing_here'.") != r.lines.end());
    CHECK_FALSE(g.save("bad/slot"));
    CHECK(r.saves->pathOf("../escape").empty());
}

TEST_CASE("Triggers keep their condition and actions in scene files", "[gameplay][content]")
{
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameplayComponents(registry);
    World w;
    const EntityId id = addTrigger(w.scene, "Elder", {0, 0}, TriggerOn::Interact, {startDialogue(kElder), addVar("gems", 2)},
                                   compare("met_elder", CompareOp::Equal, false));
    w.scene.edit<Trigger>(id)->prompt = "Greet";
    const std::string text = serializeScene(w.scene);
    CHECK(text.find("\"startDialogue\"") != std::string::npos);
    const SceneLoadResult loaded = parseScene(text, registry);
    REQUIRE(loaded.ok());
    CHECK(loaded.issues.empty());
    const Trigger* t = loaded.scene->find(id)->get<Trigger>();
    REQUIRE(t != nullptr);
    CHECK(t->prompt == "Greet");
    CHECK(t->reaction == w.scene.find(id)->get<Trigger>()->reaction);
    CHECK(loaded.scene->find(w.player)->get<Interactor>() != nullptr);
}
