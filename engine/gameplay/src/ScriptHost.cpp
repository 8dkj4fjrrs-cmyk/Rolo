#include "ScriptHost.hpp"

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Component.hpp"

// Lua is compiled as C++ (third_party/lua/CMakeLists.txt), so its headers are
// included directly, without the extern "C" wrapper of lua.hpp.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace rolo {

namespace {

constexpr int kNoRef = -2; // LUA_NOREF

// The message handler of every call: adds where the error happened. Lua's
// traceback also lists Rolo's own C functions ("[C]: in ?"); only the
// lines inside scripts help the person who wrote them, so the rest is dropped.
int addTraceback(lua_State* L)
{
    const char* message = lua_tostring(L, 1);
    if (message == nullptr) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) {
            return 1;
        }
        message = lua_pushfstring(L, "(the error is a %s, not a message)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, nullptr, 1);
    std::istringstream lines(lua_tostring(L, -1));
    std::string result = message;
    std::string line;
    bool any = false;
    while (std::getline(lines, line)) {
        if (line.find(".lua:") == std::string::npos) {
            continue; // "stack traceback:", "[C]: in ...", ...
        }
        const std::size_t start = line.find_first_not_of(" \t");
        result += (any ? "\n  " : "\n  called from: ") + line.substr(start == std::string::npos ? 0 : start);
        any = true;
    }
    lua_pushstring(L, result.c_str());
    return 1;
}

// Opens the safe parts of Lua's standard library. Runs inside lua_pcall, so
// running out of memory here is an error, not a crash.
int openLibraries(lua_State* L)
{
    const luaL_Reg libraries[] = {
        {LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},    {LUA_UTF8LIBNAME, luaopen_utf8},
    };
    for (const luaL_Reg& library : libraries) {
        luaL_requiref(L, library.name, library.func, 1);
        lua_pop(L, 1);
    }
    // The base library can still read files (dofile, loadfile) and load
    // compiled bytecode (load), which can crash the program if damaged.
    for (const char* unsafe : {"dofile", "loadfile", "load"}) {
        lua_pushnil(L);
        lua_setglobal(L, unsafe);
    }
    openScriptApi(ScriptHost::of(L));
    return 0;
}

} // namespace

ScriptHost::ScriptHost(Game& game, const Logger& log) : m_game(game), m_log(log)
{
    m_L = lua_newstate(&ScriptHost::allocate, this);
    if (m_L == nullptr) {
        throw std::runtime_error("could not start Lua: out of memory");
    }
    // Every Lua state has a little space for its host program: ours points
    // back at this ScriptHost, so the C++ functions scripts call can find it.
    *static_cast<ScriptHost**>(lua_getextraspace(m_L)) = this;
    lua_pushcfunction(m_L, openLibraries);
    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(m_L, -1) != nullptr ? lua_tostring(m_L, -1) : "unknown error";
        lua_close(m_L);
        throw std::runtime_error("could not start Lua: " + error);
    }
}

ScriptHost::~ScriptHost()
{
    lua_close(m_L);
}

ScriptHost& ScriptHost::of(lua_State* L)
{
    return **static_cast<ScriptHost**>(lua_getextraspace(L));
}

// --- Memory and time limits ---------------------------------------------------------------------------

void* ScriptHost::allocate(void* ud, void* block, std::size_t oldSize, std::size_t newSize) noexcept
{
    auto* host = static_cast<ScriptHost*>(ud);
    // When `block` is null, Lua passes a type code in oldSize, not a size.
    const std::size_t previous = block != nullptr ? oldSize : 0;
    if (newSize == 0) {
        std::free(block);
        host->m_memoryUsed -= previous;
        return nullptr;
    }
    if (newSize > previous && host->m_memoryUsed - previous + newSize > kMemoryLimit) {
        return nullptr; // Lua turns this into a "not enough memory" error
    }
    void* grown = std::realloc(block, newSize);
    if (grown != nullptr) {
        host->m_memoryUsed = host->m_memoryUsed - previous + newSize;
    }
    return grown;
}

