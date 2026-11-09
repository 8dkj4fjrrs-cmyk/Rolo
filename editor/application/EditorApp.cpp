#include "EditorApp.hpp"

#include "Exporting.hpp"

#include "../model/EditorPaths.hpp"

#include "rolo/core/Version.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Script.hpp"
#include "rolo/gameplay/GameModule.hpp"
#include "rolo/scene/SceneFile.hpp"
#include "rolo/ui/Context.hpp"
#include "rolo/ui/widgets/Basic.hpp"
#include "rolo/ui/widgets/Box.hpp"
#include "rolo/ui/widgets/Dialog.hpp"
#include "rolo/ui/widgets/DockSpace.hpp"
#include "rolo/ui/widgets/Fields.hpp"
#include "rolo/ui/widgets/Inspector.hpp"
#include "rolo/ui/widgets/Menu.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace rolo::editor {

namespace {

constexpr std::string_view kCategory = "editor";

// A saved layout is written this long after the last change (and on exit),
// so dragging a splitter does not write the file on every frame.
constexpr double kLayoutSaveDelaySeconds = 1.0;

// Every panel, in View-menu order: id, title, tab icon.
struct PanelInfo {
    const char* id;
    const char* title;
    ui::Icon icon;
};
constexpr PanelInfo kPanels[] = {
    {kHierarchyPanel, "Hierarchy", ui::Icon::Layers},
    {kAssetsPanel, "Assets", ui::Icon::Folder},
    {kViewportPanel, "Scene", ui::Icon::Scene},
    {kPreviewPanel, "Assets Preview", ui::Icon::Eye},
    {kConsolePanel, "Console", ui::Icon::Terminal},
    {kTilePalettePanel, "Tile Palette", ui::Icon::Grid},
    {kAnimationPanel, "Animation", ui::Icon::Play},
    {kInspectorPanel, "Inspector", ui::Icon::Sliders},
};

bool isEditorPanel(std::string_view id)
{
    for (const PanelInfo& panel : kPanels) {
        if (id == panel.id) {
            return true;
        }
    }
    return false;
}

ui::Button& addToolbarButton(ui::Row& bar, ui::Icon icon, std::string text, std::string tooltip,
                             std::function<void()> action)
{
    auto& button = bar.emplace<ui::Button>(std::move(text));
    button.setIcon(icon);
    button.setStyle(ui::Button::Style::Subtle);
    button.setTooltip(std::move(tooltip));
    if (action) {
        button.setOnClick(std::move(action));
    } else {
        button.setEnabled(false);
    }
    return button;
}

ui::MenuItem menuItem(std::string label, std::string shortcut, std::function<void()> action,
                      std::function<bool()> enabled = {})
{
    ui::MenuItem item;
    item.label = std::move(label);
    item.shortcut = std::move(shortcut);
    item.action = std::move(action);
    item.isEnabled = std::move(enabled);
    return item;
}

std::string sceneDisplayName(const std::string& relativePath)
{
    std::string name = std::filesystem::path(relativePath).stem().string();
    const std::string folder = std::filesystem::path(relativePath).parent_path().generic_string();
    return folder.empty() || folder == "scenes" ? name : folder + "/" + name;
}

} // namespace

EditorApp::EditorApp(Engine& engine, ConsoleLog& console, EditorOptions options)
    : m_engine(engine)
    , m_console(console)
    , m_options(std::move(options))
    , m_resources(findRoloResources(executablePath(), currentOs(), ROLO_SOURCE_DIR, ROLO_DEV_PLAYER))
    , m_document(std::make_unique<SceneDocument>(makeStartScene()))
{
    // The game's behaviours (linked game modules) join Rolo's components:
    // the Inspector can add them, scenes using them load, play mode runs them.
    registerGameplayComponents(m_registry);
    registerGameModules(m_registry, m_engine.logger());
    for (std::string_view module : linkedGameModules()) {
        m_engine.logger().info(kCategory, "Game module: " + std::string(module));
    }
}

