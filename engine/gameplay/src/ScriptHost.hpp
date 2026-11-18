#pragma once

// ScriptHost: the Lua state of one running game (see rolo/gameplay/Script.hpp).
//
// The Game creates it when the first script starts and drops it when the game
// stops, so every Play begins with fresh state and freshly read script files.
//
// How scripts are kept apart:
//   * Each script *file* is loaded once, into its own environment table: the
//     functions it defines (start, update, helpers) and any globals it sets
//     live there, not in the shared global table. Two files can both define
//     update() without clashing.
//   * Each *object* with a Script gets its own `self` table, kept in the Lua
//     registry; the component remembers its reference.
//   * The shared globals are Luau's safe libraries (string, table, math,
//     utf8, coroutine, bit32, buffer, vector and most of the base library)
//     plus Rolo's: game, input, audio, print and require. They are
//     read-only (luaL_sandbox). Nothing reaches files, programs or native code.
//
// Limits, so a broken script cannot hang or exhaust the game:
//   * a call (start, update, a required file) that runs for more than
//     GameSetup::scriptTimeLimit seconds is stopped with an error. A watchdog thread notices
//     (where there are no threads, on the web, Luau's interrupt checks the clock instead);
//     scripts pay nothing for it while they run;
//   * all scripts together may use at most kMemoryLimit bytes.

#include "rolo/core/Log.hpp"
#include "rolo/scene/EntityId.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <thread>

// Whether a watchdog thread can be used. The web build (WebAssembly without
// threads, see the root CMakeLists.txt) has none: there the scripts
// themselves look at the clock now and then.
// (It can be set to 0 when compiling, to test that way on a desktop.)
#ifndef ROLO_SCRIPT_WATCHDOG_THREAD
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define ROLO_SCRIPT_WATCHDOG_THREAD 0
#else
#define ROLO_SCRIPT_WATCHDOG_THREAD 1
#endif
#endif
#include <optional>
#include <string>
#include <string_view>

struct lua_State;
struct lua_Debug;

namespace rolo {

class Game;
struct Script;
struct ScriptSignal;

class ScriptHost {
  public:
    static constexpr std::size_t kMemoryLimit = 256u * 1024u * 1024u;

    ScriptHost(Game& game, const Logger& log);
    ~ScriptHost();
    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    /// Runs the script's start(self) (creating `self` first). Nothing if it
    /// worked, else the error message.
    std::optional<std::string> start(Script& script, EntityId object);
    /// Runs the script's update(self, dt).
    std::optional<std::string> update(Script& script, EntityId object, float dt);
    /// Runs the script's collide(self, other) (physics: its object started touching `other`).
    std::optional<std::string> collide(Script& script, EntityId object, EntityId other);
    /// Frees a `self` table (its object is gone).
    void release(int self) noexcept;

    Game& game() noexcept { return m_game; }
    const Logger& log() const noexcept { return m_log; }
    lua_State* state() noexcept { return m_L; }
    /// Bytes the scripts use now.
    std::size_t memoryUsed() const noexcept { return m_memoryUsed; }
    /// Bytes the scripts have asked for since they started (garbage included).
    std::uint64_t memoryAllocated() const noexcept { return m_allocated; }
    void countAllocated(std::size_t bytes) noexcept { m_allocated += bytes; }
    /// True when the watchdog found the current call running too long.
    bool callTimedOut() const noexcept { return m_timedOut.load(std::memory_order_relaxed); }
#if !ROLO_SCRIPT_WATCHDOG_THREAD
    /// Without a watchdog thread: counts this check, and every few thousand
    /// looks whether the current call has passed the time limit.
    bool callRanTooLong();
#endif
    /// How long one call may run, in seconds (GameSetup::scriptTimeLimit).
    double timeLimit() const noexcept { return m_timeLimit; }

    // --- Used by the Lua bindings (ScriptApi.cpp) -------------------------------------------------
    /// The ScriptHost that owns a Lua state.
    static ScriptHost& of(lua_State* L);
    /// Loads a project script for require(): pushes its result. Raises a Lua
    /// error if it cannot.
    void require(lua_State* L, std::string_view name);
    /// The project path of the file whose code is running (for print()).
    const std::string& currentFile() const noexcept { return *m_currentFile; }
    /// task.wait(seconds), called by the code running in `L`: stops it here
    /// until that much game time has passed. Returns what the Lua function
    /// must return. A Lua error if `L` is not a task (update and collide
    /// must finish within their frame).
    int taskWait(lua_State* L, double seconds);
    /// task.spawn and task.delay: the function at `first` on L's stack, and
    /// the arguments after it, become a task of the script that is running,
    /// begun now (delay 0) or after `delaySeconds` of game time.
    void taskStart(lua_State* L, int first, double delaySeconds);
    /// The most tasks one object's script may have waiting.
    static constexpr std::size_t kMostTasks = 1000;
    /// signal:connect(f) and signal:once(f), called by the code running in
    /// `L`: the function at `function` on its stack is begun (as a task, so
    /// it may wait) whenever the signal fires, for as long as the object of
    /// the script that is running exists. Returns the connection's number.
    std::uint64_t connect(lua_State* L, const ScriptSignal& signal, int function, bool once);
    /// signal:wait(): stops the task running in `L` until the signal fires.
    int signalWait(lua_State* L, const ScriptSignal& signal);
    /// connection:disconnect() and connection:isConnected(), by the object
    /// whose script connected and the connection's number.
    void disconnect(EntityId owner, std::uint64_t connection);
    bool isConnected(EntityId owner, std::uint64_t connection);
    /// The object whose script is running (null at the top of a file).
    EntityId currentObject() const;
    /// The most connections one object's script may have.
    static constexpr std::size_t kMostConnections = 1000;
    /// Game functions that are not public (the host is the Game's friend).
    void setActive(EntityId object, bool active);
    EntityId player() const;

