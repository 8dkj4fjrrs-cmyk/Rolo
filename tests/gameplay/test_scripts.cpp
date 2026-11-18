// Lua scripts (rolo/gameplay/Script.hpp): running, the API scripts call,
// keeping scripts apart, and what happens when a script goes wrong.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/gameplay/ScriptProperties.hpp"
#include "rolo/scene/SceneFile.hpp"
#include "rolo/scene/Values.hpp"
#include "rolo/scene/Components.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace rolo;
using Catch::Matchers::ContainsSubstring;

namespace {

namespace fs = std::filesystem;

// A project folder with script files, its asset database and a log that keeps
// what scripts print and what goes wrong.
struct ScriptProject {
    fs::path folder = test::uniqueTempFolder("scripts");
    AssetDatabase assets{folder};
    Logger log;
    std::vector<std::string> printed;  // info messages from scripts
    std::vector<std::string> problems; // warnings and errors

    ScriptProject()
    {
        fs::create_directories(folder / "scripts");
        log.addSink([this](const LogMessage& m) {
            if (m.level >= LogLevel::Warning) {
                problems.push_back(m.text);
            } else if (m.category == "script") {
                printed.push_back(m.text);
            }
        });
    }
    ~ScriptProject()
    {
        std::error_code ignored;
        fs::remove_all(folder, ignored);
    }

    AssetId write(const std::string& path, const std::string& code)
    {
        fs::create_directories((folder / path).parent_path());
        std::ofstream(folder / path, std::ios::binary) << code;
        assets.refresh();
        const AssetRecord* record = assets.findByPath(path);
        REQUIRE(record != nullptr);
        REQUIRE(record->type == AssetType::Script);
        return record->id;
    }

    bool hasProblem(const std::string& part) const
    {
        for (const std::string& p : problems) {
            if (p.find(part) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

EntityId addScripted(Scene& scene, const std::string& name, AssetId script, Vec2 at = {})
{
    const EntityId id = scene.create(name)->id();
    scene.setProperty(id, "rolo.Transform", "position", at);
    auto component = std::make_unique<Script>();
    component->script = script;
    scene.addComponent(id, std::move(component));
    return id;
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

} // namespace

TEST_CASE("A script's start runs once and its update every frame", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId mover = p.write("scripts/mover.lua", R"(
        function start(self)
            self.speed = 60
            print("started on " .. self.object:name())
        end
        function update(self, dt)
            local x, y = self.object:position()
            self.object:setPosition(x + self.speed * dt, y)
        end
    )");
    Scene scene;
    const EntityId box = addScripted(scene, "Box", mover, {10, 5});

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 60); // one second at 60 points per second

    CHECK(positionOf(game, box).x == Catch::Approx(70.0f).margin(0.01f));
    CHECK(positionOf(game, box).y == Catch::Approx(5.0f));
    REQUIRE(p.printed.size() == 1);
    CHECK(p.printed[0] == "scripts/mover.lua: started on Box");
    CHECK(p.problems.empty());
}

TEST_CASE("Every object has its own self, and every file its own globals", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId counter = p.write("scripts/counter.lua", R"(
        count = 0                      -- a global of this file only
        function start(self) self.ticks = 0 end
        function update(self, dt)
            self.ticks = self.ticks + 1
            count = count + 1
            self.object:setPosition(self.ticks, count)
        end
    )");
    const AssetId other = p.write("scripts/other.lua", R"(
        function update(self, dt)
            -- 'count' belongs to counter.lua's file: not visible here
            self.object:setPosition(count == nil and 1 or -1, 0)
        end
    )");
    Scene scene;
    const EntityId a = addScripted(scene, "A", counter);
    const EntityId b = addScripted(scene, "B", counter);
    const EntityId c = addScripted(scene, "C", other);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);

    CHECK(positionOf(game, a).x == 3.0f); // each object counted its own frames...
    CHECK(positionOf(game, b).x == 3.0f);
    CHECK(positionOf(game, b).y == 6.0f); // ...while the file's global counted both
    CHECK(positionOf(game, c).x == 1.0f);
    CHECK(p.problems.empty());
}

TEST_CASE("A script error names the file and line and switches only that script off", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId broken = p.write("scripts/broken.lua", "function update(self, dt)\n"
                                                         "    local missing = nil\n"
                                                         "    missing.field = 1\n"
                                                         "end\n");
    const AssetId fine = p.write("scripts/fine.lua", R"(
        function update(self, dt) local x = self.object:position(); self.object:setPosition(x + 1, 0) end
    )");
    Scene scene;
    addScripted(scene, "Broken", broken);
    const EntityId ok = addScripted(scene, "Fine", fine);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 5);

    REQUIRE(p.problems.size() == 1); // reported once, then switched off
    CHECK_THAT(p.problems[0], ContainsSubstring("'Broken'"));
    CHECK_THAT(p.problems[0], ContainsSubstring("scripts/broken.lua:3:"));
    CHECK_THAT(p.problems[0], ContainsSubstring("attempt to index nil with 'field'"));
    CHECK(positionOf(game, ok).x == 5.0f); // the other script kept going
}

TEST_CASE("A script with a syntax error is reported with its line", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId bad = p.write("scripts/bad.lua", "function start(self)\n  if true then\nend\n");
    Scene scene;
    addScripted(scene, "Bad", bad);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 2);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    CHECK(p.hasProblem("scripts/bad.lua:4:")); // the missing 'end' is noticed at the end of the file
    CHECK(p.hasProblem("Expected 'end'"));

    CHECK(checkScriptSyntax("function f() end", "scripts/ok.lua") == std::nullopt);
    const auto error = checkScriptSyntax("function f(", "scripts/x.lua");
    REQUIRE(error);
    CHECK_THAT(*error, ContainsSubstring("scripts/x.lua:1:"));
}

TEST_CASE("A script that never finishes is stopped instead of freezing the game", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId loop = p.write("scripts/loop.lua", "function update(self, dt)\n  while true do end\nend\n");
    Scene scene;
    addScripted(scene, "Loop", loop);
    Game game(p.log, GameSetup{.assets = &p.assets, .scriptTimeLimit = 1.0});   // games: 10 seconds
    game.start(scene);
    frames(game, 3);
    INFO((p.problems.empty() ? std::string("no problems") : p.problems[0]));
    CHECK(p.hasProblem("ran for more than 1 second without finishing"));
    CHECK(p.hasProblem("scripts/loop.lua:2:"));
}

TEST_CASE("Scripts cannot use more than their memory limit", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId hog = p.write("scripts/hog.lua", R"(
        function start(self)
            local t = {}
            for i = 1, 100 do t[i] = string.rep("x", 16 * 1024 * 1024) .. i end
        end
    )");
    Scene scene;
    addScripted(scene, "Hog", hog);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    CHECK(p.hasProblem("more than 256 MB"));
}

TEST_CASE("Scripts cannot reach files, programs or native code", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId probe = p.write("scripts/probe.lua", R"(
        function start(self)
            local found = {}
            for _, name in ipairs({"io", "os", "debug", "package", "dofile", "loadfile", "load", "loadstring",
                                   "getfenv", "setfenv"}) do
                if _G[name] ~= nil then found[#found + 1] = name end
            end
            print("reachable: " .. table.concat(found, ","))
            print("math: " .. math.floor(2.5) .. " " .. string.upper("ok"))
        end
    )");
    Scene scene;
    addScripted(scene, "Probe", probe);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/probe.lua: reachable: ");
    CHECK(p.printed[1] == "scripts/probe.lua: math: 2 OK");
}

TEST_CASE("require loads another script once, by name", "[gameplay][script]")
{
    ScriptProject p;
    p.write("scripts/helpers/maths.lua", R"(
        print("loading maths")
        local M = {}
        function M.double(x) return x * 2 end
        return M
    )");
    p.write("scripts/plain.lua", "function triple(x) return x * 3 end\n"); // no return: its functions
    const AssetId user = p.write("scripts/user.lua", R"(
        local maths = require("maths")
        local again = require("scripts/helpers/maths.lua")
        local plain = require("plain")
        function start(self)
            print(maths.double(4), plain.triple(2), maths == again)
        end
    )");
    Scene scene;
    addScripted(scene, "User", user);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.problems.empty());
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/helpers/maths.lua: loading maths");
    CHECK(p.printed[1] == "scripts/user.lua: 8\t6\ttrue");
}

TEST_CASE("require reports a missing script and a script that requires itself", "[gameplay][script]")
{
    ScriptProject p;
    p.write("scripts/ping.lua", "require('pong')\n");
    p.write("scripts/pong.lua", "require('ping')\n");
    const AssetId a = p.write("scripts/a.lua", "require('nowhere')\n");
    const AssetId b = p.write("scripts/b.lua", "require('ping')\n");
    Scene scene;
    addScripted(scene, "A", a);
    addScripted(scene, "B", b);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    CHECK(p.hasProblem("there is no script called \"nowhere\""));
    CHECK(p.hasProblem("requires itself"));
}

TEST_CASE("Scripts find objects, move them with collisions and destroy them", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/pusher.lua", R"(
        function start(self)
            local wall = game.find("Wall")
            local gem = game.find("Gem")
            assert(game.find("Nobody") == nil)
            local dx, dy, blocked, hit = self.object:move(200, 0)
            print(blocked, hit == wall, tostring(hit))
            print(self.object:isTouching(gem), #self.object:overlapping())
            gem:destroy()
            self.gem = gem
        end
        function update(self, dt)
            -- Objects are removed at the end of the frame, so ask in the next one.
            self.frames = (self.frames or 0) + 1
            if self.frames == 2 then
                print("gem still there: " .. tostring(self.gem:isValid()))
            end
        end
    )");
    Scene scene;
    const EntityId pusher = addScripted(scene, "Pusher", script);
    scene.addComponent(pusher, std::make_unique<Collider>()); // 32 x 32
    const EntityId wall = scene.create("Wall")->id();
    scene.setProperty(wall, "rolo.Transform", "position", Vec2{100, 0});
    scene.addComponent(wall, std::make_unique<Collider>());
    const EntityId gem = scene.create("Gem")->id();
    auto gemCollider = std::make_unique<Collider>();
    gemCollider->solid = false;
    scene.addComponent(gem, std::move(gemCollider));

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 2);

    REQUIRE(p.problems.empty());
    REQUIRE(p.printed.size() == 3);
    CHECK(p.printed[0] == "scripts/pusher.lua: true\ttrue\tobject \"Wall\"");
    CHECK(p.printed[1] == "scripts/pusher.lua: false\t0"); // moved away from the gem first
    CHECK(p.printed[2] == "scripts/pusher.lua: gem still there: false");
    CHECK(positionOf(game, pusher).x == Catch::Approx(68.0f)); // stopped against the wall
}

TEST_CASE("Using an object after it is gone is a clear error", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/late.lua", R"(
        function start(self) self.other = game.find("Other"); self.other:destroy() end
        function update(self, dt) self.other:setPosition(1, 1) end
    )");
    Scene scene;
    addScripted(scene, "Late", script);
    scene.create("Other");
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);
    CHECK(p.hasProblem("this object no longer exists"));
    CHECK(p.hasProblem("scripts/late.lua:3:"));
}