void ScriptHost::countHook(lua_State* L, lua_Debug* /*ar*/)
{
    ScriptHost& host = of(L);
    host.m_instructions += 1000;
    if (host.m_instructions > kInstructionLimit) {
        luaL_error(L, "the script ran for too long without finishing (is there a loop that never ends?)");
    }
}

// --- Calling scripts ------------------------------------------------------------------------------------

std::optional<std::string> ScriptHost::call(int arguments, const std::string& file)
{
    // Stack: function, arguments. Put the message handler below the function.
    const int handler = lua_gettop(m_L) - arguments;
    lua_pushcfunction(m_L, addTraceback);
    lua_insert(m_L, handler);

    const std::string previousFile = m_currentFile;
    m_currentFile = file;
    m_instructions = 0;
    lua_sethook(m_L, &ScriptHost::countHook, LUA_MASKCOUNT, 1000);
    const int status = lua_pcall(m_L, arguments, 0, handler);
    lua_sethook(m_L, nullptr, 0, 0);
    m_currentFile = previousFile;

    std::optional<std::string> error;
    if (status != LUA_OK) {
        if (status == LUA_ERRMEM) {
            error = "the scripts used more than " + std::to_string(kMemoryLimit / (1024u * 1024u)) +
                    " MB of memory, the most a game's scripts may use";
        } else {
            const char* message = lua_tostring(m_L, -1);
            error = message != nullptr ? message : "unknown error";
        }
        lua_pop(m_L, 1);
    }
    lua_remove(m_L, handler);
    return error;
}

bool ScriptHost::pushEnvironment(AssetId script, std::string& error)
{
    auto found = m_files.find(script);
    if (found == m_files.end()) {
        File file;
        const AssetDatabase* assets = m_game.assets();
        const AssetRecord* record = assets != nullptr ? assets->find(script) : nullptr;
        if (record == nullptr || record->type != AssetType::Script) {
            file.error =
                "its script file is missing (was it deleted?); choose a .lua file in the Script component";
        } else if (const auto code = assets->readText(script); !code) {
            file.path = record->path;
            file.error = "could not read " + record->path;
        } else {
            file.path = record->path;
            // "@path" makes Lua name the file in messages: "scripts/coin.lua:12: ...".
            const std::string chunkName = "@" + record->path;
            if (luaL_loadbufferx(m_L, code->data(), code->size(), chunkName.c_str(), "t") != LUA_OK) {
                file.error = lua_tostring(m_L, -1);
                lua_pop(m_L, 1);
            } else {
                // The file's own environment: reads fall through to the shared
                // globals (__index), writes stay in the file's table.
                lua_newtable(m_L);
                lua_newtable(m_L);
                lua_pushglobaltable(m_L);
                lua_setfield(m_L, -2, "__index");
                lua_setmetatable(m_L, -2);
                lua_pushvalue(m_L, -1);
                lua_setupvalue(m_L, -3, 1); // the chunk's _ENV is the new table
                lua_insert(m_L, -2);        // stack: environment, chunk
                if (auto failed = call(0, file.path)) {
                    file.error = *failed;
                    lua_pop(m_L, 1);
                } else {
                    file.environment = luaL_ref(m_L, LUA_REGISTRYINDEX);
                }
            }
        }
        found = m_files.emplace(script, std::move(file)).first;
    }
    if (found->second.environment == kNoRef) {
        error = found->second.error;
        return false;
    }
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, found->second.environment);
    return true;
}

void ScriptHost::ensureSelf(Script& script, EntityId object)
{
    if (script.m_self != kNoRef) {
        return;
    }
    lua_newtable(m_L);
    pushObjectForScripts(m_L, object);
    lua_setfield(m_L, -2, "object");
    script.m_self = luaL_ref(m_L, LUA_REGISTRYINDEX);
}

