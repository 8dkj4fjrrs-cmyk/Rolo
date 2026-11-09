#pragma once

// The editor application: owns the open project and scene document, builds
// the workspace (menu bar, toolbar, dockable panels, status bar) from Rolo
// UI widgets, runs the file workflow (new/open/save, unsaved-changes
// prompts), and remembers layout and preferences between sessions.

#include "../model/CommandLine.hpp"
#include "../model/ConsoleLog.hpp"
#include "../model/EditorState.hpp"
#include "../model/Preferences.hpp"
#include "../model/SceneDocument.hpp"
#include "../panels/Panels.hpp"

#include "rolo/audio/AudioEngine.hpp"
#include "rolo/core/Engine.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/runtime/Application.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/ui/DockLayout.hpp"

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace rolo::ui {
class Context;
class Button;
class DockSpace;
class Label;
} // namespace rolo::ui

namespace rolo::editor {

class EditorApp final : public EditorServices {
public:
    EditorApp(Engine& engine, ConsoleLog& console, EditorOptions options);

    /// Opens the editor window and blocks until it closes.
    Status run();

    /// The panel arrangement used on first start and by View > Reset Layout,
    /// the classic game-editor layout:
    ///   +-----------+-----------------------------+-----------+
    ///   | Hierarchy |            Scene            | Inspector |
    ///   +-----------+                             |           |
    ///   |  Assets   +-----------------------------+           |
    ///   |           | Assets Preview | [Console]  |           |
    ///   +-----------+-----------------------------+-----------+
    static ui::DockLayout defaultLayout();

    // --- EditorServices ------------------------------------------------------------------
    SceneDocument& document() override { return *m_document; }
    std::uint64_t documentGeneration() const override { return m_documentGeneration; }
    const ComponentRegistry& registry() const override { return m_registry; }
    const Project* project() const override { return m_project ? &*m_project : nullptr; }
    const RoloResources& resources() const override { return m_resources; }
    ui::Context& ui() override { return *m_ui; }
    EntityId selection() const override;
    bool isSceneSelected() const override { return m_state.selectedNodeId == EditorState::kSceneSelected; }
    void select(EntityId id) override;
    void selectScene() override;
    AssetDatabase* assets() override { return m_assetDb.get(); }
    AssetId selectedAsset() const override { return m_selectedAsset; }
    void selectAsset(AssetId id) override;
    std::string importFolder() const override { return m_importFolder; }
    void setImportFolder(std::string folder) override { m_importFolder = std::move(folder); }
    void importFiles(const std::vector<std::filesystem::path>& files) override;
    void placeAsset(AssetId asset, Vec2 worldPosition) override;
    void dropAsset(AssetId asset, ui::Vec2 windowPoint) override;
    bool execute(std::unique_ptr<Command> command) override;
    void endEdit() override;
    void createEntity(NewEntityKind kind, bool asChildOfSelection) override;
    void deleteSelection() override;
    void duplicateSelection() override;
    void renameSelection() override;
    void openScene(const std::filesystem::path& path) override;
    void newScene() override;
    void newProject() override;
    void openProject() override;
    void showMessage(const std::string& title, const std::string& text) override;
    void showImportDialog() override;
    void createContent(AssetType type) override;
    std::size_t checkContent() override;
    int tileBrush() const override { return m_tileBrush; }
    void setTileBrush(int tile) override { m_tileBrush = std::max(0, tile); }
    int tileLayer() const override { return m_tileLayer; }
    void setTileLayer(int layer) override { m_tileLayer = layer; }
    void showPanel(const std::string& id) override;
    void setSceneTool(int tool) override;
    bool isPlaying() const override { return m_game != nullptr; }
    bool isPaused() const override { return m_game != nullptr && m_paused; }
    Input* gameInput() override { return m_game != nullptr ? &m_game->input() : nullptr; }
    void previewSound(AssetId sound) override;
    void stopSoundPreview() override;

    // --- Play mode ---------------------------------------------------------------------------
    /// Plays the open scene in the Scene view (a copy of it: stopping
    /// returns to the scene exactly as it was).
    void play();
    void stopPlaying();
    void togglePause();
    /// While paused: runs exactly one frame (1/60 s).
    void stepFrame();

private:
    // Called by the runtime (see PresentationSettings).
    void setupUi(UiFrame& frame);
    void updateUi(UiFrame& frame);
    void shutdownUi(UiFrame& frame);
    bool closeRequested();

    void buildWorkspace(UiFrame& frame);
    void buildMenus(ui::Widget& root);
    void buildToolbar(ui::Widget& root);
    void addShortcuts();
    void restoreLayout();
    void updateStatusBar(UiFrame& frame);
    void updateTitle(UiFrame& frame);
    void showAbout();