TEST_CASE("Scripts read keys and the movement direction", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/keys.lua", R"(
        function update(self, dt)
            local x, y = input.direction()
            print(input.isDown("left"), input.wasPressed("Space"), input.isDown("shift"), x, y)
        end
    )");
    Scene scene;
    addScripted(scene, "Keys", script);
    Game game(p.log, &p.assets);
    game.start(scene);
    game.input().press(Key::Left);
    game.input().press(Key::Space);
    game.input().press(Key::RightShift);
    frames(game, 2);
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/keys.lua: true\ttrue\ttrue\t-1\t0");
    CHECK(p.printed[1] ==
          "scripts/keys.lua: true\tfalse\ttrue\t-1\t0"); // pressed only in the first frame
}

TEST_CASE("An unknown key name is an error that lists the names", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script =
        p.write("scripts/typo.lua", "function update(self, dt) input.isDown('spacebar') end\n");
    Scene scene;
    addScripted(scene, "Typo", script);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    CHECK(p.hasProblem("unknown key \"spacebar\""));
}

TEST_CASE("Scripts read and change game variables, send events and use content notation",
          "[gameplay][script]")
{
    ScriptProject p;
    ContentLibrary content;
    VariableSet vars;
    vars.variables.push_back({"gems", ValueType::Int, Value{2}, ""});
    vars.variables.push_back({"door_open", ValueType::Bool, Value{false}, ""});
    content.addVariables(AssetId{1}, "variables.lvars", vars);

    const AssetId script = p.write("scripts/vars.lua", R"(
        function start(self)
            game.set("gems", game.get("gems") + 1)
            game.run("set door_open true")
            print(game.get("gems"), game.check("gems >= 3 and door_open"))
            game.emit("opened")
        end
        function update(self, dt)
            if game.wasEmitted("opened") then print("heard it") end
        end
    )");
    const AssetId wrong = p.write("scripts/wrong.lua", "function start(self) game.set('gems', 'lots') end\n");
    Scene scene;
    addScripted(scene, "Vars", script);
    addScripted(scene, "Wrong", wrong);

    GameSetup setup;
    setup.assets = &p.assets;
    setup.content = &content;
    Game game(p.log, setup);
    game.start(scene);
    frames(game, 3);

    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/vars.lua: 3\ttrue");
    CHECK(p.printed[1] == "scripts/vars.lua: heard it"); // events arrive the next frame
    CHECK(p.hasProblem("game variable \"gems\" is a whole number"));
}

TEST_CASE("A script changes scene by the scene's name", "[gameplay][script]")
{
    ScriptProject p;
    std::ofstream(p.folder / "woods.lscene") << "{}";
    const AssetId script =
        p.write("scripts/door.lua", "function update(self, dt) game.changeScene('woods') end\n");
    const AssetId woods = p.assets.findByPath("woods.lscene")->id;

    Scene start;
    addScripted(start, "Door", script);
    Scene next;
    next.setName("Woods");
    AssetId loaded;
    Game game(p.log, &p.assets, [&](AssetId id, std::string&) -> std::optional<Scene> {
        loaded = id;
        return next;
    });
    game.start(start);
    frames(game, 2);
    CHECK(loaded == woods);
    CHECK(game.scene().name() == "Woods");
    CHECK(p.problems.empty());
}

TEST_CASE("Pressing Play again reads the script file again", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/edit.lua", "function start(self) print('first') end\n");
    Scene scene;
    addScripted(scene, "Edited", script);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    p.write("scripts/edit.lua", "function start(self) print('second') end\n");
    game.stop();
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[1] == "scripts/edit.lua: second");
}

TEST_CASE("A Script without a file says what to do", "[gameplay][script]")
{
    ScriptProject p;
    Scene scene;
    addScripted(scene, "Empty", AssetId{});
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    CHECK(p.hasProblem("pick a .lua file"));
}

TEST_CASE("The Script component is registered, saved and shown like any behaviour", "[gameplay][script]")
{
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameplayComponents(registry);
    const ComponentType* type = registry.find("rolo.Script");
    REQUIRE(type != nullptr);
    CHECK(type->category == "Gameplay");
    REQUIRE(type->properties.size() == 2);
    CHECK(type->properties[0].key == "script");
    CHECK(type->properties[0].options.assetType == "script");
    CHECK(type->properties[1].key == "values");
    CHECK(type->properties[1].options.data);   // named values: the Inspector shows the script's properties instead
    CHECK(assetTypeForPath("scripts/x.lua") == AssetType::Script);
    CHECK(assetTypeName(AssetType::Script) == "script");
    CHECK_THAT(newScriptTemplate("coin"), ContainsSubstring("function update(self, dt)"));
    CHECK(checkScriptSyntax(newScriptTemplate("coin"), "coin.lua") == std::nullopt);
}

TEST_CASE("The scripting guide's examples work as written", "[gameplay][script]")
{
    // docs/scripting.md: "Collect coins", "A patrolling guard" and the
    // require example. Keep them in step with the guide.
    ScriptProject p;
    ContentLibrary content;
    VariableSet vars;
    vars.variables.push_back({"coins", ValueType::Int, Value{0}, ""});
    content.addVariables(AssetId{1}, "variables.lvars", vars);
    const AssetId coin = p.write("scripts/coin.lua", R"(
function update(self, dt)
    local player = game.player()
    if player and self.object:isTouching(player) then
        game.set("coins", game.get("coins") + 1)   -- a whole-number variable in a .lvars file
        self.object:destroy()
    end
end
)");
    const AssetId guard = p.write("scripts/guard.lua", R"(
function start(self)
    self.direction = 1
end

function update(self, dt)
    local _, _, blocked = self.object:move(80 * self.direction * dt, 0)
    if blocked then
        self.direction = -self.direction
    end
    self.object:play(self.direction > 0 and "walk_right" or "walk_left")
end
)");
    p.write("scripts/maths.lua", R"(
function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end
)");
    const AssetId health = p.write("scripts/player_health.lua", R"(
local maths = require("maths")
function update(self, dt)
    self.health = maths.clamp((self.health or 100) - dt, 0, 100)
    if self.health < 100 and not self.said then self.said = true; print(self.health < 100) end
end
)");

    Scene scene;
    const EntityId player = addScripted(scene, "Player", health);
    scene.addComponent(player, std::make_unique<Collider>());
    scene.addComponent(player, std::make_unique<Interactor>());
    const EntityId c = addScripted(scene, "Coin", coin, {10, 0});
    auto coinCollider = std::make_unique<Collider>();
    coinCollider->solid = false;
    scene.addComponent(c, std::move(coinCollider));
    const EntityId g = addScripted(scene, "Guard", guard, {300, 0});
    scene.addComponent(g, std::make_unique<Collider>());
    const EntityId wall = scene.create("Wall")->id();
    scene.setProperty(wall, "rolo.Transform", "position", Vec2{360, 0});
    scene.addComponent(wall, std::make_unique<Collider>());

    GameSetup setup;
    setup.assets = &p.assets;
    setup.content = &content;
    Game game(p.log, setup);
    game.start(scene);
    frames(game, 60);

    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    CHECK(p.problems.empty());
    CHECK(std::get<int>(*game.content().state().variable("coins")) == 1);
    CHECK(game.scene().find(c) == nullptr); // collected
    CHECK(positionOf(game, g).x < 300.0f);  // turned round at the wall
    CHECK(p.printed == std::vector<std::string>{"scripts/player_health.lua: true"});
}

