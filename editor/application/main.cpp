// Rolo Editor — entry point.

#include "../model/CommandLine.hpp"
#include "../model/ConsoleLog.hpp"
#include "EditorApp.hpp"
#include "Exporting.hpp"

#include "rolo/core/Engine.hpp"
#include "rolo/core/Files.hpp"
#include "rolo/core/Version.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/GameModule.hpp"
#include "rolo/scene/Project.hpp"

#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const rolo::editor::EditorParseResult parsed = rolo::editor::parseEditorCommandLine(args);
    if (!parsed.ok()) {
        std::cerr << "Error: " << parsed.error << "\n\n" << rolo::editor::editorHelpText();
        return 2;
    }
    if (parsed.options.showHelp) {
        std::cout << "Rolo Editor " << rolo::versionString() << "\n\n" << rolo::editor::editorHelpText();
        return 0;
    }
    if (parsed.options.showVersion) {
        std::cout << "Rolo Editor " << rolo::versionString() << " (backend: "
                  << rolo::runtimeBackendName() << ")\n";
        return 0;
    }

    // Declared before the engine so it outlives every message the engine logs.
    rolo::editor::ConsoleLog console;

    rolo::Engine engine({.applicationName = "Rolo Editor"});
    engine.logger().addSink(rolo::makeConsoleSink());   // terminal
    engine.logger().addSink(console.makeSink());           // Console panel

    if (const rolo::Status status = engine.initialize(); !status) {
        engine.logger().error("editor", "Engine failed to start: " + status.message);
        return 1;
    }

    // --new-android-key FILE: make an upload key for Google Play, without a window.
    if (!parsed.options.newAndroidKey.empty()) {
        const rolo::editor::AndroidTools tools = rolo::editor::findAndroidTools(
            parsed.options.androidSdk, parsed.options.androidJava, rolo::editor::currentOs(), rolo::editor::realEnvironment);
        rolo::editor::AndroidSigning key;
        key.keystore = parsed.options.newAndroidKey;
        key.alias = parsed.options.androidKeyAlias;
        key.password = rolo::environmentVariable(rolo::editor::kKeystorePasswordVariable).value_or("");
        const rolo::Status made = rolo::editor::createAndroidKey(tools.javaHome, rolo::editor::currentOs(), key,
                                                                parsed.options.androidKeyOwner);
        if (made) {
            engine.logger().info("export", "Made the upload key " + key.keystore.string() + " (the key inside is called \""
                                               + key.alias + "\"). Keep it and its password safe, with a copy somewhere "
                                                 "else: without them a game on Google Play cannot be updated.");
        } else {
            engine.logger().error("export", made.message
                                                + (key.password.empty() ? std::string(" (The password is read from the environment variable ")
                                                                              + rolo::editor::kKeystorePasswordVariable + ".)"
                                                                        : std::string()));
        }
        engine.shutdown();
        return made ? 0 : 1;
    }

    // --project P --export-android OUT: the same for Android (minutes: Google's tools build the app).
    if (!parsed.options.exportPath.empty() && parsed.options.exportAndroid) {
        rolo::ComponentRegistry registry = rolo::ComponentRegistry::withBuiltins();
        rolo::registerGameplayComponents(registry);
        rolo::registerGameModules(registry, engine.logger());
        const auto resources = rolo::editor::findRoloResources(
            rolo::executablePath(), rolo::editor::currentOs(), ROLO_SOURCE_DIR, ROLO_DEV_PLAYER);
        const auto project = rolo::Project::open(parsed.options.projectPath);
        const std::string name = project ? project->settings().name : std::string("Game");
        rolo::AndroidSettings android = project ? project->settings().android : rolo::AndroidSettings{};
        if (!parsed.options.androidPackage.empty()) {
            android.package = parsed.options.androidPackage;
        }
        if (parsed.options.androidVersionCode != 0) {
            android.versionCode = parsed.options.androidVersionCode;
        }
        if (!parsed.options.androidVersionName.empty()) {
            android.versionName = parsed.options.androidVersionName;
        }
        if (android.package.empty()) {
            engine.logger().error("export", "The game has no Android package name yet. Give one with --android-package "
                                            "com.yourname.yourgame, or export once from the editor's window, which "
                                            "keeps it in the project.");
            engine.shutdown();
            return 1;
        }
        const rolo::editor::AndroidSetup setup =
            rolo::editor::findAndCheckAndroidTools(parsed.options.androidSdk, parsed.options.androidJava, resources);
        rolo::editor::AndroidSigning signing;
        if (!parsed.options.androidKey.empty()) {
            signing.keystore = parsed.options.androidKey;
            signing.alias = parsed.options.androidKeyAlias;
            signing.password = rolo::environmentVariable(rolo::editor::kKeystorePasswordVariable).value_or("");
        }
        const auto run = rolo::editor::exportProjectForAndroid(
            rolo::editor::androidExportOptions(parsed.options.projectPath, parsed.options.exportPath, name, android,
                                               setup.tools, resources),
            signing, setup.report, registry, engine.logger());
        if (run.ok) {
            std::cout << "Game app: " << run.result.apk.string() << "\n";
            if (!run.result.aab.empty()) {
                std::cout << "For Google Play: " << run.result.aab.string() << "\n";
            }
        }
        engine.shutdown();
        return run.ok ? 0 : 1;
    }

    // --project P --export OUT: check and export the game, without a window.
    if (!parsed.options.exportPath.empty()) {
        rolo::ComponentRegistry registry = rolo::ComponentRegistry::withBuiltins();
        rolo::registerGameplayComponents(registry);
        rolo::registerGameModules(registry, engine.logger());
        const auto resources = rolo::editor::findRoloResources(
            rolo::executablePath(), rolo::editor::currentOs(), ROLO_SOURCE_DIR, ROLO_DEV_PLAYER);
        const auto project = rolo::Project::open(parsed.options.projectPath);
        const std::string name = project ? project->settings().name : std::string("Game");
        const auto run = rolo::editor::exportProject(parsed.options.projectPath, parsed.options.exportPath, name,
                                                        resources, registry, engine.logger(),
                                                        parsed.options.exportWeb ? rolo::editor::ExportTarget::Web
                                                                                 : rolo::editor::ExportTarget::Desktop);
        if (run.ok) {
            std::cout << (parsed.options.exportWeb ? "Game page: " : "Game program: ") << run.result.program.string()
                      << "\n";
        }
        engine.shutdown();
        return run.ok ? 0 : 1;
    }

    rolo::editor::EditorApp editor(engine, console, parsed.options);
    const rolo::Status status = editor.run();
    if (!status) {
        engine.logger().error("editor", status.message);
    }

    engine.shutdown();
    return status ? 0 : 1;
}