ui::DockLayout EditorApp::defaultLayout()
{
    // Order matters: each dock splits the group it is placed beside, so the
    // left column and the Inspector are added first to span the full height.
    ui::DockLayout layout = ui::DockLayout::tabs({kViewportPanel});
    layout.dock(kHierarchyPanel, kViewportPanel, ui::DockSide::Left, 0.21f);
    layout.dock(kAssetsPanel, kHierarchyPanel, ui::DockSide::Bottom, 0.45f);
    layout.dock(kInspectorPanel, kViewportPanel, ui::DockSide::Right, 0.29f);
    layout.dock(kPreviewPanel, kViewportPanel, ui::DockSide::Bottom, 0.32f);
    layout.dock(kConsolePanel, kPreviewPanel, ui::DockSide::Center);
    layout.dock(kTilePalettePanel, kPreviewPanel, ui::DockSide::Center);
    layout.dock(kAnimationPanel, kPreviewPanel, ui::DockSide::Center);
    layout.activate(kConsolePanel);
    return layout;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
void EditorApp::loadSettings()
{
    m_configDir = m_options.configDirectory.empty() ? userConfigDirectory()
                                                    : std::filesystem::path(m_options.configDirectory);
    if (m_configDir.empty()) {
        log().warning(kCategory, "Could not find your home folder; layout and preferences will not be saved.");
        return;
    }
    log().info(kCategory, "Settings folder: " + m_configDir.string());

    if (const Status status = m_preferences.load(m_configDir / kPreferencesFileName); !status) {
        log().warning(kCategory, status.message);
    }
    m_state.loadFrom(m_preferences);
    if (m_options.resetLayout) {
        m_state.panels = PanelVisibility{};
    }
}

void EditorApp::saveSettings()
{
    if (m_configDir.empty()) {
        return;
    }
    m_state.saveTo(m_preferences);
    if (const Status status = m_preferences.save(m_configDir / kPreferencesFileName); !status) {
        log().error(kCategory, "Could not save preferences: " + status.message);
    }
}

void EditorApp::saveLayout()
{
    m_layoutDirty = false;
    if (m_configDir.empty() || m_dock == nullptr) {
        return;
    }
    const std::string text = m_dock->layout().serialize() + "\n";
    if (const Status status = writeTextFileAtomically(m_configDir / kLayoutFileName, text); !status) {
        log().error(kCategory, "Could not save the panel layout: " + status.message);
    }
}

void EditorApp::restoreLayout()
{
    if (!m_configDir.empty() && !m_options.resetLayout) {
        if (const auto text = readTextFile(m_configDir / kLayoutFileName)) {
            if (auto layout = ui::DockLayout::parse(*text, isEditorPanel)) {
                m_dock->setLayout(std::move(*layout));
                log().info(kCategory, "Restored the saved panel layout.");
                return;
            }
            log().warning(kCategory, "The saved panel layout could not be read; using the default one.");
        }
    }
    m_dock->resetLayout();
    log().info(kCategory, "Using the default panel layout.");
}

// ---------------------------------------------------------------------------
// Selection and editing
// ---------------------------------------------------------------------------
EntityId EditorApp::selection() const
{
    if (m_state.selectedNodeId == EditorState::kSceneSelected) {
        return {};
    }
    const EntityId id(m_state.selectedNodeId);
    return m_document->scene().contains(id) ? id : EntityId{};
}

void EditorApp::select(EntityId id)
{
    m_state.selectedNodeId = id.value();
    if (!id.isNull()) {
        m_selectedAsset = {};
    }
}

void EditorApp::selectScene()
{
    m_state.selectedNodeId = EditorState::kSceneSelected;
    m_selectedAsset = {};
}

void EditorApp::selectAsset(AssetId id)
{
    m_selectedAsset = id;
    if (!id.isNull()) {
        m_state.selectedNodeId = 0;
    }
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------
void EditorApp::refreshAssets()
{
    m_lastAssetScan = m_ui != nullptr ? m_ui->time() : 0.0;
    if (!m_assetDb) {
        return;
    }
    for (const FileIssue& issue : m_assetDb->refresh()) {
        log().warning("assets", issue.where.empty() ? issue.message : issue.where + ": " + issue.message);
    }
}

void EditorApp::checkSceneAssets()
{
    if (!m_assetDb || !m_document) {
        return;
    }
    for (const FileIssue& issue : checkAssetReferences(m_document->scene(), *m_assetDb)) {
        log().warning("assets", issue.where + ": " + issue.message);
    }
}

void EditorApp::importFiles(const std::vector<std::filesystem::path>& files)
{
    if (!m_assetDb || !m_project) {
        showMessage("No Project Open", "Assets belong to a project. Create or open a project first (File menu), "
                                       "then drop the files again.");
        return;
    }
    AssetId last;
    std::vector<std::string> failures;
    for (const std::filesystem::path& file : files) {
        std::string error;
        if (const auto id = m_assetDb->import(file, m_importFolder, &error)) {
            last = *id;
            log().info("assets", "Imported " + file.filename().string() + " as " + m_assetDb->find(*id)->path + ".");
        } else {
            log().error("assets", error);
            failures.push_back(error);
        }
    }
    if (!last.isNull()) {
        selectAsset(last);
    }
    if (!failures.empty()) {
        std::string text;
        for (const std::string& f : failures) {
            text += (text.empty() ? "" : "\n") + f;
        }
        showMessage(failures.size() == 1 ? "Could Not Import a File" : "Could Not Import Some Files", text);
    }
}

void EditorApp::placeAsset(AssetId id, Vec2 worldPosition)
{
    const AssetRecord* asset = m_assetDb ? m_assetDb->find(id) : nullptr;
    if (asset == nullptr) {
        return;
    }
    std::vector<std::unique_ptr<Component>> components;
    auto transform = std::make_unique<Transform>();
    transform->position = {std::round(worldPosition.x), std::round(worldPosition.y)};
    components.push_back(std::move(transform));
    std::string name = asset->fileName();
    if (const std::size_t dot = name.rfind('.'); dot != std::string::npos && dot > 0) {
        name.resize(dot);
    }
    if (asset->type == AssetType::Image) {
        auto sprite = std::make_unique<Sprite>();
        sprite->image = id;
        if (asset->image) {
            sprite->size = {static_cast<float>(asset->image->width), static_cast<float>(asset->image->height)};
        }
        components.push_back(std::move(sprite));
    } else if (asset->type == AssetType::Tileset && asset->tileset) {
        auto map = std::make_unique<Tilemap>();
        map->tileset = id;
        map->cellSize = {static_cast<float>(asset->tileset->tileWidth), static_cast<float>(asset->tileset->tileHeight)};
        components.push_back(std::move(map));
        m_tileLayer = 0;
    } else if (asset->type == AssetType::Audio) {
        // An object that plays the sound when the scene starts.
        auto sound = std::make_unique<Sound>();
        sound->sound = id;
        components.push_back(std::move(sound));
    } else {
        showMessage("Cannot Place This Asset", asset->fileName() + " cannot be placed in a scene. Drag an image, "
                                                                   "a tileset or a sound into the Scene view.");
        return;
    }
    auto command = std::make_unique<CreateEntity>(name, EntityId{}, std::nullopt, std::move(components));
    CreateEntity* raw = command.get();
    if (execute(std::move(command))) {
        endEdit();
        select(raw->id());
    }
}

void EditorApp::showPanel(const std::string& id)
{
    if (m_dock != nullptr) {
        m_dock->setOpen(id, true);
    }
}

void EditorApp::setSceneTool(int tool)
{
    if (m_viewport != nullptr) {
        m_viewport->setTool(static_cast<SceneTool>(tool));
    }
}

void EditorApp::dropAsset(AssetId id, ui::Vec2 windowPoint)
{
    if (m_viewport != nullptr) {
        if (const auto world = m_viewport->worldAt(windowPoint)) {
            placeAsset(id, *world);
        }
    }
}

void EditorApp::showImportDialog()
{
    if (!m_project) {
        showMessage("No Project Open", "Assets belong to a project. Create or open a project first.");
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Import Assets", 460.0f);
    ui::Column& body = dialog->body();
    auto& hint = body.emplace<ui::Label>(
        "The quickest way: drag files from Finder (or Explorer) onto this window. Or type or paste a file's "
        "path here:",
        ui::Label::Style::Dim);
    hint.setWrap(true);
    auto& path = body.emplace<ui::TextField>("/Users/you/Pictures/hero.png");
    path.setFixedSize({0.0f, 26.0f});
    auto& note = body.emplace<ui::Label>("Files are copied into the project's \"" + m_importFolder
                                             + "\" folder. Supported: PNG and JPEG images, WAV, OGG and MP3 sounds, "
                                               "TTF and OTF fonts.",
                                         ui::Label::Style::Faint);
    note.setWrap(true);
    dialog->addButton("Cancel");
    dialog->addButton("Import", ui::Button::Style::Primary, [this, &path] {
        std::string text = path.text();
        // Paths copied from Finder may come with quotes or a trailing space.
        while (!text.empty() && (text.back() == ' ' || text.back() == '\'' || text.back() == '"')) {
            text.pop_back();
        }
        while (!text.empty() && (text.front() == '\'' || text.front() == '"')) {
            text.erase(text.begin());
        }
        if (!text.empty()) {
            importFiles({std::filesystem::path(text)});
        }
    });
    ui::showDialog(*m_ui, std::move(dialog));
    m_ui->setFocus(&path);
}

bool EditorApp::execute(std::unique_ptr<Command> command)
{
    if (m_game) {
        refuseEditWhilePlaying();
        return false;
    }
    return m_document->execute(std::move(command));
}

void EditorApp::undo()
{
    if (m_game) {
        refuseEditWhilePlaying();
        return;
    }
    m_document->undo();
}

void EditorApp::redo()
{
    if (m_game) {
        refuseEditWhilePlaying();
        return;
    }
    m_document->redo();
}

void EditorApp::refuseEditWhilePlaying()
{
    if (!m_editRefused) {
        m_editRefused = true;
        log().warning(kCategory, "The scene cannot be changed while the game is playing. Stop the game (■) to edit.");
    }
}

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------
void EditorApp::createContent(AssetType type)
{
    if (!m_assetDb) {
        showMessage("No Project", "Open or create a project first: content files belong to a project.");
        return;
    }
    // A small working example, so the new file can be played at once.
    std::string name;
    std::string extension;
    std::string text;
    switch (type) {
    case AssetType::Dialogue: {
        name = "New Dialogue";
        extension = ContentFormat::kDialogueExtension;
        Dialogue d;
        d.speakers = {{"someone", "Someone", Color{255, 255, 255, 255}, {}}};
        d.start = "hello";
        DialogueNode hello;
        hello.id = "hello";
        hello.speaker = "someone";
        hello.text = "Hello!";
        d.nodes = {hello};
        text = serializeDialogue(d);
        break;
    }
    case AssetType::Quest: {
        name = "New Quest";
        extension = ContentFormat::kQuestExtension;
        Quest q;
        q.name = "New Quest";
        q.objectives = {{"first", "Do something", std::nullopt, false}};
        text = serializeQuest(q);
        break;
    }
    case AssetType::Variables:
        name = "Variables";
        extension = ContentFormat::kVariablesExtension;
        text = serializeVariables({});
        break;
    case AssetType::Script:
        // Lowercase with underscores, so require("new_script") works as written.
        name = "new_script";
        extension = ".lua";
        text = newScriptTemplate(name);
        break;
    default: return;
    }
    std::string error;
    const char* folder = type == AssetType::Script ? "scripts" : "content";
    if (const auto id = m_assetDb->createTextAsset(folder, name, extension, text, &error)) {
        log().info(kCategory, "Created " + m_assetDb->find(*id)->path + ".");
        selectAsset(*id);
        refreshAssets();
    } else {
        showMessage("Could Not Create the File", error);
    }
}

std::size_t EditorApp::checkContent()
{
    if (!m_assetDb) {
        log().info("content", "No project is open, so there is no content to check.");
        return 0;
    }
    m_assetDb->refresh();
    std::vector<FileIssue> issues;
    const ContentLibrary library = ContentLibrary::load(*m_assetDb, &issues);
    for (FileIssue& issue : library.check()) {
        issues.push_back(std::move(issue));
    }
    for (FileIssue& issue : checkScripts(*m_assetDb)) {   // Lua syntax errors
        issues.push_back(std::move(issue));
    }
    // The open scene's triggers (as edited, saved or not).
    const std::string sceneName = m_document->isUntitled() ? std::string("this scene") : m_document->displayName();
    m_document->scene().forEach([&](const Entity& e, int) {
        if (const Trigger* t = e.get<Trigger>()) {
            const std::string where = sceneName + ", object '" + e.name() + "', Trigger";
            if (t->reaction.condition) {
                library.checkCondition(*t->reaction.condition, where, issues);
            }
            library.checkActions(t->reaction.actions, where, issues);
            for (const Action& a : t->reaction.actions) {
                if ((a.kind == Action::Kind::SetActive || a.kind == Action::Kind::Destroy) && !a.entity.isNull()
                    && !m_document->scene().contains(a.entity)) {
                    issues.push_back({FileIssue::Severity::Error, where,
                                      "An action names object " + a.entity.toString() + ", which is not in this scene."});
                }
            }
            if ((t->on == TriggerOn::Interact || t->on == TriggerOn::EnterArea || t->on == TriggerOn::LeaveArea)
                && e.get<Collider>() == nullptr) {
                issues.push_back({FileIssue::Severity::Warning, where,
                                  "This trigger needs a Collider on the same object: it is the trigger's area."});
            }
        }
    });
    for (const FileIssue& issue : issues) {
        (issue.isError() ? log().error("content", issue.where + ": " + issue.message)
                         : log().warning("content", issue.where + ": " + issue.message));
    }
    const std::size_t errors =
        static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const FileIssue& i) { return i.isError(); }));
    log().info("content", issues.empty() ? std::string("Content check: no problems found.")
                                         : "Content check: " + std::to_string(errors) + " errors and "
                                               + std::to_string(issues.size() - errors) + " warnings (see above).");
    return issues.size();
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------
void EditorApp::play()
{
    if (m_game) {
        return;
    }
    m_document->history().breakMerge();

    // The project's content as it is on disk now, checked: problems go to
    // the Console but do not stop the game.
    m_playContent = std::make_unique<ContentLibrary>();
    if (m_assetDb) {
        m_assetDb->refresh();
        std::vector<FileIssue> issues;
        *m_playContent = ContentLibrary::load(*m_assetDb, &issues);
        for (FileIssue& issue : m_playContent->check()) {
            issues.push_back(std::move(issue));
        }
        for (const FileIssue& issue : issues) {
            (issue.isError() ? log().error("content", issue.where + ": " + issue.message)
                             : log().warning("content", issue.where + ": " + issue.message));
        }
    }
    // Saves made while playing in the editor stay in the editor's settings
    // folder, apart from the real game's saves.
    const std::string gameName = m_project ? m_project->settings().name : std::string("Untitled");
    m_playSaves.reset();
    if (!m_configDir.empty()) {
        m_playSaves = std::make_unique<FolderSaveStore>(m_configDir / "play-saves" / gameName);
    }
    // Sounds are read again for each run, so edited files are heard as they are now.
    stopSoundPreview();
    audio().setAssets(m_assetDb.get());
    m_audioAssets = m_assetDb.get();
    audio().setPaused(false);
    GameSetup setup;
    setup.sound = m_audio.get();
    setup.assets = m_assetDb.get();
    setup.content = m_playContent.get();
    setup.saves = m_playSaves.get();
    setup.gameName = gameName;
    setup.loadScene = [this](AssetId id, std::string& error) { return loadSceneForGame(id, error); };
    m_game = std::make_unique<Game>(m_engine.logger(), std::move(setup));
    AssetId sceneId;
    if (m_assetDb && m_project && !m_document->isUntitled()) {
        if (const auto relative = m_project->relative(m_document->path())) {
            if (const AssetRecord* record = m_assetDb->findByPath(*relative)) {
                sceneId = record->id;
            }
        }
    }
    m_game->start(m_document->scene(), sceneId);
    m_paused = false;
    m_stepPending = false;
    m_editRefused = false;
    m_lastFrameTime = -1.0;
    log().info(kCategory, "Playing '" + m_document->scene().name()
                              + "'. The game gets the keyboard while the Scene view is focused; stop with ■ or "
                              + ui::primaryShortcutLabel(m_ui->platform(), "P") + ".");
    showPanel(kViewportPanel);
    if (m_viewport != nullptr) {
        m_viewport->focusView();
    }
}

AudioEngine& EditorApp::audio()
{
    if (!m_audio) {
        m_audio = std::make_unique<AudioEngine>(m_engine.logger());
        m_audio->setChannelVolume(SoundChannel::Master, m_preferences.getBool(kMuteKey, false) ? 0.0f : 1.0f);
    }
    return *m_audio;
}

void EditorApp::previewSound(AssetId sound)
{
    stopSoundPreview();
    if (m_assetDb == nullptr) {
        return;
    }
    if (m_audioAssets != m_assetDb.get() && !m_game) {
        audio().setAssets(m_assetDb.get());
        m_audioAssets = m_assetDb.get();
    }
    m_soundPreview = audio().play(sound);
}

void EditorApp::stopSoundPreview()
{
    if (m_audio && m_soundPreview != 0) {
        m_audio->stop(m_soundPreview);
    }
    m_soundPreview = 0;
}

void EditorApp::stopPlaying()
{
    if (!m_game) {
        return;
    }
    const std::uint64_t frames = m_game->frameCount();
    m_game.reset();   // the game's copy of the scene (and its sounds) go with it
    m_playContent.reset();
    m_paused = false;
    if (m_audio) {
        m_audio->setPaused(false);
    }
    log().info(kCategory, "Stopped playing after " + std::to_string(frames)
                              + " frames. The scene is back as it was before playing.");
}

void EditorApp::togglePause()
{
    if (m_game) {
        m_paused = !m_paused;
        audio().setPaused(m_paused);   // sounds hold too
        log().info(kCategory, m_paused ? "Paused." : "Resumed.");
    }
}

void EditorApp::stepFrame()
{
    if (m_game) {
        m_paused = true;
        m_stepPending = true;
        audio().setPaused(true);
    }
}

void EditorApp::updateGame(UiFrame& frame)
{
    if (m_audio) {
        m_audio->update();   // frees finished sounds (the game's and previews)
    }
    if (!m_game) {
        return;
    }
    const double now = frame.ui().time();
    const float dt = m_lastFrameTime < 0.0 ? 1.0f / 60.0f : static_cast<float>(now - m_lastFrameTime);
    m_lastFrameTime = now;
    // F5 saves to the "quick" slot, F9 loads it (as in the player).
    if (m_game->input().wasPressed(Key::F5)) {
        m_game->save("quick");
    }
    if (m_game->input().wasPressed(Key::F9)) {
        m_game->load("quick");
    }
    // Gamepads go to the game whenever it plays (unlike the keyboard, they
    // cannot be typing into a panel).
    if (!m_gamepads) {
        m_gamepads = std::make_unique<GamepadReader>(m_engine.logger());
    }
    m_gamepads->read(m_game->input());
    if (!m_paused) {
        m_game->update(dt);
    } else if (m_stepPending) {
        m_stepPending = false;
        m_game->update(1.0f / 60.0f);
    }
    // Animations follow game time, so pausing freezes them too. (While
    // editing, the runtime's own clock plays them in the Scene view.)
    frame.setAnimationTime(m_game->time());
}

std::optional<Scene> EditorApp::loadSceneForGame(AssetId id, std::string& error)
{
    const AssetRecord* record = m_assetDb ? m_assetDb->find(id) : nullptr;
    if (record == nullptr || record->type != AssetType::Scene) {
        error = "it is not a scene of this project";
        return std::nullopt;
    }
    const std::filesystem::path path = m_assetDb->absolutePath(*record);
    // The scene being edited: play it as it is now, unsaved changes included.
    std::error_code ec;
    if (!m_document->isUntitled() && std::filesystem::equivalent(path, m_document->path(), ec)) {
        return m_document->scene();
    }
    SceneLoadResult loaded = loadSceneFile(path, m_registry);
    reportIssues(path, loaded.issues);
    if (!loaded.ok()) {
        error = record->path + " could not be read";
        return std::nullopt;
    }
    return std::move(*loaded.scene);
}

void EditorApp::endEdit()
{
    m_document->history().breakMerge();
}

void EditorApp::createEntity(NewEntityKind kind, bool asChildOfSelection)
{
    const EntityId parent = asChildOfSelection ? selection() : EntityId{};
    if (asChildOfSelection && parent.isNull()) {
        return;
    }
    std::vector<std::unique_ptr<Component>> components;
    // New top-level objects appear in the middle of the world; children at
    // their parent's origin.
    auto transform = std::make_unique<Transform>();
    if (parent.isNull()) {
        transform->position = Vec2{640.0f, 360.0f};
    }
    components.push_back(std::move(transform));
    std::string name = "Object";
    if (kind == NewEntityKind::Sprite) {
        name = "Sprite";
        components.push_back(std::make_unique<Sprite>());
    } else if (kind == NewEntityKind::Camera) {
        name = "Camera";
        components.push_back(std::make_unique<Camera>());
    } else if (kind == NewEntityKind::Tilemap) {
        name = "Tilemap";
        auto map = std::make_unique<Tilemap>();
        // Use the project's first tileset, if it has one, at its tile size.
        if (m_assetDb) {
            for (const AssetRecord* a : m_assetDb->all(AssetType::Tileset)) {
                if (a->tileset) {
                    map->tileset = a->id;
                    map->cellSize = {static_cast<float>(a->tileset->tileWidth), static_cast<float>(a->tileset->tileHeight)};
                    break;
                }
            }
        }
        // A tilemap's cell (0, 0) is at its position: start at the world's corner.
        if (parent.isNull()) {
            static_cast<Transform&>(*components.front()).position = Vec2{0.0f, 0.0f};
        }
        components.push_back(std::move(map));
        m_tileLayer = 0;
    } else if (kind != NewEntityKind::Empty) {
        auto shape = std::make_unique<Shape>();
        shape->kind = kind == NewEntityKind::Rectangle ? ShapeKind::Rectangle : ShapeKind::Ellipse;
        shape->color = Color{79, 179, 169, 255};
        name = kind == NewEntityKind::Rectangle ? "Rectangle" : "Ellipse";
        components.push_back(std::move(shape));
    }
    auto command = std::make_unique<CreateEntity>(name, parent, std::nullopt, std::move(components));
    CreateEntity* raw = command.get();
    if (execute(std::move(command))) {
        endEdit();
        select(raw->id());
    }
}

void EditorApp::deleteSelection()
{
    const EntityId id = selection();
    if (id.isNull()) {
        return;
    }
    // Select a neighbour so the keyboard keeps working in the tree.
    const Scene& scene = m_document->scene();
    const auto& siblings = scene.childrenOf(scene.find(id)->parent());
    const std::size_t index = scene.indexOf(id).value_or(0);
    EntityId next = scene.find(id)->parent();
    if (index + 1 < siblings.size()) {
        next = siblings[index + 1];
    } else if (index > 0) {
        next = siblings[index - 1];
    }
    if (execute(std::make_unique<DeleteEntity>(id))) {
        endEdit();
        select(next);
    }
}

void EditorApp::duplicateSelection()
{
    const EntityId id = selection();
    if (id.isNull()) {
        return;
    }
    auto command = std::make_unique<DuplicateEntity>(id);
    DuplicateEntity* raw = command.get();
    if (execute(std::move(command))) {
        endEdit();
        select(raw->copyId());
    }
}

void EditorApp::renameSelection()
{
    if (selection().isNull() && !isSceneSelected()) {
        return;
    }
    if (m_dock != nullptr) {
        m_dock->setOpen(kInspectorPanel, true);
    }
    m_renamePending = true; // done after the Inspector has caught up with the selection
}

void EditorApp::moveSelection(int direction)
{
    const EntityId id = selection();
    if (id.isNull()) {
        return;
    }
    const Scene& scene = m_document->scene();
    const EntityId parent = scene.find(id)->parent();
    const std::size_t index = scene.indexOf(id).value_or(0);
    const std::size_t count = scene.childrenOf(parent).size();
    if ((direction < 0 && index == 0) || (direction > 0 && index + 1 >= count)) {
        return;
    }
    const std::size_t target = direction < 0 ? index - 1 : index + 1;
    if (execute(std::make_unique<MoveEntity>(id, parent, target))) {
        endEdit();
    }
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
void EditorApp::reportIssues(const std::filesystem::path& file, const std::vector<FileIssue>& issues)
{
    for (const FileIssue& issue : issues) {
        const std::string text = file.filename().string() + ": " + issue.toString();
        if (issue.isError()) {
            log().error("scene", text);
        } else {
            log().warning("scene", text);
        }
    }
}

void EditorApp::setDocument(std::unique_ptr<SceneDocument> document)
{
    stopPlaying();   // the game was playing the scene being replaced
    m_document = std::move(document);
    m_document->history().setAssets(m_assetDb.get());   // for undoable asset changes
    ++m_documentGeneration;
    m_state.selectedNodeId = 0;
    if (!m_document->isUntitled()) {
        m_state.lastScene = m_document->path().string();
    }
    checkSceneAssets();
}

void EditorApp::openScene(const std::filesystem::path& path)
{
    if (!m_document->isUntitled() && m_document->path() == path) {
        return; // already open
    }
    confirmDiscard("opening another scene", [this, path] {
        std::vector<FileIssue> issues;
        auto opened = SceneDocument::open(path, m_registry, issues);
        reportIssues(path, issues);
        if (!opened) {
            showMessage("Could Not Open Scene", "The scene " + path.filename().string()
                                                    + " could not be opened. The Console has the details.");
            return;
        }
        setDocument(std::make_unique<SceneDocument>(std::move(*opened)));
        log().info(kCategory, "Opened scene " + path.filename().string() + ".");
    });
}

void EditorApp::newScene()
{
    confirmDiscard("creating a new scene", [this] {
        setDocument(std::make_unique<SceneDocument>(Scene("New Scene")));
        selectScene();
        log().info(kCategory, "New scene. Save it with File > Save Scene.");
    });
}

void EditorApp::reloadScene()
{
    if (m_document->isUntitled()) {
        return;
    }
    const std::filesystem::path path = m_document->path();
    const auto reload = [this, path] {
        std::vector<FileIssue> issues;
        auto opened = SceneDocument::open(path, m_registry, issues);
        reportIssues(path, issues);
        if (opened) {
            const std::uint64_t keep = m_state.selectedNodeId;
            setDocument(std::make_unique<SceneDocument>(std::move(*opened)));
            m_state.selectedNodeId = keep;
            log().info(kCategory, "Reverted " + path.filename().string() + " to its saved version.");
        }
    };
    if (!m_document->isDirty()) {
        return; // nothing to revert
    }
    auto dialog = std::make_unique<ui::Dialog>("Revert to Saved?", 420.0f);
    dialog->body()
        .emplace<ui::Label>("Your unsaved changes to “" + m_document->displayName() + "” will be lost.")
        .setWrap(true);
    dialog->addButton("Cancel");
    dialog->addButton("Revert", ui::Button::Style::Primary, reload);
    ui::showDialog(*m_ui, std::move(dialog));
}

void EditorApp::saveScene(std::function<void()> then)
{
    if (m_document->isUntitled()) {
        saveSceneAs(std::move(then));
        return;
    }
    if (const Status status = m_document->save(); !status) {
        log().error(kCategory, "Could not save the scene: " + status.message);
        showMessage("Could Not Save", status.message);
        return;
    }
    log().info(kCategory, "Saved " + m_document->displayName() + ".");
    if (m_assets != nullptr) {
        m_assets->refresh();
    }
    if (then) {
        then();
    }
}

void EditorApp::saveSceneAs(std::function<void()> then)
{
    if (!m_project) {
        // Scenes live in projects: offer to make one, keeping this scene.
        showNewProjectDialog(true, std::move(then));
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Save Scene", 420.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Scene name", ui::Label::Style::Dim);
    auto& name = body.emplace<ui::TextField>("");
    std::string suggestion = m_document->isUntitled() ? m_document->scene().name()
                                                      : m_document->path().stem().string() + " copy";
    if (!Project::isValidName(suggestion)) {
        suggestion = "New Scene";
    }
    name.setText(suggestion);
    auto& where = body.emplace<ui::Label>("", ui::Label::Style::Faint);
    where.setWrap(true);
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);

    const auto target = [this](const std::string& text) {
        return m_project->absolute(std::string(ProjectFormat::kScenesFolder) + "/" + text
                                   + std::string(SceneFormat::kExtension));
    };
    const auto updateHint = [this, &where, &name] {
        where.setText("Saved as " + std::string(ProjectFormat::kScenesFolder) + "/" + name.text()
                      + std::string(SceneFormat::kExtension) + " in " + m_project->settings().name + ".");
    };
    updateHint();
    name.setOnChange([updateHint](const std::string&) { updateHint(); });

    // Saving is checked first; the dialog only closes when it worked.
    auto save = [this, &name, &error, target, then](ui::Dialog& d) {
        const std::string text = name.text();
        if (!Project::isValidName(text)) {
            error.setText("A scene name cannot be empty or contain / \\ : * ? \" < > |.");
            return;
        }
        const std::filesystem::path path = target(text);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            error.setText("A scene called “" + text + "” already exists. Choose another name.");
            return;
        }
        if (const Status status = m_document->saveAs(path); !status) {
            error.setText(status.message);
            return;
        }
        m_state.lastScene = path.string();
        ++m_documentGeneration; // the document is now a different file
        log().info(kCategory, "Saved " + path.filename().string() + ".");
        if (m_assets != nullptr) {
            m_assets->refresh();
        }
        m_ui->closeOverlay(&d);
        if (then) {
            m_ui->post(then);
        }
    };
    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton("Save", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    ok.setOnClick([save, &d] { save(d); });
    name.setOnSubmit([save, &d](const std::string&) { save(d); });
    m_ui->setFocus(&name);
    name.state().selectAll();
}

void EditorApp::confirmDiscard(const std::string& action, std::function<void()> then)
{
    if (!m_document->isDirty()) {
        then();
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Save Changes?", 440.0f);
    dialog->body()
        .emplace<ui::Label>("“" + m_document->displayName() + "” has unsaved changes. Save them before "
                            + action + "?")
        .setWrap(true);
    dialog->addButton("Don’t Save", ui::Button::Style::Normal, then);
    dialog->addButton("Cancel");
    dialog->addButton("Save", ui::Button::Style::Primary, [this, then] { saveScene(then); });
    ui::showDialog(*m_ui, std::move(dialog));
}

bool EditorApp::openProjectFolder(const std::filesystem::path& folder, bool reportErrors)
{
    std::string error;
    auto project = Project::open(folder, &error);
    if (!project) {
        log().error(kCategory, error);
        if (reportErrors) {
            showMessage("Could Not Open Project", error);
        }
        return false;
    }
    m_project = std::move(*project);
    m_state.addRecentProject(m_project->folder().string());
    stopPlaying();   // the game uses the old project's assets
    if (m_assetDb) {
        m_retiredAssetDbs.push_back(std::move(m_assetDb)); // the runtime may still draw from it this frame
    }
    m_assetDb = std::make_unique<AssetDatabase>(m_project->folder());
    if (m_document) {
        m_document->history().setAssets(m_assetDb.get());   // never leave undo steps pointing at the old one
    }
    m_selectedAsset = {};
    m_importFolder = std::string(AssetFormat::kDefaultFolder);
    refreshAssets();
    log().info(kCategory, "Opened project '" + m_project->settings().name + "' (" + m_project->folder().string() + ")");

    // The scene open last time, if it is in this project; otherwise the start scene.
    std::filesystem::path scene = m_project->absolute(m_project->settings().startScene);
    std::error_code ec;
    if (!m_state.lastScene.empty() && m_project->relative(m_state.lastScene)
        && std::filesystem::exists(m_state.lastScene, ec)) {
        scene = m_state.lastScene;
    }
    std::vector<FileIssue> issues;
    auto opened = SceneDocument::open(scene, m_registry, issues);
    reportIssues(scene, issues);
    if (opened) {
        setDocument(std::make_unique<SceneDocument>(std::move(*opened)));
        log().info(kCategory, "Opened scene " + scene.filename().string() + ".");
    } else {
        log().warning(kCategory, "The project's start scene could not be opened; starting a new scene.");
        setDocument(std::make_unique<SceneDocument>(Scene("New Scene")));
    }
    if (m_assets != nullptr) {
        m_assets->refresh();
    }
    return true;
}

void EditorApp::openInitialContent()
{
    if (!m_options.newProjectPath.empty()) {
        const std::filesystem::path folder = m_options.newProjectPath;
        std::string error;
        bool made = false;
        if (!m_options.templateId.empty()) {
            const auto templates = listProjectTemplates(m_resources);
            const auto it = std::find_if(templates.begin(), templates.end(),
                                         [&](const ProjectTemplate& t) { return t.id == m_options.templateId; });
            if (it == templates.end()) {
                std::string names;
                for (const ProjectTemplate& t : templates) {
                    names += (names.empty() ? "" : ", ") + t.id;
                }
                error = "There is no template called '" + m_options.templateId + "'. Templates: " + names + ".";
            } else {
                made = createProjectFromTemplate(*it, folder, folder.filename().string(), &error);
            }
        } else {
            made = Project::create(folder, folder.filename().string(), &error).has_value();
        }
        if (made) {
            log().info(kCategory, "Created project in " + folder.string());
            openProjectFolder(folder, false);
        } else {
            log().error(kCategory, error);
        }
        return;
    }
    if (!m_options.projectPath.empty()) {
        openProjectFolder(m_options.projectPath, false);
        return;
    }
    if (!m_options.noProject && !m_state.recentProjects.empty()) {
        const std::string last = m_state.recentProjects.front();
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(last) / ProjectFormat::kFileName, ec)) {
            openProjectFolder(last, false);
            return;
        }
        m_state.removeRecentProject(last);
    }
    log().info(kCategory, "No project open: showing the start scene. Use File > New Project to create one.");
}