TEST_CASE("Text set by a script stays and fills in variables", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/score.lua", R"(
        function update(self, dt)
            self.object:setText("Frames {ticks}")
        end
    )");
    Scene scene;
    const EntityId label = addScripted(scene, "Label", script);
    auto text = std::make_unique<Text>();
    text->text = "Start";
    scene.addComponent(label, std::move(text));
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);
    // No variable called ticks: left as written (and the script's text kept).
    CHECK(game.scene().find(label)->get<Text>()->text == "Frames {ticks}");
    CHECK(p.problems.empty());
}

TEST_CASE("game.quit asks the program to close the game", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId menu = p.write("scripts/menu.lua", R"(
        function update(self, dt)
            if game.time() > 0.5 then game.quit() end
        end
    )");
    Scene scene;
    addScripted(scene, "Menu", menu);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 20);
    CHECK_FALSE(game.quitRequested());
    frames(game, 20);
    CHECK(game.quitRequested());
    CHECK(p.problems.empty());
    game.start(scene);   // a new game starts fresh
    CHECK_FALSE(game.quitRequested());
}

TEST_CASE("Scripts are Luau: types, compound assignment, continue, interpolation", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId luau = p.write("scripts/luau.lua", R"(
        type Point = { x: number, y: number }
        local function add(a: Point, b: Point): Point
            return { x = a.x + b.x, y = a.y + b.y }
        end
        function start(self)
            local total = 0
            for i = 1, 10 do
                if i % 2 == 0 then continue end
                total += i
            end
            local p = add({ x = 1, y = 2 }, { x = 3, y = 4 })
            print(`odd sum {total}, point {p.x},{p.y}, floor {7 // 2}`)
            local v = vector.create(1, 2, 3)
            print(`vector {v.x + v.y + v.z}, bits {bit32.band(6, 3)}`)
        end
    )");
    Scene scene;
    addScripted(scene, "Luau", luau);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/luau.lua: odd sum 25, point 4,6, floor 3");
    CHECK(p.printed[1] == "scripts/luau.lua: vector 6, bits 2");
}

TEST_CASE("The shared libraries are read-only; a file's own globals are its own", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId changer = p.write("scripts/changer.lua", R"(
        counter = 1   -- this file's global
        function start(self)
            counter += 1
            local ok, err = pcall(function() math.pi = 3 end)
            print(`changed math: {ok}; {counter}`)
            local ok2 = pcall(function() game.find = nil end)
            print(`changed game: {ok2}`)
        end
    )");
    const AssetId reader = p.write("scripts/reader.lua", R"(
        function start(self) print(`pi {math.pi > 3.14}, counter {counter}`) end
    )");
    Scene scene;
    addScripted(scene, "Changer", changer);
    addScripted(scene, "Reader", reader);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.printed.size() == 3);
    CHECK(p.printed[0] == "scripts/changer.lua: changed math: false; 2");
    CHECK(p.printed[1] == "scripts/changer.lua: changed game: false");
    CHECK(p.printed[2] == "scripts/reader.lua: pi true, counter nil");
    CHECK(p.problems.empty());
}

TEST_CASE("Files named .luau are scripts too, and require finds them", "[gameplay][script]")
{
    ScriptProject p;
    p.write("scripts/shapes.luau", "return { area = function(w: number, h: number): number return w * h end }\n");
    const AssetId user = p.write("scripts/main.luau", R"(
        local shapes = require("shapes")
        function start(self) print(`area {shapes.area(3, 4)}`) end
    )");
    Scene scene;
    addScripted(scene, "User", user);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    REQUIRE(p.printed.size() == 1);
    CHECK(p.printed[0] == "scripts/main.luau: area 12");
}

TEST_CASE("Functions that take x, y also take one vector", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId mover = p.write("scripts/mover.lua", R"(
        function start(self)
            local home = vector.create(self.object:position())   -- x, y (z is 0)
            self.object:setPosition(home + vector.create(10, 20, 0) * 2)
            local x, y = self.object:position()
            print(`at {x}, {y}`)
            self.object:setPosition(1, 2)                         -- two numbers still work
            print(`then {self.object:position()}`)
            print(pcall(function() self.object:setPosition(vector.create(0 / 0, 1, 0)) end))
            print(pcall(function() self.object:setPosition("left") end))
            print(pcall(function() return ("text"):position() end))
            local speed = vector.create(3, 4, 0)
            print(`{vector.magnitude(speed)} {(-speed).x} {(speed * 2).y}`)   -- as in the scripting guide
        end
    )");
    Scene scene;
    const EntityId id = addScripted(scene, "Mover", mover);
    scene.setProperty(id, "rolo.Transform", "position", Vec2{100.0f, 50.0f});
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    INFO((p.problems.empty() ? std::string() : p.problems.front()));
    REQUIRE(p.printed.size() == 6);
    CHECK(p.printed[5] == "scripts/mover.lua: 5 -3 8");
    CHECK(p.printed[0] == "scripts/mover.lua: at 120, 90");
    CHECK(p.printed[1] == "scripts/mover.lua: then 1");
    CHECK_THAT(p.printed[2], ContainsSubstring("false") && ContainsSubstring("finite"));
    CHECK_THAT(p.printed[3], ContainsSubstring("false") && ContainsSubstring("number expected"));
    CHECK_THAT(p.printed[4], ContainsSubstring("false"));
    CHECK(p.problems.empty());
}

TEST_CASE("Objects from another kind of value are refused by name", "[gameplay][script]")
{
    ScriptProject p;
    const AssetId s = p.write("scripts/wrong.lua", R"(
        function start(self)
            local setPosition = self.object.setPosition
            print(pcall(setPosition, {}, 1, 2))
            print(typeof(self.object), tostring(self.object == game.find("Wrong")))
        end
    )");
    Scene scene;
    addScripted(scene, "Wrong", s);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.printed.size() == 2);
    CHECK_THAT(p.printed[0], ContainsSubstring("false") && ContainsSubstring("object expected"));
    CHECK(p.printed[1] == "scripts/wrong.lua: object\ttrue");
}

// --- Script properties (ADR 0029) ------------------------------------------------------------------

TEST_CASE("A script's properties are read from its text, in the order written", "[gameplay][script][properties]")
{
    const ScriptProperties found = readScriptProperties(R"(
        -- a lift
        properties = {
            speed = 90,
            rise = -200.5,
            label = "Lift A",
            returns = true,
            ["walk_speed"] = 3,
        }
        local SPEED = 1
        function update(self, dt) end
    )");
    CHECK(found.problems.empty());
    REQUIRE(found.list.size() == 5);
    CHECK(found.list[0].name == "speed");
    CHECK(found.list[0].value == DataValue(90.0));
    CHECK(found.list[0].line == 4);
    CHECK(found.list[1].value == DataValue(-200.5));
    CHECK(found.list[2].value == DataValue(std::string("Lift A")));
    CHECK(found.list[3].value == DataValue(true));
    CHECK(found.list[4].name == "walk_speed");

    // Nothing declared, a syntax error (reported elsewhere), a table inside a function.
    CHECK(readScriptProperties("function update(self, dt) end").list.empty());
    CHECK(readScriptProperties("properties = { speed = ").list.empty());
    CHECK(readScriptProperties("function start(self) properties = { speed = 1 } end").list.empty());
}

TEST_CASE("What the Inspector cannot show of a script's properties is said plainly", "[gameplay][script][properties]")
{
    const ScriptProperties found = readScriptProperties(
        "properties = {\n  speed = 2 * 45,\n  ok = 1,\n  7,\n  object = 3,\n  size = { 1, 2 },\n}\n");
    REQUIRE(found.list.size() == 1);
    CHECK(found.list[0].name == "ok");
    REQUIRE(found.problems.size() == 4);
    CHECK_THAT(found.problems[0], ContainsSubstring("line 2") && ContainsSubstring("'speed' is not a plain value"));
    CHECK_THAT(found.problems[1], ContainsSubstring("line 4") && ContainsSubstring("needs a name"));
    CHECK_THAT(found.problems[2], ContainsSubstring("cannot be called 'object'"));
    CHECK_THAT(found.problems[3], ContainsSubstring("'size'"));

    const ScriptProperties hidden = readScriptProperties("local properties = { speed = 1 }");
    CHECK(hidden.list.empty());
    REQUIRE(hidden.problems.size() == 1);
    CHECK_THAT(hidden.problems[0], ContainsSubstring("without 'local'"));
    CHECK_THAT(readScriptProperties("properties = 5").problems.at(0), ContainsSubstring("in braces"));
}

