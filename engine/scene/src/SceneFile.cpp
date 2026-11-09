#include "rolo/scene/SceneFile.hpp"
#include "rolo/core/Names.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/core/Numbers.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
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
    case PropertyKind::String: return std::get<std::string>(value);
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
// Version 1 is the first format, so there is nothing to upgrade yet.
using Upgrade = void (*)(Json& document);
constexpr Upgrade kUpgrades[] = {nullptr};
static_assert(std::size(kUpgrades) == SceneFormat::kVersion, "one upgrade step per older version");

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

SceneLoadResult parseScene(std::string_view text, const ComponentRegistry& registry)
{
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

    if (!document.is_object() || !isFormatName(document.value("format", ""), SceneFormat::kName)) {
        issues.error("", "This is not a Rolo scene file (\"format\" should be \"rolo.scene\").");
        return result;
    }
    if (!document.contains("version") || !document["version"].is_number_integer()) {
        issues.error("", "The scene file has no format \"version\".");
        return result;
    }
    const int version = document["version"].get<int>();
    if (version > SceneFormat::kVersion) {
        issues.error("", "This scene was saved by a newer version of Rolo (scene format " + std::to_string(version)
                             + "; this version reads up to " + std::to_string(SceneFormat::kVersion)
                             + "). Update Rolo to open it.");
        return result;
    }
    if (version < 1) {
        issues.error("", "Unknown scene format version " + std::to_string(version) + ".");
        return result;
    }
    for (int v = version; v < SceneFormat::kVersion; ++v) {
        kUpgrades[v - 1](document);
    }

    if (!document.contains("entities") || !document["entities"].is_array()) {
        issues.error("", "The scene file has no \"entities\" list.");
        return result;
    }

    Scene scene(document.value("name", std::string("Untitled")));
    if (!document.contains("name") || !document["name"].is_string()) {
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

    std::size_t index = 0;
    for (const Json& e : document["entities"]) {
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
        for (const auto& [key, value] : e.items()) {
            if (key != "id" && key != "parent" && key != "name" && key != "active" && key != "components") {
                issues.warning(where, "Unknown field \"" + key + "\" was ignored.");
            }
        }
    }
    for (const auto& [key, value] : document.items()) {
        if (key != "format" && key != "version" && key != "name" && key != "gravity" && key != "entities") {
            issues.warning("", "Unknown field \"" + key + "\" was ignored.");
        }
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

SceneLoadResult loadSceneFile(const std::filesystem::path& path, const ComponentRegistry& registry)
{
    const auto text = readTextFile(path);
    if (!text) {
        SceneLoadResult result;
        result.issues.push_back({FileIssue::Severity::Error, path.string(), "The file could not be read."});
        return result;
    }
    return parseScene(*text, registry);
}

// --- Writing ---------------------------------------------------------------------------------------

std::string serializeScene(const Scene& scene)
{
    Json document;
    document["format"] = SceneFormat::kName;
    document["version"] = SceneFormat::kVersion;
    document["name"] = scene.name();
    // Written only when changed, so scenes without physics stay as they were.
    if (scene.gravity() != Scene::kDefaultGravity) {
        document["gravity"] = Json::array({number(scene.gravity().x), number(scene.gravity().y)});
    }
    Json entities = Json::array();
    scene.forEach([&](const Entity& e, int) {
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
                component[p.key] = toJson(p, p.get(*c));
            }
            if (const std::string data = c->saveData(); !data.empty()) {
                component["data"] = Json::parse(data);
            }
            components.push_back(std::move(component));
        }
        entity["components"] = std::move(components);
        entities.push_back(std::move(entity));
    });
    document["entities"] = std::move(entities);
    return pretty(document);
}

Status saveSceneFile(const std::filesystem::path& path, const Scene& scene)
{
    return writeTextFileAtomically(path, serializeScene(scene));
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
