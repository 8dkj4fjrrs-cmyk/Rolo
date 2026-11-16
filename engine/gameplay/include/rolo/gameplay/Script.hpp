#pragma once

// Script: gameplay code written in Lua, attached to an object.
//
// A Script component points at a .lua file in the project. While the game
// runs, the file's start(self) is called once and update(self, dt) every
// frame, like a C++ behaviour's start() and update(), and collide(self, other)
// when physics makes its object start touching another:
//
//     -- scripts/spinner.lua: turns its object around, 90 degrees a second.
//     function start(self)
//         self.speed = 90
//     end
//
//     function update(self, dt)
//         self.object:setRotation(self.object:rotation() + self.speed * dt)
//     end
//
// `self` is a table of the script's own, one per object, with `self.object`
// (the object the script is on). The functions a script can call (game.*,
// input.*, and the methods of objects) are listed in docs/scripting.md.
//
// Unlike C++ behaviours, scripts need no compiler and no rebuild of Rolo:
// a downloaded editor runs them, and pressing Play again picks up edits.
//
// All scripts of a running game share one Lua state (ScriptHost, owned by
// the Game). Scripts are sandboxed: they cannot open files, run programs or
// load native code. An error in a script, or a script that runs too long
// without returning (an endless loop), switches that script off and reports
// it with the file and line; the rest of the game keeps going.

#include "rolo/gameplay/Behaviour.hpp"
#include "rolo/scene/EntityId.hpp"
#include "rolo/scene/SceneFile.hpp" // FileIssue

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

class AssetDatabase;
class ScriptHost;

/// Something a script can listen to (ADR 0032): a game event by name, a
/// key, button or action going down or up, or an object beginning to touch
/// another. Scripts get one from game.event, input.pressed, input.released
/// or obj:collided. (Plain numbers, so that this header needs no others;
/// the script host knows what they mean.)
struct ScriptSignal {
    enum Kind { GameEvent, Pressed, Released, Collided };
    int kind = GameEvent;
    std::string name;            // the game event, or the action
    std::uint64_t subject = 0;   // Collided: the object that is touched
    int keys[2] = {0, 0};        // Pressed, Released: keys (Key), 0 for none
    int mouse = -1;              // ...or a mouse button (MouseButton)
    int padButton = -1;          // ...or a gamepad button (GamepadButton)
    int pad = -1;                // ...on this gamepad (-1: any)
};

struct Script : BehaviourBase<Script> {
    AssetId script; // a .lua script asset
    /// This object's own values for the script's properties, where they
    /// differ from the script's (rolo/gameplay/ScriptProperties.hpp), as
    /// text: {"speed":120}. Empty: all as the script says.
    std::string values;

    Script() = default;
    // Copies the property; a copy is a new instance with fresh Lua state.
    Script(const Script& other);
    Script& operator=(const Script& other);
    ~Script() override;

    void start(Game& game) override;
    void update(Game& game, float dt) override;
    void collide(Game& game, EntityId other) override;

    static const ComponentType& componentType();

  private:
    friend class ScriptHost;
    std::weak_ptr<ScriptHost> m_host; // the Lua state holding `self`
    int m_self = -2;                  // `self` in that state's registry (-2: none yet)
    // Code of this script that is waiting (task.wait) or yet to begin
    // (task.delay): each a Luau coroutine, kept until it has finished.
    struct Task {
        int thread = -2;        // the coroutine, in the state's registry
        double wake = 0.0;      // the game time it goes on at
        double since = 0.0;     // the game time it began to wait
        int arguments = 0;      // for one that has not begun: how many arguments wait on its stack
        bool started = false;
    };
    std::vector<Task> m_tasks;
    // What this script listens to (signal:connect, signal:once), and its
    // tasks that wait for an event (signal:wait). Looked at once a frame,
    // just before the object's update.
    struct Connection {
        std::uint64_t id = 0;   // the script host's number for it (for disconnect)
        ScriptSignal signal;
        int function = -2;      // the function to begin, in the state's registry; for `waits`: the task's thread (the task owns it)
        bool once = false;      // gone after the first time
        bool waits = false;     // a task waits here (signal:wait)
    };
    std::vector<Connection> m_connections;
};

/// Checks a script's code without running it: nothing if it is valid Lua,
/// else the error with its line, e.g. "scripts/coin.lua:3: 'end' expected".
/// `chunkName` is the file's project path, used in the message.
std::optional<std::string> checkScriptSyntax(std::string_view code, std::string_view chunkName);

/// Checks every script file in a project (without running them): a syntax
/// error is an Error, so exporting stops and says where.
std::vector<FileIssue> checkScripts(const AssetDatabase& assets);

/// A new script's starting text (the editor's New Script).
std::string newScriptTemplate(std::string_view name);

} // namespace rolo
