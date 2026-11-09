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

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

class AssetDatabase;
class ScriptHost;

struct Script : BehaviourBase<Script> {
    AssetId script; // a .lua script asset

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
