#pragma once

// The editor's panels, built from Rolo UI widgets.
//
// Each panel is a widget that lives in the editor's DockSpace for the whole
// session. Once per frame the editor calls update(), which refreshes what the
// panel shows. Panels never change the scene directly: they ask
// EditorServices to run a Command, so every edit can be undone.
//
// Controls for features that do not exist yet are shown disabled, with a
// tooltip saying which milestone brings them (kComing* below), so the
// editor's layout is complete without pretending to do more than it does.

#include "../model/ConsoleLog.hpp"
#include "../model/EditorState.hpp"
#include "../model/RoloResources.hpp"
#include "../model/SceneDocument.hpp"

#include "rolo/assets/Animation.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/content/Notation.hpp"
#include "rolo/gameplay/Input.hpp"
#include "rolo/runtime/Application.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/ui/widgets/Basic.hpp"
#include "rolo/ui/widgets/Box.hpp"
#include "rolo/ui/widgets/Fields.hpp"
#include "rolo/ui/widgets/Inspector.hpp"
#include "rolo/ui/widgets/Scroll.hpp"
#include "rolo/ui/widgets/TextField.hpp"
#include "rolo/ui/widgets/Tree.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rolo::editor {

// Panel ids are keys in the saved layout file, so they must never change.
inline constexpr const char* kViewportPanel = "viewport";
inline constexpr const char* kHierarchyPanel = "hierarchy";
inline constexpr const char* kAssetsPanel = "assets";
inline constexpr const char* kInspectorPanel = "inspector";
inline constexpr const char* kPreviewPanel = "preview";
inline constexpr const char* kConsolePanel = "console";
inline constexpr const char* kTilePalettePanel = "tilepalette";
inline constexpr const char* kAnimationPanel = "animation";


enum class NewEntityKind { Empty, Rectangle, Ellipse, Sprite, Camera, Tilemap };

/// What panels can ask the editor to do. Implemented by EditorApp.
class EditorServices {
public:
    virtual ~EditorServices() = default;

    virtual SceneDocument& document() = 0;
    /// Goes up whenever another document is opened (scene addresses can be reused).
    virtual std::uint64_t documentGeneration() const = 0;
    virtual const ComponentRegistry& registry() const = 0;
    /// The open project, if any.
    virtual const Project* project() const = 0;
    /// The open project's assets (null without a project).
    virtual AssetDatabase* assets() = 0;
    virtual ui::Context& ui() = 0;

    // Selection.
    virtual EntityId selection() const = 0;
    virtual bool isSceneSelected() const = 0;
    virtual void select(EntityId id) = 0;
    virtual void selectScene() = 0;
    /// Selecting an asset (in Assets or Assets Preview) shows it in the Inspector.
    virtual AssetId selectedAsset() const = 0;
    virtual void selectAsset(AssetId id) = 0;
    /// The project folder new imports go into (project-relative, e.g. "assets").
    virtual std::string importFolder() const = 0;
    virtual void setImportFolder(std::string folder) = 0;
    /// Copies files into the project as assets (they get IDs and .meta files).
    virtual void importFiles(const std::vector<std::filesystem::path>& files) = 0;
    /// Creates an object showing `asset` (an image becomes a Sprite) at a
    /// world position, e.g. when an asset is dropped on the Scene view.
    virtual void placeAsset(AssetId asset, Vec2 worldPosition) = 0;
    /// Called by a widget dragging an asset when the mouse is released at
    /// `windowPoint`; the editor decides what the drop means there.
    virtual void dropAsset(AssetId asset, ui::Vec2 windowPoint) = 0;

    /// Runs a command on the open scene (undoable). False if it changed nothing.
    virtual bool execute(std::unique_ptr<Command> command) = 0;
    /// Ends a continuous edit (e.g. a drag), so the next edit is a new undo step.
    virtual void endEdit() = 0;

    // Actions shared by menus, toolbars and panels.
    virtual void createEntity(NewEntityKind kind, bool asChildOfSelection) = 0;
    virtual void deleteSelection() = 0;
    virtual void duplicateSelection() = 0;
    virtual void renameSelection() = 0;
    virtual void openScene(const std::filesystem::path& path) = 0;
    virtual void newScene() = 0;
    virtual void newProject() = 0;
    virtual void openProject() = 0;
    virtual void showMessage(const std::string& title, const std::string& text) = 0;
    /// Rolo's templates, examples, licenses, docs and game runtime.
    virtual const RoloResources& resources() const = 0;
    /// Asks for files to import (the dialog explains drag and drop too).
    virtual void showImportDialog() = 0;
    /// Creates a new content file (Variables, Dialogue or Quest) from a
    /// starting template in the project's content folder, and selects it.
    virtual void createContent(AssetType type) = 0;
    /// Checks the project's content and the open scene's triggers; problems
    /// go to the Console. Returns how many problems were found.
    virtual std::size_t checkContent() = 0;

