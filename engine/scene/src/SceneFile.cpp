#include "rolo/scene/SceneFile.hpp"
#include "rolo/scene/Values.hpp"
#include "rolo/core/Names.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/core/Numbers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace rolo {

namespace {

// ordered_json keeps keys in the order we write them, so saved files read
// naturally (id, name, components...) and diff cleanly in version control.
using Json = nlohmann::ordered_json;

// --- Numbers -----------------------------------------------------------------------------

// Floats are written in their shortest exact form ("0.1", not
// "0.10000000149011612"), and whole numbers without a decimal point.
Json number(float value)
{
    if (!std::isfinite(value)) {
        return 0;
    }
    if (std::abs(value) < 1.0e9f && value == std::trunc(value)) {
        return static_cast<std::int64_t>(value);
    }
    return parseNumber(formatShortest(value)).value_or(0.0);
}

// --- Pretty printing ------------------------------------------------------------------------

// Like Json::dump(2), but lists of plain values stay on one line:
// "position": [640, 360] instead of four lines.
void write(const Json& j, std::string& out, int indent)
{
    const auto newline = [&](int level) {
        out += '\n';
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };
    if (j.is_object()) {
        if (j.empty()) {
            out += "{}";
            return;
        }
        out += '{';
        bool first = true;
        for (const auto& [key, value] : j.items()) {
            out += first ? "" : ",";
            first = false;
            newline(indent + 1);
            out += Json(key).dump();
            out += ": ";
            write(value, out, indent + 1);
        }
        newline(indent);
        out += '}';
    } else if (j.is_array()) {
        const bool simple = std::all_of(j.begin(), j.end(), [](const Json& v) { return v.is_primitive(); });
        if (j.empty() || simple) {
            out += '[';
            for (std::size_t i = 0; i < j.size(); ++i) {
                out += i == 0 ? "" : ", ";
                out += j[i].dump();
            }
            out += ']';
            return;
        }
        out += '[';
        for (std::size_t i = 0; i < j.size(); ++i) {
            out += i == 0 ? "" : ",";
            newline(indent + 1);
            write(j[i], out, indent + 1);
        }
        newline(indent);
        out += ']';
    } else {
        out += j.dump();
    }
}

std::string pretty(const Json& j)
{
    std::string out;
    write(j, out, 0);
    out += '\n';
    return out;
}

// --- Issues ------------------------------------------------------------------------------

struct Issues {
    std::vector<FileIssue>& list;