    // --- Files ---------------------------------------------------------------------------
    /// Opens the scene, project or start scene the editor should begin with.
    void openInitialContent();
    bool openProjectFolder(const std::filesystem::path& folder, bool reportErrors);
    void setDocument(std::unique_ptr<SceneDocument> document);
    void saveScene(std::function<void()> then = {});
    void saveSceneAs(std::function<void()> then = {});
    void reloadScene();
    void undo();
    void redo();
    /// Says (once per run) that scene edits wait until the game stops.
    void refuseEditWhilePlaying();
    /// Loads a scene of the project for a running game's scene change.
    std::optional<Scene> loadSceneForGame(AssetId id, std::string& error);
    void updateGame(UiFrame& frame);
    void moveSelection(int direction);
    /// Runs `then` once unsaved changes are saved or discarded (asks first);
    /// does nothing if the user cancels.
    void confirmDiscard(const std::string& action, std::function<void()> then);
    void showNewProjectDialog(bool keepCurrentScene, std::function<void()> then = {});
    void showOpenProjectDialog();
    void showExportDialog();
    void showWelcome();
    void showGuide(const std::string& file);
    void openExampleCopy();
    static constexpr const char* kShowWelcomeKey = "welcome.show";
    static constexpr const char* kMuteKey = "sound.muted";   // Game > Mute Sound
    void exportNow(const std::string& name, const std::filesystem::path& folder);
    void reportIssues(const std::filesystem::path& file, const std::vector<FileIssue>& issues);
    /// Re-reads the project's assets (also every few seconds) and reports problems.
    void refreshAssets();
    void checkSceneAssets();


    void loadSettings();
    void saveSettings();
    void saveLayout();

    Logger& log() { return m_engine.logger(); }

    Engine& m_engine;
    ConsoleLog& m_console;
    EditorOptions m_options;
    RoloResources m_resources;   // templates, examples, licenses, the game runtime
    ComponentRegistry m_registry = ComponentRegistry::withBuiltins();

    EditorState m_state;
    Preferences m_preferences;
    std::filesystem::path m_configDir;   // empty = do not persist anything

    std::optional<Project> m_project;
    std::filesystem::file_time_type m_projectStamp;   // project.rolo when last read
    void syncProjectSettings();
    std::unique_ptr<AssetDatabase> m_assetDb;                       // the project's assets
    std::vector<std::unique_ptr<AssetDatabase>> m_retiredAssetDbs;  // kept until the runtime lets go
    const AssetDatabase* m_runtimeAssets = nullptr;                 // what the runtime draws from
    double m_lastAssetScan = -100.0;
    AssetId m_selectedAsset;
    std::string m_importFolder = "assets";
    int m_tileBrush = 0;
    int m_tileLayer = 0;
    std::unique_ptr<SceneDocument> m_document;
    std::uint64_t m_documentGeneration = 1;
    std::shared_ptr<const Scene> m_shownScene;   // what the runtime is drawing
    std::string m_shownTitle;

    // Sound: made the first time something plays (declared before the game,
    // so the game, which stops its sounds when it goes, goes first).
    std::unique_ptr<AudioEngine> m_audio;
    const AssetDatabase* m_audioAssets = nullptr;   // what m_audio reads sounds from
    SoundHandle m_soundPreview = 0;                 // the sound file being listened to
    AudioEngine& audio();

    // Play mode.
    std::unique_ptr<Game> m_game;   // non-null while playing
    std::unique_ptr<ContentLibrary> m_playContent;   // the content the game plays
    std::unique_ptr<FolderSaveStore> m_playSaves;
    bool m_paused = false;
    bool m_stepPending = false;
    bool m_editRefused = false;     // the "stop to edit" message was shown this run
    double m_lastFrameTime = -1.0;

    // Widgets (owned by the UI context; valid between setupUi and shutdownUi).
    ui::Context* m_ui = nullptr;
    ui::DockSpace* m_dock = nullptr;
    ViewportPanel* m_viewport = nullptr;
    HierarchyPanel* m_hierarchy = nullptr;
    AssetsPanel* m_assets = nullptr;
    AssetsPreviewPanel* m_preview = nullptr;
    TilePalettePanel* m_tilePalette = nullptr;
    AnimationPanel* m_animation = nullptr;
    InspectorPanel* m_inspector = nullptr;
    ConsolePanel* m_consolePanel = nullptr;
    ui::ChoiceField* m_sceneChoice = nullptr;
    ui::Button* m_playButton = nullptr;
    ui::Button* m_pauseButton = nullptr;
    ui::Button* m_stepButton = nullptr;
    ui::Button* m_revertButton = nullptr;
    std::vector<std::string> m_sceneChoices;   // project-relative paths behind the choice
    ui::Label* m_statusFps = nullptr;
    std::array<ui::Label*, 3> m_statusCounts{};   // messages, warnings, errors

    bool m_quitRequested = false;   // set once the user has confirmed quitting
    bool m_quitting = false;        // a quit is being confirmed (avoid two prompts)
    bool m_renamePending = false;   // focus the Inspector's name field after its next update
    bool m_layoutDirty = false;
    double m_layoutChangedAt = 0.0;
};

} // namespace rolo::editor
