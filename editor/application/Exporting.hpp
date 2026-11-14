#pragma once

// Checking and exporting a project, for the Export Game dialog and for
// `rolo_editor --project P --export OUT` (which runs without a window).

#include "../model/GameExport.hpp"
#include "../model/RoloResources.hpp"

#include "rolo/core/Log.hpp"
#include "rolo/scene/Component.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace rolo::editor {

/// What checking a project found (each problem is also written to the log).
struct ProjectCheck {
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::string startScene;   // the start scene's file, if the project could be read
};

/// Reads everything a game needs: the project file and start scene, every
/// scene (with its components, asset references and triggers), and the
/// story content. `registry` must know the game's behaviours.
ProjectCheck checkProject(const std::filesystem::path& folder, const ComponentRegistry& registry, const Logger& log);

/// The names of the components in the project's scenes that come from C++
/// game modules (not Rolo's own): what a runtime without those modules,
/// like the web one, cannot run.
std::vector<std::string> moduleComponentsUsed(const std::filesystem::path& folder, const ComponentRegistry& registry);

struct ExportRun {
    bool ok = false;
    std::string message;   // what happened, for the user
    ProjectCheck check;
    ExportResult result;
};

/// Checks the project, then exports it if there are no errors (warnings are
/// reported, not blocking).
ExportRun exportProject(const std::filesystem::path& project, const std::filesystem::path& output,
                        const std::string& gameName, const RoloResources& resources,
                        const ComponentRegistry& registry, const Logger& log,
                        ExportTarget target = ExportTarget::Desktop);

} // namespace rolo::editor