    void warning(std::string where, std::string message)
    {
        list.push_back({FileIssue::Severity::Warning, std::move(where), std::move(message)});
    }
    void error(std::string where, std::string message)
    {
        list.push_back({FileIssue::Severity::Error, std::move(where), std::move(message)});
    }
};

std::string describeEntity(const std::string& name, const std::string& id)
{
    std::string text = "entity";
    if (!name.empty()) {
        text += " '" + name + "'";
    }
    if (!id.empty()) {
        text += " (" + id + ")";
    }
    return text;
}

std::string typeName(const Json& j)
{
    switch (j.type()) {
    case Json::value_t::null: return "nothing (null)";
    case Json::value_t::boolean: return "true/false";
    case Json::value_t::string: return "text";
    case Json::value_t::array: return "a list";
    case Json::value_t::object: return "an object";
    case Json::value_t::number_float:
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: return "a number";
    default: return "something else";
    }
}

// --- Property values <-> JSON ---------------------------------------------------------------

Json toJson(const PropertyInfo& info, const PropertyValue& value)
{
    switch (info.kind) {
    case PropertyKind::Bool: return std::get<bool>(value);
    case PropertyKind::Int: return std::get<int>(value);
    case PropertyKind::Float: return number(std::get<float>(value));
    case PropertyKind::Vec2: {
        const Vec2 v = std::get<Vec2>(value);
        return Json::array({number(v.x), number(v.y)});
    }
    case PropertyKind::Color: return std::get<Color>(value).toHex();
    case PropertyKind::String:
        if (info.options.data) {
            // Named values are JSON in the file, not JSON inside a text.
            const Json values = Json::parse(std::get<std::string>(value), nullptr, false);
            return values.is_object() ? values : Json::object();
        }
        return std::get<std::string>(value);
    case PropertyKind::Asset: {
        const AssetId id = std::get<AssetId>(value);
        return id.isNull() ? Json() : Json(id.toString()); // null = no asset chosen
    }
    case PropertyKind::Choice: {
        const int index = std::get<int>(value);
        if (index >= 0 && static_cast<std::size_t>(index) < info.options.choices.size()) {
            return info.options.choices[static_cast<std::size_t>(index)];
        }
        return info.options.choices.empty() ? Json() : Json(info.options.choices.front());
    }
    }
    return {};
}

// Reads a property value; on failure explains what was expected.
std::optional<PropertyValue> fromJson(const PropertyInfo& info, const Json& j, std::string& expected)
{
    const auto finiteNumber = [](const Json& n) -> std::optional<float> {
        if (!n.is_number()) {
            return std::nullopt;
        }
        const double d = n.get<double>();
        if (!std::isfinite(d) || std::abs(d) > 3.0e38) {
            return std::nullopt;
        }
        return static_cast<float>(d);
    };
    switch (info.kind) {
    case PropertyKind::Bool:
        expected = "true or false";
        if (j.is_boolean()) {
            return j.get<bool>();
        }
        break;
    case PropertyKind::Int:
        expected = "a whole number";
        if (const auto n = finiteNumber(j); n && *n == std::trunc(*n) && std::abs(*n) < 2.0e9f) {
            return static_cast<int>(*n);
        }
        break;
    case PropertyKind::Float:
        expected = "a number";
        if (const auto n = finiteNumber(j)) {
            return *n;
        }
        break;
    case PropertyKind::Vec2:
        expected = "two numbers, like [10, 20]";
        if (j.is_array() && j.size() == 2) {
            const auto x = finiteNumber(j[0]);
            const auto y = finiteNumber(j[1]);
            if (x && y) {
                return Vec2{*x, *y};
            }
        }
        break;
    case PropertyKind::Color:
        expected = "a colour like \"#ff8800ff\"";
        if (j.is_string()) {
            if (const auto c = Color::fromHex(j.get<std::string>())) {
                return *c;
            }
        }
        break;
    case PropertyKind::String:
        if (info.options.data) {
            expected = "named values, like {\"speed\": 120}";
            if (j.is_object()) {
                return writeDataValues(parseDataValues(j.dump()));
            }
            break;
        }
        expected = "text";
        if (j.is_string()) {
            return j.get<std::string>();
        }
        break;
    case PropertyKind::Asset:
        expected = "an asset ID (16 hex digits) or null";
        if (j.is_null()) {
            return AssetId{};
        }
        if (j.is_string()) {
            if (const auto id = AssetId::parse(j.get<std::string>())) {
                return *id;
            }
        }
        break;
    case PropertyKind::Choice: {
        expected = "one of:";
        for (const std::string& choice : info.options.choices) {
            expected += " \"" + choice + "\"";
        }
        if (j.is_string()) {
            const std::string name = j.get<std::string>();
            for (std::size_t i = 0; i < info.options.choices.size(); ++i) {
                if (info.options.choices[i] == name) {
                    return static_cast<int>(i);
                }
            }
        }
        break;
    }
    }
    return std::nullopt;
}

// --- Format upgrades ---------------------------------------------------------------------------

// When the scene format changes, version N files are upgraded step by step
// before reading: kUpgrades[N - 1] turns a version N document into version N + 1.
//
// 1 -> 2 (ADR 0035): version 2 adds linked copies of prefabs (entities with
// a "prefab"). A version 1 file has none and is a valid version 2 file as
// it stands, so the step changes nothing.
//
// 2 -> 3 (ADR 0036): version 3 lets a prefab file hold linked copies of
// other prefabs, which older files do not have: nothing to change either.
using Upgrade = void (*)(Json& document);
constexpr Upgrade kUpgrades[] = {[](Json&) {}, [](Json&) {}};
static_assert(std::size(kUpgrades) == SceneFormat::kVersion - 1, "one upgrade step per older version");

// --- Linked copies of prefabs (ADR 0035) ---------------------------------------------------------
//
// In a scene file, an object placed from a prefab is written as a link and
// what the copy changed:
//
//   { "id": "...", "prefab": "<asset id>", "parent": "...",
//     "name": "Coin 3",                                   only if changed
//     "active": false,                                    only if changed
//     "components": [ { "type": "rolo.Transform", "position": [40, 8] } ],
//                                                         only the changed keys of a component; a
//                                                         component the prefab lacks is written whole;
//                                                         "$unset": [keys] for keys the copy has not;
//                                                         named values (a Script's "values") name by
//                                                         name: { "$changed": {..}, "$unset": [names] }
//     "removedComponents": [ "rolo.Collider" ],
//     "objects": [ { "from": "<id in the prefab>", "id": "<id in this scene>", ...as above } ],
//                                                         the objects inside, by which of the
//                                                         prefab's each is
//     "removed": [ "<id in the prefab>" ] }               prefab objects this copy does not have
//
// Reading turns each such entry back into ordinary entities *as JSON*,
// before anything else is read: the prefab's entities, with this scene's
// IDs, and the changes laid over them. Everything after that (and the whole
// engine) sees ordinary objects, which carry "prefab" / "from" marks so
// that writing can find the link again.

Json expandLinkedCopies(const Json& entities, const PrefabTexts& prefabs, Issues& issues, std::vector<AssetId>& open);

// The entities of a prefab file, upgraded to the current format: first the
// object, then what is inside it. Nothing, with `why`, if it cannot be used.
//
// A prefab may itself hold linked copies of other prefabs (ADR 0036): they
// are turned into ordinary entities here, as a scene's are, so that what
// comes back is always the whole object. Those entities keep their marks
// ("prefab", "from", "kept"), which say which part came from which prefab.
// `open` lists the prefabs being read right now, outermost first: a prefab
// that holds itself, directly or through others, would never end.
std::optional<Json> prefabEntities(const PrefabTexts& prefabs, AssetId id, std::string& why, std::vector<AssetId>& open)
{
    if (!prefabs) {
        why = "prefabs cannot be read here";
        return std::nullopt;
    }
    if (std::find(open.begin(), open.end(), id) != open.end()) {
        why = "it holds a copy of itself, directly or through another prefab";
        return std::nullopt;
    }
    const std::optional<std::string> text = prefabs(id);
    if (!text) {
        why = "the project has no such prefab (was it deleted?)";
        return std::nullopt;
    }
    Json document = Json::parse(*text, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_string()
        || !isFormatName(document["format"].get<std::string>(), PrefabFormat::kName)) {
        why = "it is not a prefab file";
        return std::nullopt;
    }
    const int version = document.contains("version") && document["version"].is_number_integer()
                            ? document["version"].get<int>()
                            : 0;
    if (version < 1 || version > SceneFormat::kVersion) {
        why = version < 1 ? "it has no format version" : "it was saved by a newer version of Rolo";
        return std::nullopt;
    }
    for (int v = version; v < SceneFormat::kVersion; ++v) {
        kUpgrades[v - 1](document);
    }
    if (!document.contains("entities") || !document["entities"].is_array() || document["entities"].empty()) {
        why = "it holds no object";
        return std::nullopt;
    }
    for (const Json& e : document["entities"]) {
        if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) {
            why = "it is damaged";
            return std::nullopt;
        }
    }
    // (What goes wrong inside is said when the prefab itself is checked;
    // here its copies only need to know that a part is missing.)
    std::vector<FileIssue> unheard;
    Issues inner{unheard};
    open.push_back(id);
    Json entities = expandLinkedCopies(document["entities"], prefabs, inner, open);
    open.pop_back();
    if (entities.empty()) {
        why = "it holds no object";
        return std::nullopt;
    }
    return entities;
}

