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
