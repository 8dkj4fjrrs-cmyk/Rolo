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
                               const ComponentRegistry& registry)
{
    Scene work(scene);
    const SceneLoadResult old = parsePrefab(oldText, registry);
    if (old.ok()) {
        const Vec2 where = old.scene->find(old.scene->roots().front())->transform().position;
        work.setProperty(root, "rolo.Transform", "position", where);
    }
    std::string text = serializePrefab(work, root);
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

} // namespace rolo::editor