std::optional<Json> prefabEntities(const PrefabTexts& prefabs, AssetId id, std::string& why)
{
    std::vector<AssetId> open;
    return prefabEntities(prefabs, id, why, open);
}

std::string textOf(const Json& j, const char* key)
{
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

// The ID an object of a linked copy gets when the scene file does not say:
// always the same for the same copy and the same object of the prefab, so
// that an object a prefab gained later has one ID every time the scene is
// read (saved games and other objects refer to objects by ID).
std::string derivedId(const std::string& copyRoot, const std::string& prefabObject)
{
    const std::uint64_t a = EntityId::parse(copyRoot).value_or(EntityId{}).value();
    const std::uint64_t b = EntityId::parse(prefabObject).value_or(EntityId{}).value();
    std::uint64_t x = a ^ (b * 0x9E3779B97F4A7C15ull + 0x7F4A7C15ull);
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return EntityId(x == 0 ? 1 : x).toString();
}

Json* componentOfType(Json& entity, const std::string& type)
{
    if (entity.contains("components") && entity["components"].is_array()) {
        for (Json& c : entity["components"]) {
            if (textOf(c, "type") == type) {
                return &c;
            }
        }
    }
    return nullptr;
}

// Named values in a component (a Script's properties) may point at objects
// of the prefab by ID: in the copy they point at the copy's objects.
void pointInsideTheCopy(Json& entity, const std::unordered_map<std::string, std::string>& ids)
{
    if (!entity.contains("components") || !entity["components"].is_array()) {
        return;
    }
    for (Json& component : entity["components"]) {
        if (!component.is_object()) {
            continue;
        }
        for (auto& [key, value] : component.items()) {
            if (!value.is_object()) {
                continue;
            }
            for (auto& [name, held] : value.items()) {
                if (held.is_string()) {
                    if (const auto mapped = ids.find(held.get<std::string>()); mapped != ids.end()) {
                        held = mapped->second;
                    }
                }
            }
        }
    }
}

// Lays what a copy changed (`changes`: the entry itself for the top object,
// or one of its "objects") over an entity taken from the prefab.
void applyChanges(Json& entity, const Json& changes)
{
    if (changes.contains("name") && changes["name"].is_string()) {
        entity["name"] = changes["name"];
    }
    if (changes.contains("active") && changes["active"].is_boolean()) {
        entity["active"] = changes["active"];
    }
    if (changes.contains("removedComponents") && changes["removedComponents"].is_array()
        && entity.contains("components") && entity["components"].is_array()) {
        for (const Json& type : changes["removedComponents"]) {
            Json& list = entity["components"];
            for (auto it = list.begin(); it != list.end();) {
                it = type.is_string() && textOf(*it, "type") == type.get<std::string>() ? list.erase(it) : it + 1;
            }
        }
    }
    if (changes.contains("components") && changes["components"].is_array()) {
        if (!entity.contains("components") || !entity["components"].is_array()) {
            entity["components"] = Json::array();
        }
        for (const Json& changed : changes["components"]) {
            const std::string type = textOf(changed, "type");
            Json* target = type.empty() ? nullptr : componentOfType(entity, type);
            if (target == nullptr) {
                Json whole = changed;   // a component the prefab has not: the copy's own
                if (whole.is_object()) {
                    whole.erase("$unset");
                }
                entity["components"].push_back(std::move(whole));
                continue;
            }
            for (const auto& [key, value] : changed.items()) {
                if (key == "$unset") {
                    if (value.is_array()) {
                        for (const Json& gone : value) {
                            if (gone.is_string()) {
                                target->erase(gone.get<std::string>());
                            }
                        }
                    }
                } else if (key != "type" && value.is_object() && value.contains("$changed")) {
                    // Named values, changed name by name (see writeChanges).
                    Json& named = (*target)[key];
                    if (!named.is_object()) {
                        named = Json::object();
                    }
                    if (value["$changed"].is_object()) {
                        for (const auto& [name, held] : value["$changed"].items()) {
                            named[name] = held;
                        }
                    }
                    if (value.contains("$unset") && value["$unset"].is_array()) {
                        for (const Json& gone : value["$unset"]) {
                            if (gone.is_string()) {
                                named.erase(gone.get<std::string>());
                            }
                        }
                    }
                } else if (key != "type") {
                    (*target)[key] = value;
                }
            }
        }
    }
}

// The scene's "entities" with every linked copy turned into ordinary
// entities (see above).
Json expandLinkedCopies(const Json& entities, const PrefabTexts& prefabs, Issues& issues, std::vector<AssetId>& open)
{
    Json out = Json::array();
    // Objects a copy had and its prefab has no longer: what was put inside
    // them goes to the copy's top object rather than being lost.
    std::unordered_map<std::string, std::string> lostParents;
    for (const Json& e : entities) {
        if (!e.is_object() || !e.contains("prefab")) {
            if (e.is_object()) {
                if (const auto lost = lostParents.find(textOf(e, "parent")); lost != lostParents.end()) {
                    Json moved = e;
                    moved["parent"] = lost->second;
                    out.push_back(std::move(moved));
                    continue;
                }
            }
            out.push_back(e);
            continue;
        }
        const std::string rootId = textOf(e, "id");
        const std::string where = describeEntity(textOf(e, "name"), rootId);
        const std::optional<AssetId> asset = AssetId::parse(textOf(e, "prefab"));
        std::string why = "its \"prefab\" is not the ID of a prefab";
        const std::optional<Json> prefab = asset ? prefabEntities(prefabs, *asset, why, open) : std::nullopt;
        const Json* objects = e.contains("objects") && e["objects"].is_array() ? &e["objects"] : nullptr;

        if (!prefab) {
            // A stand-in, and the entry kept as it is (PrefabLink::kept).
            Json standIn;
            standIn["id"] = rootId;
            if (e.contains("parent")) {
                standIn["parent"] = e["parent"];
            }
            standIn["name"] = e.contains("name") && e["name"].is_string() ? e["name"].get<std::string>()
                                                                          : std::string("Missing prefab");
            standIn["components"] = e.contains("components") && e["components"].is_array() ? e["components"]
                                                                                           : Json::array();
            if (asset) {
                standIn["prefab"] = e["prefab"];
            }
            standIn["kept"] = e.dump();
            out.push_back(std::move(standIn));
            if (objects != nullptr) {
                for (const Json& o : *objects) {
                    if (!textOf(o, "id").empty() && !textOf(o, "from").empty()) {
                        // Things may be inside it: it must exist for them to be read.
                        out.push_back({{"id", o["id"]},
                                       {"parent", rootId},
                                       {"name", textOf(o, "name").empty() ? "(from the missing prefab)" : textOf(o, "name")},
                                       {"from", o["from"]}});
                    }
                }
            }
            issues.warning(where, "This is a copy of a prefab that could not be read (" + why
                                      + "). It is shown as an empty object and kept in the scene file as it "
                                        "was, so nothing is lost: put the prefab back and open the scene again.");
            continue;
        }

        // A prefab inside this one that could not be read left a stand-in.
        if (std::any_of(prefab->begin(), prefab->end(), [](const Json& p) { return p.contains("kept"); })) {
            issues.warning(where, "The prefab this is a copy of holds a copy of another prefab that could not be "
                                  "read. That part is shown as an empty object; nothing is lost: put the missing "
                                  "prefab back and open the scene again.");
        }

        // Which ID each object of the prefab has in this scene.
        const std::string prefabRoot = textOf(prefab->front(), "id");
        std::unordered_map<std::string, std::string> ids{{prefabRoot, rootId}};
        std::unordered_map<std::string, const Json*> changesOf;
        std::unordered_set<std::string> inPrefab;
        for (const Json& p : *prefab) {
            inPrefab.insert(textOf(p, "id"));
        }
        if (objects != nullptr) {
            for (const Json& o : *objects) {
                const std::string from = textOf(o, "from");
                const std::string id = textOf(o, "id");
                if (from.empty() || id.empty()) {
                    continue;
                }
                if (inPrefab.count(from) == 0 || from == prefabRoot) {
                    lostParents[id] = rootId;   // the prefab has it no longer
                    continue;
                }
                ids[from] = id;
                changesOf[from] = &o;
            }
        }
        std::unordered_set<std::string> gone;
        if (e.contains("removed") && e["removed"].is_array()) {
            for (const Json& r : e["removed"]) {
                if (r.is_string()) {
                    gone.insert(r.get<std::string>());
                }
            }
        }
        for (const Json& p : *prefab) {
            const std::string id = textOf(p, "id");
            if (id != prefabRoot && ids.count(id) == 0) {
                ids[id] = derivedId(rootId, id);
            }
        }
        for (const Json& p : *prefab) {
            const std::string id = textOf(p, "id");
            const bool top = id == prefabRoot;
            if (!top && (gone.count(id) != 0 || gone.count(textOf(p, "parent")) != 0 || ids.count(textOf(p, "parent")) == 0)) {
                gone.insert(id);   // removed, or inside something removed
                lostParents[ids[id]] = rootId;
                continue;
            }
            Json made = p;
            made["id"] = ids[id];
            made["from"] = id;
            made.erase("parent");
            // Seen from here, every object is this prefab's: which of them
            // the prefab took from another prefab is the prefab's business.
            made.erase("prefab");
            made.erase("kept");
            if (top) {
                made["prefab"] = e["prefab"];
                if (e.contains("parent")) {
                    made["parent"] = e["parent"];
                }
            } else {
                made["parent"] = ids[textOf(p, "parent")];
            }
            pointInsideTheCopy(made, ids);
            if (top) {
                applyChanges(made, e);
            } else if (const auto changes = changesOf.find(id); changes != changesOf.end()) {
                applyChanges(made, *changes->second);
            }
            out.push_back(std::move(made));
        }
    }
    return out;
}

// --- Line and column for parse errors ------------------------------------------------------------

std::string position(std::string_view text, std::size_t byte)
{
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < byte && i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return "line " + std::to_string(line) + ", column " + std::to_string(column);
}

void readComponent(Scene& scene, EntityId id, const std::string& where, const Json& j,
                   const ComponentRegistry& registry, Issues& issues)
{
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) {
        issues.warning(where, "A component without a \"type\" was skipped.");
        return;
    }
    const std::string typeId = j["type"].get<std::string>();
    const std::string here = where + ", component " + typeId;
    const Entity* entity = scene.find(id);

    if (typeId == UnknownComponent::kTypeId) {
        issues.warning(here, "A component cannot use the reserved type name; it was skipped.");
        return;
    }
    const ComponentType* type = registry.find(typeId);
    if (type == nullptr) {
        if (entity->component(typeId) != nullptr) {
            issues.warning(here, "A second component of this type was skipped.");
            return;
        }
        scene.addComponent(id, std::make_unique<UnknownComponent>(typeId, j.dump()));
        issues.warning(here, "This component type is not known to this version of Rolo. Its data is kept "
                             "and will be saved unchanged.");
        return;
    }

    // The type's own ID, which may differ from the file's: Lantern's names
    // ("lantern.Transform") find Rolo's types (rolo/core/Names.hpp).
    const std::string& componentId = type->id;

    // Transform already exists on every entity; other types are added now.
    if (!type->required) {
        if (entity->component(componentId) != nullptr) {
            issues.warning(here, "A second component of this type was skipped.");
            return;
        }
        scene.addComponent(id, type->create());
    }

    for (const auto& [key, value] : j.items()) {
        if (key == "type") {
            continue;
        }
        if (key == "data") {
            std::string problem;
            scene.modifyComponent(id, componentId, [&](Component& c) {
                problem = c.loadData(value.dump());
                return true;
            });
            if (!problem.empty()) {
                issues.warning(here, problem);
            }
            continue;
        }
        const PropertyInfo* info = type->findProperty(key);
        if (info == nullptr) {
            issues.warning(here, "Unknown property \"" + key + "\" was ignored.");
            continue;
        }
        std::string expected;
        const auto parsed = fromJson(*info, value, expected);
        if (!parsed) {
            issues.warning(here, "\"" + key + "\" should be " + expected + " but is " + typeName(value)
                                     + "; the default value is used.");
            continue;
        }
        const auto coerced = info->coerce(*parsed);
        if (coerced && *coerced != *parsed) {
            issues.warning(here, "\"" + key + "\" was out of range and has been adjusted.");
        }
        scene.setProperty(id, componentId, key, *parsed);
    }
}

} // namespace

