#include "Json.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace rolo {

using content_detail::Json;

namespace {

constexpr int kMaxDepth = 32;   // how deeply all/any/not may nest

struct KindName {
    int kind;
    std::string_view name;
};

constexpr std::array kConditionNames{
    KindName{static_cast<int>(Condition::Kind::Always), "always"},
    KindName{static_cast<int>(Condition::Kind::Compare), "compare"},
    KindName{static_cast<int>(Condition::Kind::QuestIs), "quest"},
    KindName{static_cast<int>(Condition::Kind::ObjectiveDone), "objective"},
    KindName{static_cast<int>(Condition::Kind::All), "all"},
    KindName{static_cast<int>(Condition::Kind::Any), "any"},
    KindName{static_cast<int>(Condition::Kind::Not), "not"},
};

constexpr std::array kActionNames{
    KindName{static_cast<int>(Action::Kind::Set), "set"},
    KindName{static_cast<int>(Action::Kind::Add), "add"},
    KindName{static_cast<int>(Action::Kind::StartQuest), "startQuest"},
    KindName{static_cast<int>(Action::Kind::CompleteObjective), "completeObjective"},
    KindName{static_cast<int>(Action::Kind::FailQuest), "failQuest"},
    KindName{static_cast<int>(Action::Kind::StartDialogue), "startDialogue"},
    KindName{static_cast<int>(Action::Kind::ChangeScene), "changeScene"},
    KindName{static_cast<int>(Action::Kind::SetActive), "setActive"},
    KindName{static_cast<int>(Action::Kind::Destroy), "destroy"},
    KindName{static_cast<int>(Action::Kind::Emit), "emit"},
    KindName{static_cast<int>(Action::Kind::Log), "log"},
    KindName{static_cast<int>(Action::Kind::Wait), "wait"},
    KindName{static_cast<int>(Action::Kind::SaveGame), "saveGame"},
    KindName{static_cast<int>(Action::Kind::LoadGame), "loadGame"},
    KindName{static_cast<int>(Action::Kind::PlaySound), "playSound"},
    KindName{static_cast<int>(Action::Kind::PlayMusic), "playMusic"},
    KindName{static_cast<int>(Action::Kind::StopMusic), "stopMusic"},
};

constexpr std::array<std::string_view, 6> kOpNames{"==", "!=", "<", "<=", ">", ">="};
constexpr std::array<std::string_view, 4> kQuestStateNames{"notStarted", "active", "completed", "failed"};

template <std::size_t N>
std::optional<int> kindFromName(const std::array<KindName, N>& names, std::string_view name)
{
    for (const KindName& k : names) {
        if (k.name == name) {
            return k.kind;
        }
    }
    return std::nullopt;
}

template <std::size_t N>
std::string_view nameFromKind(const std::array<KindName, N>& names, int kind)
{
    for (const KindName& k : names) {
        if (k.kind == kind) {
            return k.name;
        }
    }
    return "?";
}

template <std::size_t N>
std::string listNames(const std::array<KindName, N>& names)
{
    std::string out;
    for (const KindName& k : names) {
        out += (out.empty() ? "" : ", ") + std::string(k.name);
    }
    return out;
}

// Reads a required text field.
bool readText(const Json& j, const char* key, std::string& out, std::string& error, std::string_view type)
{
    if (!j.contains(key) || !j[key].is_string()) {
        error = "\"" + std::string(type) + "\" needs \"" + key + "\" (text)";
        return false;
    }
    out = j[key].get<std::string>();
    return true;
}

bool readAsset(const Json& j, const char* key, AssetId& out, std::string& error, std::string_view type)
{
    const auto id = j.contains(key) ? content_detail::assetIdFromJson(j[key]) : std::nullopt;
    if (!id || id->isNull()) {
        error = "\"" + std::string(type) + "\" needs \"" + key + "\": an asset ID (16 hex digits)";
        return false;
    }
    out = *id;
    return true;
}

std::optional<Condition> readCondition(const Json& j, std::string& error, int depth)
{
    if (depth > kMaxDepth) {
        error = "conditions are nested too deeply (more than " + std::to_string(kMaxDepth) + " levels)";
        return std::nullopt;
    }
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
        error = "a condition must be an object with a \"type\"";
        return std::nullopt;
    }
    const std::string type = j["type"].get<std::string>();
    const auto kind = kindFromName(kConditionNames, type);
    if (!kind) {
        error = "unknown condition type \"" + type + "\" (known: " + listNames(kConditionNames) + ")";
        return std::nullopt;
    }
    Condition c;
    c.kind = static_cast<Condition::Kind>(*kind);
    switch (c.kind) {
    case Condition::Kind::Always:
        break;
    case Condition::Kind::Compare: {
        if (!readText(j, "variable", c.variable, error, type)) {
            return std::nullopt;
        }
        std::string op = "==";
        if (j.contains("op")) {
            if (!j["op"].is_string()) {
                error = "\"op\" must be one of == != < <= > >=";
                return std::nullopt;
            }
            op = j["op"].get<std::string>();
        }
        bool found = false;
        for (std::size_t i = 0; i < kOpNames.size(); ++i) {
            if (kOpNames[i] == op) {
                c.op = static_cast<CompareOp>(i);
                found = true;
            }
        }
        if (!found) {
            error = "\"op\" must be one of == != < <= > >=, not \"" + op + "\"";
            return std::nullopt;
        }
        const auto value = j.contains("value") ? content_detail::valueFromJson(j["value"]) : std::nullopt;
        if (!value) {
            error = "\"compare\" needs a \"value\": true/false, a whole number or text";
            return std::nullopt;
        }
        c.value = *value;
        break;
    }
    case Condition::Kind::QuestIs: {
        std::string state;
        if (!readAsset(j, "quest", c.quest, error, type) || !readText(j, "state", state, error, type)) {
            return std::nullopt;
        }
        const auto s = questStateFromName(state);
        if (!s) {
            error = "\"state\" must be notStarted, active, completed or failed, not \"" + state + "\"";
            return std::nullopt;
        }
        c.state = *s;
        break;
    }
    case Condition::Kind::ObjectiveDone:
        if (!readAsset(j, "quest", c.quest, error, type) || !readText(j, "objective", c.objective, error, type)) {
            return std::nullopt;
        }
        break;
    case Condition::Kind::All:
    case Condition::Kind::Any:
        if (!j.contains("conditions") || !j["conditions"].is_array()) {
            error = "\"" + type + "\" needs \"conditions\": a list of conditions";
            return std::nullopt;
        }
        for (const Json& child : j["conditions"]) {
            auto parsed = readCondition(child, error, depth + 1);
            if (!parsed) {
                return std::nullopt;
            }
            c.children.push_back(std::move(*parsed));
        }
        break;
    case Condition::Kind::Not: {
        if (!j.contains("condition")) {
            error = "\"not\" needs \"condition\"";
            return std::nullopt;
        }
        auto parsed = readCondition(j["condition"], error, depth + 1);
        if (!parsed) {
            return std::nullopt;
        }
        c.children.push_back(std::move(*parsed));
        break;
    }
    }
    return c;
}

