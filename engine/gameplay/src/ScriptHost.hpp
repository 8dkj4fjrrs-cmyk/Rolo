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
//     GameSetup::scriptTimeLimit seconds is stopped with an error. A watchdog thread notices;
//     scripts pay nothing for it while they run;
//   * all scripts together may use at most kMemoryLimit bytes.

#include "rolo/core/Log.hpp"
#include "rolo/scene/EntityId.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <thread>
#include <optional>
#include <string>
#include <string_view>

struct lua_State;
struct lua_Debug;

namespace rolo {

class Game;
struct Script;

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
    /// How long one call may run, in seconds (GameSetup::scriptTimeLimit).
    double timeLimit() const noexcept { return m_timeLimit; }

    // --- Used by the Lua bindings (ScriptApi.cpp) -------------------------------------------------
    /// The ScriptHost that owns a Lua state.
    static ScriptHost& of(lua_State* L);
    /// Loads a project script for require(): pushes its result. Raises a Lua
    /// error if it cannot.
    void require(lua_State* L, std::string_view name);
    /// The project path of the file whose code is running (for print()).
    const std::string& currentFile() const noexcept { return m_currentFile; }
    /// Game functions that are not public (the host is the Game's friend).
    void setActive(EntityId object, bool active);
    EntityId player() const;

  private:
    struct File {
        std::string path;     // project-relative, e.g. "scripts/coin.lua"
        int environment = -2; // registry reference of its environment table (-2: failed)
        std::string error;    // why it could not be loaded
    };

    // The file's environment, loaded on first use. Pushes it and returns
    // true; or pushes nothing and sets `error`.
    bool pushEnvironment(AssetId script, std::string& error);
    // Calls the function on the stack below `arguments` arguments with the
    // instruction limit and an error handler. Nothing, or the error.
    std::optional<std::string> call(int arguments, const std::string& file);
    // Calls the script's function `name` (start, update or collide) with
    // self (and dt, or the other object); nothing to do if the script does
    // not define it.
    std::optional<std::string> call(Script& script, EntityId object, const char* name,
                                    std::optional<float> dt, EntityId other = {});
    void ensureSelf(Script& script, EntityId object);

    static void* allocate(void* ud, void* block, std::size_t oldSize, std::size_t newSize) noexcept;
    void watch();   // the watchdog thread

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
    std::thread m_watchdog;        // started by the constructor once Luau is ready
    std::map<AssetId, File> m_files;
    std::map<std::string, int, std::less<>> m_modules;         // require() results by name (registry refs)
    std::map<std::string, bool, std::less<>> m_loadingModules; // to catch require loops
    std::string m_currentFile;

    friend void openScriptApi(ScriptHost& host);
};

/// Adds game, input, print, require and the Object type to the host's state (ScriptApi.cpp).
void openScriptApi(ScriptHost& host);
/// Pushes an object handle (nil for a null ID) (ScriptApi.cpp).
void pushObjectForScripts(lua_State* L, EntityId id);

} // namespace rolo
