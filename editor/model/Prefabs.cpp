#include "Prefabs.hpp"

#include <functional>
#include <unordered_set>

namespace rolo::editor {

namespace {

// Calls `visit` for `id` and everything inside it, top first.
void forEachInside(const Scene& scene, EntityId id, const std::function<void(const Entity&)>& visit)
{
    const Entity* e = scene.find(id);
    if (e == nullptr) {
        return;
    }
    visit(*e);
    for (const EntityId child : e->children()) {
        forEachInside(scene, child, visit);
    }
}

void replaceAll(std::string& text, const std::string& from, const std::string& to)
{
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

} // namespace

std::unordered_map<EntityId, EntityId> prefabCopyMembers(const Scene& scene, EntityId root)
{
    std::unordered_map<EntityId, EntityId> members;
    const Entity* top = scene.find(root);
    if (top == nullptr || !top->isPrefabCopy()) {
        return members;
    }
    members[root] = top->prefabLink().source;
    // An object is the prefab's if it is marked so and sits inside one that
    // is; of two with the same mark under one parent (one was duplicated),
    // the first.
    const std::function<void(const Entity&)> inside = [&](const Entity& parent) {
        std::unordered_set<EntityId> taken;
        for (const EntityId childId : parent.children()) {
            const Entity* child = scene.find(childId);
            if (child == nullptr) {
                continue;
            }
            const EntityId source = child->prefabLink().source;
            if (source.isNull() || child->isPrefabCopy() || !taken.insert(source).second) {
                continue;
            }
            members[childId] = source;
            inside(*child);
        }
    };
    inside(*top);
    return members;
}

std::vector<std::pair<EntityId, PrefabLink>> linksAfterSaving(const Scene& scene, EntityId root, AssetId asset,
                                                              bool applied)
{
    // Applied: the objects that were the prefab's keep which they are.
    // Everything else is in the file under its own ID.
    const std::unordered_map<EntityId, EntityId> members =
        applied ? prefabCopyMembers(scene, root) : std::unordered_map<EntityId, EntityId>{};
    std::vector<std::pair<EntityId, PrefabLink>> links;
    forEachInside(scene, root, [&](const Entity& e) {
        PrefabLink link;
        const auto member = members.find(e.id());
        link.source = member != members.end() ? member->second : e.id();
        if (e.id() == root) {
            link.prefab = asset;
        }
        links.emplace_back(e.id(), std::move(link));
    });
    return links;
}

std::string prefabTextFromCopy(const Scene& scene, EntityId root, const std::string& oldText,
                               const ComponentRegistry& registry, const PrefabTexts& prefabs)
{
    Scene work(scene);
    const SceneLoadResult old = parsePrefab(oldText, registry, prefabs);
    if (old.ok()) {
        const Vec2 where = old.scene->find(old.scene->roots().front())->transform().position;
        work.setProperty(root, "rolo.Transform", "position", where);
        // A part the prefab has as a linked copy of another prefab stays
        // one (ADR 0036). In a scene such a part is simply the copy's; the
        // prefab as it was knows which of its objects came from where.
        for (const auto& [here, inPrefab] : prefabCopyMembers(scene, root)) {
            const Entity* theirs = old.scene->find(inPrefab);
            work.setPrefabLink(here, here == root || theirs == nullptr ? PrefabLink{} : theirs->prefabLink());
        }
    }
    std::string text = serializePrefab(work, root, prefabs);
    // In the file the prefab's objects have the prefab's IDs, wherever an ID
    // is written: as an object's own, as a parent, in a property that points
    // at an object. (IDs are sixteen random digits in quotes: replacing the
    // text cannot hit anything else.)
    for (const auto& [here, inPrefab] : prefabCopyMembers(scene, root)) {
        if (here != inPrefab) {
            replaceAll(text, "\"" + here.toString() + "\"", "\"" + inPrefab.toString() + "\"");
        }
    }
    return text;
}

std::optional<Scene> sceneAfterApply(const Scene& scene, EntityId applied, AssetId asset, const std::string& oldText,
                                     const std::string& newText, const ComponentRegistry& registry,
                                     const PrefabTexts& others, std::string& error)
{
    const Entity* top = scene.find(applied);
    if (top == nullptr || asset.isNull()) {
        error = "there is no such copy";
        return std::nullopt;
    }
    // The scene is written against the prefab as it was, which records what
    // every copy has of its own, and read again against the prefab as it is
    // now, which lays that over the new prefab. The applied copy is the one
    // exception: it *is* the new prefab, so it is written against the new
    // one, under a stand-in ID that only this function knows.
    const AssetId standIn{~asset.value() == 0 ? 1 : ~asset.value()};
    const EntityId source = top->prefabLink().source.isNull() ? applied : top->prefabLink().source;
    Scene work(scene);
    for (auto& [id, link] : linksAfterSaving(scene, applied, standIn, true)) {
        work.setPrefabLink(id, std::move(link));
    }
    const PrefabTexts before = [&](AssetId id) -> std::optional<std::string> {
        if (id == standIn) {
            return newText;
        }
        if (id == asset) {
            return oldText;
        }
        return others ? others(id) : std::nullopt;
    };
    const PrefabTexts after = [&](AssetId id) -> std::optional<std::string> {
        if (id == standIn || id == asset) {
            return newText;
        }
        return others ? others(id) : std::nullopt;
    };
    SceneLoadResult made = parseScene(serializeScene(work, before), registry, after);
    if (!made.ok()) {
        error = made.firstError();
        return std::nullopt;
    }
    PrefabLink link;
    link.prefab = asset;
    link.source = source;
    made.scene->setPrefabLink(applied, std::move(link));
    return std::move(*made.scene);
}

bool applyWouldLoop(const Scene& scene, EntityId root, const PrefabTexts& prefabs)
{
    const Entity* top = scene.find(root);
    if (top == nullptr || !top->isPrefabCopy()) {
        return false;
    }
    const AssetId asset = top->prefabLink().prefab;
    bool loop = false;
    forEachInside(scene, root, [&](const Entity& e) {
        loop = loop || (e.id() != root && e.isPrefabCopy() && prefabHolds(prefabs, e.prefabLink().prefab, asset));
    });
    return loop;
}

std::vector<std::pair<EntityId, PrefabLink>> linksAfterUnlinking(const Scene& scene, EntityId root, const Scene* prefab)
{
    std::vector<std::pair<EntityId, PrefabLink>> links;
    for (const auto& [member, inPrefab] : prefabCopyMembers(scene, root)) {
        const Entity* theirs = prefab != nullptr && member != root ? prefab->find(inPrefab) : nullptr;
        // (A stand-in's kept text is the prefab's, with the prefab's IDs:
        // it has no place in a scene.)
        links.emplace_back(member, theirs != nullptr && theirs->prefabLink().kept.empty() ? theirs->prefabLink()
                                                                                         : PrefabLink{});
    }
    return links;
}

EntitySnapshot copyReset(Scene& scene, EntityId root, const Scene& prefab)
{
    const Entity* top = scene.find(root);
    if (top == nullptr || !top->isPrefabCopy() || prefab.roots().size() != 1) {
        return {};
    }
    // Which ID each object of the prefab gets: the one its object in this
    // copy has (so that what refers to it still does), or a new one.
    std::unordered_map<EntityId, EntityId> here;
    for (const auto& [id, inPrefab] : prefabCopyMembers(scene, root)) {
        here[inPrefab] = id;
    }
    EntitySnapshot taken = prefab.capture(prefab.roots().front());
    std::unordered_map<EntityId, EntityId> ids;
    for (std::size_t i = 0; i < taken.items.size(); ++i) {
        EntitySnapshot::Item& item = taken.items[i];
        PrefabLink link;
        link.source = item.id;
        if (i == 0) {
            link.prefab = top->prefabLink().prefab;
            ids[item.id] = root;
        } else if (const auto known = here.find(item.id); known != here.end()) {
            ids[item.id] = known->second;
        }
        item.link = std::move(link);
    }
    EntitySnapshot fresh = scene.withIds(taken, ids);
    for (const auto& component : fresh.items.front().components) {
        if (auto* transform = dynamic_cast<Transform*>(component.get())) {
            transform->position = top->transform().position;   // where a copy stands is its own
        }
    }
    return fresh;
}

EntityId copyRootOf(const Scene& scene, EntityId object)
{
    for (const Entity* e = scene.find(object); e != nullptr; e = scene.find(e->parent())) {
        if (e->isPrefabCopy()) {
            // The nearest copy above it decides: inside a copy, an object
            // is the prefab's or it was put there in this scene.
            return prefabCopyMembers(scene, e->id()).count(object) != 0 ? e->id() : EntityId{};
        }
    }
    return {};
}

std::string CopyDifferences::summary() const
{
    std::vector<std::string> parts;
    const auto count = [&parts](std::size_t n, const char* one, const char* many) {
        if (n > 0) {
            parts.push_back(std::to_string(n) + " " + (n == 1 ? one : many));
        }
    };
    count(fields.size() + values.size(), "field", "fields");
    count(added.size(), "component added", "components added");
    count(removed.size(), "component removed", "components removed");
    count(changedData.size(), "component's contents", "components' contents");
    if (name) {
        parts.emplace_back("its name");
    }
    if (active) {
        parts.emplace_back("on / off");
    }
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        text += (i == 0 ? "" : ", ") + parts[i];
    }
    return text;
}

std::optional<CopyDifferences> differencesFromPrefab(const Scene& scene, EntityId object, const Scene& prefab)
{
    const EntityId root = copyRootOf(scene, object);
    const Entity* mine = scene.find(object);
    if (root.isNull() || mine == nullptr) {
        return std::nullopt;
    }
    const std::unordered_map<EntityId, EntityId> members = prefabCopyMembers(scene, root);
    const Entity* theirs = prefab.find(members.at(object));
    if (theirs == nullptr) {
        return std::nullopt;
    }
    CopyDifferences d;
    d.root = root;
    d.prefab = scene.find(root)->prefabLink().prefab;
    d.top = root == object;
    for (const Entity* e = theirs; e != nullptr; e = prefab.find(e->parent())) {
        if (e->isPrefabCopy()) {
            d.within = e->prefabLink().prefab;
            break;
        }
    }
    if (mine->name() != theirs->name()) {
        d.name = theirs->name();
    }
    if (mine->isActive() != theirs->isActive()) {
        d.active = theirs->isActive();
    }
    // A value of the prefab's that points at one of its objects points, in
    // this copy, at the copy's object.
    std::unordered_map<std::string, std::string> here;
    for (const auto& [id, inPrefab] : members) {
        here[inPrefab.toString()] = id.toString();
    }
    for (const auto& component : mine->components()) {
        const std::string& type = component->type().id;
        const Component* other = theirs->component(type);
        if (other == nullptr) {
            d.added.push_back(type);
            continue;
        }
        for (const PropertyInfo& info : component->type().properties) {
            if (!info.get) {
                continue;
            }
            const PropertyValue value = info.get(*component);
            const PropertyValue theirValue = info.get(*other);
            if (info.options.data) {
                const DataValues own = parseDataValues(std::get<std::string>(value));
                DataValues prefabs = parseDataValues(std::get<std::string>(theirValue));
                for (auto& [name, held] : prefabs) {
                    if (const std::string* text = std::get_if<std::string>(&held)) {
                        if (const auto mapped = here.find(*text); mapped != here.end()) {
                            held = mapped->second;
                        }
                    }
                }
                for (const auto& [name, held] : own) {
                    const auto same = prefabs.find(name);
                    if (same == prefabs.end()) {
                        d.values[name] = std::nullopt;
                    } else if (same->second != held) {
                        d.values[name] = same->second;
                    }
                }
                for (const auto& [name, held] : prefabs) {
                    if (own.count(name) == 0) {
                        d.values[name] = held;
                    }
                }
                d.valuesIn = {type, info.key};
                continue;
            }
            if (value == theirValue) {
                continue;
            }
            if (d.top && type == Transform::componentType().id && info.key == "position") {
                continue;   // where a copy stands is always its own
            }
            d.fields[{type, info.key}] = theirValue;
        }
        if (component->saveData() != other->saveData()) {
            d.changedData.push_back(type);
        }
    }
    for (const auto& component : theirs->components()) {
        if (mine->component(component->type().id) == nullptr) {
            d.removed.push_back(component->type().id);
        }
    }
    return d;
}

std::string valuesWithNamePutBack(const std::string& values, const std::string& name,
                                  const std::optional<DataValue>& theirs)
{
    DataValues all = parseDataValues(values);
    if (theirs) {
        all[name] = *theirs;
    } else {
        all.erase(name);
    }
    return writeDataValues(all);
}

} // namespace rolo::editor