// --- FileIssue / SceneLoadResult -------------------------------------------------------------

std::string FileIssue::toString() const
{
    std::string text = isError() ? "Error: " : "Warning: ";
    if (!where.empty()) {
        text += where + ": ";
    }
    return text + message;
}

std::size_t SceneLoadResult::warningCount() const
{
    std::size_t n = 0;
    for (const FileIssue& issue : issues) {
        n += issue.isError() ? 0u : 1u;
    }
    return n;
}

std::string SceneLoadResult::firstError() const
{
    for (const FileIssue& issue : issues) {
        if (issue.isError()) {
            return issue.where.empty() ? issue.message : issue.where + ": " + issue.message;
        }
    }
    return {};
}

// --- Reading ------------------------------------------------------------------------------------

namespace {

// Scenes and prefabs are the same kind of file under two names.
struct FileKind {
    std::string_view format;
    const char* word;   // "scene" or "prefab", for messages
    bool prefab;
};
constexpr FileKind kSceneKind{SceneFormat::kName, "scene", false};
constexpr FileKind kPrefabKind{PrefabFormat::kName, "prefab", true};

SceneLoadResult parseDocument(std::string_view text, const ComponentRegistry& registry, const FileKind& kind,
                              const PrefabTexts& prefabs = {})
{
    const std::string word = kind.word;
    SceneLoadResult result;
    Issues issues{result.issues};

    Json document;
    try {
        document = Json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error& e) {
        issues.error("", "This is not valid JSON (problem at " + position(text, e.byte > 0 ? e.byte - 1 : 0)
                             + "). Was the file edited by hand or cut short?");
        return result;
    }

    if (!document.is_object() || !document.contains("format") || !document["format"].is_string()
        || !isFormatName(document["format"].get<std::string>(), kind.format)) {
        issues.error("", "This is not a Rolo " + word + " file (\"format\" should be \"" + std::string(kind.format)
                             + "\").");
        return result;
    }
    if (!document.contains("version") || !document["version"].is_number_integer()) {
        issues.error("", "The " + word + " file has no format \"version\".");
        return result;
    }
    const int version = document["version"].get<int>();
    if (version > SceneFormat::kVersion) {
        issues.error("", "This " + word + " was saved by a newer version of Rolo (" + word + " format "
                             + std::to_string(version)
                             + "; this version reads up to " + std::to_string(SceneFormat::kVersion)
                             + "). Update Rolo to open it.");
        return result;
    }
    if (version < 1) {
        issues.error("", "Unknown " + word + " format version " + std::to_string(version) + ".");
        return result;
    }
    for (int v = version; v < SceneFormat::kVersion; ++v) {
        kUpgrades[v - 1](document);
    }

    if (!document.contains("entities") || !document["entities"].is_array()) {
        issues.error("", "The " + word + " file has no \"entities\" list.");
        return result;
    }

    Scene scene(!kind.prefab && document.contains("name") && document["name"].is_string()
                    ? document["name"].get<std::string>()
                    : std::string(kind.prefab ? "Prefab" : "Untitled"));
    if (!kind.prefab && (!document.contains("name") || !document["name"].is_string())) {
        issues.warning("", "The scene has no name; it is called \"Untitled\".");
    }
    // Optional (added with physics): files without it have the default gravity.
    if (document.contains("gravity")) {
        const Json& g = document["gravity"];
        const bool ok = g.is_array() && g.size() == 2 && g[0].is_number() && g[1].is_number()
                        && std::isfinite(g[0].get<double>()) && std::isfinite(g[1].get<double>());
        if (ok) {
            scene.setGravity({g[0].get<float>(), g[1].get<float>()});
        } else {
            issues.warning("", "\"gravity\" should be two numbers, like [0, -980]; the default is used.");
        }
    }

    // Linked copies of prefabs become ordinary entities first.
    std::vector<AssetId> open;
    const Json entities = expandLinkedCopies(document["entities"], prefabs, issues, open);

    std::size_t index = 0;
    for (const Json& e : entities) {
        ++index;
        const std::string fallbackWhere = "entity #" + std::to_string(index);
        if (!e.is_object()) {
            issues.error(fallbackWhere, "Each entity must be an object.");
            return result;
        }
        const std::string name = e.contains("name") && e["name"].is_string() ? e["name"].get<std::string>() : "";
        const std::string idText = e.contains("id") && e["id"].is_string() ? e["id"].get<std::string>() : "";
        const std::string where = describeEntity(name, idText).empty() ? fallbackWhere : describeEntity(name, idText);

        const auto id = EntityId::parse(idText);
        if (!id) {
            issues.error(where, "Every entity needs an \"id\" of 16 hex digits (other scenes and saved games refer "
                                "to entities by ID, so it cannot be made up).");
            return result;
        }
        if (scene.contains(*id)) {
            issues.error(where, "Two entities have the same ID " + idText
                                    + ". IDs must be unique; was an entity copied by hand?");
            return result;
        }
        EntityId parent;
        if (e.contains("parent") && !(e["parent"].is_string() && e["parent"].get<std::string>().empty())) {
            const auto p = e["parent"].is_string() ? EntityId::parse(e["parent"].get<std::string>()) : std::nullopt;
            if (!p || !scene.contains(*p)) {
                issues.error(where, "Its \"parent\" must be the ID of an entity listed before it.");
                return result;
            }
            parent = *p;
        }
        if (!e.contains("name") || !e["name"].is_string()) {
            issues.warning(where, "The entity has no name.");
        }
        Entity* entity = scene.create(name, parent, std::nullopt, *id);
        if (e.contains("active")) {
            if (e["active"].is_boolean()) {
                scene.setActive(entity->id(), e["active"].get<bool>());
            } else {
                issues.warning(where, "\"active\" should be true or false; it is treated as true.");
            }
        }
        if (e.contains("components")) {
            if (!e["components"].is_array()) {
                issues.warning(where, "\"components\" should be a list; it was ignored.");
            } else {
                for (const Json& c : e["components"]) {
                    readComponent(scene, entity->id(), where, c, registry, issues);
                }
            }
        }
        {
            // Marks left by expandLinkedCopies: where the object came from.
            PrefabLink link;
            link.prefab = AssetId::parse(textOf(e, "prefab")).value_or(AssetId{});
            link.source = EntityId::parse(textOf(e, "from")).value_or(EntityId{});
            link.kept = textOf(e, "kept");
            if (!link.none()) {
                scene.setPrefabLink(entity->id(), std::move(link));
            }
        }
        for (const auto& [key, value] : e.items()) {
            const bool known = key == "id" || key == "parent" || key == "name" || key == "active" || key == "components"
                               || key == "prefab" || key == "from" || key == "kept";
            if (!known) {
                issues.warning(where, "Unknown field \"" + key + "\" was ignored.");
            }
        }
    }
    for (const auto& [key, value] : document.items()) {
        const bool known = key == "format" || key == "version" || key == "entities"
                           || (!kind.prefab && (key == "name" || key == "gravity"));
        if (!known) {
            issues.warning("", "Unknown field \"" + key + "\" was ignored.");
        }
    }
    if (kind.prefab && scene.roots().size() != 1) {
        issues.error("", "A prefab is one object, with others inside it if you like; this file has "
                             + std::to_string(scene.roots().size())
                             + " objects at the top. Was it edited by hand? Make the prefab again in the editor.");
        return result;
    }

    for (FileIssue& issue : validateScene(scene)) {
        result.issues.push_back(std::move(issue));
    }
    for (const FileIssue& issue : result.issues) {
        if (issue.isError()) {
            return result;
        }
    }
    result.scene = std::move(scene);
    return result;
}

SceneLoadResult loadFile(const std::filesystem::path& path, const ComponentRegistry& registry, const FileKind& kind,
                         const PrefabTexts& prefabs = {})
{
    const auto text = readTextFile(path);
    if (!text) {
        SceneLoadResult result;
        result.issues.push_back({FileIssue::Severity::Error, path.string(), "The file could not be read."});
        return result;
    }
    return parseDocument(*text, registry, kind, prefabs);
}

std::string serializeDocument(const Scene& scene, const FileKind& kind, const PrefabTexts& prefabs = {});

} // namespace

