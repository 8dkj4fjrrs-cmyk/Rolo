// Controls in a running game: actions and remapping from Lua, the player's
// remapped controls kept beside their saves, button pictures in texts and
// prompts, and rumble requests.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/SaveStore.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Components.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rolo;
namespace fs = std::filesystem;

namespace {

struct ControlsProject {
    fs::path folder = test::uniqueTempFolder("controls");
    AssetDatabase assets{folder};
    FolderSaveStore saves{folder / "saves"};
    Logger log;
    std::vector<std::string> printed;
    std::vector<std::string> problems;

    ControlsProject()
    {
        fs::create_directories(folder / "scripts");
        log.addSink([this](const LogMessage& m) {
            (m.level >= LogLevel::Warning ? problems : printed).push_back(m.text);
        });
    }
    ~ControlsProject()
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }
    AssetId write(const std::string& code)
    {
        std::ofstream(folder / "scripts" / "controls.lua", std::ios::binary) << code;
        assets.refresh();
        return assets.findByPath("scripts/controls.lua")->id;
    }
    std::unique_ptr<Game> makeGame()
    {
        GameSetup setup;
        setup.assets = &assets;
        setup.saves = &saves;
        return std::make_unique<Game>(log, std::move(setup));
    }
    bool printedLine(const std::string& line) const
    {
        return std::find(printed.begin(), printed.end(), "scripts/controls.lua: " + line) != printed.end();
    }
};

void frames(Game& game, int n)
{
    for (int i = 0; i < n; ++i) {
        game.update(1.0f / 60.0f);
    }
}

void tap(Game& game, Key key)
{
    game.input().press(key);
    frames(game, 1);
    game.input().release(key);
    frames(game, 1);
}

Scene sceneWith(AssetId script)
{
    Scene scene;
    const EntityId thing = scene.create("Thing")->id();
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(thing, std::move(component));
    auto text = std::make_unique<Text>();
    text->text = "Jump with {button:jump}";
    scene.addComponent(thing, std::move(text));
    return scene;
}

} // namespace

TEST_CASE("Scripts define actions, players remap them, and the game keeps their choice", "[gameplay][actions][script]")
{
    ControlsProject p;
    const AssetId script = p.write(R"(
        function start(self)
            input.action("jump", {"space", "pad a"})
        end
        function update(self, dt)
            if input.wasPressed("jump") then print("jump!") end
            if input.wasPressed("r") then
                input.rebind("jump")
                print("rebinding " .. tostring(input.isRebinding()))
            end
            if input.wasPressed("f2") then print(table.concat(input.bindings("jump"), " + ")) end
        end
    )");
    const Scene scene = sceneWith(script);
    {
        auto game = p.makeGame();
        game->start(scene);
        frames(*game, 1);
        CHECK(game->scene().find(game->find("Thing"))->get<Text>()->text == "Jump with {icon:key:Space}");
        tap(*game, Key::Space);
        CHECK(p.printedLine("jump!"));
        tap(*game, Key::R);
        CHECK(p.printedLine("rebinding true"));
        tap(*game, Key::J);   // the new jump key (used up: no jump yet)
        CHECK(std::count(p.printed.begin(), p.printed.end(), "scripts/controls.lua: jump!") == 1);
        tap(*game, Key::F2);
        CHECK(p.printedLine("J + Pad A"));
        CHECK(game->scene().find(game->find("Thing"))->get<Text>()->text == "Jump with {icon:key:J}");
        tap(*game, Key::J);
        CHECK(std::count(p.printed.begin(), p.printed.end(), "scripts/controls.lua: jump!") == 2);
    }
    // Saved straight away, beside the saves, and used by the next game.
    CHECK(fs::exists(p.folder / "saves" / "_controls.lsave"));
    {
        auto game = p.makeGame();
        game->start(scene);
        frames(*game, 1);
        tap(*game, Key::Space);
        tap(*game, Key::J);
        CHECK(std::count(p.printed.begin(), p.printed.end(), "scripts/controls.lua: jump!") == 3);
    }
    CHECK(p.problems.empty());
}

TEST_CASE("Rolo's own controls can be remapped: walking and talking follow", "[gameplay][actions]")
{
    ControlsProject p;
    Scene scene;
    const EntityId player = scene.create("Player")->id();
    auto collider = std::make_unique<Collider>();
    collider->size = {10, 10};
    scene.addComponent(player, std::move(collider));
    scene.addComponent(player, std::make_unique<Interactor>());
    const EntityId sign = scene.create("Sign")->id();
    auto area = std::make_unique<Collider>();
    area->size = {40, 40};
    area->solid = false;
    scene.addComponent(sign, std::move(area));
    auto trigger = std::make_unique<Trigger>();
    trigger->prompt = "Read";
    Action log;
    log.kind = Action::Kind::Log;
    log.text = "read the sign";
    trigger->reaction.actions = {log};
    scene.addComponent(sign, std::move(trigger));

    auto game = p.makeGame();
    game->start(scene);
    frames(*game, 1);
    CHECK(game->interactionPrompt() == "[E] Read");
    game->input().setBindings(Input::kInteract, {Binding::ofKey(Key::F), Binding::ofPad(GamepadButton::Y)});
    frames(*game, 1);
    CHECK(game->interactionPrompt() == "[F] Read");
    tap(*game, Key::E);   // no longer the interact key
    CHECK(std::find(p.printed.begin(), p.printed.end(), "read the sign") == p.printed.end());
    tap(*game, Key::F);
    CHECK(std::find(p.printed.begin(), p.printed.end(), "read the sign") != p.printed.end());

    // With a PlayStation pad in hand, the prompt names its button.
    GamepadState pad;
    pad.connected = true;
    pad.style = GamepadStyle::PlayStation;
    pad.buttons[static_cast<std::size_t>(GamepadButton::Down)] = true;
    game->input().setGamepad(0, pad);
    frames(*game, 1);
    CHECK(game->interactionPrompt() == "[Triangle] Read");
}

TEST_CASE("Save slots starting with _ are Rolo's own", "[gameplay][actions]")
{
    ControlsProject p;
    auto game = p.makeGame();
    game->start(Scene{});
    CHECK_FALSE(game->save("_controls"));
    REQUIRE(p.problems.size() == 1);
    CHECK(p.problems[0].find("cannot start with _") != std::string::npos);
}

TEST_CASE("Scripts ask for rumble and which device is in use", "[gameplay][gamepad][script]")
{
    ControlsProject p;
    const AssetId script = p.write(R"(
        function start(self)
            print(input.lastDevice())
            input.rumble(0.8, 0.25)
            input.rumble({low = 1, high = 0.2}, 0.5, 2)
        end
    )");
    auto game = p.makeGame();
    game->start(sceneWith(script));
    frames(*game, 1);
    CHECK(p.printedLine("keyboard"));
    const auto rumbles = game->input().takeRumbles();
    REQUIRE(rumbles.size() == 2);
    CHECK(rumbles[0].low == 0.8f);
    CHECK(rumbles[1].pad == 1);
    CHECK(rumbles[1].high == 0.2f);
    CHECK(p.problems.empty());
}

TEST_CASE("Mistakes with actions say what was wrong", "[gameplay][actions][script]")
{
    for (const char* code : {"input.action('jump', {'hyperspace'})", "input.rebind('nope')", "input.bindings('nope')",
                             "input.action('', {})"}) {
        INFO(code);
        ControlsProject p;
        const AssetId script = p.write(std::string("function start(self) ") + code + " end");
        auto game = p.makeGame();
        game->start(sceneWith(script));
        frames(*game, 1);
        CHECK(p.problems.size() == 1);
    }
}
