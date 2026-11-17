#include "Exporting.hpp"

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/core/Version.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <algorithm>
#include <cstdio>

namespace rolo::editor {

namespace {
constexpr const char* kCategory = "export";
}

ProjectCheck checkProject(const std::filesystem::path& folder, const ComponentRegistry& registry, const Logger& log)
{
    ProjectCheck check;
    const auto report = [&](const FileIssue& issue) {
        if (issue.isError()) {
            ++check.errors;
            log.error(kCategory, issue.toString());
        } else {
            ++check.warnings;
            log.warning(kCategory, issue.toString());
        }
    };
    std::string error;
    const auto project = Project::open(folder, &error);
    if (!project) {
        report({FileIssue::Severity::Error, folder.string(), error});
        return check;
    }
    check.startScene = project->settings().startScene;
    std::error_code ec;
    if (!std::filesystem::exists(project->absolute(check.startScene), ec)) {
        report({FileIssue::Severity::Error, "project.rolo",
                "The start scene " + check.startScene + " does not exist. Open a scene and choose Make Start Scene "
                "(select the scene row in the Hierarchy)."});
    }

    AssetDatabase assets(folder, AssetDatabase::Access::ReadOnly);
    for (const FileIssue& issue : assets.refresh()) {
        report(issue);
    }
    std::vector<FileIssue> issues;
    const ContentLibrary content = ContentLibrary::load(assets, &issues);
    for (FileIssue& issue : content.check()) {
        issues.push_back(std::move(issue));
    }
    for (FileIssue& issue : checkScripts(assets)) {   // Lua syntax errors
        issues.push_back(std::move(issue));
    }
    // Scenes, and prefabs: a prefab is read as a scene holding its one object.
    std::vector<const AssetRecord*> scenes = assets.all(AssetType::Scene);
    for (const AssetRecord* prefab : assets.all(AssetType::Prefab)) {
        scenes.push_back(prefab);
    }
    for (const AssetRecord* scene : scenes) {
        SceneLoadResult loaded = scene->type == AssetType::Prefab
                                     ? loadPrefabFile(assets.absolutePath(*scene), registry)
                                     : loadSceneFile(assets.absolutePath(*scene), registry);
        for (FileIssue& issue : loaded.issues) {
            if (issue.where.empty()) {
                issue.where = scene->path;
            }
        }
        for (FileIssue& issue : loaded.issues) {
            issues.push_back(std::move(issue));
        }
        if (!loaded.ok()) {
            continue;
        }
        for (FileIssue& issue : checkAssetReferences(*loaded.scene, assets)) {
            issues.push_back(std::move(issue));
        }
        loaded.scene->forEach([&](const Entity& e, int) {
            if (const Script* s = e.get<Script>(); s != nullptr && s->script.isNull()) {
                issues.push_back({FileIssue::Severity::Warning, scene->path + ", object '" + e.name() + "', Script",
                                  "No script file is chosen, so it does nothing."});
            }
            const Trigger* t = e.get<Trigger>();
            if (t == nullptr) {
                return;
            }
            const std::string where = scene->path + ", object '" + e.name() + "', Trigger";
            if (t->reaction.condition) {
                content.checkCondition(*t->reaction.condition, where, issues);
            }
            content.checkActions(t->reaction.actions, where, issues);
            for (const Action& a : t->reaction.actions) {
                if ((a.kind == Action::Kind::SetActive || a.kind == Action::Kind::Destroy) && !a.entity.isNull()
                    && !loaded.scene->contains(a.entity)) {
                    issues.push_back({FileIssue::Severity::Error, where,
                                      "An action names object " + a.entity.toString() + ", which is not in this scene."});
                }
            }
        });
    }
    for (const FileIssue& issue : issues) {
        report(issue);
    }
    return check;
}

std::vector<std::string> moduleComponentsUsed(const std::filesystem::path& folder, const ComponentRegistry& registry)
{
    // Rolo's own components: what every runtime has, the web one too.
    ComponentRegistry own = ComponentRegistry::withBuiltins();
    registerGameplayComponents(own);
    std::vector<std::string> used;
    AssetDatabase assets(folder, AssetDatabase::Access::ReadOnly);
    static_cast<void>(assets.refresh());
    std::vector<const AssetRecord*> scenes = assets.all(AssetType::Scene);
    for (const AssetRecord* prefab : assets.all(AssetType::Prefab)) {
        scenes.push_back(prefab);
    }
    for (const AssetRecord* scene : scenes) {
        const SceneLoadResult loaded = scene->type == AssetType::Prefab
                                           ? loadPrefabFile(assets.absolutePath(*scene), registry)
                                           : loadSceneFile(assets.absolutePath(*scene), registry);
        if (!loaded.ok()) {
            continue;
        }
        loaded.scene->forEach([&](const Entity& e, int) {
            for (const auto& c : e.components()) {
                const ComponentType& type = c->type();
                if (&type != &UnknownComponent::unknownType() && own.find(type.id) == nullptr
                    && std::find(used.begin(), used.end(), type.displayName) == used.end()) {
                    used.push_back(type.displayName);
                }
            }
        });
    }
    return used;
}

std::size_t warnAboutModuleComponents(const std::filesystem::path& project, const ComponentRegistry& registry,
                                      const Logger& log, const char* where)
{
    // The web and Android players have Rolo's own components and Luau
    // scripts, not the C++ game modules this editor was built with.
    const bool web = std::string_view(where) == "web";
    const std::vector<std::string> used = moduleComponentsUsed(project, registry);
    for (const std::string& type : used) {
        log.warning(kCategory, "The game uses " + type + ", a C++ behaviour from a game module. Rolo's "
                                   + (web ? "web" : "Android") + " runtime does not contain game modules, so it will "
                                   + "do nothing " + (web ? "in a browser" : "on Android") + " (docs/exporting.md, \"For "
                                   + (web ? "the web" : "Android") + "\").");
    }
    return used.size();
}

AndroidSetup findAndCheckAndroidTools(const std::filesystem::path& chosenSdk, const std::filesystem::path& chosenJava,
                                      const RoloResources& resources)
{
    AndroidSetup setup;
    const HostOs os = currentOs();
    setup.tools = findAndroidTools(chosenSdk, chosenJava, os, realEnvironment);
    std::string version;
    if (isJavaHome(setup.tools.javaHome, os)) {
        static_cast<void>(runProcess(javaVersionCommand(setup.tools.javaHome, os),
                                     [&](std::string_view line) { version += std::string(line) + "\n"; }));
    }
    setup.report = checkAndroidTools(setup.tools, os, version, resources.androidRuntime);
    return setup;
}

AndroidExportOptions androidExportOptions(const std::filesystem::path& project, const std::filesystem::path& output,
                                          const std::string& gameName, const AndroidSettings& android,
                                          const AndroidTools& tools, const RoloResources& resources)
{
    AndroidExportOptions options;
    options.project = project;
    options.output = output;
    options.gameName = gameName;
    options.runtime = resources.androidRuntime;
    options.licenses = resources.licenses;
    options.version = std::string(versionString());
    options.android = android;
    options.tools = tools;
    return options;
}

AndroidRun exportProjectForAndroid(const AndroidExportOptions& options, const AndroidSigning& signing,
                                   const AndroidToolsReport& tools, const ComponentRegistry& registry, const Logger& log)
{
    AndroidRun run;
    for (const std::string& note : tools.notes) {
        log.info(kCategory, note);
    }
    if (!tools.ok()) {
        for (const std::string& problem : tools.problems) {
            log.error(kCategory, problem);
        }
        run.message = "The game was not exported: " + tools.problems.front();
        return run;
    }
    log.info(kCategory, "Checking " + options.project.string() + " before exporting...");
    run.check = checkProject(options.project, registry, log);
    if (run.check.errors > 0) {
        run.message = "The game was not exported: the check found " + std::to_string(run.check.errors)
                      + (run.check.errors == 1 ? " error" : " errors") + " (each one is listed above). Fix them, then "
                        "export again.";
        log.error(kCategory, run.message);
        return run;
    }
    run.check.warnings += warnAboutModuleComponents(options.project, registry, log, "android");
    run.result = exportAndroidGame(options, signing, [&](std::string_view line) { log.info("android", std::string(line)); });
    run.ok = run.result.ok;
    run.message = run.result.message;
    if (run.ok) {
        log.info(kCategory, run.message);
    } else {
        log.error(kCategory, run.message);
    }
    return run;
}

AndroidExportJob::AndroidExportJob(AndroidExportOptions options, AndroidSigning signing)
{
    m_worker = std::thread([this, options = std::move(options), signing = std::move(signing)] {
        m_result = exportAndroidGame(
            options, signing,
            [this](std::string_view line) {
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_lines.emplace_back(line);
            },
            &m_cancel);
        m_finished = true;
    });
}

AndroidExportJob::~AndroidExportJob()
{
    m_cancel = true;
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

std::vector<std::string> AndroidExportJob::takeLines()
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> lines;
    lines.swap(m_lines);
    return lines;
}

#if defined(__APPLE__)
namespace {
// Signs an app bundle ad hoc with the codesign tool every Mac has. False,
// with codesign's message, if it failed.
bool signAdHoc(const std::filesystem::path& app, std::string& error)
{
    std::string quoted = "'";
    for (const char c : app.string()) {
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }
    quoted += "'";
    const std::string command = "/usr/bin/codesign --force --deep --sign - " + quoted + " 2>&1";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        error = "codesign could not be started";
        return false;
    }
    char buffer[256];
    while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) {
        error += buffer;
    }
    const int status = pclose(pipe);
    while (!error.empty() && (error.back() == '\n' || error.back() == '\r')) {
        error.pop_back();
    }
    return status == 0;
}
} // namespace
#endif