SceneLoadResult parseScene(std::string_view text, const ComponentRegistry& registry, const PrefabTexts& prefabs)
{
    return parseDocument(text, registry, kSceneKind, prefabs);
}

SceneLoadResult parsePrefab(std::string_view text, const ComponentRegistry& registry, const PrefabTexts& prefabs)
{
    return parseDocument(text, registry, kPrefabKind, prefabs);
}

SceneLoadResult loadSceneFile(const std::filesystem::path& path, const ComponentRegistry& registry,
                              const PrefabTexts& prefabs)
{
    return loadFile(path, registry, kSceneKind, prefabs);
}

SceneLoadResult loadPrefabFile(const std::filesystem::path& path, const ComponentRegistry& registry,
                               const PrefabTexts& prefabs)
{
    return loadFile(path, registry, kPrefabKind, prefabs);
}

// --- Writing ---------------------------------------------------------------------------------------

std::string serializeScene(const Scene& scene, const PrefabTexts& prefabs)
{
    return serializeDocument(scene, kSceneKind, prefabs);
}

std::string serializePrefab(const Scene& scene, EntityId root, const PrefabTexts& prefabs)
{
    if (!scene.contains(root)) {
        return {};
    }
    // A scene of its own holding just that object: then it is written as
    // any scene is. (The object keeps its IDs in the file, so saving the
    // same prefab again changes only what was changed; whoever puts a
    // prefab into a scene gives the copy new ones.)
    Scene alone("Prefab");
    alone.restore(scene.capture(root), EntityId{}, 0);
    // The object itself is always written in full, also when it is a copy
    // of another prefab: a prefab file starts with an object, not a link.
    // Linked copies *inside* it stay links (ADR 0036).
    alone.setPrefabLink(root, PrefabLink{});
    return serializeDocument(alone, kPrefabKind, prefabs);
}

