#pragma once

// Command-line options for rolo_editor. No UI code, so it is unit-tested.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rolo::editor {

struct EditorOptions {
    bool showHelp = false;
    bool showVersion = false;

    /// Folder for preferences and layout. Empty = the per-user default.
    /// Tests point this at a temporary folder so they never touch real settings.
    std::string configDirectory;

    /// A project folder to open (default: the last project used).
    std::string projectPath;
    /// Start without opening any project (not even the last one).
    bool noProject = false;
    /// Create a new project in this folder (named after the folder) and open it.
    std::string newProjectPath;
    /// With newProjectPath: the template to start from (its folder name, e.g.
    /// "topdown"). Empty: an empty project.
    std::string templateId;

    /// Export the project (--project) as a playable game into this folder,
    /// then quit without opening a window. See docs/exporting.md.
    std::string exportPath;
    /// With exportPath: export for the web (--export-web) instead of for this computer.
    bool exportWeb = false;

    /// Ignore the saved panel layout and start with the default one.
    bool resetLayout = false;
    /// Start playing the opened scene straight away (used by tests).
    bool play = false;

    std::uint64_t maxFrames = 0;   // 0 = until closed
    std::string screenshotPath;    // empty = none
};

struct EditorParseResult {
    EditorOptions options;
    std::string error; // empty on success
    bool ok() const noexcept { return error.empty(); }
};

/// Parses arguments *after* the program name.
EditorParseResult parseEditorCommandLine(std::span<const std::string_view> args);

std::string editorHelpText();

} // namespace rolo::editor
