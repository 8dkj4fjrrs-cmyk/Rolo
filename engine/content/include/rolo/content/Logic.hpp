#pragma once

// Conditions and actions: the small, fixed language RPG content uses to ask
// questions about the game ("has the player 3 gems?") and to change it
// ("start the quest", "give a gem").
//
// Content never contains code. Each condition and action is a JSON object
// with a "type" and the fields that type needs, for example:
//
//     { "type": "compare", "variable": "gems", "op": ">=", "value": 3 }
//     { "type": "all", "conditions": [ ... ] }
//     { "type": "set", "variable": "met_elder", "value": true }
//     { "type": "startQuest", "quest": "5a4d1e0000000010" }
//
// Reading checks the shape (known type, required fields, right kinds of
// value); ContentLibrary::check() then checks the references (the variable
// exists and has that type, the quest exists...). Every type is listed in
// docs/formats/content.md.

#include "rolo/scene/EntityId.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rolo {

/// The kinds of value a game variable holds.
enum class ValueType { Bool, Int, Text };
using Value = std::variant<bool, int, std::string>;

ValueType typeOf(const Value& value);
/// "bool", "int", "text" (as in files).
std::string_view valueTypeName(ValueType type);
std::optional<ValueType> valueTypeFromName(std::string_view name);
/// How a value is shown in text: true/false, 12, or the text itself.
std::string toDisplayString(const Value& value);

enum class CompareOp { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };
/// "==", "!=", "<", "<=", ">", ">=".
std::string_view compareOpName(CompareOp op);

/// Where a quest is for the player.
enum class QuestState { NotStarted, Active, Completed, Failed };
/// "notStarted", "active", "completed", "failed".
std::string_view questStateName(QuestState state);
std::optional<QuestState> questStateFromName(std::string_view name);

struct Condition {
    enum class Kind {
        Always,          // { "type": "always" } — true (the default)
        Compare,         // a variable compared with a value
        QuestIs,         // a quest is in a state
        ObjectiveDone,   // an objective of a quest is complete
        All,             // every condition in `children` is true
        Any,             // at least one is true
        Not,             // `children[0]` is false
    };
    Kind kind = Kind::Always;
    std::string variable;      // Compare
    CompareOp op = CompareOp::Equal;
    Value value;
    AssetId quest;             // QuestIs, ObjectiveDone
    QuestState state = QuestState::Active;
    std::string objective;     // ObjectiveDone
    std::vector<Condition> children;

    bool isAlways() const noexcept { return kind == Kind::Always; }
    friend bool operator==(const Condition&, const Condition&) = default;
};

struct Action {
    enum class Kind {
        Set,                // set a variable to `value`
        Add,                // add `value` (an int) to an int variable
        StartQuest,         // start `quest` (if its prerequisites are met)
        CompleteObjective,  // complete `objective` of `quest`
        FailQuest,          // fail `quest`
        StartDialogue,      // start the dialogue `asset`
        ChangeScene,        // change to the scene `asset`
        SetActive,          // switch an object on or off (`entity`; null = the trigger's own object)
        Destroy,            // remove an object for good (`entity`; null = the trigger's own object)
        Emit,               // send the game event named `text` (gameplay code and triggers hear it)
        Log,                // write `text` to the log
        Wait,               // pause the action list for `seconds` (timed sequences)
        SaveGame,           // save to the slot named `text`
        LoadGame,           // load the slot named `text`
        PlaySound,          // play the sound effect `asset`
        PlayMusic,          // play `asset` as the music (fading from what played before)
        StopMusic,          // fade the music out
    };
    Kind kind = Kind::Log;
    std::string variable;   // Set, Add
    Value value;
    AssetId quest;          // StartQuest, CompleteObjective, FailQuest
    std::string objective;  // CompleteObjective
    AssetId asset;          // StartDialogue, ChangeScene, PlaySound, PlayMusic
    EntityId entity;        // SetActive, Destroy
    bool active = true;     // SetActive
    std::string text;       // Emit, Log, SaveGame, LoadGame
    float seconds = 0.0f;   // Wait

    friend bool operator==(const Action&, const Action&) = default;
};

/// The "type" names used in files, e.g. "compare", "startQuest".
std::string_view conditionTypeName(Condition::Kind kind);
std::string_view actionTypeName(Action::Kind kind);

/// Reads a condition or an action list from JSON text (e.g. a component's
/// data). Nothing, with `error` set, if the shape is wrong.
std::optional<Condition> parseCondition(std::string_view json, std::string* error = nullptr);
std::optional<std::vector<Action>> parseActions(std::string_view json, std::string* error = nullptr);
std::string serializeCondition(const Condition& condition);
std::string serializeActions(const std::vector<Action>& actions);

/// What something does when it happens: actions, run only if the
/// condition (when there is one) is true. Triggers in scenes store one.
struct Reaction {
    std::optional<Condition> condition;
    std::vector<Action> actions;
    friend bool operator==(const Reaction&, const Reaction&) = default;
};
/// { "condition": {...} or null, "actions": [...] }
std::optional<Reaction> parseReaction(std::string_view json, std::string* error = nullptr);
std::string serializeReaction(const Reaction& reaction);

/// One line describing a condition or action for people, e.g.
/// "gems >= 3", "start quest 5a4d1e0000000010".
std::string describe(const Condition& condition);
std::string describe(const Action& action);

/// Replaces {variable} in `text` with the variable's value as text, using
/// `lookup` (which returns nothing for unknown names; those stay as written).
/// "{{" gives a literal "{".
template <class Lookup>
std::string substituteVariables(std::string_view text, Lookup&& lookup);

} // namespace rolo

#include "rolo/content/detail/Substitute.inl"