ExportRun exportProject(const std::filesystem::path& project, const std::filesystem::path& output,
                        const std::string& gameName, const RoloResources& resources,
                        const ComponentRegistry& registry, const Logger& log, ExportTarget target)
{
    ExportRun run;
    log.info(kCategory, "Checking " + project.string() + " before exporting...");
    run.check = checkProject(project, registry, log);
    if (run.check.errors > 0) {
        run.message = "The game was not exported: the check found " + std::to_string(run.check.errors)
                      + (run.check.errors == 1 ? " error" : " errors")
                      + " (each one is listed in the Console, or above when exporting from the command line). Fix "
                        "them, then export again.";
        log.error(kCategory, run.message);
        return run;
    }
    ExportOptions options;
    options.target = target;
    options.webPlayer = resources.webPlayer;
    options.project = project;
    options.output = output;
    options.gameName = gameName;
    options.player = resources.player;
    options.licenses = resources.licenses;
    options.version = std::string(versionString());
    run.result = exportGame(options);
    if (!run.result.ok) {
        run.message = "The game was not exported: " + run.result.error;
        log.error(kCategory, run.message);
        return run;
    }
    if (target == ExportTarget::Web) {
        run.check.warnings += warnAboutModuleComponents(project, registry, log, "web");
    }
#if defined(__APPLE__)
    if (target == ExportTarget::Desktop) {
    // The exported app was copied, renamed and given the game: its old
    // signature no longer matches. Apple silicon Macs call a downloaded app
    // with no valid signature "damaged" and refuse to open it, so sign it
    // again ("ad hoc": valid, though not from an Apple developer account;
    // players then right-click > Open the first time, docs/exporting.md).
    if (std::string error; !signAdHoc(run.result.game, error)) {
        log.warning(kCategory, "Could not sign " + run.result.game.filename().string() + " (" + error
                                   + "). Other Macs may call it damaged. Run: codesign --force --deep --sign - \""
                                   + run.result.game.string() + "\"");
    }
    }
#endif
    char size[32];
    std::snprintf(size, sizeof size, "%.1f MB", static_cast<double>(run.result.bytes) / (1024.0 * 1024.0));
    run.ok = true;
    run.message = "Exported " + run.result.game.filename().string() + " (" + std::to_string(run.result.files)
                  + " files, " + size + ") to " + run.result.game.parent_path().string() + "."
                  + (run.check.warnings > 0 ? " The check found " + std::to_string(run.check.warnings)
                                                  + " warnings (see the Console)."
                                            : std::string());
    log.info(kCategory, run.message);
    return run;
}

} // namespace rolo::editor