void EditorApp::newProject()
{
    confirmDiscard("creating a project", [this] { showNewProjectDialog(false); });
}

void EditorApp::showNewProjectDialog(bool keepCurrentScene, std::function<void()> then)
{
    auto dialog = std::make_unique<ui::Dialog>("New Project", 480.0f);
    ui::Column& body = dialog->body();
    if (keepCurrentScene) {
        body.emplace<ui::Label>("Scenes are saved inside a project. Create one now; this scene becomes its first "
                                "scene.",
                                ui::Label::Style::Dim)
            .setWrap(true);
    }
    // What to start from: a template or an example game (not when an
    // existing scene becomes the new project's first scene).
    auto templates = std::make_shared<std::vector<ProjectTemplate>>();
    auto chosen = std::make_shared<int>(-1);
    if (!keepCurrentScene) {
        *templates = listProjectTemplates(m_resources);
    }
    if (!templates->empty()) {
        body.emplace<ui::Label>("Start from", ui::Label::Style::Dim);
        std::vector<std::string> names;
        for (std::size_t i = 0; i < templates->size(); ++i) {
            names.push_back((*templates)[i].name);
            if ((*templates)[i].id == "topdown") {
                *chosen = static_cast<int>(i);   // the friendliest start: something to play at once
            }
        }
        if (*chosen < 0) {
            *chosen = 0;
        }
        auto& choice = body.emplace<ui::ChoiceField>(std::move(names));
        choice.setSelected(*chosen);
        auto& about = body.emplace<ui::Label>((*templates)[static_cast<std::size_t>(*chosen)].description,
                                              ui::Label::Style::Faint);
        about.setWrap(true);
        choice.setOnChoose([templates, chosen, &about](int index) {
            if (index >= 0 && static_cast<std::size_t>(index) < templates->size()) {
                *chosen = index;
                about.setText((*templates)[static_cast<std::size_t>(index)].description);
            }
        });
    } else if (!keepCurrentScene) {
        body.emplace<ui::Label>("No templates were found (" + m_resources.templates.string()
                                    + "), so the project starts empty. Reinstalling Rolo brings them back.",
                                ui::Label::Style::Faint)
            .setWrap(true);
    }
    body.emplace<ui::Label>("Project name", ui::Label::Style::Dim);
    auto& name = body.emplace<ui::TextField>("");
    name.setText("My Game");
    body.emplace<ui::Label>("Location", ui::Label::Style::Dim);
    auto& locationRow = body.emplace<ui::Row>(6.0f);
    auto& location = locationRow.emplace<ui::TextField>("");
    location.setStretch(1.0f);
    location.setText(defaultProjectsDirectory().string());

    auto& hint = body.emplace<ui::Label>("", ui::Label::Style::Faint);
    hint.setWrap(true);
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);

    const auto updateHint = [&hint, &name, &location] {
        hint.setText("Creates the folder " + (std::filesystem::path(location.text()) / name.text()).string());
    };
    updateHint();
    name.setOnChange([updateHint](const std::string&) { updateHint(); });
    location.setOnChange([updateHint](const std::string&) { updateHint(); });
    locationRow.emplace<ui::Button>("Browse…", [this, &location, updateHint] {
        showFolderPicker(*m_ui, "Where to Create the Project", location.text(),
                         [&location, updateHint](const std::filesystem::path& folder) {
                             location.setText(folder.string());
                             updateHint();
                         });
    });

    auto create = [this, &name, &location, &error, keepCurrentScene, then, templates, chosen](ui::Dialog& d) {
        if (location.text().empty()) {
            error.setText("Choose a location: the folder the project folder is created in.");
            return;
        }
        const std::filesystem::path folder = std::filesystem::path(location.text()) / name.text();
        std::string message;
        std::optional<Project> project;
        if (*chosen >= 0 && static_cast<std::size_t>(*chosen) < templates->size()) {
            const ProjectTemplate& from = (*templates)[static_cast<std::size_t>(*chosen)];
            if (createProjectFromTemplate(from, folder, name.text(), &message)) {
                project = Project::open(folder, &message);
            }
        } else {
            project = Project::create(folder, name.text(), &message);
        }
        if (!project) {
            error.setText(message);
            return;
        }
        log().info(kCategory, "Created project '" + name.text() + "' in " + folder.string());
        std::unique_ptr<SceneDocument> keep;
        if (keepCurrentScene) {
            keep = std::move(m_document);
            m_document = std::make_unique<SceneDocument>(Scene()); // placeholder until the project opens
        }
        openProjectFolder(project->folder(), true);
        if (keep) {
            // The current scene replaces the project's first scene.
            const std::filesystem::path path = m_project->absolute(m_project->settings().startScene);
            if (const Status s = keep->saveAs(path); !s) {
                log().error(kCategory, "Could not save the scene into the new project: " + s.message);
            }
            setDocument(std::move(keep));
        }
        m_ui->closeOverlay(&d);
        if (then) {
            m_ui->post(then);
        }
    };
    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton("Create Project", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    ok.setOnClick([create, &d] { create(d); });
    name.setOnSubmit([create, &d](const std::string&) { create(d); });
    location.setOnSubmit([create, &d](const std::string&) { create(d); });
    m_ui->setFocus(&name);
    name.state().selectAll();
}