    // Tilemap painting.
    /// The tile the Brush paints (a number in the tileset).
    virtual int tileBrush() const = 0;
    virtual void setTileBrush(int tile) = 0;
    /// The layer painted on: an index, or PaintTiles::kCollisionLayer.
    virtual int tileLayer() const = 0;
    virtual void setTileLayer(int layer) = 0;
    /// Opens a panel (if closed) and brings it to the front.
    virtual void showPanel(const std::string& id) = 0;
    /// Chooses a Scene view tool (e.g. the Brush after picking a tile).
    virtual void setSceneTool(int tool) = 0;

    // Play mode: the game runs on a copy of the scene inside the Scene view.
    virtual bool isPlaying() const = 0;
    virtual bool isPaused() const = 0;
    /// Where the Scene view reports keys and the mouse while playing (null otherwise).
    virtual Input* gameInput() = 0;

    // Sound: listening to a sound file from the Inspector.
    virtual void previewSound(AssetId sound) = 0;
    virtual void stopSoundPreview() = 0;
};

/// A toolbar strip along the top of a panel.
ui::Row& makePanelToolbar(ui::Widget& parent);
/// A small icon-only button for toolbars. Without an action it is shown
/// disabled; its tooltip then says when the feature arrives.
ui::Button& addToolButton(ui::Widget& toolbar, ui::Icon icon, std::string tooltip, std::function<void()> action = {});
/// Opens a menu just below `anchor`.
void openMenuAt(ui::Context& ctx, ui::Rect anchor, std::vector<ui::MenuItem> items);

/// A dialog for choosing a folder by browsing (FolderPicker.cpp). With
/// `projectsOnly`, only a Rolo project folder can be chosen.
void showFolderPicker(ui::Context& ctx, const std::string& title, const std::filesystem::path& start,
                      std::function<void(const std::filesystem::path&)> onChosen, bool projectsOnly = false);

/// Changes an animation set (.lanim) and saves it as one undoable step.
/// With `merge`, consecutive changes with the same label join into one step
/// until the next endEdit() (e.g. while typing a number). False if nothing
/// changed or it could not be saved (the user is told why).
bool editAnimationSet(EditorServices& editor, AssetId id, const std::string& label,
                      const std::function<void(AnimationSet&)>& change, bool merge = false);

/// The game world, scaled to fit. Click an object to select it.
/// The tools of the Scene view (W, E, R, T as in most editors), and the
/// tile tools shown while a tilemap is selected (B, X, G, I).
enum class SceneTool { Move, Rotate, Scale, Rect, Brush, Eraser, Fill, Picker };
inline bool isTileTool(SceneTool t)
{
    return t == SceneTool::Brush || t == SceneTool::Eraser || t == SceneTool::Fill || t == SceneTool::Picker;
}

/// The scene as you edit it: pan and zoom freely, click to select, and move,
/// rotate, scale or resize the selected object with the tools.
class ViewportPanel final : public ui::Column {
public:
    explicit ViewportPanel(EditorServices& editor);
    void update(UiFrame& frame);

    /// The world point under a window point, if it is over the Scene view.
    std::optional<Vec2> worldAt(ui::Vec2 windowPoint) const;
    void setTool(SceneTool tool);
    SceneTool tool() const noexcept { return m_tool; }
    /// Centres the view on the selected object (F).
    void frameSelection();
    /// Shows the whole game view (what the player sees).
    void fitGameView();
    /// Gives the Scene view keyboard focus (so a running game gets the keys).
    void focusView();

private:
    class SceneView;
    void openZoomMenu();

    EditorServices& m_editor;
    SceneView* m_view = nullptr;
    ui::Button* m_zoom = nullptr;
    std::array<ui::Button*, 8> m_toolButtons{};
    ui::Widget* m_tileToolsDivider = nullptr;
    ui::Widget* m_toolbar = nullptr;
    ui::Label* m_hint = nullptr;
    std::optional<std::pair<Vec2, float>> m_editorCamera;   // saved while playing
    SceneTool m_tool = SceneTool::Move;
};

