#include "ScriptHost.hpp"

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Component.hpp"
#include "rolo/scene/Values.hpp"

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

// An error message with the lines of Luau's traceback that are inside
// scripts ("called from: scripts/coin.lua:12 function update").
std::string withScriptLines(const char* message, const char* traceback)
{
    std::istringstream lines(traceback != nullptr ? traceback : "");
    std::string result = message != nullptr ? message : "unknown error";
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
    return result;
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
    const std::string result = withScriptLines(message, lua_tostring(L, -1));
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

// Raises the "ran for too long" error in the script that is running.
[[noreturn]] void raiseTimeLimit(lua_State* L, double limit)
{
    // Level 0: the script's own function is running here (no C function
    // in between), so this names its file and line.
    luaL_where(L, 0);
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

#if ROLO_SCRIPT_WATCHDOG_THREAD
// Called by Luau at safe points once the watchdog asked it to (see
// ScriptHost::watch): stops a call that has run for too long.
void stopLongCall(lua_State* L, int gc)
{
    if (gc >= 0) {
        return; // inside the garbage collector: an error cannot be raised here
    }
    lua_callbacks(L)->interrupt = nullptr;
    ScriptHost& host = ScriptHost::of(L);
    if (host.callTimedOut()) {
        raiseTimeLimit(L, host.timeLimit());
    }
}
#else
// Without threads: Luau calls this at every safe point (each turn of a loop,
// each call). Looking at the clock that often would be slow, so it counts,
// and looks every few thousand times.
void stopLongCall(lua_State* L, int gc)
{
    if (gc >= 0) {
        return; // inside the garbage collector: an error cannot be raised here
    }
    ScriptHost& host = ScriptHost::of(L);
    if (host.callRanTooLong()) {
        raiseTimeLimit(L, host.timeLimit());
    }
}
#endif

// Counts the memory scripts ask for (garbage included), for measuring.
void countAllocation(lua_State* L, void*, std::size_t, std::size_t nsize, std::uint8_t, int, int)
{
    ScriptHost::of(L).countAllocated(nsize);
}

} // namespace

const std::string ScriptHost::kNoFile;

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
    // The names of the functions Rolo calls, as Luau strings made once.
    const char* const hookNames[] = {"start", "update", "collide"};
    for (std::size_t i = 0; i < m_hookNames.size(); ++i) {
        lua_pushstring(m_L, hookNames[i]);
        m_hookNames[i] = lua_ref(m_L, -1);
        lua_pop(m_L, 1);
    }
#if ROLO_SCRIPT_WATCHDOG_THREAD
    m_watchdog = std::thread([this] { watch(); });
#else
    lua_callbacks(m_L)->interrupt = stopLongCall;
#endif
}

