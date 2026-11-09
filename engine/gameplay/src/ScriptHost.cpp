#include "ScriptHost.hpp"

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Component.hpp"

// Luau is compiled as C++ (third_party/luau/CMakeLists.txt), so its errors
// are C++ exceptions and its headers are included directly.
#include "lua.h"
#include "luacode.h"
#include "lualib.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace rolo {

namespace {

constexpr int kNoRef = -2; // LUA_NOREF

// Compiles a script file to Luau bytecode and loads it as a function in
// environment `env` (a stack index), pushed on the stack. False, with the
// error message pushed instead, if the code has a mistake.
bool loadScript(lua_State* L, std::string_view code, const std::string& path, int env)
{
    // Optimisation level 1 (Luau's default): fast, and every line still
    // reports its own number in errors. "@path" names the file in messages:
    // "scripts/coin.lua:12: ...".
    lua_CompileOptions options{};
    options.optimizationLevel = 1;
    options.debugLevel = 1;
    std::size_t size = 0;
    char* bytecode = luau_compile(code.data(), code.size(), &options, &size);
    if (bytecode == nullptr) {
        lua_pushstring(L, "could not compile the script: out of memory");
        return false;
    }
    const std::string chunkName = "@" + path;
    const int status = luau_load(L, chunkName.c_str(), bytecode, size, env);
    std::free(bytecode);
    return status == 0;
}

// A new environment for a script file: its own globals, falling through to
// the shared (read-only) ones. Marked "safe", so Luau's fast paths for the
// built-in libraries (math.sin and so on) stay on.
void pushFileEnvironment(lua_State* L)
{
    lua_newtable(L);
    lua_newtable(L);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    lua_setfield(L, -2, "__index");
    lua_setreadonly(L, -1, 1);
    lua_setmetatable(L, -2);
    lua_setsafeenv(L, -1, 1);
}

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

// Opens the safe parts of Luau's standard library. Runs inside lua_pcall, so
// running out of memory here is an error, not a crash.
int openLibraries(lua_State* L)
{
    // Not opened: os and debug (and Luau has no io or package at all).
    const luaL_Reg libraries[] = {
        {"", luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_BITLIBNAME, luaopen_bit32},
        {LUA_BUFFERLIBNAME, luaopen_buffer},
        {LUA_VECLIBNAME, luaopen_vector},
    };
    for (const luaL_Reg& library : libraries) {
        lua_pushcfunction(L, library.func, nullptr);
        lua_pushstring(L, library.name);
        lua_call(L, 1, 0);
    }
    // loadstring compiles code at run time outside the file's environment,
    // and getfenv/setfenv reach other functions' environments (and switch off
    // Luau's fast paths).
    for (const char* unsafe : {"loadstring", "getfenv", "setfenv"}) {
        lua_pushnil(L);
        lua_setglobal(L, unsafe);
    }
    openScriptApi(ScriptHost::of(L));
    // The shared globals and libraries become read-only: a script cannot
    // change math or game for the others. (A file's own globals live in its
    // environment.)
    luaL_sandbox(L);
    return 0;
}

// Called by Luau at safe points once the watchdog asked it to (see
// ScriptHost::Watchdog): stops a call that has run for too long.
void stopLongCall(lua_State* L, int gc)
{
    if (gc >= 0) {
        return; // inside the garbage collector: an error cannot be raised here
    }
    lua_callbacks(L)->interrupt = nullptr;
    ScriptHost& host = ScriptHost::of(L);
    if (host.callTimedOut()) {
        // Level 0: the script's own function is running here (no C function
        // in between), so this names its file and line.
        luaL_where(L, 0);
        const double limit = host.timeLimit();
        if (limit >= 1.0) {
            const int seconds = static_cast<int>(limit);
            lua_pushfstring(L, "the script ran for more than %d second%s without finishing (is there a loop that "
                               "never ends?)",
                            seconds, seconds == 1 ? "" : "s");
        } else {
            lua_pushstring(L, "the script ran for too long without finishing (is there a loop that never ends?)");
        }
        lua_concat(L, 2);
        lua_error(L);
    }
}

// Counts the memory scripts ask for (garbage included), for measuring.
void countAllocation(lua_State* L, void*, std::size_t, std::size_t nsize, std::uint8_t, int, int)
{
    ScriptHost::of(L).countAllocated(nsize);
}

} // namespace

ScriptHost::ScriptHost(Game& game, const Logger& log) : m_game(game), m_log(log)
{
    m_L = lua_newstate(&ScriptHost::allocate, this);
    if (m_L == nullptr) {
        throw std::runtime_error("could not start Luau: out of memory");
    }
    // Luau keeps one pointer for its host program: ours points back at this
    // ScriptHost, so the C++ functions scripts call can find it.
    m_timeLimit = std::max(0.01, game.m_setup.scriptTimeLimit);
    lua_callbacks(m_L)->userdata = this;
    lua_callbacks(m_L)->onallocate = countAllocation;
    lua_pushcfunction(m_L, openLibraries, "openLibraries");
    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
        const std::string error = lua_tostring(m_L, -1) != nullptr ? lua_tostring(m_L, -1) : "unknown error";
        lua_close(m_L);
        throw std::runtime_error("could not start Luau: " + error);
    }
    lua_pushcfunction(m_L, addTraceback, "addTraceback");
    m_traceback = lua_ref(m_L, -1);
    lua_pop(m_L, 1);
    m_watchdog = std::thread([this] { watch(); });
}

