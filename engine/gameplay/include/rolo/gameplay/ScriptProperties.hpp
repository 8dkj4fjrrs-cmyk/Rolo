#pragma once

// Script properties: settings a script declares, shown in the Inspector and
// set per object (ADR 0029). What Roblox calls Attributes and other engines
// "exported variables".
//
// A script declares them at its top, with the values they start with:
//
//     properties = {
//         speed = 90,        -- a number
//         label = "Lift",    -- text
//         returns = true,    -- a tick box
//     }
//
// The Inspector shows a field for each under the object's Script component.
// When the game runs, every object's `self` starts with them: `self.speed`
// is what the Inspector says for that object, or 90 if nobody changed it.
//
// The editor must know the list without running the script (a script's code
// may do anything, and the editor is not a game), so it is *read* from the
// script's text here, with Luau's own parser. That is why the values must be
// written out plainly: a number, a text or true/false, not a calculation.
// (The running game uses the real table, so there a calculation works too.)

#include "rolo/scene/Values.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

struct ScriptProperty {
    std::string name;    // as written: "walkSpeed"
    DataValue value;     // what the script gives it: decides its kind, too
    int line = 0;        // where it is declared, from 1
};

struct ScriptProperties {
    std::vector<ScriptProperty> list;    // in the order they are written
    /// What could not be shown, in plain words with the line, e.g. "line 4:
    /// 'size' is a calculation; the Inspector can only show ...".
    std::vector<std::string> problems;
};

/// The properties a script declares. None if it declares none, or if its
/// code has a syntax error (that is reported by checkScriptSyntax).
ScriptProperties readScriptProperties(std::string_view code);

/// A property's name as a label: "walkSpeed" and "walk_speed" give "Walk Speed".
std::string scriptPropertyLabel(std::string_view name);

/// What an object's Script should use for `property`: its own value from
/// `values` (the Script component's text) if there is one of the right kind,
/// else the script's.
DataValue scriptPropertyValue(const ScriptProperty& property, std::string_view values);

/// `values` with `property` set to `value` for this object. Setting it to
/// what the script gives it anyway takes it out again (so a later change to
/// the script's number reaches every object that never differed).
std::string withScriptPropertyValue(std::string_view values, const ScriptProperty& property, const DataValue& value);

} // namespace rolo