TEST_CASE("Property names become labels, and an object's own values are kept apart", "[gameplay][script][properties]")
{
    CHECK(scriptPropertyLabel("speed") == "Speed");
    CHECK(scriptPropertyLabel("walkSpeed") == "Walk Speed");
    CHECK(scriptPropertyLabel("walk_speed") == "Walk Speed");
    CHECK(scriptPropertyLabel("maxHP") == "Max HP");

    const ScriptProperty speed{"speed", DataValue(90.0), 1};
    const ScriptProperty label{"label", DataValue(std::string("Lift")), 2};
    CHECK(scriptPropertyValue(speed, "") == DataValue(90.0));
    CHECK(scriptPropertyValue(speed, R"({"speed":120})") == DataValue(120.0));
    // A value of another kind (the script changed since) does not count.
    CHECK(scriptPropertyValue(speed, R"({"speed":"fast"})") == DataValue(90.0));
    CHECK(scriptPropertyValue(speed, "not values at all") == DataValue(90.0));

    std::string values = withScriptPropertyValue("", speed, DataValue(120.0));
    CHECK(values == R"({"speed":120})");
    values = withScriptPropertyValue(values, label, DataValue(std::string("Lift \"B\"")));
    CHECK(scriptPropertyValue(label, values) == DataValue(std::string("Lift \"B\"")));
    CHECK(scriptPropertyValue(speed, values) == DataValue(120.0));
    // Back to what the script says: taken out, so the script's number rules again.
    values = withScriptPropertyValue(values, speed, DataValue(90.0));
    // (In a variable: Microsoft's compiler cannot have this text inside CHECK.)
    const std::string onlyLabel = "{\"label\":\"Lift \\\"B\\\"\"}";
    CHECK(values == onlyLabel);
    CHECK(withScriptPropertyValue(values, label, DataValue(std::string("Lift"))).empty());
    CHECK(writeDataValues(parseDataValues(R"({"a":1.5,"b":true,"c":[1],"d":null})")) == R"({"a":1.5,"b":true})");
    const std::string points = "{\"at\":[3,-4.5],\"bad\":[1,2,3],\"worse\":[\"x\",1]}";
    CHECK(writeDataValues(parseDataValues(points)) == "{\"at\":[3,-4.5]}");
}

TEST_CASE("self starts with the script's properties, and with the object's own where it has them",
          "[gameplay][script][properties]")
{
    ScriptProject p;
    const AssetId mover = p.write("scripts/mover.lua", R"(
        properties = { speed = 60, label = "plain", fast = false, made = 2 * 3 }
        function start(self)
            print(self.object:name() .. ": " .. self.speed .. " " .. self.label .. " " .. tostring(self.fast) .. " " .. self.made
                  .. " " .. tostring(self.unknown))
        end
        function update(self, dt)
            local x, y = self.object:position()
            self.object:setPosition(x + self.speed * dt, y)
        end
    )");
    Scene scene;
    const EntityId plain = addScripted(scene, "Plain", mover);
    const EntityId quick = addScripted(scene, "Quick", mover);
    // Quick has its own speed and label; "fast" is given as text (the wrong
    // kind: ignored) and "unknown" is no property of this script.
    REQUIRE(scene.setProperty(quick, "rolo.Script", "values",
                              std::string(R"({"speed":120,"label":"mine","fast":"yes","unknown":5})")));

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 60);

    CHECK(positionOf(game, plain).x == Catch::Approx(60.0f).margin(0.01f));
    CHECK(positionOf(game, quick).x == Catch::Approx(120.0f).margin(0.01f));
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/mover.lua: Plain: 60 plain false 6 nil");
    CHECK(p.printed[1] == "scripts/mover.lua: Quick: 120 mine false 6 nil");
    CHECK(p.problems.empty());
}

TEST_CASE("A Script's own values are written to scene files as values, not as text", "[gameplay][script][properties]")
{
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameplayComponents(registry);
    Scene scene;
    const EntityId withValues = scene.create("A")->id();
    const EntityId without = scene.create("B")->id();
    scene.addComponent(withValues, std::make_unique<Script>());
    scene.addComponent(without, std::make_unique<Script>());
    REQUIRE(scene.setProperty(withValues, "rolo.Script", "values", std::string(R"({"label":"Lift","speed":120})")));

    const std::string file = serializeScene(scene);
    CHECK_THAT(file, ContainsSubstring("\"values\": {"));
    CHECK_THAT(file, ContainsSubstring("\"speed\": 120"));
    CHECK_THAT(file, !ContainsSubstring("\\\"speed"));
    // An object without its own values writes none.
    CHECK(file.find("\"values\"") == file.rfind("\"values\""));

    const SceneLoadResult read = parseScene(file, registry);
    REQUIRE(read.scene);
    CHECK(read.issues.empty());
    CHECK(read.scene->find(withValues)->get<Script>()->values == R"({"label":"Lift","speed":120})");
    CHECK(read.scene->find(without)->get<Script>()->values.empty());

    // A file from before properties existed (no "values") reads as before.
    std::string older = file;
    const auto at = older.find("\"values\"");
    REQUIRE(at != std::string::npos);
    const auto end = older.find('}', at);
    older.erase(older.rfind(',', at), end + 1 - older.rfind(',', at));
    const SceneLoadResult old = parseScene(older, registry);
    REQUIRE(old.scene);
    CHECK(old.scene->find(withValues)->get<Script>()->values.empty());
}

// --- Making objects while playing --------------------------------------------------------------------

TEST_CASE("obj:clone() makes a copy with its children; its script starts fresh in the next frame",
          "[gameplay][script][spawn]")
{
    ScriptProject p;
    const AssetId mover = p.write("scripts/mover.lua", R"(
        properties = { speed = 60 }
        function start(self)
            self.started = (self.started or 0) + 1
            print(self.object:name() .. " started " .. self.started .. " at speed " .. self.speed)
        end
        function update(self, dt)
            local x, y = self.object:position()
            self.object:setPosition(x + self.speed * dt, y)
        end
    )");
    const AssetId maker = p.write("scripts/maker.lua", R"(
        function start(self)
            local original = game.find("Box")
            local copy = original:clone()
            print("copy is " .. tostring(copy) .. ", the same object: " .. tostring(copy == original))
            copy:setPosition(0, 100)
            -- The first one of that name is still the original.
            print("find gives the original: " .. tostring(game.find("Box") == original))
        end
    )");
    Scene scene;
    const EntityId box = addScripted(scene, "Box", mover, {10, 5});
    const EntityId wheel = scene.create("Wheel", box)->id();
    addScripted(scene, "Maker", maker);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(game.scene().size() == 5);   // Box, Wheel, Maker and the copies of Box and Wheel
    // The copy: another object of the same name, with its own child.
    EntityId copy;
    game.scene().forEach([&](const Entity& e, int) {
        if (e.name() == "Box" && e.id() != box) {
            copy = e.id();
        }
    });
    REQUIRE_FALSE(copy.isNull());
    REQUIRE(game.scene().childrenOf(copy).size() == 1);
    CHECK(game.scene().childrenOf(copy)[0] != wheel);
    CHECK(game.scene().find(game.scene().childrenOf(copy)[0])->name() == "Wheel");

    frames(game, 60);
    // Both move by themselves: the copy has its own self, started once.
    CHECK(positionOf(game, box).x == Catch::Approx(10.0f + 61.0f).margin(0.01f));
    CHECK(positionOf(game, copy).x == Catch::Approx(60.0f).margin(0.01f));
    CHECK(positionOf(game, copy).y == Catch::Approx(100.0f));
    int started = 0;
    for (const std::string& line : p.printed) {
        started += line.find("Box started 1 at speed 60") != std::string::npos ? 1 : 0;
    }
    CHECK(started == 2);
    CHECK(std::find(p.printed.begin(), p.printed.end(), "scripts/maker.lua: copy is object \"Box\", the same object: false")
          != p.printed.end());
    CHECK(std::find(p.printed.begin(), p.printed.end(), "scripts/maker.lua: find gives the original: true") != p.printed.end());
    CHECK(p.problems.empty());
}

TEST_CASE("game.spawn copies a switched-off pattern, places it, switches it on and gives it its own properties",
          "[gameplay][script][spawn]")
{
    ScriptProject p;
    const AssetId bullet = p.write("scripts/bullet.lua", R"(
        properties = { speed = 100, label = "plain" }
        function start(self) print("bullet " .. self.label .. " " .. self.speed) end
        function update(self, dt)
            local x, y = self.object:position()
            self.object:setPosition(x + self.speed * dt, y)
            if x > 150 then self.object:destroy() end
        end
    )");
    const AssetId gun = p.write("scripts/gun.lua", R"(
        function start(self)
            self.a = game.spawn("Bullet", 10, 20)
            self.b = game.spawn("Bullet", vector.create(0, 40), { speed = 300, label = "fast", unknown = 1 })
            self.c = game.spawn(self.a)                      -- an object instead of a name, where it is
            print("made " .. self.a:name() .. ", on: " .. tostring(self.a:isActive()))
        end
        function update(self, dt)
            if game.time() > 0.6 and not self.told then
                self.told = true
                print("b is gone: " .. tostring(not self.b:isValid()) .. ", a is here: " .. tostring(self.a:isValid()))
            end
        end
    )");
    Scene scene;
    const EntityId pattern = addScripted(scene, "Bullet", bullet);
    scene.setActive(pattern, false);
    addScripted(scene, "Gun", gun);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 45);   // 0.75 seconds

    // The pattern itself never ran: it stayed switched off, where it was.
    CHECK_FALSE(game.scene().find(pattern)->isActive());
    CHECK(positionOf(game, pattern).x == 0.0f);
    // a and c fly at 100; b flew at 300, passed 150 and destroyed itself.
    CHECK(game.scene().size() == 4);   // the pattern, the gun, a and c
    const auto has = [&](const std::string& line) {
        return std::find(p.printed.begin(), p.printed.end(), line) != p.printed.end();
    };
    CHECK(has("scripts/gun.lua: made Bullet, on: true"));
    CHECK(has("scripts/bullet.lua: bullet fast 300"));
    CHECK(std::count(p.printed.begin(), p.printed.end(), "scripts/bullet.lua: bullet plain 100") == 2);
    CHECK(has("scripts/gun.lua: b is gone: true, a is here: true"));
    CHECK(p.problems.empty());
}