std::string describeValue(const Value& v)
{
    if (const auto* text = std::get_if<std::string>(&v)) {
        return "\"" + *text + "\"";
    }
    return toDisplayString(v);
}

} // namespace

// --- Names ---------------------------------------------------------------------------------------

ValueType typeOf(const Value& value)
{
    return static_cast<ValueType>(value.index());
}

std::string_view valueTypeName(ValueType type)
{
    switch (type) {
    case ValueType::Bool: return "bool";
    case ValueType::Int: return "int";
    case ValueType::Text: return "text";
    }
    return "bool";
}

std::optional<ValueType> valueTypeFromName(std::string_view name)
{
    for (ValueType t : {ValueType::Bool, ValueType::Int, ValueType::Text}) {
        if (valueTypeName(t) == name) {
            return t;
        }
    }
    return std::nullopt;
}

std::string toDisplayString(const Value& value)
{
    if (const bool* b = std::get_if<bool>(&value)) {
        return *b ? "true" : "false";
    }
    if (const int* i = std::get_if<int>(&value)) {
        return std::to_string(*i);
    }
    return std::get<std::string>(value);
}

std::string_view compareOpName(CompareOp op)
{
    return kOpNames[static_cast<std::size_t>(op)];
}

std::string_view questStateName(QuestState state)
{
    return kQuestStateNames[static_cast<std::size_t>(state)];
}

std::optional<QuestState> questStateFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kQuestStateNames.size(); ++i) {
        if (kQuestStateNames[i] == name) {
            return static_cast<QuestState>(i);
        }
    }
    return std::nullopt;
}

std::string_view conditionTypeName(Condition::Kind kind)
{
    return nameFromKind(kConditionNames, static_cast<int>(kind));
}

