#pragma once

// Checking and exporting a project, for the Export Game dialog and for
// `rolo_editor --project P --export OUT` (which runs without a window).

#include "../model/AndroidExport.hpp"
#include "../model/GameExport.hpp"
#include "../model/RoloResources.hpp"

#include "rolo/core/Log.hpp"
#include "rolo/scene/Component.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
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

// --- Android (ADR 0028) --------------------------------------------------------------------------

/// Google's tools on this computer, and what is wrong with them, if anything.
struct AndroidSetup {
    AndroidTools tools;
    AndroidToolsReport report;
};

/// Finds the Android SDK and Java (the folders the maker chose, or where
/// Android Studio puts them), asks the Java its version, and checks both
/// and Rolo's Android runtime.
AndroidSetup findAndCheckAndroidTools(const std::filesystem::path& chosenSdk, const std::filesystem::path& chosenJava,
                                      const RoloResources& resources);

/// What an Android export of `project` needs, filled in from Rolo's resources.
AndroidExportOptions androidExportOptions(const std::filesystem::path& project, const std::filesystem::path& output,
                                          const std::string& gameName, const AndroidSettings& android,
                                          const AndroidTools& tools, const RoloResources& resources);

struct AndroidRun {
    bool ok = false;
    std::string message;   // what happened, for the user
    ProjectCheck check;
    AndroidExportResult result;
};

/// Checks the project and Google's tools, then exports the game for Android
/// and waits for it (minutes). Everything is written to the log. For
/// `rolo_editor --export-android`; the editor's window does the same in
/// steps, with the building on a worker thread (EditorApp).
AndroidRun exportProjectForAndroid(const AndroidExportOptions& options, const AndroidSigning& signing,
                                   const AndroidToolsReport& tools, const ComponentRegistry& registry, const Logger& log);

/// An Android export going on in the background, so that the editor's
/// window stays alive for the minutes Google's tools take. The work
/// (exportAndroidGame) runs on a thread of its own; the window asks every
/// frame what it has said and whether it is done. Destroying it cancels the
/// export and waits for it.
class AndroidExportJob {
public:
    AndroidExportJob(AndroidExportOptions options, AndroidSigning signing);
    ~AndroidExportJob();
    AndroidExportJob(const AndroidExportJob&) = delete;
    AndroidExportJob& operator=(const AndroidExportJob&) = delete;

    /// The lines said since the last call (progress, Gradle's output).
    std::vector<std::string> takeLines();
    bool finished() const noexcept { return m_finished.load(); }
    /// Asks the export to stop; it finishes (as cancelled) shortly after.
    void cancel() noexcept { m_cancel = true; }
    bool cancelling() const noexcept { return m_cancel.load(); }
    /// What came of it. Only once finished() is true.
    const AndroidExportResult& result() const noexcept { return m_result; }

private:
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_finished{false};
    std::mutex m_mutex;                 // guards m_lines
    std::vector<std::string> m_lines;
    AndroidExportResult m_result;       // written by the worker before m_finished is set
    std::thread m_worker;
};

/// Says in the log what a C++ game module's components will not do on
/// Android (as on the web). Returns how many kinds there are.
std::size_t warnAboutModuleComponents(const std::filesystem::path& project, const ComponentRegistry& registry,
                                      const Logger& log, const char* where);

} // namespace rolo::editor