TEST_CASE("Spawning says plainly what is wrong, and stops a script that never stops making objects",
          "[gameplay][script][spawn]")
{
    ScriptProject p;
    const AssetId plain = p.write("scripts/plain.lua", "function update(self, dt) end");
    const AssetId wrong = p.write("scripts/wrong.lua", R"(
        function start(self)
            local ok, why = pcall(game.spawn, "Nobody")
            print(tostring(ok) .. ": " .. tostring(why))
            ok, why = pcall(game.spawn, "Stone", 1, 2, { speed = 3 })   -- Stone has no Script
            print(tostring(ok) .. ": " .. tostring(why))
            ok, why = pcall(game.spawn, "Pattern", 1, 2, { speed = {} })
            print(tostring(ok) .. ": " .. tostring(why))
        end
    )");
    const AssetId flood = p.write("scripts/flood.lua", R"(
        function update(self, dt)
            for i = 1, 4000 do game.spawn("Stone") end
        end
    )");
    Scene scene;
    scene.create("Stone");
    addScripted(scene, "Pattern", plain);
    addScripted(scene, "Wrong", wrong);
    const EntityId flooder = addScripted(scene, "Flood", flood);
    scene.setActive(flooder, false);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 2);
    REQUIRE(p.printed.size() == 3);
    CHECK_THAT(p.printed[0], ContainsSubstring("false") && ContainsSubstring("no object called \"Nobody\""));
    CHECK_THAT(p.printed[1], ContainsSubstring("has no Script"));
    CHECK_THAT(p.printed[2], ContainsSubstring("'speed' must be a number, a text, true / false, a vector or an object"));
    CHECK(game.scene().size() == 4);   // nothing was made by the three that failed
    CHECK(p.problems.empty());

    game.scene().setActive(flooder, true);
    frames(game, 5);
    for (const std::string& problem : p.problems) {
        INFO(problem);
    }
    CHECK(game.scene().size() == Game::kMostObjects);
    CHECK(p.hasProblem("the most a game may make"));
    CHECK(p.hasProblem("Destroy the ones that are no longer needed"));
}

TEST_CASE("Scripts reach an object's parent and what is inside it", "[gameplay][script][tree]")
{
    ScriptProject p;
    const AssetId looker = p.write("scripts/looker.lua", R"(
        function start(self)
            local car = self.object
            print("parent of the car: " .. tostring(car:parent()))
            local names = {}
            for i, child in ipairs(car:children()) do names[i] = child:name() end
            print("children: " .. table.concat(names, ", "))
            local wheel = car:child("Back Wheel")
            print("wheel's parent is the car: " .. tostring(wheel:parent() == car))
            print("child finds only what is directly inside: " .. tostring(car:child("Nut")))
            print("find looks deeper: " .. tostring(car:find("Nut")) .. ", inside " .. car:find("Nut"):parent():name())
            print("nobody of that name: " .. tostring(car:find("Sail")))
            -- A copy has its own children.
            local copy = car:clone()
            print("the copy's nut is another: " .. tostring(copy:find("Nut") ~= car:find("Nut")))
        end
    )");
    Scene scene;
    const EntityId car = addScripted(scene, "Car", looker);
    scene.create("Front Wheel", car);
    const EntityId back = scene.create("Back Wheel", car)->id();
    scene.create("Nut", back);
    scene.create("Nut");   // another Nut, outside the car: not the car's

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 1);
    REQUIRE(p.printed.size() == 7);
    CHECK(p.printed[0] == "scripts/looker.lua: parent of the car: nil");
    CHECK(p.printed[1] == "scripts/looker.lua: children: Front Wheel, Back Wheel");
    CHECK(p.printed[2] == "scripts/looker.lua: wheel's parent is the car: true");
    CHECK(p.printed[3] == "scripts/looker.lua: child finds only what is directly inside: nil");
    CHECK(p.printed[4] == "scripts/looker.lua: find looks deeper: object \"Nut\", inside Back Wheel");
    CHECK(p.printed[5] == "scripts/looker.lua: nobody of that name: nil");
    CHECK(p.printed[6] == "scripts/looker.lua: the copy's nut is another: true");
    CHECK(p.problems.empty());
}

// --- Waiting: task.wait, task.spawn, task.delay ----------------------------------------------------------

TEST_CASE("start(self) may wait: a script written from top to bottom", "[gameplay][script][task]")
{
    ScriptProject p;
    const AssetId door = p.write("scripts/door.lua", R"(
        function start(self)
            print("closed at " .. string.format("%.2f", game.time()))
            local waited = task.wait(0.5)
            print("open after " .. string.format("%.2f", waited))
            self.open = true
            for i = 1, 3 do
                task.wait()            -- until the next frame
                self.steps = (self.steps or 0) + 1
            end
            print("steps " .. self.steps)
        end
        function update(self, dt)
            -- update goes on every frame while start waits.
            self.frames = (self.frames or 0) + 1
            if self.open and not self.told then
                self.told = true
                print("update saw the door open in frame " .. self.frames)
            end
        end
    )");
    Scene scene;
    addScripted(scene, "Door", door);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 20);
    REQUIRE(p.printed.size() == 1);   // still waiting: a third of a second has passed
    frames(game, 20);
    REQUIRE(p.printed.size() == 4);
    CHECK(p.printed[0] == "scripts/door.lua: closed at 0.02");
    CHECK_THAT(p.printed[1], ContainsSubstring("open after 0.5"));
    CHECK_THAT(p.printed[2], ContainsSubstring("update saw the door open in frame 3"));
    CHECK(p.printed[3] == "scripts/door.lua: steps 3");
    CHECK(p.problems.empty());
}

TEST_CASE("task.spawn begins code of its own, task.delay begins it later; each object's tasks are its own",
          "[gameplay][script][task]")
{
    ScriptProject p;
    const AssetId blinker = p.write("scripts/blinker.lua", R"(
        properties = { every = 0.1 }
        function start(self)
            self.blinks = 0
            task.spawn(function(name)
                while true do                      -- a loop for ever is fine in a task that waits
                    task.wait(self.every)
                    self.blinks += 1
                end
            end, self.object:name())
            task.delay(0.25, function(a, b) print(self.object:name() .. " delayed " .. a .. b .. " at blinks " .. self.blinks) end, "x", "y")
            print(self.object:name() .. " start went on past spawn")
        end
        function update(self, dt)
            if game.time() > 0.99 and not self.told then
                self.told = true
                print(self.object:name() .. " blinked " .. self.blinks)
            end
        end
    )");
    Scene scene;
    addScripted(scene, "Fast", blinker);
    const EntityId slow = addScripted(scene, "Slow", blinker);
    scene.setProperty(slow, "rolo.Script", "values", std::string(R"({"every":0.3})"));
    const EntityId doomed = addScripted(scene, "Doomed", blinker);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 6);
    game.destroy(doomed);   // its tasks go with it
    frames(game, 60);
    const auto has = [&](const std::string& part) {
        return std::any_of(p.printed.begin(), p.printed.end(),
                           [&](const std::string& line) { return line.find(part) != std::string::npos; });
    };
    CHECK(has("Fast start went on past spawn"));
    CHECK(has("Fast delayed xy at blinks 2"));
    CHECK(has("Slow delayed xy at blinks 0"));
    CHECK(has("Fast blinked 9"));
    CHECK(has("Slow blinked 3"));
    CHECK_FALSE(has("Doomed delayed"));
    CHECK_FALSE(has("Doomed blinked"));
    CHECK(p.problems.empty());
}

