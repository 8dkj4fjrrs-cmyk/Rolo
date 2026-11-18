#pragma once

// What the editor does with linked copies of prefabs (ADR 0035), as plain
// functions on scenes and texts, so that it is tested without a window.
//
// A linked copy is an ordinary object in the scene that remembers which
// prefab file it follows (PrefabLink). These work out which of its objects
// are the prefab's, what the prefab file must say for the prefab to become
// what a copy is now ("Apply to Prefab"), and what a scene looks like after
// its prefab has changed.

#include "rolo/scene/SceneFile.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace rolo::editor {

/// The objects of the linked copy whose top object is `root` that come from
/// its prefab: for each, which object of the prefab file it is. `root` is
/// among them. (What was put into the copy in this scene is not.)
std::unordered_map<EntityId, EntityId> prefabCopyMembers(const Scene& scene, EntityId root);

/// The links that make `root` and everything inside it a linked copy of the
/// prefab `asset`, when the prefab file was just written from that object:
/// for Scene::setPrefabLink. `applied`: the file was written with
/// prefabTextFromCopy (the prefab's objects keep the prefab's IDs); else
/// with serializePrefab (a new prefab: every object is in it under its own ID).
std::vector<std::pair<EntityId, PrefabLink>> linksAfterSaving(const Scene& scene, EntityId root, AssetId asset,
                                                              bool applied = false);

/// The text the prefab file must have for the prefab to be what the copy
/// `root` is now, with everything inside it. `oldText` is the prefab as it
/// is: objects keep the IDs they have in it (other copies know them by
/// those), and the top object stays where the prefab had it (where a copy
/// stands is always the copy's own).
std::string prefabTextFromCopy(const Scene& scene, EntityId root, const std::string& oldText,
                               const ComponentRegistry& registry);

/// `scene` after its prefab `asset` changed from `oldText` to `newText`
/// because the copy `applied` was applied to it: every other copy of the
/// prefab keeps its own changes and takes the rest from the new prefab,
/// and `applied` is as it was. `others` reads the project's other prefabs.
/// Nothing, with `error`, if the result could not be made.
std::optional<Scene> sceneAfterApply(const Scene& scene, EntityId applied, AssetId asset, const std::string& oldText,
                                     const std::string& newText, const ComponentRegistry& registry,
                                     const PrefabTexts& others, std::string& error);

/// The copy `root` as its prefab (`prefab`, as parsePrefab gives it) has it,
/// to put in its place: its own changes and what was put inside it are
/// dropped, but it stays where it stands, and it and the objects it keeps
/// have the IDs they had. Empty if `root` is not a linked copy.
EntitySnapshot copyReset(Scene& scene, EntityId root, const Scene& prefab);

} // namespace rolo::editor