std::optional<std::string> ScriptHost::call(Script& script, EntityId object, const char* name,
                                            std::optional<float> dt, EntityId other)
{
    std::string error;
    if (!pushEnvironment(script.script, error)) {
        return error;
    }
    ensureSelf(script, object);
    lua_getfield(m_L, -1, name);
    lua_remove(m_L, -2); // the environment
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return std::nullopt; // the script does not define it: nothing to do
    }
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, script.m_self);
    int arguments = 1;
    if (dt) {
        lua_pushnumber(m_L, static_cast<lua_Number>(*dt));
        arguments = 2;
    } else if (!other.isNull()) {
        pushObjectForScripts(m_L, other);
        arguments = 2;
    }
    return call(arguments, m_files.at(script.script).path);
}

std::optional<std::string> ScriptHost::start(Script& script, EntityId object)
{
    return call(script, object, "start", std::nullopt);
}

std::optional<std::string> ScriptHost::update(Script& script, EntityId object, float dt)
{
    return call(script, object, "update", dt);
}

std::optional<std::string> ScriptHost::collide(Script& script, EntityId object, EntityId other)
{
    return call(script, object, "collide", std::nullopt, other);
}

void ScriptHost::release(int self) noexcept
{
    if (self != kNoRef) {
        luaL_unref(m_L, LUA_REGISTRYINDEX, self);
    }
}

void ScriptHost::setActive(EntityId object, bool active)
{
    m_game.setActive(object, active);
}

EntityId ScriptHost::player() const
{
    return m_game.firstPlayer();
}

// --- require() -----------------------------------------------------------------------------------------

void ScriptHost::require(lua_State* L, std::string_view name)
{
    // Which script: "util", "scripts/util" or "scripts/util.lua".
    const AssetDatabase* assets = m_game.assets();
    std::vector<const AssetRecord*> matches;
    if (assets != nullptr) {
        for (const AssetRecord* a : assets->all(AssetType::Script)) {
            const std::string_view path = a->path;
            const std::string_view withoutExt = path.substr(0, path.size() - 4); // ".lua"
            const std::size_t slash = withoutExt.rfind('/');
            const std::string_view stem =
                slash == std::string_view::npos ? withoutExt : withoutExt.substr(slash + 1);
            if (path == name || withoutExt == name || stem == name) {
                matches.push_back(a);
            }
        }
    }
    if (matches.empty()) {
        luaL_error(L, "require: there is no script called \"%s\" in the project", std::string(name).c_str());
    }
    if (matches.size() > 1) {
        luaL_error(L, "require: several scripts are called \"%s\"; write the path, e.g. \"%s\"",
                   std::string(name).c_str(), matches.front()->path.c_str());
    }
    const AssetRecord& record = *matches.front();
    if (const auto loaded = m_modules.find(record.path); loaded != m_modules.end()) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, loaded->second);
        return;
    }
    if (m_loadingModules.count(record.path) != 0) {
        luaL_error(L, "require: \"%s\" requires itself (through other scripts)", record.path.c_str());
    }
    const auto code = assets->readText(record.id);
    if (!code) {
        luaL_error(L, "require: could not read %s", record.path.c_str());
    }
    const std::string chunkName = "@" + record.path;
    if (luaL_loadbufferx(L, code->data(), code->size(), chunkName.c_str(), "t") != LUA_OK) {
        lua_error(L); // the syntax error message is on the stack
    }
    // Like a script file, a module runs in its own environment. What it
    // returns is the module; if it returns nothing, its environment is (so a
    // file of plain functions works too).
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_setupvalue(L, -3, 1);
    lua_insert(L, -2); // environment, chunk
    m_loadingModules[record.path] = true;
    const std::string previousFile = m_currentFile;
    m_currentFile = record.path;
    struct Restore {
        ScriptHost& host;
        std::string path;
        std::string file;
        ~Restore()
        {
            host.m_loadingModules.erase(path);
            host.m_currentFile = file;
        }
    } restore{*this, record.path,
              previousFile}; // also when the module fails (Lua errors unwind as C++ exceptions)
    lua_call(L, 0, 1);       // errors go up to the script that required it
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
    } else {
        lua_remove(L, -2); // keep the returned value, drop the environment
    }
    lua_pushvalue(L, -1);
    m_modules[record.path] = luaL_ref(L, LUA_REGISTRYINDEX);
}