// --- Exporting ------------------------------------------------------------------------------------

void EditorApp::showExportDialog()
{
    if (!m_project) {
        showMessage("Export Game", "Open or create a project first: a game is exported from its project.");
        return;
    }
    if (m_game) {
        stopPlaying();
    }
    const char* system = currentOs() == HostOs::MacOS ? "macOS" : currentOs() == HostOs::Windows ? "Windows" : "Linux";
    auto dialog = std::make_unique<ui::Dialog>("Export Game", 500.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>(std::string("Makes a copy of your game that runs without the editor, for ") + system
                                + " (this computer). To make a version for another system, export on that system.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    body.emplace<ui::Label>("Game name", ui::Label::Style::Dim);
    auto& name = body.emplace<ui::TextField>("");
    name.setText(m_project->settings().name);
    body.emplace<ui::Label>("Export into", ui::Label::Style::Dim);
    auto& folderRow = body.emplace<ui::Row>(6.0f);
    auto& folder = folderRow.emplace<ui::TextField>("");
    folder.setStretch(1.0f);
    const std::filesystem::path projects = defaultProjectsDirectory();
    folder.setText((projects.empty() ? m_project->folder().parent_path() : projects.parent_path() / "Rolo Exports")
                       .string());
    auto& hint = body.emplace<ui::Label>("", ui::Label::Style::Faint);
    hint.setWrap(true);
    const auto updateHint = [&hint, &name, &folder] {
        const std::string file = exportFileName(name.text());
        hint.setText("Creates " + (std::filesystem::path(folder.text()) / (currentOs() == HostOs::MacOS ? file + ".app" : file)).string()
                     + ", replacing an earlier export of the same game.");
    };
    updateHint();
    name.setOnChange([updateHint](const std::string&) { updateHint(); });
    folder.setOnChange([updateHint](const std::string&) { updateHint(); });
    folderRow.emplace<ui::Button>("Browse…", [this, &folder, updateHint] {
        showFolderPicker(*m_ui, "Export Into", folder.text(), [&folder, updateHint](const std::filesystem::path& chosen) {
            folder.setText(chosen.string());
            updateHint();
        });
    });
    if (m_document->isDirty()) {
        body.emplace<ui::Label>("The open scene has unsaved changes. It is saved first, so the game has them.",
                                ui::Label::Style::Faint)
            .setWrap(true);
    }
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);
    std::error_code ec;
    const bool runtimeFound = !m_resources.player.empty() && std::filesystem::exists(m_resources.player, ec);
    if (!runtimeFound) {
        error.setText("Rolo's game runtime (rolo_player) was not found"
                      + (m_resources.player.empty() ? std::string() : " at " + m_resources.player.string())
                      + ". Reinstall Rolo; when working from source, build the rolo_player target.");
    }
    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton(m_document->isDirty() ? "Save and Export" : "Export",
                                       ui::Button::Style::Primary);
    ok.setEnabled(runtimeFound);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    ok.setOnClick([this, &d, &name, &folder, &error] {
        if (folder.text().empty()) {
            error.setText("Choose the folder to export into.");
            return;
        }
        const std::string gameName = name.text();
        const std::filesystem::path into = folder.text();
        m_ui->closeOverlay(&d);
        if (m_document->isDirty()) {
            saveScene([this, gameName, into] { exportNow(gameName, into); });
        } else {
            exportNow(gameName, into);
        }
    });
}

void EditorApp::exportNow(const std::string& name, const std::filesystem::path& folder)
{
    const ExportRun run = exportProject(m_project->folder(), folder, name, m_resources, m_registry, log());
    if (!run.ok) {
        showMessage("Could Not Export", run.message);
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Game Exported", 480.0f);
    dialog->body().emplace<ui::Label>(run.message).setWrap(true);
    dialog->body()
        .emplace<ui::Label>("Players run " + run.result.program.filename().string()
                                + ". Share the whole folder (zip it); its licenses folder must stay with the game.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    const std::filesystem::path where = run.result.game.parent_path();
    dialog->addButton("Show in Folder", ui::Button::Style::Normal, [where] { revealInFileManager(where); });
    dialog->addButton("Done", ui::Button::Style::Primary);
    ui::showDialog(*m_ui, std::move(dialog));
}

void EditorApp::openProject()
{
    confirmDiscard("opening a project", [this] { showOpenProjectDialog(); });
}

void EditorApp::showOpenProjectDialog()
{
    auto dialog = std::make_unique<ui::Dialog>("Open Project", 480.0f);
    ui::Column& body = dialog->body();
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);

    auto open = [this, &error](ui::Dialog& d, const std::string& folder) {
        if (folder.empty()) {
            return;
        }
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(folder) / ProjectFormat::kFileName, ec)) {
            error.setText("There is no Rolo project in " + folder + " (no project.rolo file).");
            return;
        }
        if (openProjectFolder(folder, true)) {
            m_ui->closeOverlay(&d);
        }
    };

    if (!m_state.recentProjects.empty()) {
        body.emplace<ui::Label>("Recent projects", ui::Label::Style::Dim);
    }
    std::vector<std::pair<ui::Button*, std::string>> recentButtons;
    for (const std::string& folder : m_state.recentProjects) {
        auto& button = body.emplace<ui::Button>(std::filesystem::path(folder).filename().string());
        button.setStyle(ui::Button::Style::Subtle);
        button.setIcon(ui::Icon::Folder);
        button.setTooltip(folder);
        recentButtons.emplace_back(&button, folder);
    }
    body.emplace<ui::Label>("Project folder", ui::Label::Style::Dim);
    auto& pathRow = body.emplace<ui::Row>(6.0f);
    auto& path = pathRow.emplace<ui::TextField>("Path to the folder containing project.rolo");
    path.setStretch(1.0f);
    if (m_state.recentProjects.empty()) {
        path.setText(defaultProjectsDirectory().string());
    }
    auto& browse = pathRow.emplace<ui::Button>("Browse…");
    body.emplace<ui::Label>("Browse to the project's folder (marked with a flame), or type or paste its path.",
                            ui::Label::Style::Faint)
        .setWrap(true);

    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton("Open", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    ok.setOnClick([open, &d, &path] { open(d, path.text()); });
    path.setOnSubmit([open, &d](const std::string& text) { open(d, text); });
    browse.setOnClick([this, open, &d, &path] {
        showFolderPicker(*m_ui, "Open Project", path.text(), [open, &d, &path](const std::filesystem::path& folder) {
            path.setText(folder.string());
            open(d, folder.string());
        }, true);
    });
    for (auto& entry : recentButtons) {
        entry.first->setOnClick([open, &d, target = entry.second] { open(d, target); });
    }
    m_ui->setFocus(&path);
}

void EditorApp::showMessage(const std::string& title, const std::string& text)
{
    if (m_ui == nullptr) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>(title, 420.0f);
    dialog->body().emplace<ui::Label>(text).setWrap(true);
    dialog->addButton("OK", ui::Button::Style::Primary);
    ui::showDialog(*m_ui, std::move(dialog));
}

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------
void EditorApp::buildMenus(ui::Widget& root)
{
    const ui::Platform& platform = m_ui->platform();
    const auto key = [&platform](std::string_view k) { return ui::primaryShortcutLabel(platform, k); };
    const auto hasSelection = [this] { return !selection().isNull(); };

    auto& menuBar = root.emplace<ui::MenuBar>();
    menuBar.addMenu({"File",
                     {menuItem("New Project…", "", [this] { newProject(); }),
                      menuItem("Open Project…", "", [this] { openProject(); }),
                      menuItem(
                          "Show Project Folder", "",
                          [this] {
                              if (m_project) {
                                  revealInFileManager(m_project->folder());
                              }
                          },
                          [this] { return m_project.has_value(); }),
                      ui::MenuItem::divider(),
                      menuItem("New Scene", key("N"), [this] { newScene(); }),
                      menuItem("Save Scene", key("S"), [this] { saveScene(); }),
                      menuItem("Save Scene As…", platform.macConventions ? "⇧⌘S" : "Ctrl+Shift+S",
                               [this] { saveSceneAs(); }),
                      menuItem("Import Assets…", "", [this] { showImportDialog(); },
                               [this] { return m_project.has_value(); }),
                      menuItem("Export Game…", "", [this] { showExportDialog(); },
                               [this] { return m_project.has_value(); }),
                      ui::MenuItem::divider(),
                      menuItem("Revert to Saved", "", [this] { reloadScene(); },
                               [this] { return !m_document->isUntitled() && m_document->isDirty(); }),
                      ui::MenuItem::divider(),
                      menuItem("Quit Rolo Editor", key("Q"), [this] { closeRequested(); })}});

    ui::MenuItem undo = menuItem("Undo", key("Z"), [this] { this->undo(); },
                                 [this] { return m_document->history().canUndo(); });
    undo.dynamicLabel = [this] {
        const std::string what = m_document->history().undoLabel();
        return what.empty() ? std::string("Undo") : "Undo " + what;
    };
    ui::MenuItem redo = menuItem("Redo", platform.macConventions ? "⇧⌘Z" : "Ctrl+Y",
                                 [this] { this->redo(); }, [this] { return m_document->history().canRedo(); });
    redo.dynamicLabel = [this] {
        const std::string what = m_document->history().redoLabel();
        return what.empty() ? std::string("Redo") : "Redo " + what;
    };
    menuBar.addMenu({"Edit",
                     {std::move(undo), std::move(redo), ui::MenuItem::divider(),
                      menuItem("New Empty Object", "", [this] { createEntity(NewEntityKind::Empty, false); }),
                      menuItem("New Rectangle", "", [this] { createEntity(NewEntityKind::Rectangle, false); }),
                      menuItem("New Ellipse", "", [this] { createEntity(NewEntityKind::Ellipse, false); }),
                      menuItem("New Sprite", "", [this] { createEntity(NewEntityKind::Sprite, false); }),
                      menuItem("New Camera", "", [this] { createEntity(NewEntityKind::Camera, false); }),
                      menuItem("New Tilemap", "", [this] { createEntity(NewEntityKind::Tilemap, false); }),
                      ui::MenuItem::divider(),
                      menuItem("Rename", "F2", [this] { renameSelection(); }, hasSelection),
                      menuItem("Duplicate", key("D"), [this] { duplicateSelection(); }, hasSelection),
                      menuItem("Delete", platform.macConventions ? "⌫" : "Delete", [this] { deleteSelection(); },
                               hasSelection),
                      ui::MenuItem::divider(),
                      menuItem("Move Up", "", [this] { moveSelection(-1); }, hasSelection),
                      menuItem("Move Down", "", [this] { moveSelection(1); }, hasSelection)}});

    std::vector<ui::MenuItem> view;
    for (const PanelInfo& panel : kPanels) {
        const char* id = panel.id;
        ui::MenuItem item;
        item.label = panel.title;
        item.isChecked = [this, id] { return m_dock->isOpen(id); };
        item.action = [this, id] { m_dock->setOpen(id, !m_dock->isOpen(id)); };
        view.push_back(std::move(item));
    }
    view.push_back(ui::MenuItem::divider());
    view.push_back(menuItem("Reset Layout", "", [this] {
        m_dock->resetLayout();
        log().info(kCategory, "Panel layout reset.");
    }));
    menuBar.addMenu({"View", std::move(view)});

    ui::MenuItem playItem = menuItem("Play", key("P"), [this] { m_game ? stopPlaying() : play(); });
    playItem.dynamicLabel = [this] { return m_game ? std::string("Stop") : std::string("Play"); };
    ui::MenuItem pauseItem = menuItem("Pause", platform.macConventions ? "⇧⌘P" : "Ctrl+Shift+P",
                                      [this] { togglePause(); }, [this] { return m_game != nullptr; });
    pauseItem.dynamicLabel = [this] { return m_paused && m_game ? std::string("Resume") : std::string("Pause"); };
    ui::MenuItem muteItem = menuItem("Mute Sound", "", [this] {
        const bool mute = !m_preferences.getBool(kMuteKey, false);
        m_preferences.setBool(kMuteKey, mute);
        if (m_audio) {
            m_audio->setChannelVolume(SoundChannel::Master, mute ? 0.0f : 1.0f);
        }
        log().info(kCategory, mute ? "Sound muted." : "Sound on.");
    });
    muteItem.isChecked = [this] { return m_preferences.getBool(kMuteKey, false); };
    menuBar.addMenu({"Game",
                     {std::move(playItem), std::move(pauseItem),
                      menuItem("Step One Frame", platform.macConventions ? "⌥⌘P" : "Ctrl+Alt+P",
                               [this] { stepFrame(); }, [this] { return m_game != nullptr; }),
                      muteItem,
                      ui::MenuItem::divider(),
                      menuItem("Check Content", "", [this] {
                          checkContent();
                          showPanel(kConsolePanel);
                      }, [this] { return m_assetDb != nullptr; }),
                      menuItem("New Dialogue", "", [this] { createContent(AssetType::Dialogue); },
                               [this] { return m_assetDb != nullptr; }),
                      menuItem("New Quest", "", [this] { createContent(AssetType::Quest); },
                               [this] { return m_assetDb != nullptr; }),
                      menuItem("New Variables", "", [this] { createContent(AssetType::Variables); },
                               [this] { return m_assetDb != nullptr; }),
                      menuItem("New Script", "", [this] { createContent(AssetType::Script); },
                               [this] { return m_assetDb != nullptr; })}});
    menuBar.addMenu({"Help", {menuItem("Welcome…", "", [this] { showWelcome(); }),
                              menuItem("Getting Started Guide", "", [this] { showGuide("getting-started.md"); }),
                              menuItem("Exporting Games", "", [this] { showGuide("exporting.md"); }),
                              menuItem("Scripting Guide (Lua)", "", [this] { showGuide("scripting.md"); }),
                              ui::MenuItem::divider(),
                              menuItem("About Rolo Engine…", "", [this] { showAbout(); })}});
}

void EditorApp::buildToolbar(ui::Widget& root)
{
    const ui::Theme& theme = m_ui->theme();
    const ui::Platform& platform = m_ui->platform();
    //   [save] [undo] [redo] ...... [scene v] [play] [revert] ...... [build] [project folder]
    auto& toolbar = root.emplace<ui::Row>(4.0f, ui::Insets::symmetric(8.0f, 0.0f));
    toolbar.setBackground(theme.panelHeader);
    toolbar.setCrossAlign(ui::CrossAlign::Center);
    toolbar.setFixedSize({0.0f, theme.toolbarHeight});

    const auto small = [](ui::Button& b) { b.setFixedSize({30.0f, 0.0f}); };
    small(addToolbarButton(toolbar, ui::Icon::Save, "", "Save scene (" + ui::primaryShortcutLabel(platform, "S") + ")",
                           [this] { saveScene(); }));
    small(addToolbarButton(toolbar, ui::Icon::Undo, "", "Undo (" + ui::primaryShortcutLabel(platform, "Z") + ")",
                           [this] { undo(); }));
    small(addToolbarButton(toolbar, ui::Icon::Redo, "", "Redo", [this] { redo(); }));
    toolbar.emplace<ui::Spacer>();

    auto& scene = toolbar.emplace<ui::ChoiceField>();
    scene.setStretch(0.0f);
    scene.setFixedSize({210.0f, 0.0f});
    scene.setTooltip("The scene being edited. Choose another scene of the project to open it.");
    scene.setOnChoose([this](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < m_sceneChoices.size() && m_project) {
            const std::string rel = m_sceneChoices[static_cast<std::size_t>(index)];
            if (!rel.empty()) {
                openScene(m_project->absolute(rel));
            }
        }
    });
    m_sceneChoice = &scene;
    m_playButton = &addToolbarButton(toolbar, ui::Icon::Play, "", "", [this] { m_game ? stopPlaying() : play(); });
    small(*m_playButton);
    m_pauseButton = &addToolbarButton(toolbar, ui::Icon::Pause, "",
                                      "Pause or resume the game (" + std::string(platform.macConventions ? "⇧⌘P" : "Ctrl+Shift+P") + ")",
                                      [this] { togglePause(); });
    small(*m_pauseButton);
    m_stepButton = &addToolbarButton(toolbar, ui::Icon::Step, "", "Pause and run one frame", [this] { stepFrame(); });
    small(*m_stepButton);
    m_revertButton = &addToolbarButton(toolbar, ui::Icon::Refresh, "", "Revert the scene to its saved version",
                                       [this] { reloadScene(); });
    small(*m_revertButton);
    toolbar.emplace<ui::Spacer>();
    addToolbarButton(toolbar, ui::Icon::Send, "Build",
                     "Export the game: a copy that runs without the editor, to share with players",
                     [this] { showExportDialog(); });
    small(addToolbarButton(toolbar, ui::Icon::Folder, "", "Show the project folder", [this] {
        if (m_project) {
            revealInFileManager(m_project->folder());
        }
    }));
}

