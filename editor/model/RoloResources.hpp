#pragma once

// Rolo's own files that the editor uses: project templates, the example
// games, license texts, the documentation, and the game runtime that
// exported games are made from.
//
// They are found from where the editor program is:
//
//   an installed copy (a download)       Rolo-<version>/rolo_editor
//                                        Rolo-<version>/templates, examples, licenses, docs
//                                        Rolo-<version>/runtime/rolo_player
//   the same on macOS                    Rolo Editor.app/Contents/Resources/rolo/...
//                                        .../rolo/runtime/rolo_player.app
//   a build folder (developing Rolo)  the source folder's templates/, examples/, ...
//                                        and the rolo_player built beside the editor
//
// See docs/releasing.md for how an installed copy is made.

#include "EditorPaths.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace rolo::editor {

struct RoloResources {
    std::filesystem::path root;        // the folder they were found in (for messages)
    std::filesystem::path templates;   // project templates, one folder each
    std::filesystem::path examples;    // example games
    std::filesystem::path licenses;    // license texts shipped with every game
    std::filesystem::path docs;
    std::filesystem::path player;      // rolo_player(.exe), or rolo_player.app on macOS; empty if unknown
    /// The web runtime's folder (index.html, rolo_player.js, .wasm, .data),
    /// for exporting games for the web: "web-runtime" beside the other
    /// resources when installed; in a source tree, where a web build puts it
    /// (build/web/bin/rolo_player). May not exist: exporting says so.
    std::filesystem::path webPlayer;
    bool installed = false;            // false: running from a build folder
};

/// Finds the resources for an editor program at `executable`. `sourceDir`
/// and `devPlayer` describe the build it came from (used when the program
/// is not in an installed copy).
RoloResources findRoloResources(const std::filesystem::path& executable, HostOs os,
                                      const std::filesystem::path& sourceDir,
                                      const std::filesystem::path& devPlayer);

/// A starting point for a new project: a template or an example game.
struct ProjectTemplate {
    std::string id;            // the folder's name, e.g. "topdown"
    std::string name;          // shown in the wizard, e.g. "Top-Down Starter"
    std::string description;
    std::filesystem::path project;   // the project folder to copy
    bool example = false;      // an example game rather than a template
    int order = 100;           // position in the list (lower first)
};

/// The templates and example games in `resources`. Each is a folder holding
/// `template.json` ({"name", "description", "order"}) and the `project/`
/// folder to copy. Sorted: templates first, then examples, each by order.
std::vector<ProjectTemplate> listProjectTemplates(const RoloResources& resources);

/// Makes a new project in `folder` (which must not exist, or be empty) as a
/// copy of the template's project, named `name`. Nothing (and `error` set)
/// if it could not.
bool createProjectFromTemplate(const ProjectTemplate& from, const std::filesystem::path& folder,
                               const std::string& name, std::string* error = nullptr);

} // namespace rolo::editor