bool prefabHolds(const PrefabTexts& prefabs, AssetId outer, AssetId inner)
{
    if (!prefabs) {
        return outer == inner;
    }
    std::vector<AssetId> todo{outer};
    std::vector<AssetId> seen;
    while (!todo.empty()) {
        const AssetId id = todo.back();
        todo.pop_back();
        if (id == inner) {
            return true;
        }
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) {
            continue;
        }
        seen.push_back(id);
        const std::optional<std::string> text = prefabs(id);
        const Json document = text ? Json::parse(*text, nullptr, false) : Json();
        if (!document.is_object() || !document.contains("entities") || !document["entities"].is_array()) {
            continue;
        }
        for (const Json& e : document["entities"]) {
            if (const auto held = AssetId::parse(textOf(e, "prefab"))) {
                todo.push_back(*held);
            }
        }
    }
    return false;
}

EntitySnapshot linkedCopyOf(Scene& scene, const Scene& prefab, AssetId asset)
{
    if (prefab.roots().size() != 1 || asset.isNull()) {
        return {};
    }
    EntitySnapshot taken = prefab.capture(prefab.roots().front());
    for (std::size_t i = 0; i < taken.items.size(); ++i) {
        PrefabLink link;
        link.source = taken.items[i].id;
        if (i == 0) {
            link.prefab = asset;
        }
        taken.items[i].link = std::move(link);
    }
    return scene.withNewIds(taken);
}