void EditorApp::addShortcuts()
{
    ui::Modifiers primary;
    (m_ui->platform().macConventions ? primary.super : primary.ctrl) = true;
    ui::Modifiers primaryShift = primary;
    primaryShift.shift = true;
    m_ui->addShortcut(ui::Key::Q, primary, [this] { closeRequested(); });
    m_ui->addShortcut(ui::Key::S, primary, [this] { saveScene(); });
    m_ui->addShortcut(ui::Key::S, primaryShift, [this] { saveSceneAs(); });
    m_ui->addShortcut(ui::Key::N, primary, [this] { newScene(); });
    m_ui->addShortcut(ui::Key::Z, primary, [this] { undo(); });
    m_ui->addShortcut(ui::Key::Z, primaryShift, [this] { redo(); });
    m_ui->addShortcut(ui::Key::Y, primary, [this] { redo(); });
    m_ui->addShortcut(ui::Key::P, primary, [this] { m_game ? stopPlaying() : play(); });
    m_ui->addShortcut(ui::Key::P, primaryShift, [this] { togglePause(); });
    ui::Modifiers primaryAlt = primary;
    primaryAlt.alt = true;
    m_ui->addShortcut(ui::Key::P, primaryAlt, [this] { stepFrame(); });
    m_ui->addShortcut(ui::Key::D, primary, [this] { duplicateSelection(); });
    m_ui->addShortcut(ui::Key::F2, {}, [this] { renameSelection(); });

    // Scene view tools, from anywhere (a focused text field still gets its letters first).
    const auto tool = [this](SceneTool t) {
        return [this, t] {
            if (m_viewport == nullptr) {
                return;
            }
            const Entity* e = m_document->scene().find(selection());
            if (isTileTool(t) && (e == nullptr || e->get<Tilemap>() == nullptr)) {
                return; // tile tools need a tilemap
            }
            m_viewport->setTool(t);
        };
    };
    m_ui->addShortcut(ui::Key::W, {}, tool(SceneTool::Move));
    m_ui->addShortcut(ui::Key::E, {}, tool(SceneTool::Rotate));
    m_ui->addShortcut(ui::Key::R, {}, tool(SceneTool::Scale));
    m_ui->addShortcut(ui::Key::T, {}, tool(SceneTool::Rect));
    m_ui->addShortcut(ui::Key::B, {}, tool(SceneTool::Brush));
    m_ui->addShortcut(ui::Key::X, {}, tool(SceneTool::Eraser));
    m_ui->addShortcut(ui::Key::G, {}, tool(SceneTool::Fill));
    m_ui->addShortcut(ui::Key::I, {}, tool(SceneTool::Picker));
    m_ui->addShortcut(ui::Key::F, {}, [this] {
        if (m_viewport != nullptr) {
            m_viewport->frameSelection();
        }
    });
}