TEST_CASE("Waiting where it cannot be done, and errors in tasks, are said plainly", "[gameplay][script][task]")
{
    ScriptProject p;
    const AssetId waitsInUpdate = p.write("scripts/waits.lua", R"(
        function update(self, dt)
            task.wait(1)
        end
    )");
    const AssetId failsLater = p.write("scripts/later.lua", R"(
        function start(self)
            task.wait(0.1)
            local nothing = nil
            print(nothing.name)
        end
        function update(self, dt) self.frames = (self.frames or 0) + 1; self.object:setPosition(self.frames, 0) end
    )");
    const AssetId atTop = p.write("scripts/top.lua", R"(
        local ok, why = pcall(task.spawn, function() end)
        print("at the top of a file: " .. tostring(ok) .. " " .. tostring(why))
        function start(self)
            local co = coroutine.create(function() task.wait(1) end)
            local ok2, why2 = coroutine.resume(co)
            print("in a coroutine of its own: " .. tostring(ok2) .. " " .. tostring(why2))
        end
    )");
    const AssetId flood = p.write("scripts/flood.lua", R"(
        function update(self, dt)
            for i = 1, 600 do task.spawn(function() task.wait(100) end) end
        end
    )");
    Scene scene;
    addScripted(scene, "Waits", waitsInUpdate);
    const EntityId later = addScripted(scene, "Later", failsLater);
    addScripted(scene, "Top", atTop);
    addScripted(scene, "Flood", flood);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 30);
    CHECK(p.hasProblem("task.wait can only be used in start(self)"));
    CHECK(p.hasProblem("Script on 'Waits' failed in"));
    // The error after the wait names the line, and switches that script off.
    CHECK(p.hasProblem("in a task (code that waited): scripts/later.lua:5:"));
    CHECK(p.hasProblem("called from: scripts/later.lua:5"));
    const float stoppedAt = positionOf(game, later).x;
    frames(game, 10);
    CHECK(positionOf(game, later).x == stoppedAt);
    REQUIRE(p.printed.size() == 2);
    CHECK_THAT(p.printed[0], ContainsSubstring("at the top of a file: false") && ContainsSubstring("tasks belong to an object"));
    CHECK_THAT(p.printed[1], ContainsSubstring("in a coroutine of its own: false") && ContainsSubstring("task.wait can only be used"));
    CHECK(p.hasProblem("1000 tasks waiting"));
}

// --- Events: connect, once, wait, disconnect (ADR 0032) --------------------------------------------------

TEST_CASE("A script listens to game events: connect, once, disconnect and wait", "[gameplay][script][event]")
{
    ScriptProject p;
    const AssetId gate = p.write("scripts/gate.lua", R"(
        function start(self)
            local opened = game.event("gate_open")
            print(tostring(opened), typeof(opened))
            self.every = opened:connect(function()
                self.count = (self.count or 0) + 1
                print("heard " .. self.count)
                task.wait(0.05)                 -- a connected function may wait
                print("and waited after " .. self.count)
            end)
            opened:once(function() print("once") end)
            print("connected", self.every:isConnected())
            opened:wait()                       -- start goes on when it is sent
            print("start went on")
            game.event("never"):wait()
            print("not reached")
        end
        function update(self, dt)
            if game.wasEmitted("stop") then
                self.every:disconnect()
                self.every:disconnect()         -- twice does no harm
                print("disconnected", self.every:isConnected())
            end
        end
    )");
    Scene scene;
    addScripted(scene, "Gate", gate);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);
    REQUIRE(p.printed.size() == 2);
    const std::string shown = "scripts/gate.lua: event \"gate_open\"\tevent";
    CHECK(p.printed[0] == shown);
    CHECK(p.printed[1] == "scripts/gate.lua: connected\ttrue");

    game.emit("gate_open");   // heard in the next frame, in the order connected
    frames(game, 1);
    REQUIRE(p.printed.size() == 5);
    CHECK(p.printed[2] == "scripts/gate.lua: heard 1");
    CHECK(p.printed[3] == "scripts/gate.lua: once");
    CHECK(p.printed[4] == "scripts/gate.lua: start went on");
    frames(game, 5);
    REQUIRE(p.printed.size() == 6);
    CHECK(p.printed[5] == "scripts/gate.lua: and waited after 1");

    game.emit("gate_open");   // again: only the lasting connection is left
    frames(game, 8);
    REQUIRE(p.printed.size() == 8);
    CHECK(p.printed[6] == "scripts/gate.lua: heard 2");
    CHECK(p.printed[7] == "scripts/gate.lua: and waited after 2");

    game.emit("stop");
    frames(game, 2);
    game.emit("gate_open");
    frames(game, 8);
    REQUIRE(p.printed.size() == 9);
    CHECK(p.printed[8] == "scripts/gate.lua: disconnected\tfalse");
    CHECK(p.problems.empty());
}

TEST_CASE("A script listens to keys and actions, and waits for one", "[gameplay][script][event]")
{
    ScriptProject p;
    const AssetId script = p.write("scripts/intro.lua", R"(
        function start(self)
            input.action("jump", {"space", "pad a"})
            input.pressed("jump"):connect(function() print("jump") end)
            input.released("jump"):connect(function() print("let go") end)
            input.pressed("mouse left"):connect(function() print("click") end)
            print("press E")
            input.pressed("e"):wait()
            print("thank you")
        end
    )");
    Scene scene;
    addScripted(scene, "Intro", script);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);
    REQUIRE(p.printed.size() == 1);
    game.input().press(Key::Space);
    frames(game, 2);   // held down: pressed once
    game.input().release(Key::Space);
    frames(game, 1);
    game.input().press(MouseButton::Left);
    game.input().press(Key::E);
    frames(game, 2);
    REQUIRE(p.printed.size() == 5);
    CHECK(p.printed[1] == "scripts/intro.lua: jump");
    CHECK(p.printed[2] == "scripts/intro.lua: let go");
    CHECK(p.printed[3] == "scripts/intro.lua: click");
    CHECK(p.printed[4] == "scripts/intro.lua: thank you");
    CHECK(p.problems.empty());
}

TEST_CASE("A script hears any object begin to touch another", "[gameplay][script][event]")
{
    ScriptProject p;
    const AssetId watcher = p.write("scripts/watcher.lua", R"(
        function start(self)
            local crate = game.find("Crate")
            crate:collided():connect(function(other)
                print("crate hit " .. other:name())
            end)
            local other = crate:collided():wait()
            print("waited for " .. other:name())
        end
    )");
    Scene scene;
    const auto solid = [&scene](const std::string& name, Vec2 at, Vec2 size, bool body) {
        const EntityId id = scene.create(name)->id();
        scene.setProperty(id, "rolo.Transform", "position", at);
        auto collider = std::make_unique<Collider>();
        collider->size = size;
        scene.addComponent(id, std::move(collider));
        if (body) {
            scene.addComponent(id, std::make_unique<RigidBody>());
        }
        return id;
    };
    solid("Floor", {0.0f, -50.0f}, {2000.0f, 100.0f}, false);
    solid("Crate", {0.0f, 60.0f}, {20.0f, 20.0f}, true);
    addScripted(scene, "Watcher", watcher);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 120);
    REQUIRE(p.printed.size() == 2);   // it landed once
    CHECK(p.printed[0] == "scripts/watcher.lua: crate hit Floor");
    CHECK(p.printed[1] == "scripts/watcher.lua: waited for Floor");
    CHECK(p.problems.empty());
}

TEST_CASE("Connections belong to their object: they rest while it is off and go with it",
          "[gameplay][script][event]")
{
    ScriptProject p;
    const AssetId listener = p.write("scripts/listener.lua", R"(
        function start(self)
            game.event("ping"):connect(function() print(self.object:name() .. " heard ping") end)
        end
    )");
    Scene scene;
    const EntityId a = addScripted(scene, "A", listener);
    const EntityId b = addScripted(scene, "B", listener);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 2);
    game.emit("ping");
    frames(game, 2);
    REQUIRE(p.printed.size() == 2);

    game.scene().setActive(a, false);   // A sleeps through the next one
    game.destroy(b);
    frames(game, 1);
    game.emit("ping");
    frames(game, 3);
    CHECK(p.printed.size() == 2);

    game.scene().setActive(a, true);
    game.emit("ping");
    frames(game, 2);
    REQUIRE(p.printed.size() == 3);
    CHECK(p.printed[2] == "scripts/listener.lua: A heard ping");
    CHECK(p.problems.empty());
}

TEST_CASE("Mistakes with events are said plainly", "[gameplay][script][event]")
{
    ScriptProject p;
    const AssetId roblox = p.write("scripts/roblox.lua", R"(
        function start(self)
            game.event("x"):Connect(function() end)
        end
    )");
    const AssetId inUpdate = p.write("scripts/in_update.lua", R"(
        function update(self, dt)
            game.event("x"):wait()
        end
    )");
    const AssetId everyFrame = p.write("scripts/every_frame.lua", R"(
        function update(self, dt)
            for i = 1, 300 do game.event("x"):connect(function() end) end
        end
    )");
    const AssetId broken = p.write("scripts/broken.lua", R"(
        function start(self)
            game.event("boom"):connect(function()
                local nothing = nil
                nothing.field = 1
            end)
        end
        function update(self, dt)
            self.frames = (self.frames or 0) + 1
            if game.wasEmitted("report") then print("frames " .. self.frames) end
        end
    )");
    const AssetId misc = p.write("scripts/misc.lua", R"(
        local ok, why = pcall(function() game.event("x"):connect(function() end) end)
        print("at the top of a file: " .. tostring(ok) .. " " .. tostring(why))
        function start(self)
            print(pcall(game.event, ""))
            print(pcall(input.pressed, "no such key"))
            print(pcall(function() game.event("x"):connect(5) end))
        end
    )");
    Scene scene;
    addScripted(scene, "Roblox", roblox);
    addScripted(scene, "InUpdate", inUpdate);
    addScripted(scene, "EveryFrame", everyFrame);
    addScripted(scene, "Broken", broken);
    addScripted(scene, "Misc", misc);
    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 6);
    CHECK(p.hasProblem("begin with a small letter: connect, once, wait, disconnect"));
    CHECK(p.hasProblem("wait can only be used in start(self)"));
    CHECK(p.hasProblem("listens to 1000 events"));
    REQUIRE(p.printed.size() == 4);
    CHECK_THAT(p.printed[0], ContainsSubstring("at the top of a file: false") && ContainsSubstring("an object listens to events"));
    CHECK_THAT(p.printed[1], ContainsSubstring("false") && ContainsSubstring("an event's name expected"));
    CHECK_THAT(p.printed[2], ContainsSubstring("false") && ContainsSubstring("unknown key"));
    CHECK_THAT(p.printed[3], ContainsSubstring("false") && ContainsSubstring("function expected"));

    // An error in a connected function names its line and switches that
    // object's script off, like an error in update.
    const std::size_t before = p.problems.size();
    game.emit("boom");
    frames(game, 2);
    REQUIRE(p.problems.size() == before + 1);
    CHECK_THAT(p.problems.back(), ContainsSubstring("in a function connected to an event")
                                      && ContainsSubstring("scripts/broken.lua:5"));
    game.emit("report");
    frames(game, 2);
    CHECK(p.printed.size() == 4);   // its update no longer runs
}

