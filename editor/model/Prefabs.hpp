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

#include "rolo/scene/Values.hpp"

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
///
/// A part the prefab has as a linked copy of another prefab is written as
/// one again (with what this copy changed in it), and so is a linked copy
/// that was put into this copy here; `prefabs` reads those prefabs.
std::string prefabTextFromCopy(const Scene& scene, EntityId root, const std::string& oldText,
                               const ComponentRegistry& registry, const PrefabTexts& prefabs = {});

/// Whether applying the copy `root` to its prefab would make a prefab that
/// holds itself: a copy of the same prefab, or of a prefab that holds it,
/// was put inside this copy.
bool applyWouldLoop(const Scene& scene, EntityId root, const PrefabTexts& prefabs);

/// The links that make the copy `root` ordinary objects again, for
/// Scene::setPrefabLink. A part its prefab (`prefab`, as parsePrefab gives
/// it; null if it cannot be read) has as a linked copy of another prefab
/// becomes a linked copy of that prefab.
std::vector<std::pair<EntityId, PrefabLink>> linksAfterUnlinking(const Scene& scene, EntityId root,
                                                                 const Scene* prefab);

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

/// The top object of the linked copy that `object` is one of the prefab's
/// objects of (itself, if it is a copy's top object). Null if it is not
/// part of a linked copy: an ordinary object, or one put into a copy here.
EntityId copyRootOf(const Scene& scene, EntityId object);

/// What one object of a linked copy has of its own, for the Inspector to
/// show and to put back one by one.
struct CopyDifferences {
    EntityId root;       // the copy's top object
    AssetId prefab;      // the prefab file it follows
    bool top = false;    // the object is that top object
    /// If the prefab has this object from another prefab it holds a copy
    /// of (ADR 0036): that prefab. Null for the prefab's very own objects.
    AssetId within;

    /// Properties whose value differs: (component type, property key) and
    /// the prefab's value, which putting the field back sets. Where the top
    /// object stands is always its own and is not listed.
    std::map<std::pair<std::string, std::string>, PropertyValue> fields;
    /// Named values (a Script's properties) that differ, by name, and the
    /// prefab's value: none if the prefab gives that name no value of its
    /// own. `valuesIn` is the component and property that hold them.
    std::map<std::string, std::optional<DataValue>> values;
    std::pair<std::string, std::string> valuesIn;
    /// Component types the object has and the prefab's has not, and the
    /// other way round; and components whose other data differs (a
    /// Tilemap's tiles), which can only be put back with the whole copy.
    std::vector<std::string> added;
    std::vector<std::string> removed;
    std::vector<std::string> changedData;
    /// The prefab's name and on/off, where the object's differ.
    std::optional<std::string> name;
    std::optional<bool> active;

    bool none() const
    {
        return fields.empty() && values.empty() && added.empty() && removed.empty() && changedData.empty() && !name
               && !active;
    }
    /// In a few words, for the Inspector: "2 fields, 1 component added".
    std::string summary() const;
};

/// What `object` has of its own compared with `prefab` (the copy's prefab,
/// as parsePrefab gives it). Nothing if `object` is not one of a linked
/// copy's objects from the prefab, or the prefab has no such object now.
std::optional<CopyDifferences> differencesFromPrefab(const Scene& scene, EntityId object, const Scene& prefab);

/// `values` (named values as text) with `name` put back to what the prefab
/// has (`theirs`; none: no value of its own).
std::string valuesWithNamePutBack(const std::string& values, const std::string& name,
                                  const std::optional<DataValue>& theirs);

} // namespace rolo::editor
