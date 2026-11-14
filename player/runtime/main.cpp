// Rolo Player — runs a Rolo game in a window.
//
// Plays a Rolo scene: a project's start scene (--project), one scene file
// (--scene), or Rolo's own start scene. The game's behaviours (C++ code
// from the game modules linked into this program) run every frame; doors and
// other gameplay can change to other scenes of the project.

#include "CommandLine.hpp"

#include "rolo/audio/AudioEngine.hpp"
#include "rolo/core/Engine.hpp"
#include "rolo/core/Files.hpp"
#include "rolo/core/Version.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/GameModule.hpp"
#include "rolo/gameplay/TouchControls.hpp"

#include <algorithm>
#include "rolo/runtime/Application.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

// Reads a scene file, reporting what it finds wrong to the log.
std::optional<rolo::Scene> loadScene(const std::filesystem::path& path, const rolo::ComponentRegistry& registry,
                                        const rolo::Logger& log)
{
    rolo::SceneLoadResult loaded = rolo::loadSceneFile(path, registry);
    for (const rolo::FileIssue& issue : loaded.issues) {
        if (issue.isError()) {
            log.error("scene", issue.toString());
        } else {
            log.warning("scene", issue.toString());
        }
    }
    if (!loaded.ok()) {
        return std::nullopt;
    }
    return std::move(*loaded.scene);
}

#ifdef __EMSCRIPTEN__
// In a web browser, files live in memory and are gone when the page closes.
// The page keeps the saves folder in the browser's own storage
// (player/web/pre.js); after each save it is told to store the new file.
class BrowserSaveStore final : public rolo::SaveStore {
public:
    explicit BrowserSaveStore(rolo::SaveStore& files)
        : m_files(files)
    {
    }
    rolo::Status write(std::string_view slot, std::string_view text) override
    {
        rolo::Status status = m_files.write(slot, text);
        if (status) {
            // clang-format off
            EM_ASM({ if (Module["roloKeepSaves"]) { Module["roloKeepSaves"](); } });
            // clang-format on
        }
        return status;
    }
    std::optional<std::string> read(std::string_view slot) const override { return m_files.read(slot); }

private:
    rolo::SaveStore& m_files;
};
#endif

} // namespace

// A game being played: everything the player sets up and keeps while the game
// runs. It is one object (not variables of main) because of web browsers:
// there, starting the game returns at once and the browser calls each frame
// afterwards, so all of this has to outlive main() (see main, below).
struct Player {
    rolo::player::PlayerOptions opt;
    std::unique_ptr<rolo::Engine> engine;
    rolo::ComponentRegistry registry;
    std::unique_ptr<rolo::AssetDatabase> assets;
    rolo::ProjectSettings gameOptions;   // the project's Escape, F5, F9 and touch choices
    rolo::TouchControls touchControls;   // the on-screen stick and buttons (touch screens)
    rolo::ContentLibrary content;
    std::unique_ptr<rolo::FolderSaveStore> saveFiles;
    std::unique_ptr<rolo::SaveStore> browserSaves;   // only in a web browser
    std::unique_ptr<rolo::AudioEngine> audio;
    std::unique_ptr<rolo::Game> game;
    // --frame-times: every frame's length, and the part gameplay took.
    static constexpr std::uint64_t kFrameTimesWarmup = 30;   // start-up frames are slow and not counted
    std::vector<double> frameMs;
    std::vector<double> gameplayMs;
    std::unique_ptr<rolo::Application> app;
    bool continues = false;   // a web browser: the game goes on after run() returned

    /// Sets the game up and plays it. Returns the program's exit code.
    int run();
    std::vector<std::string> touchButtonsNow() const;
};

// The actions that get an on-screen button now: the project's list, or else
// the game's own actions in the order it defined them, then Rolo's
// "interact" if the scene has someone who can interact (or a dialogue is
// open, which "interact" moves on).
std::vector<std::string> Player::touchButtonsNow() const
{
    if (!gameOptions.touchButtons.empty()) {
        return gameOptions.touchButtons;
    }
    std::vector<std::string> names;
    for (const std::string& name : game->input().actions()) {
        if (name != rolo::Input::kInteract) {
            names.push_back(name);
        }
    }
    const auto scene = game->sharedScene();
    if (game->isInDialogue() || (scene != nullptr && !scene->all<rolo::Interactor>().empty())) {
        names.emplace_back(rolo::Input::kInteract);
    }
    return names;
}