namespace {

// One object as it is written when it is an ordinary one.
Json entityJson(const Entity& e)
{
    Json entity;
    entity["id"] = e.id().toString();
    if (!e.parent().isNull()) {
        entity["parent"] = e.parent().toString();
    }
    entity["name"] = e.name();
    entity["active"] = e.isActive();
    Json components = Json::array();
    for (const auto& c : e.components()) {
        if (const auto* unknown = dynamic_cast<const UnknownComponent*>(c.get())) {
            components.push_back(Json::parse(unknown->json()));
            continue;
        }
        Json component;
        component["type"] = c->type().id;
        for (const PropertyInfo& p : c->type().properties) {
            if (p.options.data && std::get<std::string>(p.get(*c)).empty()) {
                continue;   // no values: nothing to write (a missing key reads as none)
            }
            component[p.key] = toJson(p, p.get(*c));
        }
        if (const std::string data = c->saveData(); !data.empty()) {
            component["data"] = Json::parse(data);
        }
        components.push_back(std::move(component));
    }
    entity["components"] = std::move(components);
    return entity;
}

// What an object of a linked copy (`object`, written out as `mine`) has
// that the prefab's object (`theirs`, with this scene's IDs in it) has
// not: added to `changes`.
void writeChanges(const Entity& object, const Json& mine, const Json& theirs, Json& changes)
{
    if (mine["name"] != theirs.value("name", Json(""))) {
        changes["name"] = mine["name"];
    }
    const bool theirActive = theirs.contains("active") && theirs["active"].is_boolean() ? theirs["active"].get<bool>() : true;
    if (mine["active"].get<bool>() != theirActive) {
        changes["active"] = mine["active"];
    }
    Json changed = Json::array();
    std::unordered_set<std::string> have;
    for (const Json& component : mine["components"]) {
        const std::string type = textOf(component, "type");
        have.insert(type);
        const Json* same = nullptr;
        if (theirs.contains("components") && theirs["components"].is_array()) {
            for (const Json& c : theirs["components"]) {
                if (textOf(c, "type") == type) {
                    same = &c;
                    break;
                }
            }
        }
        if (same == nullptr) {
            changed.push_back(component);   // the copy's own component
            continue;
        }
        // Key by key. The prefab's value is read and written the way this
        // object's is before comparing (0.5 against 0.50000001, a choice by
        // an older name), so that only real differences count.
        const Component* held = object.component(type);
        Json difference;
        difference["type"] = type;
        Json unset = Json::array();
        for (const auto& [key, value] : component.items()) {
            if (key == "type") {
                continue;
            }
            if (!same->contains(key)) {
                difference[key] = value;
                continue;
            }
            Json other = (*same)[key];
            const PropertyInfo* info = held != nullptr ? held->type().findProperty(key) : nullptr;
            if (info != nullptr) {
                std::string expected;
                if (const auto parsed = fromJson(*info, other, expected)) {
                    other = toJson(*info, info->coerce(*parsed).value_or(*parsed));
                }
            }
            if (other == value) {
                continue;
            }
            if (info != nullptr && info->options.data && value.is_object() && other.is_object()) {
                // Named values (a Script's properties): name by name, so
                // that a copy with its own speed still follows the prefab's
                // other values.
                Json named;
                named["$changed"] = Json::object();
                Json gone = Json::array();
                for (const auto& [name, held2] : value.items()) {
                    if (!other.contains(name) || other[name] != held2) {
                        named["$changed"][name] = held2;
                    }
                }
                for (const auto& [name, held2] : other.items()) {
                    if (!value.contains(name)) {
                        gone.push_back(name);
                    }
                }
                if (!gone.empty()) {
                    named["$unset"] = std::move(gone);
                }
                difference[key] = std::move(named);
                continue;
            }
            difference[key] = value;
        }
        for (const auto& [key, value] : same->items()) {
            if (key != "type" && !component.contains(key)) {
                unset.push_back(key);
            }
        }
        if (!unset.empty()) {
            difference["$unset"] = std::move(unset);
        }
        if (difference.size() > 1) {
            changed.push_back(std::move(difference));
        }
    }
    if (!changed.empty()) {
        changes["components"] = std::move(changed);
    }
    Json removed = Json::array();
    if (theirs.contains("components") && theirs["components"].is_array()) {
        for (const Json& c : theirs["components"]) {
            if (const std::string type = textOf(c, "type"); !type.empty() && have.count(type) == 0) {
                removed.push_back(type);
            }
        }
    }
    if (!removed.empty()) {
        changes["removedComponents"] = std::move(removed);
    }
}

// The entry for the linked copy whose top object is `root`, and which
// objects it covers (they are not written again as ordinary ones).
// Nothing if the prefab cannot be read: the copy is then written out as
// ordinary objects, which keeps everything but the link.
std::optional<Json> linkedCopyJson(const Scene& scene, const Entity& root, const PrefabTexts& prefabs,
                                   std::unordered_set<EntityId>& covered)
{
    std::string why;
    const std::optional<Json> prefab = prefabEntities(prefabs, root.prefabLink().prefab, why);
    if (!prefab) {
        return std::nullopt;
    }
    // Which object of the scene is which object of the prefab: by the mark
    // each carries, and only where it still sits where the prefab has it.
    const std::string prefabRoot = textOf(prefab->front(), "id");
    std::unordered_map<std::string, const Entity*> memberOf{{prefabRoot, &root}};
    std::unordered_map<std::string, std::string> ids{{prefabRoot, root.id().toString()}};
    Json removed = Json::array();
    for (const Json& p : *prefab) {
        const std::string id = textOf(p, "id");
        if (id == prefabRoot) {
            continue;
        }
        const auto parent = memberOf.find(textOf(p, "parent"));
        if (parent == memberOf.end()) {
            continue;   // inside something this copy has not: gone with it
        }
        const Entity* found = nullptr;
        for (const EntityId child : parent->second->children()) {
            const Entity* c = scene.find(child);
            if (c != nullptr && c->prefabLink().source.toString() == id && covered.count(c->id()) == 0) {
                found = c;
                break;
            }
        }
        if (found == nullptr) {
            removed.push_back(id);
            continue;
        }
        memberOf[id] = found;
        ids[id] = found->id().toString();
        covered.insert(found->id());
    }

    Json entry;
    entry["id"] = root.id().toString();
    if (!root.parent().isNull()) {
        entry["parent"] = root.parent().toString();
    }
    entry["prefab"] = root.prefabLink().prefab.toString();
    Json objects = Json::array();
    for (const Json& p : *prefab) {
        const std::string id = textOf(p, "id");
        const auto member = memberOf.find(id);
        if (member == memberOf.end()) {
            continue;
        }
        Json theirs = p;
        pointInsideTheCopy(theirs, ids);
        const Json mine = entityJson(*member->second);
        if (id == prefabRoot) {
            writeChanges(*member->second, mine, theirs, entry);
        } else {
            Json object;
            object["from"] = id;
            object["id"] = ids[id];
            writeChanges(*member->second, mine, theirs, object);
            objects.push_back(std::move(object));
        }
    }
    if (!objects.empty()) {
        entry["objects"] = std::move(objects);
    }
    if (!removed.empty()) {
        entry["removed"] = std::move(removed);
    }
    return entry;
}

std::string serializeDocument(const Scene& scene, const FileKind& kind, const PrefabTexts& prefabs)
{
    Json document;
    document["format"] = kind.format;
    document["version"] = 1;   // (2 or 3 below, if the file needs it)
    if (!kind.prefab) {
        document["name"] = scene.name();
        // Written only when changed, so scenes without physics stay as they were.
        if (scene.gravity() != Scene::kDefaultGravity) {
            document["gravity"] = Json::array({number(scene.gravity().x), number(scene.gravity().y)});
        }
    }
    Json entities = Json::array();
    std::unordered_set<EntityId> covered;   // written inside a linked copy's entry
    bool linkedCopies = false;
    scene.forEach([&](const Entity& e, int) {
        if (covered.count(e.id()) != 0) {
            return;
        }
        const PrefabLink& link = e.prefabLink();
        if (!link.kept.empty()) {
            // A stand-in for a copy whose prefab could not be read: back
            // into the file as it came, with its own stand-ins inside.
            Json kept = Json::parse(link.kept, nullptr, false);
            if (kept.is_object()) {
                // (Set in place: the entry keeps its order, and so its text.)
                if (e.parent().isNull()) {
                    kept.erase("parent");
                } else {
                    kept["parent"] = e.parent().toString();
                }
                for (const EntityId child : e.children()) {
                    const Entity* c = scene.find(child);
                    if (c != nullptr && !c->prefabLink().source.isNull() && c->components().size() <= 1) {
                        covered.insert(child);
                    }
                }
                entities.push_back(std::move(kept));
                linkedCopies = true;
                return;
            }
        }
        if (prefabs && e.isPrefabCopy()) {
            if (auto entry = linkedCopyJson(scene, e, prefabs, covered)) {
                entities.push_back(std::move(*entry));
                linkedCopies = true;
                return;
            }
        }
        entities.push_back(entityJson(e));
    });
    // A newer version only where it is needed: a file without linked copies
    // is a version 1 file still, and stays readable by older Rolo. Links in
    // a scene came with version 2, links in a prefab with version 3.
    if (linkedCopies) {
        document["version"] = kind.prefab ? 3 : 2;
    }
    document["entities"] = std::move(entities);
    return pretty(document);
}

} // namespace