void EditorApp::buildWorkspace(UiFrame& frame)
{
    ui::Context& ctx = frame.ui();
    m_ui = &ctx;
    const ui::Theme& theme = ctx.theme();

    auto root = std::make_unique<ui::Column>(0.0f);
    root->setBackground(theme.window);
    buildMenus(*root);
    buildToolbar(*root);

    // --- Panels ------------------------------------------------------------------
    auto& dock = root->emplace<ui::DockSpace>();
    dock.setStretch(1.0f);
    m_dock = &dock;
    for (const PanelInfo& panel : kPanels) {
        std::unique_ptr<ui::Widget> content;
        const std::string id = panel.id;
        if (id == kViewportPanel) {
            auto p = std::make_unique<ViewportPanel>(*this);
            m_viewport = p.get();
            content = std::move(p);
        } else if (id == kHierarchyPanel) {
            auto p = std::make_unique<HierarchyPanel>(*this);
            m_hierarchy = p.get();
            content = std::move(p);
        } else if (id == kInspectorPanel) {
            auto p = std::make_unique<InspectorPanel>(*this);
            m_inspector = p.get();
            content = std::move(p);
        } else if (id == kConsolePanel) {
            auto p = std::make_unique<ConsolePanel>(m_console);
            m_consolePanel = p.get();
            content = std::move(p);
        } else if (id == kTilePalettePanel) {
            auto p = std::make_unique<TilePalettePanel>(*this);
            m_tilePalette = p.get();
            content = std::move(p);
        } else if (id == kAnimationPanel) {
            auto p = std::make_unique<AnimationPanel>(*this);
            m_animation = p.get();
            content = std::move(p);
        } else if (id == kAssetsPanel) {
            auto p = std::make_unique<AssetsPanel>(*this);
            m_assets = p.get();
            content = std::move(p);
        } else {
            auto p = std::make_unique<AssetsPreviewPanel>(*this);
            m_preview = p.get();
            content = std::move(p);
        }
        dock.addPanel(id, panel.title, std::move(content), panel.icon);
    }
    dock.setDefaultLayout(defaultLayout());
    dock.setOnLayoutChanged([this, &ctx] {
        m_layoutDirty = true;
        m_layoutChangedAt = ctx.time();
    });

    // --- Status bar ----------------------------------------------------------------
    //   60 fps  ........................  (i) 12  (!) 0  (x) 0   Version 0.3.0
    auto& status = root->emplace<ui::Row>(5.0f, ui::Insets::symmetric(theme.padding, 0.0f));
    status.setBackground(theme.panelHeader);
    status.setCrossAlign(ui::CrossAlign::Center);
    status.setFixedSize({0.0f, theme.statusBarHeight});
    m_statusFps = &status.emplace<ui::Label>("", ui::Label::Style::Small);
    m_statusFps->setColor(theme.textFaint);
    m_statusFps->setTooltip("Frames drawn per second");
    status.emplace<ui::Spacer>();
    const std::pair<ui::Icon, ui::Color> counters[] = {
        {ui::Icon::Info, theme.debug}, {ui::Icon::Warning, theme.warning}, {ui::Icon::Error, theme.error}};
    const char* tips[] = {"Messages in the Console", "Warnings in the Console", "Errors in the Console"};
    for (std::size_t i = 0; i < 3; ++i) {
        status.emplace<ui::IconView>(counters[i].first, 13.0f, counters[i].second);
        m_statusCounts[i] = &status.emplace<ui::Label>("0", ui::Label::Style::Small);
        m_statusCounts[i]->setColor(theme.textDim);
        m_statusCounts[i]->setTooltip(tips[i]);
        status.emplace<ui::Spacer>(0.0f).setFixedSize({6.0f, 1.0f});
    }
    status.emplace<ui::Spacer>(0.0f).setFixedSize({6.0f, 1.0f});
    auto& version = status.emplace<ui::Label>("Version " + std::string(versionString()), ui::Label::Style::Small);
    version.setColor(theme.textDim);

    ctx.setRoot(std::move(root));
    addShortcuts();
}