ScriptHost::~ScriptHost()
{
#if ROLO_SCRIPT_WATCHDOG_THREAD
    {
        const std::lock_guard<std::mutex> lock(m_watchMutex);
        m_stopWatching = true;
    }
    m_watchWake.notify_all();
    m_watchdog.join();
#endif
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

#if ROLO_SCRIPT_WATCHDOG_THREAD
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
#else
bool ScriptHost::callRanTooLong()
{
    constexpr int kChecksPerLook = 4096;
    if (++m_interrupts < kChecksPerLook) {
        return false;
    }
    m_interrupts = 0;
    return std::chrono::steady_clock::now() - m_callStarted > std::chrono::duration<double>(m_timeLimit);
}
#endif

bool ScriptHost::enterCall()
{
    const bool outermost = !m_inCall.load(std::memory_order_relaxed);
    if (outermost) {
        m_timedOut.store(false, std::memory_order_relaxed);
        m_callNumber.fetch_add(1, std::memory_order_relaxed);
        m_inCall.store(true, std::memory_order_relaxed);
#if !ROLO_SCRIPT_WATCHDOG_THREAD
        m_interrupts = 0;
        m_callStarted = std::chrono::steady_clock::now();
#endif
    }
    return outermost;
}

void ScriptHost::leaveCall(bool outermost)
{
    if (outermost) {
        m_inCall.store(false, std::memory_order_relaxed);
#if ROLO_SCRIPT_WATCHDOG_THREAD
        lua_callbacks(m_L)->interrupt = nullptr;
#endif
    }
}

std::string ScriptHost::errorFrom(lua_State* L, int status)
{
    std::string error;
    if (status == LUA_ERRMEM) {
        error = "the scripts used more than " + std::to_string(kMemoryLimit / (1024u * 1024u)) +
                " MB of memory, the most a game's scripts may use";
    } else {
        const char* message = lua_tostring(L, -1);
        error = message != nullptr ? message : "unknown error";
    }
    lua_pop(L, 1);
    return error;
}

std::optional<std::string> ScriptHost::call(int arguments, const std::string& file)
{
    // Stack: function, arguments. Put the message handler below the function.
    // (Made once and reused: in Luau, pushing a C function makes a new
    // object, which would be garbage after every call.)
    const int handler = lua_gettop(m_L) - arguments;
    lua_getref(m_L, m_traceback);
    lua_insert(m_L, handler);

    const std::string* const previousFile = m_currentFile;   // (a pointer: no text copied per call)
    m_currentFile = &file;
    const bool outermost = enterCall();
    const int status = lua_pcall(m_L, arguments, 0, handler);
    leaveCall(outermost);
    m_currentFile = previousFile;

    std::optional<std::string> error;
    if (status != LUA_OK) {
        error = errorFrom(m_L, status);
    }
    lua_remove(m_L, handler);
    return error;
}

// --- Tasks: code that waits (task.wait, task.spawn, task.delay) -------------------------------------------
//
// A script's start(self), and any function given to task.spawn or
// task.delay, runs as a Luau coroutine: code that can stop in the middle
// (at task.wait) and be let go on later. Each belongs to the object whose
// script made it, and goes on, when its time has come, just before that
// object's update. So a waiting task costs nothing while it waits, stops
// with its object, and never runs for an object that is switched off.

std::optional<std::string> ScriptHost::resumeTask(Script& script, int thread)
{
    const auto find = [&script, thread] {
        return std::find_if(script.m_tasks.begin(), script.m_tasks.end(),
                            [thread](const Script::Task& t) { return t.thread == thread; });
    };
    auto found = find();
    if (found == script.m_tasks.end()) {
        return std::nullopt;
    }
    const Script::Task task = *found;   // (a copy: the list may grow while the task runs)
    const File& file = fileFor(script.script);
    const double now = m_game.time();

    lua_getref(m_L, thread);
    lua_State* const T = lua_tothread(m_L, -1);
    lua_pop(m_L, 1);   // (the registry still holds it)
    int arguments = task.arguments;
    if (task.started) {
        lua_pushnumber(T, now - task.since);   // what task.wait gives back: how long it waited
        arguments = 1;
    }

    const std::string* const previousFile = m_currentFile;
    Script* const previousScript = m_currentScript;
    lua_State* const previousTask = m_runningTask;
    const double previousWake = m_pendingWake;
    m_currentFile = &file.path;
    m_currentScript = &script;
    m_runningTask = T;
    m_pendingWake = -1.0;
    const bool outermost = enterCall();
    const int status = lua_resume(T, m_L, arguments);
    leaveCall(outermost);
    const double wake = m_pendingWake;
    m_currentFile = previousFile;
    m_currentScript = previousScript;
    m_runningTask = previousTask;
    m_pendingWake = previousWake;

    found = find();   // (again: tasks begun meanwhile may have moved it)
    if (status == LUA_YIELD) {
        lua_settop(T, 0);
        if (found != script.m_tasks.end()) {
            found->started = true;
            found->arguments = 0;
            found->since = now;
            found->wake = wake >= 0.0 ? wake : now;   // (a plain coroutine.yield: goes on next frame)
        }
        return std::nullopt;
    }
    std::optional<std::string> error;
    if (status != LUA_OK) {
        if (status == LUA_ERRMEM) {
            error = errorFrom(T, status);
        } else {
            // The message, and where in the script it happened (as for
            // calls). The task's own stack still shows where it stopped.
            luaL_traceback(m_L, T, nullptr, 0);
            error = withScriptLines(lua_tostring(T, -1), lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
            lua_pop(T, 1);
        }
    }
    if (found != script.m_tasks.end()) {
        script.m_tasks.erase(found);
    }
    lua_unref(m_L, thread);
    return error;
}

std::optional<std::string> ScriptHost::runTasks(Script& script)
{
    const double now = m_game.time();
    for (std::size_t i = 0; i < script.m_tasks.size();) {
        const Script::Task& task = script.m_tasks[i];
        // Its time has come, and not in the frame it began to wait in
        // (task.wait() with nothing means "until the next frame").
        if (task.wake <= now && task.since < now) {
            const int thread = task.thread;
            if (auto error = resumeTask(script, thread)) {
                return error;
            }
            if (i < script.m_tasks.size() && script.m_tasks[i].thread == thread) {
                ++i;   // it waits again; else it ended and the next took its place
            }
        } else {
            ++i;
        }
    }
    return std::nullopt;
}

int ScriptHost::taskWait(lua_State* L, double seconds)
{
    if (L != m_runningTask || m_runningTask == nullptr) {
        luaL_error(L, "task.wait can only be used in start(self), or in a function begun with task.spawn or "
                      "task.delay. update and collide must finish within their frame: count the time there "
                      "(self.timer = self.timer - dt), or begin a task");
    }
    m_pendingWake = m_game.time() + std::max(0.0, seconds);
    return lua_yield(L, 0);
}

void ScriptHost::taskStart(lua_State* L, int first, double delaySeconds)
{
    luaL_checktype(L, first, LUA_TFUNCTION);
    Script* const script = m_currentScript;
    if (script == nullptr) {
        luaL_error(L, "tasks belong to an object: begin them in start, update or collide, not at the top of the file");
    }
    if (script->m_tasks.size() >= kMostTasks) {
        luaL_error(L, "this object's script has %d tasks waiting, the most it may have. Is one begun every frame?",
                   static_cast<int>(kMostTasks));
    }
    const int arguments = lua_gettop(L) - first;
    lua_State* const T = lua_newthread(L);
    const int thread = lua_ref(L, -1);
    lua_pop(L, 1);
    lua_xmove(L, T, arguments + 1);   // the function and its arguments
    const double now = m_game.time();
    Script::Task task;
    task.thread = thread;
    task.arguments = arguments;
    task.since = now;
    task.wake = now + std::max(0.0, delaySeconds);
    script->m_tasks.push_back(task);
    if (delaySeconds <= 0.0) {
        // task.spawn: it begins at once, and runs to its first wait.
        if (const auto error = resumeTask(*script, thread)) {
            luaL_error(L, "%s", error->c_str());
        }
    }
}

ScriptHost::File& ScriptHost::fileFor(AssetId script)
{
    // Objects that share a script come one after another in most scenes:
    // remembering the last file saves looking it up for each of them.
    if (m_lastFile != nullptr && m_lastScript == script) {
        return *m_lastFile;
    }
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
    m_lastScript = script;
    m_lastFile = &found->second;   // (map entries stay where they are)
    return found->second;
}

void ScriptHost::ensureSelf(Script& script, EntityId object, const File& file)
{
    if (script.m_self != kNoRef) {
        return;
    }
    lua_newtable(m_L);
    // The script's properties (ScriptProperties.hpp): self starts with what
    // the script's `properties` table says...
    const int self = lua_gettop(m_L);
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, file.environment);
    lua_pushstring(m_L, "properties");
    lua_rawget(m_L, -2);
    if (lua_istable(m_L, -1)) {
        lua_pushnil(m_L);
        while (lua_next(m_L, -2) != 0) {
            // stack: ..., properties, name, value
            const int kind = lua_type(m_L, -1);
            if (lua_type(m_L, -2) == LUA_TSTRING && (kind == LUA_TNUMBER || kind == LUA_TSTRING || kind == LUA_TBOOLEAN)) {
                lua_pushvalue(m_L, -2);
                lua_pushvalue(m_L, -2);
                lua_rawset(m_L, self);
            }
            lua_pop(m_L, 1);   // the value; the name stays for lua_next
        }
        // ...and then with this object's own values from the Inspector,
        // where it has any of the same kind (a script that changed a
        // property from a number to a text wins over an old number).
        if (!script.values.empty()) {
            for (const auto& [name, value] : parseDataValues(script.values)) {
                lua_pushlstring(m_L, name.data(), name.size());
                lua_rawget(m_L, self);
                const int declared = lua_type(m_L, -1);
                lua_pop(m_L, 1);
                lua_pushlstring(m_L, name.data(), name.size());
                if (const bool* flag = std::get_if<bool>(&value); flag != nullptr && declared == LUA_TBOOLEAN) {
                    lua_pushboolean(m_L, *flag ? 1 : 0);
                } else if (const double* number = std::get_if<double>(&value); number != nullptr && declared == LUA_TNUMBER) {
                    lua_pushnumber(m_L, *number);
                } else if (const std::string* text = std::get_if<std::string>(&value); text != nullptr && declared == LUA_TSTRING) {
                    lua_pushlstring(m_L, text->data(), text->size());
                } else {
                    lua_pop(m_L, 1);   // the name: not a property of this script (any more)
                    continue;
                }
                lua_rawset(m_L, self);
            }
        }
    }
    lua_pop(m_L, 2);   // properties (or nil), the environment
    pushObjectForScripts(m_L, object);
    lua_setfield(m_L, -2, "object");
    script.m_self = lua_ref(m_L, -1);
    lua_pop(m_L, 1);
}

std::optional<std::string> ScriptHost::call(Script& script, EntityId object, Hook hook, std::optional<float> dt,
                                            EntityId other)
{
    // This runs for every script of every object, every frame: it avoids
    // anything that costs per call (looking the file up, hashing the
    // function's name, copying text).
    File& file = fileFor(script.script);
    if (file.environment == kNoRef) {
        return file.error;
    }
    ensureSelf(script, object, file);
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, file.environment);
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_hookNames[static_cast<std::size_t>(hook)]);   // "update", already a Luau string
    lua_rawget(m_L, -2);   // the file's own function of that name
    lua_remove(m_L, -2);   // the environment
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
    Script* const previousScript = m_currentScript;
    m_currentScript = &script;
    auto error = call(arguments, file.path);
    m_currentScript = previousScript;
    return error;
}