int Player::run()
{
    engine = std::make_unique<rolo::Engine>(rolo::EngineConfig{.applicationName = opt.title});
    engine->logger().addSink(rolo::makeConsoleSink());

    if (const rolo::Status status = engine->initialize(); !status) {
        engine->logger().error("player", "Engine failed to start: " + status.message);
        return 1;
    }

    // An exported game: the player was copied beside the game's files, and
    // started without saying what to play.
    if (opt.projectPath.empty() && opt.scenePath.empty()) {
        if (const auto bundled = rolo::player::bundledGameFolder(rolo::executablePath())) {
            opt.projectPath = bundled->string();
            if (!opt.statsChosen) {
                opt.showStats = false;
            }
            engine->logger().info("player", "Playing the game bundled with this program (" + bundled->string() + ").");
        }
    }

    // Find the scene to play.
    std::filesystem::path scenePath = opt.scenePath;
    std::string title = opt.title;
    if (!opt.projectPath.empty()) {
        std::string error;
        const auto project = rolo::Project::open(opt.projectPath, &error);
        if (!project) {
            engine->logger().error("player", error);
            return 1;
        }
        scenePath = project->absolute(project->settings().startScene);
        std::error_code missing;
        if (!std::filesystem::exists(scenePath, missing)) {
            engine->logger().error("player", "The project's start scene " + project->settings().startScene
                                                + " does not exist. Open the project in the editor, open the scene "
                                                  "the game should begin with, and choose Make Start Scene.");
            return 1;
        }
        if (title == rolo::player::PlayerOptions{}.title) {
            title = project->settings().name;
        }
    }
    // The component types this game can use: Rolo's own, plus the
    // behaviours of the game modules linked into the player.
    registry = rolo::ComponentRegistry::withBuiltins();
    rolo::registerGameplayComponents(registry);
    rolo::registerGameModules(registry, engine->logger());

    std::optional<rolo::Scene> firstScene;
    if (!scenePath.empty()) {
        firstScene = loadScene(scenePath, registry, engine->logger());
        if (!firstScene) {
            engine->logger().error("player", "Could not open the scene " + scenePath.string() + ".");
            return 1;
        }
        engine->logger().info("player", "Playing scene '" + firstScene->name() + "' (" + scenePath.string() + ")");
    }

    // The project's images, tilesets and scenes: from --project, or the
    // project folder the scene file is in (searching upwards for project.rolo).
    std::filesystem::path projectFolder = opt.projectPath;
    if (projectFolder.empty() && !scenePath.empty()) {
        std::error_code ec;
        for (std::filesystem::path dir = std::filesystem::absolute(scenePath, ec).parent_path(); !dir.empty();
             dir = dir.parent_path()) {
            if (std::filesystem::exists(dir / rolo::ProjectFormat::kFileName, ec)) {
                projectFolder = dir;
                break;
            }
            if (dir == dir.parent_path()) {
                break;
            }
        }
    }
    // The project's game options (Escape, F5 and F9); the defaults without a project.
    if (!projectFolder.empty()) {
        if (const auto project = rolo::Project::open(projectFolder)) {
            gameOptions = project->settings();
        }
    }
    if (!projectFolder.empty()) {
        assets = std::make_unique<rolo::AssetDatabase>(projectFolder, rolo::AssetDatabase::Access::ReadOnly);
        for (const rolo::FileIssue& issue : assets->refresh()) {
            engine->logger().warning("assets", issue.where + ": " + issue.message);
        }
        if (firstScene) {
            for (const rolo::FileIssue& issue : rolo::checkAssetReferences(*firstScene, *assets)) {
                engine->logger().warning("assets", issue.where + ": " + issue.message);
            }
        }
    }

    // The project's RPG content (variables, dialogues, quests), checked.
    if (assets) {
        std::vector<rolo::FileIssue> issues;
        content = rolo::ContentLibrary::load(*assets, &issues);
        for (rolo::FileIssue& issue : content.check()) {
            issues.push_back(std::move(issue));
        }
        for (const rolo::FileIssue& issue : issues) {
            if (issue.isError()) {
                engine->logger().error("content", issue.where + ": " + issue.message);
            } else {
                engine->logger().warning("content", issue.where + ": " + issue.message);
            }
        }
    }

    // Save slots: in the user's application data folder (or --save-dir).
    std::filesystem::path saveFolder = opt.saveDir;
    if (saveFolder.empty()) {
        saveFolder = rolo::FolderSaveStore::userFolder(title);
    }
    saveFiles = std::make_unique<rolo::FolderSaveStore>(saveFolder);
    rolo::SaveStore* saves = saveFiles.get();
#ifdef __EMSCRIPTEN__
    browserSaves = std::make_unique<BrowserSaveStore>(*saveFiles);
    saves = browserSaves.get();
#endif

    // Sound: the computer's sound output (or silence with --mute, or when
    // there is none).
    if (opt.sound) {
        audio = std::make_unique<rolo::AudioEngine>(engine->logger());
        audio->setAssets(assets.get());
    }

    // The game: plays the scene, loading other scenes of the project when
    // gameplay asks for them.
    rolo::GameSetup setup;
    setup.assets = assets.get();
    setup.content = &content;
    setup.saves = saveFolder.empty() ? nullptr : saves;
    setup.gameName = title;
    setup.sound = audio.get();
    setup.loadScene = [&](rolo::AssetId id, std::string& error) -> std::optional<rolo::Scene> {
        const rolo::AssetRecord* record = assets != nullptr ? assets->find(id) : nullptr;
        if (record == nullptr || record->type != rolo::AssetType::Scene) {
            error = "it is not a scene of this project";
            return std::nullopt;
        }
        auto scene = loadScene(assets->absolutePath(*record), registry, engine->logger());
        if (!scene) {
            error = record->path + " could not be read";
        }
        return scene;
    };
    game = std::make_unique<rolo::Game>(engine->logger(), std::move(setup));
    rolo::PresentationSettings presentation;
    // The game size and what other screen shapes show come from the project.
    presentation.worldWidth = gameOptions.width;
    presentation.worldHeight = gameOptions.height;
    presentation.screenFit = gameOptions.screenFit;
    if (!opt.sizeChosen) {
        // The window: the game's size, made smaller (same shape) if that is
        // larger than an ordinary laptop screen has room for.
        const double fit = std::min({1.0, 1280.0 / gameOptions.width, 800.0 / gameOptions.height});
        opt.width = std::max(160, static_cast<int>(gameOptions.width * fit + 0.5));
        opt.height = std::max(160, static_cast<int>(gameOptions.height * fit + 0.5));
    }
    if (firstScene) {
        // The scene's asset ID lets the game remember progress per scene.
        rolo::AssetId sceneId;
        if (assets) {
            std::error_code ec;
            const auto relative = std::filesystem::relative(std::filesystem::absolute(scenePath, ec), projectFolder, ec);
            if (const rolo::AssetRecord* record = assets->findByPath(relative.generic_string())) {
                sceneId = record->id;
            }
        }
        game->start(*firstScene, sceneId);
        presentation.scene = game->sharedScene();
        for (const rolo::Key key : opt.heldKeys) {
            game->input().press(key);
        }
    }

    app = std::make_unique<rolo::Application>(
        *engine, rolo::WindowSettings{.title = title, .width = opt.width, .height = opt.height},
        std::move(presentation));
    app->setAssets(assets.get());
    if (game->isRunning()) {
        app->setGameInput(&game->input());
        if (gameOptions.touchControls) {
            app->setTouchControls(&touchControls, opt.touch);
        }
        std::shared_ptr<const rolo::Scene> shown = game->sharedScene();
        app->setFrameCallback([&, shown](const rolo::FrameInfo& frame) mutable {
            // Keys pressed by tests (--tap KEY@FROM-TO).
            for (const auto& tap : opt.tappedKeys) {
                if (frame.frameIndex == tap.from) {
                    game->input().press(tap.key);
                } else if (frame.frameIndex == tap.to) {
                    game->input().release(tap.key);
                }
            }
            // Escape quits, unless the project turned that off. (Read from the
            // game's input, so the Escape that cancels remapping a control does not quit.)
            if (gameOptions.escapeQuits && game->input().wasPressed(rolo::Key::Escape)) {
                engine->logger().info("player", "Escape pressed; closing.");
                app->requestQuit();
            }
            // F5 saves to the "quick" slot, F9 loads it (unless the project turned that off).
            if (gameOptions.quickSaveKeys && game->input().wasPressed(rolo::Key::F5)) {
                game->save("quick");
            }
            if (gameOptions.quickSaveKeys && game->input().wasPressed(rolo::Key::F9)) {
                game->load("quick");
            }
            if (gameOptions.touchControls) {
                touchControls.setActions(game->input(), touchButtonsNow());
            }
            game->setViewSize(app->viewSize());   // how much of the world the window shows now
            // Fingers put on the screen by tests (--finger): as the window
            // reports real ones, to the on-screen controls first.
            for (std::size_t i = 0; i < opt.fingers.size(); ++i) {
                const auto& finger = opt.fingers[i];
                const int id = 1000 + static_cast<int>(i);
                const bool onControls = gameOptions.touchControls;
                if (frame.frameIndex == finger.from) {
                    game->input().noteTouchUsed();
                    if (!onControls || !touchControls.touchBegan(id, {finger.x, finger.y})) {
                        game->input().touchBegan(id, {finger.x, finger.y});
                    }
                } else if (frame.frameIndex == finger.from + 1 && frame.frameIndex < finger.to) {
                    if (onControls && touchControls.owns(id)) {
                        touchControls.touchMoved(id, {finger.toX, finger.toY});
                    } else {
                        game->input().touchMoved(id, {finger.toX, finger.toY});
                    }
                } else if (frame.frameIndex == finger.to) {
                    if (onControls && touchControls.owns(id)) {
                        touchControls.touchEnded(id);
                    } else {
                        game->input().touchEnded(id);
                    }
                }
                if (onControls) {
                    touchControls.apply(game->input());
                }
            }
            const float dt = opt.fixedStep ? 1.0f / 60.0f : static_cast<float>(frame.deltaSeconds);
            const auto updateStart = std::chrono::steady_clock::now();
            game->update(dt);
            if (opt.frameTimes && frame.frameIndex >= kFrameTimesWarmup) {
                frameMs.push_back(frame.deltaSeconds * 1000.0);
                gameplayMs.push_back(
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStart).count());
            }
            if (audio) {
                audio->update();   // frees the sounds that finished
            }
            app->setAnimationTime(game->time());   // animations follow game time
            if (game->sharedScene() != shown) {   // the game changed scene
                shown = game->sharedScene();
                app->showScene(shown);
            }
            if (game->quitRequested()) {   // game->quit()
                app->requestQuit();
            }
        });
    }
    const rolo::Status runStatus = app->run({
        .maxFrames = opt.maxFrames,
        .screenshotPath = opt.screenshotPath,
        .showStats = opt.showStats,
        // With a game, Escape is handled above; without one, the window closes on it.
        .escapeQuits = !game->isRunning(),
    });

    if (app->lastRunReport().continuesInBackground) {
        // A web browser: the game goes on after this returns (the browser
        // calls its frames), so everything above must stay as it is.
        continues = true;
        return 0;
    }
    if (!runStatus) {
        engine->logger().error("player", runStatus.message);
    }
    if (opt.frameTimes) {
        engine->logger().info("player", rolo::player::frameTimesReport(frameMs, gameplayMs));
    }

    engine->shutdown();
    return runStatus ? 0 : 1;
}

int main(int argc, char** argv)

{
    // Copy the arguments (minus the program name) into a safe, sized list.
    std::vector<std::string_view> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const rolo::player::ParseResult parsed = rolo::player::parseCommandLine(args);
    if (!parsed.ok()) {
        std::cerr << "Error: " << parsed.error << "\n\n" << rolo::player::helpText();
        return 2;
    }
    rolo::player::PlayerOptions opt = parsed.options;
    if (opt.showHelp) {
        std::cout << rolo::player::helpText();
        return 0;
    }
    if (opt.showVersion) {
        std::cout << "Rolo Player " << rolo::versionString() << " (backend: "
                  << rolo::runtimeBackendName() << ")\n";
        return 0;
    }

    // On the heap: in a web browser the game outlives this function.
    auto player = std::make_unique<Player>();
    player->opt = std::move(opt);
    const int code = player->run();
    if (player->continues) {
        static_cast<void>(player.release());   // never destroyed: the page is simply closed
    }
    return code;
}
