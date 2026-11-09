#pragma once

// A Rolo project: a folder containing a project file and the game's
// content.
//
//   My Game/
//     project.rolo        project settings (JSON)
//     scenes/
//       main.lscene          scenes (more folders arrive with assets in Milestone 4)
//
// Everything the project refers to is stored as a path relative to the
// project folder, written with forward slashes ("scenes/main.lscene"), so a
// project can be moved, shared or put in version control.

#include "rolo/core/Engine.hpp" // Status

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

struct ProjectSettings {
    std::string name = "My Game";
    std::string startScene = "scenes/main.lscene";   // relative to the project folder
    /// Escape closes the game at once. Off: Escape is an ordinary key the
    /// game can use (e.g. for a pause menu with a Quit button: game.quit()).
    bool escapeQuits = true;
    /// F5 saves to the "quick" slot and F9 loads it. Off: the game handles
    /// saving itself, and F5 and F9 are ordinary keys.
    bool quickSaveKeys = true;
};

namespace ProjectFormat {
inline constexpr std::string_view kName = "rolo.project";
inline constexpr int kVersion = 1;
inline constexpr std::string_view kFileName = "project.rolo";
/// What projects made with Lantern (Rolo's old name) call it. Still read;
/// saving a project writes project.rolo instead (rolo/core/Names.hpp).
inline constexpr std::string_view kLegacyFileName = "project.lantern";
inline constexpr std::string_view kScenesFolder = "scenes";
} // namespace ProjectFormat

/// The project file in `folder`: project.rolo, or Lantern's project.lantern
/// if only that exists. Empty if there is neither.
std::filesystem::path projectFileIn(const std::filesystem::path& folder);
/// True if `folder` holds a project (either file name).
bool isProjectFolder(const std::filesystem::path& folder);

class Project {
public:
    /// Opens the project in `folder` (the folder containing project.rolo).
    static std::optional<Project> open(const std::filesystem::path& folder, std::string* error = nullptr);

    /// Creates a new project folder `folder` with a project file and a first
    /// scene (the Rolo start scene). Fails if the folder exists and is not empty.
    static std::optional<Project> create(const std::filesystem::path& folder, std::string name,
                                         std::string* error = nullptr);

    const std::filesystem::path& folder() const noexcept { return m_folder; }
    const ProjectSettings& settings() const noexcept { return m_settings; }
    void setSettings(ProjectSettings settings) { m_settings = std::move(settings); }
    Status save() const;

    /// Absolute path of a project-relative path.
    std::filesystem::path absolute(std::string_view relative) const;
    /// The project-relative form of `path` ("scenes/main.lscene"), or nothing
    /// if `path` is outside the project folder.
    std::optional<std::string> relative(const std::filesystem::path& path) const;

    /// Every scene file in the project, as project-relative paths, sorted.
    std::vector<std::string> sceneFiles() const;

    /// Checks that a project-relative path is safe to store: not empty, not
    /// absolute, no ".." escaping the project, forward slashes only.
    static bool isValidRelativePath(std::string_view path);

    /// Is `name` usable as a project, scene or file name on every OS?
    /// (Not empty, no / \ : * ? " < > |, not only dots or spaces.)
    static bool isValidName(std::string_view name);

    /// Parses project.rolo text (exposed for tests).
    static std::optional<ProjectSettings> parseSettings(std::string_view json, std::string* error = nullptr);
    static std::string serializeSettings(const ProjectSettings& settings);

private:
    std::filesystem::path m_folder;
    ProjectSettings m_settings;
};

} // namespace rolo