void EditorApp::updateStatusBar(UiFrame& frame)
{
    m_statusFps->setText(std::to_string(static_cast<int>(frame.framesPerSecond() + 0.5f)) + " fps");
    const std::size_t messages = m_console.count(LogLevel::Trace) + m_console.count(LogLevel::Debug)
                                 + m_console.count(LogLevel::Info);
    m_statusCounts[0]->setText(std::to_string(messages));
    m_statusCounts[1]->setText(std::to_string(m_console.count(LogLevel::Warning)));
    m_statusCounts[2]->setText(std::to_string(m_console.count(LogLevel::Error)));
}

void EditorApp::updateTitle(UiFrame& frame)
{
    // Title bar: "main.lscene • — My Game — Rolo Editor" (• = unsaved changes).
    std::string title = m_document->displayName() + (m_document->isDirty() ? " •" : "");
    if (m_project) {
        title += " — " + m_project->settings().name;
    }
    title += " — Rolo Editor";
    if (title != m_shownTitle) {
        frame.setWindowTitle(title);
        m_shownTitle = title;
    }

    // The toolbar's scene picker: the project's scenes, the open one chosen.
    std::vector<std::string> labels;
    m_sceneChoices.clear();
    int selected = -1;
    if (m_project) {
        const auto openRel = m_document->isUntitled() ? std::nullopt : m_project->relative(m_document->path());
        for (const std::string& rel : m_project->sceneFiles()) {
            if (openRel == rel) {
                selected = static_cast<int>(labels.size());
            }
            labels.push_back(sceneDisplayName(rel));
            m_sceneChoices.push_back(rel);
        }
    }
    if (selected < 0) {
        selected = static_cast<int>(labels.size());
        labels.push_back(m_document->isUntitled() ? m_document->scene().name() + " (not saved)"
                                                  : m_document->displayName());
        m_sceneChoices.emplace_back();
    }
    if (m_document->isDirty()) {
        labels[static_cast<std::size_t>(selected)] += "  •";
    }
    m_sceneChoice->setOptions(std::move(labels));
    m_sceneChoice->setSelected(selected);
}

void EditorApp::showGuide(const std::string& file)
{
    const std::filesystem::path guide = m_resources.docs / file;
    std::error_code ec;
    if (!std::filesystem::exists(guide, ec)) {
        showMessage("Guide Not Found", "The documentation is missing (" + guide.string() + "). Reinstall Rolo.");
        return;
    }
    log().info(kCategory, "Guide: " + guide.string());
    showMessage("Rolo Documentation", "The guide is a text file you can read in any editor or web browser:\n"
                                             + guide.string() + "\n\nIts folder opens now.");
    revealInFileManager(guide.parent_path());
}