std::optional<std::string> ScriptHost::start(Script& script, EntityId object)
{
    // start(self) runs as a task, so that it may wait (task.wait): a script
    // can be written from top to bottom, "wait two seconds, then open the
    // door", as people who come from Roblox are used to.
    File& file = fileFor(script.script);
    if (file.environment == kNoRef) {
        return file.error;
    }
    ensureSelf(script, object, file);
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, file.environment);
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, m_hookNames[static_cast<std::size_t>(Hook::Start)]);
    lua_rawget(m_L, -2);
    lua_remove(m_L, -2);   // the environment
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 1);
        return std::nullopt;   // the script has no start: nothing to do
    }
    lua_rawgeti(m_L, LUA_REGISTRYINDEX, script.m_self);
    lua_State* const T = lua_newthread(m_L);
    const int thread = lua_ref(m_L, -1);
    lua_pop(m_L, 1);
    lua_xmove(m_L, T, 2);   // start, self
    Script::Task task;
    task.thread = thread;
    task.arguments = 1;
    task.since = task.wake = m_game.time();
    script.m_tasks.push_back(task);
    return resumeTask(script, thread);
}

std::optional<std::string> ScriptHost::update(Script& script, EntityId object, float dt)
{
    // What was waiting goes on first, then this frame's update.
    if (!script.m_tasks.empty()) {
        if (auto error = runTasks(script)) {
            return "in a task (code that waited): " + *error;
        }
    }
    return call(script, object, Hook::Update, dt);
}

