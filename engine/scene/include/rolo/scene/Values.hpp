#pragma once

// A small set of named values, kept as text.
//
// Some components carry values whose names Rolo cannot know in advance: a
// Script's properties are declared by the script (docs/scripting.md), not by
// Rolo. They are kept in one text property of the component as compact JSON
// ({"speed":120,"message":"Hi"}), which makes them an ordinary property to
// everything else (undo, copy and paste, files). These functions read and
// write that text. In scene files it is written as JSON itself, not as text
// (PropertyOptions::data).

#include <map>
#include <string>
#include <string_view>
#include <variant>

namespace rolo {

/// One value: true/false, a number, or text.
using DataValue = std::variant<bool, double, std::string>;
/// Values by name (in the order of their names).
using DataValues = std::map<std::string, DataValue>;

/// The values in `text`. Text that is empty or not such a set gives none;
/// values of other kinds (lists, nothing) are left out.
DataValues parseDataValues(std::string_view text);
/// The values as text: "" for none, else compact JSON with the names in order.
std::string writeDataValues(const DataValues& values);

} // namespace rolo