void EditorApp::openExampleCopy()
{
    const auto templates = listProjectTemplates(m_resources);
    const auto it = std::find_if(templates.begin(), templates.end(), [](const ProjectTemplate& t) { return t.example; });
    if (it == templates.end()) {
        showMessage("No Example Found", "The example game was not found in " + m_resources.examples.string()
                                            + ". Reinstall Rolo.");
        return;
    }
    // A copy of its own in the projects folder, so the original stays as it is.
    const std::filesystem::path projects = defaultProjectsDirectory();
    const std::string base = it->name.rfind("Example: ", 0) == 0 ? it->name.substr(9) : it->name;
    std::error_code ec;
    std::filesystem::path folder = projects / base;
    if (std::filesystem::exists(folder / ProjectFormat::kFileName, ec)) {
        openProjectFolder(folder, true);   // made earlier: open it again
        return;
    }
    for (int n = 2; std::filesystem::exists(folder, ec); ++n) {
        folder = projects / (base + " " + std::to_string(n));
    }
    std::string error;
    if (!createProjectFromTemplate(*it, folder, base, &error)) {
        showMessage("Could Not Copy the Example", error);
        return;
    }
    log().info(kCategory, "Copied the example game to " + folder.string());
    openProjectFolder(folder, true);
}

void EditorApp::showWelcome()
{
    auto dialog = std::make_unique<ui::Dialog>("Welcome to Rolo", 520.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Rolo " + std::string(versionString())
                                + " is an editor for 2D games: top-down RPGs, pixel-art adventures, tile-based and "
                                  "story-driven games.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    body.emplace<ui::Label>("New here? Start a project from the Top-Down Starter and press Play, or look inside "
                            "The Lantern Keeper, a small finished game. Help > Getting Started Guide walks you from "
                            "a new project to a game you can share.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    auto& again = body.emplace<ui::Checkbox>("Show this window when Rolo starts without a project",
                                             m_preferences.getBool(kShowWelcomeKey, true));
    again.setOnChange([this](bool on) {
        m_preferences.setBool(kShowWelcomeKey, on);
        saveSettings();
    });
    dialog->addButton("Open Project…", ui::Button::Style::Normal, [this] { m_ui->post([this] { openProject(); }); });
    dialog->addButton("Try The Lantern Keeper", ui::Button::Style::Normal,
                      [this] { m_ui->post([this] { confirmDiscard("opening the example", [this] { openExampleCopy(); }); }); });
    dialog->addButton("New Project…", ui::Button::Style::Primary, [this] { m_ui->post([this] { newProject(); }); });
    ui::showDialog(*m_ui, std::move(dialog));
}

void EditorApp::showAbout()
{
    auto dialog = std::make_unique<ui::Dialog>("About Rolo Engine", 400.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Rolo Engine " + std::string(versionString()), ui::Label::Style::Strong);
    body.emplace<ui::Label>("A C++-first 2D game engine and editor.", ui::Label::Style::Dim);
    // The runtime backend is an implementation detail, so it is not named here.
    // It is still reported by --version and in the Console, for bug reports.
    body.emplace<ui::Separator>();
    body.emplace<ui::Label>("Copyright (c) 2026 Oscar Hird. MIT License.");
    body.emplace<ui::Label>("Third-party licenses: THIRD_PARTY_NOTICES.md and the licenses folder ("
                                + m_resources.licenses.string() + "). Exported games carry them.",
                            ui::Label::Style::Faint)
        .setWrap(true);
    dialog->addButton("Close", ui::Button::Style::Primary);
    ui::showDialog(*m_ui, std::move(dialog));
}

// ---------------------------------------------------------------------------
// Runtime callbacks
// ---------------------------------------------------------------------------
void EditorApp::setupUi(UiFrame& frame)
{
    buildWorkspace(frame);
    restoreLayout();
    m_layoutDirty = false;
    openInitialContent();
    if (!m_project && m_preferences.getBool(kShowWelcomeKey, true)) {
        showWelcome();   // a first start, or no project to come back to
    }
    if (m_options.play) {
        play();
    }
    // Files dragged from Finder / Explorer onto the window are imported.
    frame.setDropHandler([this](const std::vector<std::filesystem::path>& files) { importFiles(files); });
}

bool EditorApp::closeRequested()
{
    if (m_quitRequested) {
        return true;
    }
    if (m_quitting) {
        return false; // the question is already on screen
    }
    if (!m_document->isDirty()) {
        m_quitRequested = true;
        return true;
    }
    m_quitting = true;
    auto dialog = std::make_unique<ui::Dialog>("Save Changes?", 440.0f);
    dialog->body()
        .emplace<ui::Label>("“" + m_document->displayName()
                            + "” has unsaved changes. Save them before quitting?")
        .setWrap(true);
    dialog->setOnClosed([this] { m_quitting = false; }); // Cancel, Escape, or an answer
    dialog->addButton("Don’t Save", ui::Button::Style::Normal, [this] { m_quitRequested = true; });
    dialog->addButton("Cancel");
    dialog->addButton("Save", ui::Button::Style::Primary, [this] { saveScene([this] { m_quitRequested = true; }); });
    ui::showDialog(*m_ui, std::move(dialog));
    return false;
}

// project.rolo can change under us (Set Start Scene, its undo, or another
// program): read the settings again when the file's time stamp changes.
void EditorApp::syncProjectSettings()
{
    if (!m_project) {
        return;
    }
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(m_project->folder() / ProjectFormat::kFileName, ec);
    if (ec || stamp == m_projectStamp) {
        return;
    }
    m_projectStamp = stamp;
    if (auto fresh = Project::open(m_project->folder())) {
        m_project->setSettings(fresh->settings());
    }
}

void EditorApp::updateUi(UiFrame& frame)
{
    syncProjectSettings();
    if (m_quitRequested) {
        frame.requestQuit();
    }
    // The project's assets: re-read every few seconds (files may be added,
    // changed or deleted outside the editor), and handed to the runtime.
    if (m_assetDb && frame.ui().time() - m_lastAssetScan >= 2.0) {
        refreshAssets();
    }
    if (m_runtimeAssets != m_assetDb.get()) {
        m_runtimeAssets = m_assetDb.get();
        frame.setAssets(m_runtimeAssets);
    }
    m_retiredAssetDbs.clear(); // the runtime no longer uses them
    // Run the game (if playing), then show its scene; otherwise show the
    // open document's scene (it changes when another scene is opened).
    updateGame(frame);
    const std::shared_ptr<const Scene> show = m_game ? m_game->sharedScene() : m_document->sharedScene();
    if (m_shownScene != show) {
        m_shownScene = show;
        frame.showScene(m_shownScene);
    }
    const ui::Platform& platform = frame.ui().platform();
    m_playButton->setIcon(m_game ? ui::Icon::Stop : ui::Icon::Play);
    m_playButton->setToggled(m_game != nullptr);
    m_playButton->setTooltip(std::string(m_game ? "Stop the game and return to editing" : "Play the scene in the Scene view")
                             + " (" + ui::primaryShortcutLabel(platform, "P") + ")");
    m_pauseButton->setEnabled(m_game != nullptr);
    m_pauseButton->setToggled(isPaused());
    m_stepButton->setEnabled(m_game != nullptr);
    m_revertButton->setEnabled(m_game == nullptr);
    m_viewport->update(frame);
    m_hierarchy->update(frame);
    m_assets->update(frame);
    m_preview->update(frame);
    m_tilePalette->update(frame);
    m_animation->update(frame);
    m_inspector->update(frame);
    if (m_renamePending) {
        m_renamePending = false;
        m_inspector->focusName();
    }
    m_consolePanel->update(frame);
    updateStatusBar(frame);
    updateTitle(frame);

    if (m_layoutDirty && frame.ui().time() - m_layoutChangedAt >= kLayoutSaveDelaySeconds) {
        saveLayout();
    }
}

void EditorApp::shutdownUi(UiFrame& /*frame*/)
{
    stopPlaying();
    saveLayout();
    m_state.panels.viewport = m_dock->isOpen(kViewportPanel);
    m_state.panels.hierarchy = m_dock->isOpen(kHierarchyPanel);
    m_state.panels.inspector = m_dock->isOpen(kInspectorPanel);
    m_state.panels.console = m_dock->isOpen(kConsolePanel);
    if (!m_document->isUntitled()) {
        m_state.lastScene = m_document->path().string();
    }
    // The widgets are destroyed with the UI; forget them.
    m_ui = nullptr;
    m_dock = nullptr;
    m_viewport = nullptr;
    m_hierarchy = nullptr;
    m_assets = nullptr;
    m_preview = nullptr;
    m_tilePalette = nullptr;
    m_animation = nullptr;
    m_inspector = nullptr;
    m_consolePanel = nullptr;
    m_sceneChoice = nullptr;
    m_playButton = nullptr;
    m_pauseButton = nullptr;
    m_stepButton = nullptr;
    m_revertButton = nullptr;
    m_statusFps = nullptr;
    m_statusCounts = {};
}

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------
Status EditorApp::run()
{
    loadSettings();

    PresentationSettings presentation;
    presentation.worldDisplay = WorldDisplay::Offscreen;
    presentation.scene = m_document->sharedScene();
    presentation.uiSetup = [this](UiFrame& frame) { setupUi(frame); };
    presentation.uiUpdate = [this](UiFrame& frame) { updateUi(frame); };
    presentation.uiShutdown = [this](UiFrame& frame) { shutdownUi(frame); };
    presentation.onCloseRequested = [this] { return closeRequested(); };

    Application app(m_engine,
                    WindowSettings{.title = "Rolo Editor",
                                   .width = m_state.windowWidth,
                                   .height = m_state.windowHeight,
                                   .resizable = true},
                    std::move(presentation));

    const Status status = app.run(RunOptions{.maxFrames = m_options.maxFrames,
                                             .screenshotPath = m_options.screenshotPath,
                                             .showStats = false,
                                             .escapeQuits = false});

    const RunReport& report = app.lastRunReport();
    m_state.setWindowSize(report.finalWindowWidth, report.finalWindowHeight);
    saveSettings();
    return status;
}

} // namespace rolo::editor