/// The scene's objects as a tree; select, create, delete, search.
class HierarchyPanel final : public ui::Column {
public:
    explicit HierarchyPanel(EditorServices& editor);
    void update(UiFrame& frame);

private:
    class SceneTree;
    void rebuild();

    EditorServices& m_editor;
    SceneTree* m_tree = nullptr;
    ui::Button* m_create = nullptr;
    std::string m_filter;
    std::uint64_t m_shownGeneration = 0;
    std::uint64_t m_shownRevision = 0;
    std::string m_shownFilter;
};

/// The project's files: double-click a scene to open it.
class AssetsPanel final : public ui::Column {
public:
    explicit AssetsPanel(EditorServices& editor);
    void update(UiFrame& frame);
    /// Re-reads the project folder (also done every few seconds).
    void refresh();

private:
    void rebuild();

    EditorServices& m_editor;
    ui::Widget* m_noProject = nullptr;
    ui::Widget* m_browser = nullptr;
    ui::TreeView* m_tree = nullptr;
    std::string m_filter;
    std::vector<std::string> m_paths;   // project-relative; item id = index + 1
    const Project* m_shownProject = nullptr;
    std::filesystem::path m_shownFolder;
    double m_lastScan = -100.0;
    std::uint64_t m_shownAssets = 0;
};

/// Thumbnails of the folder selected in Assets (Milestone 4).
/// The contents of the folder selected in Assets, as thumbnails. Click to
/// inspect, double-click to open, drag an image into the Scene view.
class AssetsPreviewPanel final : public ui::Column {
public:
    explicit AssetsPreviewPanel(EditorServices& editor);
    void update(UiFrame& frame);

private:
    class Grid;
    EditorServices& m_editor;
    ui::Label* m_path = nullptr;
    Grid* m_grid = nullptr;
};

/// The selected tilemap's tileset: click a tile to paint with it; with
/// "Edit solid tiles" on, click tiles to make them block movement.
class TilePalettePanel final : public ui::Column {
public:
    explicit TilePalettePanel(EditorServices& editor);
    void update(UiFrame& frame);

private:
    class Palette;
    EditorServices& m_editor;
    ui::Widget* m_empty = nullptr;
    ui::Widget* m_body = nullptr;
    ui::Label* m_info = nullptr;
    ui::Checkbox* m_solidMode = nullptr;
    Palette* m_palette = nullptr;
};

/// Edits an animation set: the sprite sheet with its frames (click to add
/// one to the clip), the clip's frames in order, its speed and looping, and
/// a preview playing it. Shows the set selected in Assets, or the one the
/// selected object's Animator uses.
class AnimationPanel final : public ui::Column {
public:
    explicit AnimationPanel(EditorServices& editor);
    void update(UiFrame& frame);

    /// What the panel is editing; shared with its parts.
    struct State {
        AssetId setId;
        std::optional<AnimationSet> set;
        ui::TextureId texture = 0;
        ui::Size image;            // the sheet's size in pixels
        std::string clip;          // the clip being edited
        int selected = -1;         // a frame of the clip (index in its list), or -1
        bool previewPlaying = true;

        const AnimationClip* currentClip() const { return set ? set->findClip(clip) : nullptr; }
        int frameCount() const;
        /// The frame's corners in the image, as fractions (for drawing it).
        std::pair<ui::Vec2, ui::Vec2> frameUv(int frame) const;
    };

private:
    class Sheet;
    class Strip;
    class Preview;
    void showSet(AssetId id, const std::string& clip);
    void refreshControls();
    void editClip(const std::string& label, const std::function<void(AnimationClip&)>& change, bool merge = false);
    void showClipNameDialog(bool rename);
    void deleteClip();
    void moveSelected(int by);
    void removeSelected();

    EditorServices& m_editor;
    State m_state;
    AssetId m_lastSelectedAsset;
    EntityId m_lastSelectedEntity;
    std::uint64_t m_shownRevision = 0;
    ui::Widget* m_empty = nullptr;
    ui::Widget* m_body = nullptr;
    ui::Label* m_info = nullptr;
    ui::ChoiceField* m_clipChoice = nullptr;
    ui::NumberField* m_fps = nullptr;
    ui::Checkbox* m_loop = nullptr;
    ui::Button* m_play = nullptr;
    ui::Label* m_frameInfo = nullptr;
    std::vector<ui::Button*> m_clipButtons;    // need a clip
    std::vector<ui::Button*> m_frameButtons;   // need a selected frame
};