// --- Prefabs: game.spawn from a file (ADR 0033) -----------------------------------------------------------

namespace {

// A game whose scene loader reads prefab files, as the player's and the editor's do.
struct PrefabGame {
    ScriptProject& p;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    int loads = 0;
    std::unique_ptr<Game> game;

    explicit PrefabGame(ScriptProject& project)
        : p(project)
    {
        registerGameplayComponents(registry);
        GameSetup setup;
        setup.assets = &p.assets;
        setup.loadScene = [this](AssetId id, std::string& error) -> std::optional<Scene> {
            const AssetRecord* record = p.assets.find(id);
            if (record == nullptr || record->type != AssetType::Prefab) {
                error = "not a prefab";
                return std::nullopt;
            }
            ++loads;
            SceneLoadResult loaded = loadPrefabFile(p.assets.absolutePath(*record), registry);
            if (!loaded.ok()) {
                error = loaded.firstError();
                return std::nullopt;
            }
            return std::move(*loaded.scene);
        };
        game = std::make_unique<Game>(p.log, std::move(setup));
    }

    // Writes `root` of `scene` as the prefab file `path`.
    void save(const std::string& path, const Scene& scene, EntityId root)
    {
        fs::create_directories((p.folder / path).parent_path());
        std::ofstream(p.folder / path, std::ios::binary) << serializePrefab(scene, root);
        p.assets.refresh();
        REQUIRE(p.assets.findByPath(path) != nullptr);
    }
};

} // namespace

