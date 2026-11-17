#pragma once

// Reading and writing scene files (.lscene) and checking scenes for problems.
//
// A scene file is UTF-8 JSON with an explicit format name and version:
//
//   {
//     "format": "rolo.scene",
//     "version": 1,
//     "name": "Start Scene",
//     "entities": [
//       { "id": "3f9c2a7d10b84e65", "name": "Rolo", "active": true,
//         "components": [
//           { "type": "rolo.Transform", "position": [640, 360], "rotation": 0, "scale": [1, 1] } ] },
//       { "id": "91d0c3b2a4e5f607", "parent": "3f9c2a7d10b84e65", "name": "Flame", ... }
//     ]
//   }
//
// Entities are listed parents first; siblings keep their order (= drawing
// order). Colours are "#rrggbbaa", choices are saved by name. The full
// description is in docs/formats/scene.md.
//
// Loading is forgiving where it is safe: unknown properties are skipped with
// a warning, unknown component types are kept unchanged, bad values fall back
// to defaults with a warning. Problems that would lose or scramble data (a
// missing ID, a duplicate ID, a newer format version) stop loading with an
// error that says what to do.

#include "rolo/scene/Scene.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

struct FileIssue {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Warning;
    std::string where;     // e.g. "entity 'Flame' (91d0c3b2a4e5f607), component rolo.Shape"
    std::string message;   // what is wrong and, where possible, what to do

    bool isError() const noexcept { return severity == Severity::Error; }
    /// "Warning: <where>: <message>"
    std::string toString() const;
};

struct SceneLoadResult {
    std::optional<Scene> scene;       // empty if loading failed
    std::vector<FileIssue> issues;    // warnings (and the errors that stopped loading)

    bool ok() const noexcept { return scene.has_value(); }
    std::size_t warningCount() const;
    /// The first error's message, for a one-line report.
    std::string firstError() const;
};

namespace SceneFormat {
inline constexpr std::string_view kName = "rolo.scene";
inline constexpr int kVersion = 1;
inline constexpr std::string_view kExtension = ".lscene";
} // namespace SceneFormat

/// A prefab file (.lprefab): one object and everything inside it, kept as a
/// file of its own so that it can be put into any scene and copied by a
/// running game (game.spawn). It is written exactly as a scene is, with
/// another format name and no scene name or gravity:
///
///   { "format": "rolo.prefab", "version": 1, "entities": [ ... ] }
///
/// The first entity is the object (it has no parent), the others are inside
/// it. The version is the scene format's, and moves with it: one set of
/// upgrade steps serves both. See docs/formats/scene.md.
namespace PrefabFormat {
inline constexpr std::string_view kName = "rolo.prefab";
inline constexpr int kVersion = SceneFormat::kVersion;
inline constexpr std::string_view kExtension = ".lprefab";
} // namespace PrefabFormat

/// Reads a scene from JSON text.
SceneLoadResult parseScene(std::string_view json, const ComponentRegistry& registry);
/// Writes a scene as JSON text (2-space indented, short lists on one line,
/// stable order, ends with a newline).
std::string serializeScene(const Scene& scene);

SceneLoadResult loadSceneFile(const std::filesystem::path& path, const ComponentRegistry& registry);
/// Saves atomically (never leaves a half-written file).
Status saveSceneFile(const std::filesystem::path& path, const Scene& scene);

/// Reads a prefab: the result is a scene holding just the prefab's object
/// (scene.roots().front()) and what is inside it.
SceneLoadResult parsePrefab(std::string_view json, const ComponentRegistry& registry);
SceneLoadResult loadPrefabFile(const std::filesystem::path& path, const ComponentRegistry& registry);
/// Writes the object `root` of `scene`, with everything inside it, as a
/// prefab file's text. Empty if there is no such object.
std::string serializePrefab(const Scene& scene, EntityId root);

/// Checks a scene in memory: the tree is consistent, every entity has a
/// Transform, numbers are finite. Returns the problems found (empty = fine).
std::vector<FileIssue> validateScene(const Scene& scene);

} // namespace rolo