// --- Script, the component ------------------------------------------------------------------------------

Script::Script(const Script& other) : BehaviourBase<Script>(other), script(other.script) {}

Script& Script::operator=(const Script& other)
{
    if (this != &other) {
        BehaviourBase<Script>::operator=(other);
        script = other.script;
    }
    return *this;
}

Script::~Script()
{
    if (const auto host = m_host.lock()) {
        host->release(m_self);
    }
}

void Script::start(Game& game)
{
    if (script.isNull()) {
        throw std::runtime_error("no script file is chosen; pick a .lua file in its Script component");
    }
    const std::shared_ptr<ScriptHost> host = game.scriptHost();
    m_host = host;
    if (auto error = host->start(*this, entity())) {
        throw std::runtime_error(*error);
    }
}

void Script::update(Game& game, float dt)
{
    const std::shared_ptr<ScriptHost> host = game.scriptHost();
    if (auto error = host->update(*this, entity(), dt)) {
        throw std::runtime_error(*error);
    }
}

void Script::collide(Game& game, EntityId other)
{
    const std::shared_ptr<ScriptHost> host = game.scriptHost();
    if (auto error = host->collide(*this, entity(), other)) {
        throw std::runtime_error(*error);
    }
}

const ComponentType& Script::componentType()
{
    static const ComponentType type =
        ComponentTypeBuilder<Script>("rolo.Script", "Script")
            .category("Gameplay")
            .description(
                "Runs a Lua script: its start(self) once, then update(self, dt) every frame. Scripts need "
                "no compiler; edit the .lua file and press Play again. See the scripting guide (Help).")
            .property("script", "Script", &Script::script,
                      {.help = "The .lua file to run. Make one with New Script in the Assets panel.",
                       .assetType = "script"})
            .build();
    return type;
}

std::optional<std::string> checkScriptSyntax(std::string_view code, std::string_view chunkName)
{
    // A bare state is enough to compile (not run) the code.
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return std::string("could not start Lua to check the script");
    }
    const std::string name = "@" + std::string(chunkName);
    std::optional<std::string> error;
    if (luaL_loadbufferx(L, code.data(), code.size(), name.c_str(), "t") != LUA_OK) {
        error = lua_tostring(L, -1);
    }
    lua_close(L);
    return error;
}

std::vector<FileIssue> checkScripts(const AssetDatabase& assets)
{
    std::vector<FileIssue> issues;
    for (const AssetRecord* script : assets.all(AssetType::Script)) {
        const auto code = assets.readText(script->id);
        if (!code) {
            issues.push_back(
                {FileIssue::Severity::Error, script->path, "The script file could not be read."});
        } else if (auto error = checkScriptSyntax(*code, script->path)) {
            issues.push_back({FileIssue::Severity::Error, script->path, *error});
        }
    }
    return issues;
}

std::string newScriptTemplate(std::string_view name)
{
    std::string text;
    text += "-- " + std::string(name) + ".lua\n";
    text += "-- Put this script on an object with a Script component.\n";
    text += "-- self.object is that object; see Help > Scripting Guide for everything a script can use.\n";
    text += "\n";
    text += "-- Runs once, when the game starts (or when the object first appears).\n";
    text += "function start(self)\n";
    text += "    print(\"Hello from \" .. self.object:name())\n";
    text += "end\n";
    text += "\n";
    text += "-- Runs every frame. dt is the time since the last frame, in seconds.\n";
    text += "function update(self, dt)\n";
    text += "end\n";
    return text;
}

} // namespace rolo