  private:
    struct File {
        std::string path;     // project-relative, e.g. "scripts/coin.lua"
        int environment = -2; // registry reference of its environment table (-2: failed)
        std::string error;    // why it could not be loaded
    };

    // The script's file, loaded and run on first use (its environment is
    // -2 and `error` says why if that failed).
    File& fileFor(AssetId script);
    enum class Hook { Start, Update, Collide };
    // Calls the function on the stack below `arguments` arguments with the
    // instruction limit and an error handler. Nothing, or the error.
    std::optional<std::string> call(int arguments, const std::string& file);
    // Calls the script's function `name` (start, update or collide) with
    // self (and dt, or the other object); nothing to do if the script does
    // not define it.
    std::optional<std::string> call(Script& script, EntityId object, Hook hook,
                                    std::optional<float> dt, EntityId other = {});
    void ensureSelf(Script& script, EntityId object, const File& file);
    // The start and end of a run of script code (a call, or a task going
    // on): the time limit counts from the outermost one.
    bool enterCall();
    void leaveCall(bool outermost);
    // Lets the task of `script` whose coroutine is `thread` run until it
    // waits again or ends. Nothing, or its error.
    // `given`: how many values for it wait on its stack already (what
    // signal:wait gives back); -1: none, and a task that waited for time
    // is told how long it waited.
    std::optional<std::string> resumeTask(Script& script, int thread, int given = -1);
    // Makes the function on top of L's stack, below `arguments` arguments,
    // a task of `script` that begins after `delaySeconds` (0: now, running
    // to its first wait). Nothing, or why not (L's stack is cleared of them either way).
    std::optional<std::string> beginTask(Script& script, lua_State* L, int arguments, double delaySeconds);
    // Looks at what `script` listens to, and begins or lets go on what
    // listens to something that happened.
    std::optional<std::string> runConnections(Script& script);
    Script* scriptOf(EntityId owner);
    // Lets every task of `script` whose time has come go on.
    std::optional<std::string> runTasks(Script& script);
    // What an error status of a call or a task means, as a message (popped from `L`).
    std::string errorFrom(lua_State* L, int status);

    Script* m_currentScript = nullptr;   // whose code is running (for task.spawn)
    lua_State* m_runningTask = nullptr;  // the task that is running, if one is
    double m_pendingWake = -1.0;         // set by task.wait: when the running task goes on
    int m_runningThread = -2;            // the running task's coroutine, in the registry
    std::uint64_t m_lastConnection = 0;  // connections are numbered from 1

    static void* allocate(void* ud, void* block, std::size_t oldSize, std::size_t newSize) noexcept;
#if ROLO_SCRIPT_WATCHDOG_THREAD
    void watch();   // the watchdog thread
#endif

    Game& m_game;
    const Logger& m_log;
    lua_State* m_L = nullptr;
    std::size_t m_memoryUsed = 0;
    int m_traceback = -2;   // registry reference of the message handler of every call
    std::uint64_t m_allocated = 0;
    // The watchdog (see watch()).
    double m_timeLimit = 10.0;
    std::atomic<std::uint64_t> m_callNumber{0};   // counts outermost calls
    std::atomic<bool> m_inCall{false};
    std::atomic<bool> m_timedOut{false};
    std::mutex m_watchMutex;
    std::condition_variable m_watchWake;
    bool m_stopWatching = false;   // guarded by m_watchMutex
#if ROLO_SCRIPT_WATCHDOG_THREAD
    std::thread m_watchdog;        // started by the constructor once Luau is ready
#else
    int m_interrupts = 0;                                  // safe points since the clock was last looked at
    std::chrono::steady_clock::time_point m_callStarted;   // when the outermost call began
#endif
    std::map<AssetId, File> m_files;
    std::map<std::string, int, std::less<>> m_modules;         // require() results by name (registry refs)
    std::map<std::string, bool, std::less<>> m_loadingModules; // to catch require loops
    const std::string* m_currentFile = &kNoFile;   // the file whose code is running (its path)
    static const std::string kNoFile;
    AssetId m_lastScript;            // fileFor's memory of its last answer
    File* m_lastFile = nullptr;
    std::array<int, 3> m_hookNames{};   // registry references of "start", "update", "collide"

    friend void openScriptApi(ScriptHost& host);
};

/// Adds game, input, print, require and the Object type to the host's state (ScriptApi.cpp).
void openScriptApi(ScriptHost& host);
/// Pushes an object handle (nil for a null ID) (ScriptApi.cpp).
void pushObjectForScripts(lua_State* L, EntityId id);
/// Whether the value at `index` is an object as scripts see it (what
/// object() in a `properties` table stands for too).
bool isObjectForScripts(lua_State* L, int index);

} // namespace rolo