ScriptHost::~ScriptHost()
{
    {
        const std::lock_guard<std::mutex> lock(m_watchMutex);
        m_stopWatching = true;
    }
    m_watchWake.notify_all();
    m_watchdog.join();
    lua_close(m_L);
}

ScriptHost& ScriptHost::of(lua_State* L)
{
    return *static_cast<ScriptHost*>(lua_callbacks(L)->userdata);
}

// --- Memory and time limits ---------------------------------------------------------------------------

void* ScriptHost::allocate(void* ud, void* block, std::size_t oldSize, std::size_t newSize) noexcept
{
    auto* host = static_cast<ScriptHost*>(ud);
    // Luau asks here for its own blocks (pages of small objects, and big
    // objects); this keeps the total under the limit.
    const std::size_t previous = block != nullptr ? oldSize : 0;
    if (newSize == 0) {
        std::free(block);
        host->m_memoryUsed -= previous;
        return nullptr;
    }
    if (newSize > previous && host->m_memoryUsed - previous + newSize > kMemoryLimit) {
        return nullptr; // Luau turns this into a "not enough memory" error
    }
    void* grown = std::realloc(block, newSize);
    if (grown != nullptr) {
        host->m_memoryUsed = host->m_memoryUsed - previous + newSize;
    }
    return grown;
}

// The watchdog: a thread that looks every quarter of a second whether one
// script call has been running for longer than the limit. Then it asks Luau
// to call stopLongCall at its next safe point. Scripts that finish in time
// cost nothing: no counting while they run, only two numbers stored per call.
void ScriptHost::watch()
{
    std::uint64_t seenCall = 0;
    auto seenSince = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(m_watchMutex);
    while (!m_stopWatching) {
        m_watchWake.wait_for(lock, std::chrono::duration<double>(std::min(0.25, m_timeLimit / 4.0)));
        const std::uint64_t call = m_callNumber.load(std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        if (!m_inCall.load(std::memory_order_relaxed) || call != seenCall) {
            seenCall = call;
            seenSince = now;
            continue;
        }
        if (now - seenSince > std::chrono::duration<double>(m_timeLimit)) {
            m_timedOut.store(true, std::memory_order_relaxed);
            lua_callbacks(m_L)->interrupt = stopLongCall;   // Luau allows setting this from another thread
        }
    }
}

// --- Calling scripts ------------------------------------------------------------------------------------

std::optional<std::string> ScriptHost::call(int arguments, const std::string& file)
{
    // Stack: function, arguments. Put the message handler below the function.
    // (Made once and reused: in Luau, pushing a C function makes a new
    // object, which would be garbage after every call.)
    const int handler = lua_gettop(m_L) - arguments;
    lua_getref(m_L, m_traceback);
    lua_insert(m_L, handler);

    const std::string previousFile = m_currentFile;
    m_currentFile = file;
    const bool outermost = !m_inCall.load(std::memory_order_relaxed);
    if (outermost) {
        m_timedOut.store(false, std::memory_order_relaxed);
        m_callNumber.fetch_add(1, std::memory_order_relaxed);
        m_inCall.store(true, std::memory_order_relaxed);
    }
    const int status = lua_pcall(m_L, arguments, 0, handler);
    if (outermost) {
        m_inCall.store(false, std::memory_order_relaxed);
        lua_callbacks(m_L)->interrupt = nullptr;
    }
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
            // The file's own environment: reads fall through to the shared
            // globals (__index), writes stay in the file's table.
            pushFileEnvironment(m_L);
            if (!loadScript(m_L, *code, record->path, -1)) {
                file.error = lua_tostring(m_L, -1);
                lua_pop(m_L, 2); // the message and the environment
            } else {
                // stack: environment, chunk
                if (auto failed = call(0, file.path)) {
                    file.error = *failed;
                    lua_pop(m_L, 1);
                } else {
                    file.environment = lua_ref(m_L, -1);
                    lua_pop(m_L, 1);
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
    script.m_self = lua_ref(m_L, -1);
    lua_pop(m_L, 1);
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
        lua_unref(m_L, self);
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
            const std::size_t dot = path.rfind('.');
            const std::string_view withoutExt = path.substr(0, dot); // without ".lua" or ".luau"
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
    // Like a script file, a module runs in its own environment. What it
    // returns is the module; if it returns nothing, its environment is (so a
    // file of plain functions works too).
    pushFileEnvironment(L);
    if (!loadScript(L, *code, record.path, -1)) {
        lua_error(L); // the syntax error message is on the stack
    }
    // stack: environment, chunk
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
    m_modules[record.path] = lua_ref(L, -1);
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
                "Runs a Luau script: its start(self) once, then update(self, dt) every frame. Scripts need "
                "no compiler; edit the .lua or .luau file and press Play again. See the scripting guide (Help).")
            .property("script", "Script", &Script::script,
                      {.help = "The .lua file to run. Make one with New Script in the Assets panel.",
                       .assetType = "script"})
            .build();
    return type;
}

std::optional<std::string> checkScriptSyntax(std::string_view code, std::string_view chunkName)
{
    // A bare state is enough to compile and load (not run) the code; loading
    // turns a compile error into the same message a game would show.
    lua_State* L = luaL_newstate();
    if (L == nullptr) {
        return std::string("could not start Luau to check the script");
    }
    std::optional<std::string> error;
    if (!loadScript(L, code, std::string(chunkName), 0)) {
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
