// Lua scripts (rolo/gameplay/Script.hpp): running, the API scripts call,
// keeping scripts apart, and what happens when a script goes wrong.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
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
    REQUIRE(type->properties.size() == 1);
    CHECK(type->properties[0].key == "script");
    CHECK(type->properties[0].options.assetType == "script");
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