/// The selected object's (or the scene's) properties, editable.
class InspectorPanel final : public ui::Column {
public:
    explicit InspectorPanel(EditorServices& editor);
    void update(UiFrame& frame);
    /// Puts keyboard focus in the name field (Edit > Rename).
    void focusName();

private:
    void rebuild();
    void buildEntity(ui::Column& content, const Entity& entity);
    void buildScene(ui::Column& content);
    void buildAsset(ui::Column& content, const AssetRecord& asset);
    void showCreateTilesetDialog(AssetId image);
    void showCreateAnimationsDialog(AssetId image);
    void buildAnimationAsset(ui::Column& body, const AssetRecord& asset);
    void addClipChoice(ui::PropertyField& row, EntityId id, std::function<void(PropertyValue)> set,
                       std::function<void()> end);
    void confirmDeleteAsset(AssetId id);
    void buildComponent(ui::Column& content, const Entity& entity, const Component& component);
    void buildTilemapLayers(ui::Section& section, EntityId id);

    // RPG content pages and the Trigger editor (InspectorContent.cpp).
    void buildContentAsset(ui::Column& content, const AssetRecord& asset);
    void buildVariablesPage(ui::Column& content, AssetId id, const VariableSet& vars);
    void buildDialoguePage(ui::Column& content, AssetId id, const Dialogue& dialogue);
    void buildQuestPage(ui::Column& content, AssetId id, const Quest& quest);
    void buildTriggerEditor(ui::Section& section, EntityId id);
    /// How conditions and actions name assets and objects here.
    NotationNames notationNames() const;
    /// A text field for a condition in its written form; `apply` gets the
    /// new condition (nothing: empty), or the field shows what is wrong.
    void conditionField(ui::Widget& parent, const std::string& key, const std::optional<Condition>& value,
                        const std::string& placeholder, std::function<void(std::optional<Condition>)> apply);
    /// One text field per action, a remove button each, and a field to add one.
    void actionList(ui::Column& parent, const std::string& key, const std::vector<Action>& actions,
                    std::function<void(std::vector<Action>)> apply);
    /// Remembers an editable widget by a key that stays the same when the
    /// page is rebuilt, so keyboard focus can come back to it.
    ui::Widget& keyed(std::string key, ui::Widget& widget);
    void addPropertyEditor(ui::Section& section, EntityId id, const std::string& typeId, const PropertyInfo& info);
    void openComponentMenu(ui::Section& section, EntityId id, const std::string& typeId);
    void openAddComponentMenu(ui::Button& button, EntityId id);
    std::string signature() const;

    EditorServices& m_editor;
    ui::Widget* m_empty = nullptr;
    ui::ScrollView* m_details = nullptr;
    ui::TextField* m_name = nullptr;
    std::string m_builtFor;                      // signature() when last built
    UiFrame* m_frame = nullptr;                  // valid during update() (thumbnails)
    std::vector<std::function<void()>> m_refresh; // copy scene values into the widgets
    std::map<std::string, ui::Widget*> m_keyed;   // see keyed()
};

/// Log messages from the engine, the backend and the editor.
class ConsolePanel final : public ui::Column {
public:
    explicit ConsolePanel(ConsoleLog& log);
    void update(UiFrame& frame);

private:
    struct LevelToggle {
        LogLevel level;
        const char* name;
        bool ConsoleFilter::*shown;
        ui::Checkbox* checkbox = nullptr;
    };

    void refresh();
    void paintHeader(ui::Context& ctx, ui::DrawList& draw, ui::Rect rect) const;
    void paintRow(ui::Context& ctx, ui::DrawList& draw, const ui::RowInfo& row) const;

    ConsoleLog& m_log;
    ConsoleFilter m_filter;
    std::vector<ConsoleEntry> m_entries;   // what the list shows
    std::uint64_t m_shownTotal = UINT64_MAX;
    std::size_t m_shownSize = 0;
    bool m_dirty = true;

    std::array<LevelToggle, 4> m_toggles;
    ui::ListView* m_list = nullptr;
    ui::Checkbox* m_autoScroll = nullptr;
};

} // namespace rolo::editor