TEST_CASE("game.spawn makes copies of a prefab when the scene has no object of that name",
          "[gameplay][script][spawn][prefab]")
{
    ScriptProject p;
    const AssetId bulletScript = p.write("scripts/bullet.lua", R"(
        properties = { speed = 500 }
        function start(self)
            local x, y = self.object:position()
            print("bullet at " .. x .. "," .. y .. " speed " .. self.speed .. " with " .. #self.object:children() .. " inside")
        end
    )");
    const AssetId gun = p.write("scripts/gun.lua", R"(
        function start(self)
            local a = game.spawn("bullet", 10, 20)
            local b = game.spawn("bullet", 30, 40, { speed = 90 })
            local c = game.spawn("prefabs/bullet.lprefab")       -- by path, where it was saved
            print(a:name(), a:isActive(), a ~= b, c:position())
            print(pcall(game.spawn, "rocket"))
        end
    )");
    PrefabGame g(p);
    {
        // The prefab is made in some other scene: a Bullet with a Glow inside.
        Scene workshop("Workshop", EntityIdGenerator(21));
        const EntityId bullet = addScripted(workshop, "Bullet", bulletScript);
        workshop.setProperty(bullet, "rolo.Transform", "position", Vec2{7.0f, 8.0f});
        workshop.create("Glow", bullet);
        g.save("prefabs/bullet.lprefab", workshop, bullet);
    }
    Scene scene;
    addScripted(scene, "Gun", gun);
    g.game->start(scene);
    frames(*g.game, 3);
    CHECK(p.problems.empty());
    REQUIRE(p.printed.size() == 5);
    CHECK(p.printed[0] == "scripts/gun.lua: Bullet\ttrue\ttrue\t7\t8");
    CHECK_THAT(p.printed[1], ContainsSubstring("false") && ContainsSubstring("there is no object called")
                                 && ContainsSubstring("rocket") && ContainsSubstring("no prefab of that name"));
    // (The copies start in the scene's order, which is not the order they were made in.)
    const auto started = [&p](const std::string& line) {
        return std::count(p.printed.begin() + 2, p.printed.end(), "scripts/bullet.lua: " + line) == 1;
    };
    CHECK(started("bullet at 10,20 speed 500 with 1 inside"));
    CHECK(started("bullet at 30,40 speed 90 with 1 inside"));
    CHECK(started("bullet at 7,8 speed 500 with 1 inside"));

    // The file was read once for each name it was asked by; the pattern
    // waits in the playing scene, switched off, and its script never ran.
    CHECK(g.loads == 2);
    CHECK(scene.size() == 1);   // the scene that was started is untouched
    // Gun, 2 patterns and 3 copies, each Bullet with its Glow.
    CHECK(g.game->scene().size() == 1 + 5 * 2);
}

TEST_CASE("An object of the scene comes before a prefab of the same name, and broken prefabs are said",
          "[gameplay][script][spawn][prefab]")
{
    ScriptProject p;
    const AssetId maker = p.write("scripts/maker.lua", R"(
        function start(self)
            local crate = game.spawn("Crate")
            print(#crate:children())
            print(pcall(game.spawn, "broken"))
            print(pcall(game.spawn, "twin"))
        end
    )");
    PrefabGame g(p);
    {
        Scene workshop("Workshop", EntityIdGenerator(22));
        const EntityId crate = workshop.create("Crate")->id();
        workshop.create("Lid", crate);
        g.save("prefabs/Crate.lprefab", workshop, crate);
        g.save("a/twin.lprefab", workshop, crate);
        g.save("b/twin.lprefab", workshop, crate);
    }
    std::ofstream(p.folder / "prefabs/broken.lprefab", std::ios::binary) << "{ not json";
    p.assets.refresh();
    Scene scene;
    scene.create("Crate");   // the scene's own Crate has nothing inside
    addScripted(scene, "Maker", maker);
    g.game->start(scene);
    frames(*g.game, 2);
    REQUIRE(p.printed.size() == 3);
    CHECK(p.printed[0] == "scripts/maker.lua: 0");
    CHECK_THAT(p.printed[1], ContainsSubstring("false") && ContainsSubstring("prefabs/broken.lprefab could not be used"));
    CHECK_THAT(p.printed[2], ContainsSubstring("false") && ContainsSubstring("several prefabs are called")
                                 && ContainsSubstring("write the path"));
    CHECK(p.problems.empty());
}

// --- More kinds of property: colours, points, other objects (ADR 0034) ------------------------------------

TEST_CASE("A script's properties can be colours, points and other objects", "[gameplay][script][properties][kinds]")
{
    const ScriptProperties found = readScriptProperties(
        "properties = {\n"
        "  tint = color(\"#FFB84D\"),\n"
        "  faint = color(\"00000080\"),\n"
        "  goal = vector.create(300, -120.5),\n"
        "  door = object(),\n"
        "  speed = 90,\n"
        "}\n");
    CHECK(found.problems.empty());
    REQUIRE(found.list.size() == 5);
    using Kind = ScriptProperty::Kind;
    CHECK(found.list[0].kind == Kind::Color);
    CHECK(std::get<std::string>(found.list[0].value) == Color{0xFF, 0xB8, 0x4D, 0xFF}.toHex());
    CHECK(std::get<std::string>(found.list[1].value) == Color{0, 0, 0, 0x80}.toHex());
    CHECK(found.list[2].kind == Kind::Point);
    CHECK(std::get<Vec2>(found.list[2].value) == Vec2{300.0f, -120.5f});
    CHECK(found.list[3].kind == Kind::Object);
    CHECK(std::get<std::string>(found.list[3].value).empty());
    CHECK(found.list[4].kind == Kind::Number);

    // An object's own values: kept only where they are of the kind, and differ.
    const ScriptProperty& tint = found.list[0];
    const ScriptProperty& goal = found.list[2];
    const ScriptProperty& door = found.list[3];
    std::string values;
    values = withScriptPropertyValue(values, tint, DataValue(std::string("#ff0000")));
    values = withScriptPropertyValue(values, goal, DataValue(Vec2{10.0f, 2.5f}));
    values = withScriptPropertyValue(values, door, DataValue(std::string("00000000000000a7")));
    const std::string expected = "{\"door\":\"00000000000000a7\",\"goal\":[10,2.5],\"tint\":\"" + Color{255, 0, 0, 255}.toHex() + "\"}";
    CHECK(values == expected);
    CHECK(std::get<Vec2>(scriptPropertyValue(goal, values)) == Vec2{10.0f, 2.5f});
    CHECK(std::get<std::string>(scriptPropertyValue(door, values)) == "00000000000000a7");
    // Not a colour, not an ID, not a point: the script's value serves, and setting such a thing takes the value out.
    const std::string wrong = "{\"door\":\"the door\",\"goal\":\"far\",\"tint\":\"red\"}";
    CHECK(scriptPropertyValue(tint, wrong) == tint.value);
    CHECK(scriptPropertyValue(goal, wrong) == goal.value);
    CHECK(scriptPropertyValue(door, wrong) == door.value);
    CHECK(withScriptPropertyValue(values, tint, DataValue(std::string("red"))).find("tint") == std::string::npos);
    // Back to the script's value, or to no object: nothing is kept.
    values = withScriptPropertyValue(values, tint, tint.value);
    values = withScriptPropertyValue(values, goal, goal.value);
    values = withScriptPropertyValue(values, door, DataValue(std::string()));
    CHECK(values.empty());

    const ScriptProperties bad = readScriptProperties(
        "properties = {\n  a = color(\"red\"),\n  b = color(),\n  c = vector.create(1),\n  d = vector.create(x, 2),\n"
        "  e = object(\"Door\"),\n  f = game.find(\"Door\"),\n}\n");
    CHECK(bad.list.empty());
    REQUIRE(bad.problems.size() == 6);
    CHECK_THAT(bad.problems[0], ContainsSubstring("line 2") && ContainsSubstring("'a' is not a colour"));
    CHECK_THAT(bad.problems[1], ContainsSubstring("'b' is not a colour"));
    CHECK_THAT(bad.problems[2], ContainsSubstring("'c' is not a point"));
    CHECK_THAT(bad.problems[3], ContainsSubstring("'d' is not a point"));
    CHECK_THAT(bad.problems[4], ContainsSubstring("nothing in the brackets"));
    CHECK_THAT(bad.problems[5], ContainsSubstring("'f' is not a plain value") && ContainsSubstring("object()"));
}

TEST_CASE("Colours, points and objects reach self, and a copy's objects are its own", "[gameplay][script][properties][kinds]")
{
    ScriptProject p;
    const AssetId tank = p.write("scripts/tank.lua", R"(
        properties = {
            tint = color("#ffb84d"),
            goal = vector.create(300, 120),
            turret = object(),
            target = object(),
            gone = object(),
            never = object(),
        }
        function start(self)
            print(self.object:name(), self.tint, self.goal.x, self.goal.y)
            print(self.turret and self.turret:parent() == self.object, self.target and self.target:name(),
                  self.gone, self.never)
        end
    )");
    const AssetId maker = p.write("scripts/maker.lua", R"(
        function start(self)
            local flag = game.find("Flag")
            local copy = game.spawn("Tank", 0, 0, { goal = vector.create(1, 2), target = flag, tint = "#00ff00" })
            copy:setColor(color("#102030"))
            print(copy:color(), game.find("Flag"):color())
            print(pcall(game.spawn, "Tank", 0, 0, { goal = {} }))
            print(pcall(function() flag:setColor("red") end))
            print(pcall(function() self.object:setColor("#ffffff") end))
            print(pcall(object, "Door"))
            print(object():isValid(), color("FFFFFFF0"))
        end
    )");
    Scene scene("Level", EntityIdGenerator(9));
    const EntityId flag = scene.create("Flag")->id();
    scene.addComponent(flag, std::make_unique<Shape>());
    const EntityId target = scene.create("Target")->id();
    const EntityId tankId = addScripted(scene, "Tank", tank);
    scene.addComponent(tankId, std::make_unique<Shape>());
    const EntityId turret = scene.create("Turret", tankId)->id();
    const std::string values = "{\"gone\":\"00000000000000ff\",\"goal\":[7,8],\"target\":\"" + target.toString()
                               + "\",\"tint\":\"#ff0000ff\",\"turret\":\"" + turret.toString() + "\"}";
    scene.setProperty(tankId, "rolo.Script", "values", values);
    addScripted(scene, "Maker", maker);

    Game game(p.log, &p.assets);
    game.start(scene);
    frames(game, 3);
    CHECK(p.problems.empty());
    REQUIRE(p.printed.size() == 10);
    const auto has = [&p](const std::string& line) {
        return std::count(p.printed.begin(), p.printed.end(), "scripts/" + line) == 1;
    };
    // The tank of the scene: what the Inspector gave it. (An object that is not there is nil.)
    CHECK(has("tank.lua: Tank\t#ff0000ff\t7\t8"));
    CHECK(has("tank.lua: true\tTarget\tnil\tnil"));
    // The copy: its own values, and its turret is the copy's turret, not the pattern's.
    CHECK(has("tank.lua: Tank\t#00ff00\t1\t2"));
    CHECK(has("tank.lua: true\tFlag\tnil\tnil"));
    CHECK(has("maker.lua: #102030ff\t#ffffffff"));
    const auto said = [&p](const std::string& part) {
        return std::count_if(p.printed.begin(), p.printed.end(), [&part](const std::string& line) {
                   return line.find("false") != std::string::npos && line.find(part) != std::string::npos;
               }) == 1;
    };
    CHECK(said("a vector or an object"));
    CHECK(said("a colour expected"));
    CHECK(said("no Sprite, Shape or Text"));
    CHECK(said("object() takes nothing"));
    CHECK(has("maker.lua: false\t#fffffff0"));
}

// --- Linked copies of prefabs in a game (ADR 0035) ---------------------------------------------------------

TEST_CASE("A scene's linked copies of a prefab play as ordinary objects, each with its own parts",
          "[gameplay][script][prefab][linked]")
{
    ScriptProject p;
    const AssetId tank = p.write("scripts/tank.lua", R"(
        properties = { turret = object(), speed = 10 }
        function start(self)
            local x = self.object:position()
            print(self.object:name() .. " at " .. x .. " speed " .. self.speed .. " own turret: "
                  .. tostring(self.turret ~= nil and self.turret:parent() == self.object))
        end
    )");
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    registerGameplayComponents(registry);

    // The prefab: a Tank whose script points at its own Turret.
    Scene workshop("Workshop", EntityIdGenerator(31));
    const EntityId tankId = addScripted(workshop, "Tank", tank);
    const EntityId turret = workshop.create("Turret", tankId)->id();
    workshop.setProperty(tankId, "rolo.Script", "values", "{\"turret\":\"" + turret.toString() + "\"}");
    const AssetId prefabId{0x7A4C};
    const std::string prefabText = serializePrefab(workshop, tankId);
    const PrefabTexts prefabs = [&](AssetId id) -> std::optional<std::string> {
        return id == prefabId ? std::optional<std::string>(prefabText) : std::nullopt;
    };

    // A level with two copies; the second is faster and stands elsewhere.
    Scene level("Level", EntityIdGenerator(32));
    const SceneLoadResult prefab = parsePrefab(prefabText, registry);
    REQUIRE(prefab.ok());
    const EntitySnapshot first = linkedCopyOf(level, *prefab.scene, prefabId);
    const EntitySnapshot second = linkedCopyOf(level, *prefab.scene, prefabId);
    REQUIRE(level.restore(first, EntityId{}, 0));
    REQUIRE(level.restore(second, EntityId{}, 1));
    level.setProperty(second.rootId(), "rolo.Transform", "position", Vec2{300.0f, 0.0f});
    level.rename(second.rootId(), "Fast Tank");
    const std::string values = "{\"speed\":50,\"turret\":\"" + second.items[1].id.toString() + "\"}";
    level.setProperty(second.rootId(), "rolo.Script", "values", values);

    // Saved and read again, as a game reads it.
    const std::string file = serializeScene(level, prefabs);
    CHECK(file.find("Turret") == std::string::npos);
    // The second copy's own speed is there; that its turret is its own Turret is the prefab's doing, and is not.
    CHECK(file.find("\"speed\": 50") != std::string::npos);
    CHECK(file.find("\"turret\"") == std::string::npos);
    const SceneLoadResult read = parseScene(file, registry, prefabs);
    REQUIRE(read.ok());
    CHECK(read.warningCount() == 0);

    // The prefab's speed changes later: the copy with its own speed keeps it, the other follows.
    workshop.setProperty(tankId, "rolo.Script", "values", "{\"speed\":20,\"turret\":\"" + turret.toString() + "\"}");
    const std::string faster = serializePrefab(workshop, tankId);
    const PrefabTexts later = [&](AssetId id) -> std::optional<std::string> {
        return id == prefabId ? std::optional<std::string>(faster) : std::nullopt;
    };
    const SceneLoadResult followed = parseScene(file, registry, later);
    REQUIRE(followed.ok());
    const DataValues firstValues = parseDataValues(followed.scene->find(first.rootId())->get<Script>()->values);
    const DataValues secondValues = parseDataValues(followed.scene->find(second.rootId())->get<Script>()->values);
    CHECK(std::get<double>(firstValues.at("speed")) == 20.0);
    CHECK(std::get<double>(secondValues.at("speed")) == 50.0);
    CHECK(std::get<std::string>(secondValues.at("turret")) == second.items[1].id.toString());

    Game game(p.log, &p.assets);
    game.start(*read.scene);
    frames(game, 2);
    CHECK(p.problems.empty());
    REQUIRE(p.printed.size() == 2);
    CHECK(p.printed[0] == "scripts/tank.lua: Tank at 0 speed 10 own turret: true");
    CHECK(p.printed[1] == "scripts/tank.lua: Fast Tank at 300 speed 50 own turret: true");
}