std::optional<std::string> ScriptHost::collide(Script& script, EntityId object, EntityId other)
{
    return call(script, object, Hook::Collide, std::nullopt, other);
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
    const std::string* const previousFile = m_currentFile;
    m_currentFile = &record.path;
    struct Restore {
        ScriptHost& host;
        std::string path;
        const std::string* file;
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

Script::Script(const Script& other) : BehaviourBase<Script>(other), script(other.script), values(other.values) {}

Script& Script::operator=(const Script& other)
{
    if (this != &other) {
        BehaviourBase<Script>::operator=(other);
        script = other.script;
        values = other.values;
    }
    return *this;
}

Script::~Script()
{
    if (const auto host = m_host.lock()) {
        host->release(m_self);
        for (const Task& task : m_tasks) {
            host->release(task.thread);   // what was waiting never goes on
        }
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
            .property("values", "Properties", &Script::values,
                      {.help = "This object's own values for the script's properties (the script declares "
                               "them: properties = { speed = 90 }). The Inspector shows a field for each.",
                       .data = true})
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
    text += "-- Settings shown in the Inspector, under this script's object. Each object can have\n";
    text += "-- its own; the script reads them as self.speed and so on.\n";
    text += "properties = {\n";
    text += "    speed = 100,\n";
    text += "}\n";
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