std::string_view actionTypeName(Action::Kind kind)
{
    return nameFromKind(kActionNames, static_cast<int>(kind));
}

// --- JSON ----------------------------------------------------------------------------------------

namespace content_detail {

std::optional<Value> valueFromJson(const Json& j)
{
    if (j.is_boolean()) {
        return Value{j.get<bool>()};
    }
    if (j.is_number_integer()) {
        const auto v = j.get<long long>();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return Value{static_cast<int>(v)};
    }
    if (j.is_number_float()) {
        // 3.0 written by another tool is still a whole number.
        const double d = j.get<double>();
        if (std::floor(d) == d && std::abs(d) < 2e9) {
            return Value{static_cast<int>(d)};
        }
        return std::nullopt;
    }
    if (j.is_string()) {
        return Value{j.get<std::string>()};
    }
    return std::nullopt;
}

Json toJson(const Value& value)
{
    return std::visit([](const auto& v) { return Json(v); }, value);
}

std::optional<AssetId> assetIdFromJson(const Json& j)
{
    if (j.is_null()) {
        return AssetId{};
    }
    if (!j.is_string()) {
        return std::nullopt;
    }
    return AssetId::parse(j.get<std::string>());
}

std::optional<Condition> conditionFromJson(const Json& j, std::string& error)
{
    return readCondition(j, error, 0);
}

Json toJson(const Condition& c)
{
    Json j;
    j["type"] = conditionTypeName(c.kind);
    switch (c.kind) {
    case Condition::Kind::Always: break;
    case Condition::Kind::Compare:
        j["variable"] = c.variable;
        j["op"] = compareOpName(c.op);
        j["value"] = toJson(c.value);
        break;
    case Condition::Kind::QuestIs:
        j["quest"] = toJson(c.quest);
        j["state"] = questStateName(c.state);
        break;
    case Condition::Kind::ObjectiveDone:
        j["quest"] = toJson(c.quest);
        j["objective"] = c.objective;
        break;
    case Condition::Kind::All:
    case Condition::Kind::Any: {
        Json list = Json::array();
        for (const Condition& child : c.children) {
            list.push_back(toJson(child));
        }
        j["conditions"] = std::move(list);
        break;
    }
    case Condition::Kind::Not:
        j["condition"] = c.children.empty() ? toJson(Condition{}) : toJson(c.children.front());
        break;
    }
    return j;
}

std::optional<Action> actionFromJson(const Json& j, std::string& error)
{
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
        error = "an action must be an object with a \"type\"";
        return std::nullopt;
    }
    const std::string type = j["type"].get<std::string>();
    const auto kind = kindFromName(kActionNames, type);
    if (!kind) {
        error = "unknown action type \"" + type + "\" (known: " + listNames(kActionNames) + ")";
        return std::nullopt;
    }
    Action a;
    a.kind = static_cast<Action::Kind>(*kind);
    // An optional object reference: null or missing = the trigger's own object.
    const auto readEntity = [&]() -> bool {
        if (!j.contains("entity") || j["entity"].is_null()) {
            return true;
        }
        const auto id = j["entity"].is_string() ? EntityId::parse(j["entity"].get<std::string>()) : std::nullopt;
        if (!id) {
            error = "\"entity\" must be an object's ID (16 hex digits), or null for the trigger's own object";
            return false;
        }
        a.entity = *id;
        return true;
    };
    switch (a.kind) {
    case Action::Kind::Set:
    case Action::Kind::Add: {
        if (!readText(j, "variable", a.variable, error, type)) {
            return std::nullopt;
        }
        const auto value = j.contains("value") ? valueFromJson(j["value"]) : std::nullopt;
        if (!value || (a.kind == Action::Kind::Add && !std::holds_alternative<int>(*value))) {
            error = a.kind == Action::Kind::Add ? "\"add\" needs a whole number \"value\""
                                                : "\"set\" needs a \"value\": true/false, a whole number or text";
            return std::nullopt;
        }
        a.value = *value;
        break;
    }
    case Action::Kind::StartQuest:
    case Action::Kind::FailQuest:
        if (!readAsset(j, "quest", a.quest, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::CompleteObjective:
        if (!readAsset(j, "quest", a.quest, error, type) || !readText(j, "objective", a.objective, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::StartDialogue:
        if (!readAsset(j, "dialogue", a.asset, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::ChangeScene:
        if (!readAsset(j, "scene", a.asset, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::PlaySound:
    case Action::Kind::PlayMusic:
        if (!readAsset(j, "sound", a.asset, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::StopMusic: break;
    case Action::Kind::SetActive:
        if (!readEntity()) {
            return std::nullopt;
        }
        if (!j.contains("active") || !j["active"].is_boolean()) {
            error = "\"setActive\" needs \"active\": true or false";
            return std::nullopt;
        }
        a.active = j["active"].get<bool>();
        break;
    case Action::Kind::Destroy:
        if (!readEntity()) {
            return std::nullopt;
        }
        break;
    case Action::Kind::Emit:
        if (!readText(j, "event", a.text, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::Log:
        if (!readText(j, "text", a.text, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::SaveGame:
    case Action::Kind::LoadGame:
        if (!readText(j, "slot", a.text, error, type)) {
            return std::nullopt;
        }
        break;
    case Action::Kind::Wait:
        if (!j.contains("seconds") || !j["seconds"].is_number() || j["seconds"].get<double>() < 0.0
            || j["seconds"].get<double>() > 3600.0) {
            error = "\"wait\" needs \"seconds\": a number from 0 to 3600";
            return std::nullopt;
        }
        a.seconds = j["seconds"].get<float>();
        break;
    }
    return a;
}

std::optional<std::vector<Action>> actionsFromJson(const Json& j, std::string& error)
{
    if (!j.is_array()) {
        error = "actions must be a list";
        return std::nullopt;
    }
    std::vector<Action> out;
    for (std::size_t i = 0; i < j.size(); ++i) {
        auto a = actionFromJson(j[i], error);
        if (!a) {
            error = "action " + std::to_string(i + 1) + ": " + error;
            return std::nullopt;
        }
        out.push_back(std::move(*a));
    }
    return out;
}

Json toJson(const Action& a)
{
    Json j;
    j["type"] = actionTypeName(a.kind);
    const auto entity = [&] { return a.entity.isNull() ? Json() : Json(a.entity.toString()); };
    switch (a.kind) {
    case Action::Kind::Set:
    case Action::Kind::Add:
        j["variable"] = a.variable;
        j["value"] = toJson(a.value);
        break;
    case Action::Kind::StartQuest:
    case Action::Kind::FailQuest: j["quest"] = toJson(a.quest); break;
    case Action::Kind::CompleteObjective:
        j["quest"] = toJson(a.quest);
        j["objective"] = a.objective;
        break;
    case Action::Kind::StartDialogue: j["dialogue"] = toJson(a.asset); break;
    case Action::Kind::ChangeScene: j["scene"] = toJson(a.asset); break;
    case Action::Kind::PlaySound:
    case Action::Kind::PlayMusic: j["sound"] = toJson(a.asset); break;
    case Action::Kind::StopMusic: break;
    case Action::Kind::SetActive:
        j["entity"] = entity();
        j["active"] = a.active;
        break;
    case Action::Kind::Destroy: j["entity"] = entity(); break;
    case Action::Kind::Emit: j["event"] = a.text; break;
    case Action::Kind::Log: j["text"] = a.text; break;
    case Action::Kind::SaveGame:
    case Action::Kind::LoadGame: j["slot"] = a.text; break;
    case Action::Kind::Wait: j["seconds"] = a.seconds; break;
    }
    return j;
}

Json toJson(const std::vector<Action>& actions)
{
    Json list = Json::array();
    for (const Action& a : actions) {
        list.push_back(toJson(a));
    }
    return list;
}

} // namespace content_detail

// --- Public text forms ----------------------------------------------------------------------------

std::optional<Condition> parseCondition(std::string_view json, std::string* error)
{
    std::string message;
    std::optional<Condition> result;
    try {
        result = content_detail::conditionFromJson(Json::parse(json.begin(), json.end()), message);
    } catch (const nlohmann::json::parse_error&) {
        message = "not valid JSON";
    }
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

std::optional<std::vector<Action>> parseActions(std::string_view json, std::string* error)
{
    std::string message;
    std::optional<std::vector<Action>> result;
    try {
        result = content_detail::actionsFromJson(Json::parse(json.begin(), json.end()), message);
    } catch (const nlohmann::json::parse_error&) {
        message = "not valid JSON";
    }
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

std::optional<Reaction> parseReaction(std::string_view json, std::string* error)
{
    std::string message;
    std::optional<Reaction> result;
    try {
        const Json j = Json::parse(json.begin(), json.end());
        if (!j.is_object()) {
            message = "expected { \"condition\": ..., \"actions\": [...] }";
        } else {
            Reaction r;
            bool ok = true;
            if (j.contains("condition") && !j["condition"].is_null()) {
                r.condition = content_detail::conditionFromJson(j["condition"], message);
                ok = r.condition.has_value();
            }
            if (ok && j.contains("actions")) {
                auto actions = content_detail::actionsFromJson(j["actions"], message);
                ok = actions.has_value();
                if (ok) {
                    r.actions = std::move(*actions);
                }
            }
            if (ok) {
                result = std::move(r);
            }
        }
    } catch (const nlohmann::json::parse_error&) {
        message = "not valid JSON";
    }
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

std::string serializeReaction(const Reaction& reaction)
{
    Json j;
    j["condition"] = reaction.condition ? content_detail::toJson(*reaction.condition) : Json();
    j["actions"] = content_detail::toJson(reaction.actions);
    return j.dump();
}

std::string serializeCondition(const Condition& condition)
{
    return content_detail::toJson(condition).dump();
}

std::string serializeActions(const std::vector<Action>& actions)
{
    return content_detail::toJson(actions).dump();
}

std::string describe(const Condition& c)
{
    switch (c.kind) {
    case Condition::Kind::Always: return "always";
    case Condition::Kind::Compare:
        if (std::holds_alternative<bool>(c.value) && c.op == CompareOp::Equal) {
            return (std::get<bool>(c.value) ? "" : "not ") + c.variable;
        }
        return c.variable + " " + std::string(compareOpName(c.op)) + " " + describeValue(c.value);
    case Condition::Kind::QuestIs:
        return "quest " + c.quest.toString() + " is " + std::string(questStateName(c.state));
    case Condition::Kind::ObjectiveDone:
        return "objective '" + c.objective + "' of quest " + c.quest.toString() + " is done";
    case Condition::Kind::All:
    case Condition::Kind::Any: {
        if (c.children.empty()) {
            return c.kind == Condition::Kind::All ? "always" : "never";
        }
        std::string out;
        for (const Condition& child : c.children) {
            out += (out.empty() ? "" : (c.kind == Condition::Kind::All ? " and " : " or ")) + std::string("(")
                   + describe(child) + ")";
        }
        return out;
    }
    case Condition::Kind::Not:
        return "not (" + (c.children.empty() ? std::string("always") : describe(c.children.front())) + ")";
    }
    return "?";
}

std::string describe(const Action& a)
{
    switch (a.kind) {
    case Action::Kind::Set: return "set " + a.variable + " to " + describeValue(a.value);
    case Action::Kind::Add: return "add " + toDisplayString(a.value) + " to " + a.variable;
    case Action::Kind::StartQuest: return "start quest " + a.quest.toString();
    case Action::Kind::CompleteObjective:
        return "complete objective '" + a.objective + "' of quest " + a.quest.toString();
    case Action::Kind::FailQuest: return "fail quest " + a.quest.toString();
    case Action::Kind::StartDialogue: return "start dialogue " + a.asset.toString();
    case Action::Kind::ChangeScene: return "change to scene " + a.asset.toString();
    case Action::Kind::PlaySound: return "play sound " + a.asset.toString();
    case Action::Kind::PlayMusic: return "play music " + a.asset.toString();
    case Action::Kind::StopMusic: return "stop the music";
    case Action::Kind::SetActive:
        return std::string(a.active ? "switch on " : "switch off ")
               + (a.entity.isNull() ? std::string("this object") : "object " + a.entity.toString());
    case Action::Kind::Destroy:
        return "remove " + (a.entity.isNull() ? std::string("this object") : "object " + a.entity.toString());
    case Action::Kind::Emit: return "send event '" + a.text + "'";
    case Action::Kind::Log: return "log \"" + a.text + "\"";
    case Action::Kind::Wait: {
        std::string seconds = std::to_string(std::round(a.seconds * 100.0f) / 100.0f);
        seconds.erase(seconds.find_last_not_of('0') + 1);   // "1.500000" -> "1.5"
        if (!seconds.empty() && seconds.back() == '.') {
            seconds.pop_back();
        }
        return "wait " + seconds + " s";
    }
    case Action::Kind::SaveGame: return "save to slot '" + a.text + "'";
    case Action::Kind::LoadGame: return "load slot '" + a.text + "'";
    }
    return "?";
}

} // namespace rolo