Status saveSceneFile(const std::filesystem::path& path, const Scene& scene, const PrefabTexts& prefabs)
{
    return writeTextFileAtomically(path, serializeScene(scene, prefabs));
}

// --- Validation -------------------------------------------------------------------------------------

std::vector<FileIssue> validateScene(const Scene& scene)
{
    std::vector<FileIssue> list;
    Issues issues{list};
    std::unordered_set<EntityId> seen;

    scene.forEach([&](const Entity& e, int) {
        const std::string where = describeEntity(e.name(), e.id().toString());
        if (!seen.insert(e.id()).second) {
            issues.error(where, "The entity appears twice in the tree.");
            return;
        }
        if (e.get<Transform>() == nullptr) {
            issues.error(where, "The entity has no Transform.");
        }
        for (EntityId child : e.children()) {
            const Entity* c = scene.find(child);
            if (c == nullptr) {
                issues.error(where, "A child entity is missing.");
            } else if (c->parent() != e.id()) {
                issues.error(where, "A child entity names a different parent.");
            }
        }
        std::unordered_set<std::string> types;
        for (const auto& c : e.components()) {
            const auto* unknown = dynamic_cast<const UnknownComponent*>(c.get());
            const std::string typeId = unknown != nullptr ? unknown->originalTypeId() : c->type().id;
            if (!types.insert(typeId).second) {
                issues.error(where, "The entity has two " + typeId + " components.");
            }
            for (const PropertyInfo& p : c->type().properties) {
                const PropertyValue v = p.get(*c);
                const bool finite = std::visit(
                    [](const auto& x) {
                        using T = std::decay_t<decltype(x)>;
                        if constexpr (std::is_same_v<T, float>) {
                            return std::isfinite(x);
                        } else if constexpr (std::is_same_v<T, Vec2>) {
                            return std::isfinite(x.x) && std::isfinite(x.y);
                        } else {
                            return true;
                        }
                    },
                    v);
                if (!finite) {
                    issues.error(where + ", component " + typeId, "\"" + p.key + "\" is not a valid number.");
                }
            }
        }
    });
    for (EntityId root : scene.roots()) {
        if (const Entity* r = scene.find(root); r != nullptr && !r->parent().isNull()) {
            issues.error(describeEntity(r->name(), root.toString()), "A top-level entity names a parent.");
        }
    }
    if (seen.size() != scene.size()) {
        issues.error("", std::to_string(scene.size() - std::min(scene.size(), seen.size()))
                             + " entities are not reachable from the top of the scene.");
    }
    return list;
}

} // namespace rolo
