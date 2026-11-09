#include "Panels.hpp"

#include "rolo/ui/Context.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace rolo::editor {

namespace {

constexpr double kRescanSeconds = 2.0;

bool containsIgnoringCase(std::string_view text, std::string_view needle)
{
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); })
           != text.end();
}

// Every visible file and folder in the project, project-relative, sorted so
// that each folder comes right before its contents.
std::vector<std::string> listProject(const std::filesystem::path& root)
{
    std::vector<std::string> paths;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::string leaf = it->path().filename().string();
        if (!leaf.empty() && leaf.front() == '.') {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (leaf.size() > 4 && leaf.compare(leaf.size() - 4, 4, ".tmp") == 0) {
            continue; // half-written files from a save in progress
        }
        if (leaf.size() > 5 && leaf.compare(leaf.size() - 5, 5, ".meta") == 0) {
            continue; // asset IDs: Rolo's business, not the user's
        }
        paths.push_back(it->path().lexically_relative(root).generic_string() + (it->is_directory(ec) ? "/" : ""));
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

} // namespace

AssetsPanel::AssetsPanel(EditorServices& editor)
    : ui::Column(0.0f)
    , m_editor(editor)
{
    // Without a project: an explanation and the two ways to get one.
    auto noProject = std::make_unique<ui::Column>(10.0f, ui::Insets::all(16.0f));
    noProject->setStretch(1.0f);
    noProject->setCrossAlign(ui::CrossAlign::Center);
    noProject->emplace<ui::Spacer>();
    noProject->emplace<ui::IconView>(ui::Icon::Folder, 28.0f);
    noProject->emplace<ui::Label>("No project open", ui::Label::Style::Dim);
    auto& hint = noProject->emplace<ui::Label>("A project is a folder for your game's scenes and, from Milestone 4, "
                                               "its images and sounds.",
                                               ui::Label::Style::Faint);
    hint.setWrap(true);
    hint.setAlign(ui::TextAlign::Center);
    auto& buttons = noProject->emplace<ui::Row>(8.0f);
    buttons.emplace<ui::Button>("New Project…", [this] { m_editor.newProject(); }).setStyle(ui::Button::Style::Primary);
    buttons.emplace<ui::Button>("Open…", [this] { m_editor.openProject(); });
    noProject->emplace<ui::Spacer>(1.4f);
    m_noProject = &addChild(std::move(noProject));

    // With a project: toolbar and file tree.
    auto browser = std::make_unique<ui::Column>(0.0f);
    browser->setStretch(1.0f);
    auto& toolbar = makePanelToolbar(*browser);
    ui::Button& plus = addToolButton(toolbar, ui::Icon::Plus, "Create", nullptr);
    plus.setEnabled(true);
    plus.setTooltip("Create a scene, a dialogue, a quest or game variables, or import images, sounds and fonts");
    plus.setOnClick([this, &plus] {
        ui::MenuItem scene;
        scene.label = "New Scene";
        scene.action = [this] { m_editor.newScene(); };
        ui::MenuItem import;
        import.label = "Import Assets…";
        import.action = [this] { m_editor.showImportDialog(); };
        const auto content = [this](const char* label, AssetType type) {
            ui::MenuItem item;
            item.label = label;
            item.action = [this, type] { m_editor.createContent(type); };
            return item;
        };
        openMenuAt(m_editor.ui(), plus.bounds(),
                   {std::move(scene), ui::MenuItem::divider(), content("New Dialogue", AssetType::Dialogue),
                    content("New Quest", AssetType::Quest), content("New Variables", AssetType::Variables),
                    ui::MenuItem::divider(), content("New Script", AssetType::Script), ui::MenuItem::divider(),
                    std::move(import)});
    });
    auto& search = toolbar.emplace<ui::TextField>("Search assets");
    search.setLeadingIcon(ui::Icon::Search);
    search.setClearButton(true);
    search.setStretch(1.0f);
    search.setOnChange([this](const std::string& text) {
        m_filter = text;
        rebuild();
    });
    addToolButton(toolbar, ui::Icon::Refresh, "Refresh", [this] { refresh(); });

    auto tree = std::make_unique<ui::TreeView>();
    tree->setStretch(1.0f);
    tree->setAutoExpandDepth(3);
    tree->setEmptyText("No files match.");
    tree->setOnActivate([this](std::uint64_t id) {
        if (id == 0 || id > m_paths.size() || m_editor.project() == nullptr) {
            return;
        }
        const std::string& path = m_paths[id - 1];
        if (path.size() > 7 && path.compare(path.size() - 7, 7, ".lscene") == 0) {
            m_editor.openScene(m_editor.project()->absolute(path));
        }
    });
    tree->setOnSelect([this](std::uint64_t id) {
        if (id == 0 || id > m_paths.size()) {
            return;
        }
        const std::string& path = m_paths[id - 1];
        if (path.back() == '/') {
            m_editor.setImportFolder(path.substr(0, path.size() - 1)); // Assets Preview shows this folder
            return;
        }
        const std::size_t slash = path.rfind('/');
        m_editor.setImportFolder(slash == std::string::npos ? std::string() : path.substr(0, slash));
        if (const AssetDatabase* db = m_editor.assets()) {
            if (const AssetRecord* asset = db->findByPath(path)) {
                m_editor.selectAsset(asset->id);
            }
        }
    });
    tree->setTooltip("Click to inspect · double-click a scene to open it · drop files onto the window to import them");
    m_tree = &browser->addChild(std::move(tree));
    m_browser = &addChild(std::move(browser));
}

void AssetsPanel::refresh()
{
    m_lastScan = -100.0;
}

void AssetsPanel::update(UiFrame& frame)
{
    const Project* project = m_editor.project();
    m_noProject->setVisible(project == nullptr);
    m_browser->setVisible(project != nullptr);
    if (project == nullptr) {
        m_shownProject = nullptr;
        return;
    }
    const double now = frame.ui().time();
    const std::uint64_t assetsRevision = m_editor.assets() != nullptr ? m_editor.assets()->revision() : 0;
    if (project != m_shownProject || project->folder() != m_shownFolder || now - m_lastScan >= kRescanSeconds
        || assetsRevision != m_shownAssets) {
        m_shownAssets = assetsRevision;
        m_shownProject = project;
        m_shownFolder = project->folder();
        m_lastScan = now;
        m_paths = listProject(project->folder());
        rebuild();
    }
    // Highlight the selected asset, or else the open scene.
    if (const AssetDatabase* db = m_editor.assets(); db != nullptr && !m_editor.selectedAsset().isNull()) {
        if (const AssetRecord* asset = db->find(m_editor.selectedAsset())) {
            const auto it = std::find(m_paths.begin(), m_paths.end(), asset->path);
            if (it != m_paths.end()) {
                m_tree->setSelectedId(static_cast<std::uint64_t>(it - m_paths.begin()) + 1);
            }
            return;
        }
    }
    if (const auto rel = project->relative(m_editor.document().path())) {
        const auto it = std::find(m_paths.begin(), m_paths.end(), *rel);
        if (it != m_paths.end()) {
            m_tree->setSelectedId(static_cast<std::uint64_t>(it - m_paths.begin()) + 1);
        }
    }
}

void AssetsPanel::rebuild()
{
    // A folder row for the project itself, then its files.
    std::vector<ui::TreeItem> items;
    std::map<std::string, std::uint64_t> folderIds; // "scenes/" -> item id
    for (std::size_t i = 0; i < m_paths.size(); ++i) {
        const std::string& path = m_paths[i];
        const bool folder = path.back() == '/';
        const std::string trimmed = folder ? path.substr(0, path.size() - 1) : path;
        const std::size_t slash = trimmed.rfind('/');
        const std::string name = slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
        const std::string parent = slash == std::string::npos ? "" : trimmed.substr(0, slash + 1);
        if (!m_filter.empty() && !folder && !containsIgnoringCase(name, m_filter)) {
            continue;
        }
        ui::TreeItem item;
        item.id = i + 1;
        item.label = name;
        const auto p = folderIds.find(parent);
        item.parentId = p != folderIds.end() ? p->second : 0;
        item.depth = static_cast<int>(std::count(trimmed.begin(), trimmed.end(), '/'));
        if (folder) {
            item.icon = ui::Icon::Folder;
            folderIds[path] = item.id;
        } else if (name.size() > 7 && name.compare(name.size() - 7, 7, ".lscene") == 0) {
            item.icon = ui::Icon::Rolo;
            item.detail = "Scene";
        } else if (name == "project.rolo" || name == "project.lantern") {
            item.icon = ui::Icon::Sliders;
            item.detail = "Project settings";
        } else if (const AssetRecord* asset = m_editor.assets() != nullptr ? m_editor.assets()->findByPath(path) : nullptr) {
            switch (asset->type) {
            case AssetType::Image:
                item.icon = ui::Icon::Scene;
                item.detail = asset->image ? std::to_string(asset->image->width) + "×"
                                                 + std::to_string(asset->image->height)
                                           : "Image";
                break;
            case AssetType::Tileset: item.icon = ui::Icon::Grid; item.detail = "Tileset"; break;
            case AssetType::Audio: item.icon = ui::Icon::Sound; item.detail = "Sound"; break;
            case AssetType::Font: item.icon = ui::Icon::Dot; item.detail = "Font"; break;
            case AssetType::Scene: item.icon = ui::Icon::Rolo; item.detail = "Scene"; break;
            case AssetType::Variables: item.icon = ui::Icon::Sliders; item.detail = "Variables"; break;
            case AssetType::Dialogue: item.icon = ui::Icon::Terminal; item.detail = "Dialogue"; break;
            case AssetType::Quest: item.icon = ui::Icon::Check; item.detail = "Quest"; break;
            case AssetType::Script: item.icon = ui::Icon::Code; item.detail = "Luau script"; break;
            case AssetType::Animation:
                item.icon = ui::Icon::Play;
                item.detail = asset->animation ? std::to_string(asset->animation->clips.size())
                                                     + (asset->animation->clips.size() == 1 ? " clip" : " clips")
                                               : "Animations";
                break;
            }
            if (!asset->isUsable()) {
                item.icon = ui::Icon::Warning;
                item.detail = "Problem: see Inspector";
            }
        } else {
            item.dimmed = true;
        }
        items.push_back(std::move(item));
    }
    // Folders that end up empty under a search are left out.
    if (!m_filter.empty()) {
        std::vector<ui::TreeItem> kept;
        for (const ui::TreeItem& item : items) {
            const bool isFolder = m_paths[item.id - 1].back() == '/';
            const bool hasChild = std::any_of(items.begin(), items.end(), [&](const ui::TreeItem& other) {
                return other.parentId == item.id;
            });
            if (!isFolder || hasChild) {
                kept.push_back(item);
            }
        }
        items = std::move(kept);
    }
    for (ui::TreeItem& item : items) {
        item.hasChildren = std::any_of(items.begin(), items.end(),
                                       [&](const ui::TreeItem& other) { return other.parentId == item.id; });
    }
    m_tree->setItems(std::move(items));
}

} // namespace rolo::editor

// ---------------------------------------------------------------------------
// Assets Preview: the selected folder as a grid of thumbnails
// ---------------------------------------------------------------------------
namespace rolo::editor {

namespace {
constexpr float kTileWidth = 92.0f;
constexpr float kTileHeight = 100.0f;
constexpr float kThumb = 64.0f;
} // namespace

class AssetsPreviewPanel::Grid final : public ui::Widget {
public:
    struct Entry {
        std::string path;   // project-relative; folders end with "/"
        std::string name;
        AssetId asset;
        ui::Icon icon = ui::Icon::Dot;
        ui::TextureId thumbnail = 0;
        ui::Size thumbnailSize;
    };

    explicit Grid(EditorServices& editor)
        : m_editor(editor)
    {
        setStretch(1.0f);
        setFocusable(true);
    }

    std::vector<Entry>& entries() noexcept { return m_entries; }

    void paint(ui::Context& ctx, ui::DrawList& draw) override
    {
        const ui::Theme& t = ctx.theme();
        const ui::Rect b = bounds();
        draw.pushClip(b);
        if (m_entries.empty()) {
            const std::string text = "This folder is empty. Drop images, sounds or fonts onto the window to import them.";
            draw.textEllipsized(t.font, t.fontSize, {b.x + t.padding, b.y + t.padding}, t.textFaint, text,
                                b.width - 2.0f * t.padding);
        }
        const AssetId selected = m_editor.selectedAsset();
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            const Entry& e = m_entries[i];
            const ui::Rect tile = tileRect(i);
            if (tile.y > b.bottom() || tile.bottom() < b.y) {
                continue;
            }
            const bool isSelected = !e.asset.isNull() && e.asset == selected;
            if (isSelected) {
                draw.fillRoundedRect(tile, t.radius, t.selection);
            } else if (isHovered() && tile.contains(ctx.pointerPosition())) {
                draw.fillRoundedRect(tile, t.radius, t.hover);
            }
            const ui::Rect thumb{tile.x + (tile.width - kThumb) * 0.5f, tile.y + 6.0f, kThumb, kThumb};
            if (e.thumbnail != 0 && e.thumbnailSize.width > 0.0f) {
                // Fit inside the square, keeping the shape; on a dark backing so transparency shows.
                draw.fillRoundedRect(thumb, 3.0f, ui::Color::hex(0x1B1B1B));
                const float s = std::min(kThumb / e.thumbnailSize.width, kThumb / e.thumbnailSize.height);
                const float w = std::round(e.thumbnailSize.width * s);
                const float h = std::round(e.thumbnailSize.height * s);
                draw.image(e.thumbnail, {std::round(thumb.x + (kThumb - w) * 0.5f), std::round(thumb.y + (kThumb - h) * 0.5f), w, h});
            } else {
                drawIcon(draw, e.icon, thumb.inset(14.0f), e.icon == ui::Icon::Folder ? ui::Color::hex(0x56A8F5) : t.textDim);
            }
            const float textW = ctx.atlas().measure(t.font, t.smallFontSize, e.name);
            const float maxW = tile.width - 6.0f;
            const float x = textW < maxW ? tile.x + (tile.width - textW) * 0.5f : tile.x + 3.0f;
            draw.textEllipsized(t.font, t.smallFontSize, {std::round(x), thumb.bottom() + 6.0f},
                                isSelected ? t.text : t.textDim, e.name, maxW);
        }
        draw.popClip();
    }

    bool onPointerDown(ui::Context& ctx, const ui::PointerEvent& event) override
    {
        if (event.button != ui::MouseButton::Left) {
            return false;
        }
        ctx.setFocus(this);
        m_pressed = indexAt(event.position);
        m_dragging = false;
        m_press = event.position;
        if (!m_pressed) {
            return true;
        }
        const Entry& e = m_entries[*m_pressed];
        if (event.clickCount >= 2) {
            activate(e);
            m_pressed.reset();
            return true;
        }
        if (!e.asset.isNull()) {
            m_editor.selectAsset(e.asset);
        }
        return true;
    }

    void onPointerMove(ui::Context&, const ui::PointerEvent& event) override
    {
        if (m_pressed && !m_dragging
            && std::abs(event.position.x - m_press.x) + std::abs(event.position.y - m_press.y) > 6.0f) {
            m_dragging = !m_entries[*m_pressed].asset.isNull();
        }
    }

    bool onPointerUp(ui::Context&, const ui::PointerEvent& event) override
    {
        if (m_dragging && m_pressed && *m_pressed < m_entries.size()) {
            m_editor.dropAsset(m_entries[*m_pressed].asset, event.position);
        }
        m_pressed.reset();
        m_dragging = false;
        return true;
    }

    ui::CursorShape cursor(ui::Vec2) const override
    {
        return m_dragging ? ui::CursorShape::Hand : ui::CursorShape::Arrow;
    }

    std::string tooltip() const override
    {
        return "Click to inspect · double-click to open · drag an image into the Scene view to place it";
    }

private:
    int columns() const
    {
        return std::max(1, static_cast<int>((bounds().width - 8.0f) / kTileWidth));
    }

    ui::Rect tileRect(std::size_t i) const
    {
        const int cols = columns();
        const auto col = static_cast<float>(static_cast<int>(i) % cols);
        const auto row = static_cast<float>(static_cast<int>(i) / cols);
        return {bounds().x + 8.0f + col * kTileWidth, bounds().y + 8.0f + row * kTileHeight, kTileWidth - 4.0f,
                kTileHeight - 4.0f};
    }

    std::optional<std::size_t> indexAt(ui::Vec2 p) const
    {
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            if (tileRect(i).contains(p)) {
                return i;
            }
        }
        return std::nullopt;
    }

    void activate(const Entry& e)
    {
        const Project* project = m_editor.project();
        if (project == nullptr) {
            return;
        }
        if (e.path.back() == '/') {
            m_editor.setImportFolder(e.path.substr(0, e.path.size() - 1));
        } else if (e.path.size() > 7 && e.path.compare(e.path.size() - 7, 7, ".lscene") == 0) {
            m_editor.openScene(project->absolute(e.path));
        } else if (!e.asset.isNull()) {
            m_editor.selectAsset(e.asset);
        }
    }

    EditorServices& m_editor;
    std::vector<Entry> m_entries;
    std::optional<std::size_t> m_pressed;
    bool m_dragging = false;
    ui::Vec2 m_press;
};

AssetsPreviewPanel::AssetsPreviewPanel(EditorServices& editor)
    : ui::Column(0.0f)
    , m_editor(editor)
{
    auto& toolbar = makePanelToolbar(*this);
    addToolButton(toolbar, ui::Icon::ChevronLeft, "Up one folder", [this] {
        std::string folder = m_editor.importFolder();
        const std::size_t slash = folder.rfind('/');
        m_editor.setImportFolder(slash == std::string::npos ? std::string() : folder.substr(0, slash));
    });
    toolbar.emplace<ui::IconView>(ui::Icon::Folder, 14.0f).setFixedSize({22.0f, 14.0f});
    m_path = &toolbar.emplace<ui::Label>("", ui::Label::Style::Dim);
    m_grid = &addChild(std::make_unique<Grid>(editor));
}

void AssetsPreviewPanel::update(UiFrame& frame)
{
    const Project* project = m_editor.project();
    std::vector<Grid::Entry>& entries = m_grid->entries();
    entries.clear();
    if (project == nullptr) {
        m_path->setText("No project open");
        return;
    }
    const std::string folder = m_editor.importFolder();
    m_path->setText(project->settings().name + (folder.empty() ? "" : " / " + folder));
    const std::filesystem::path dir = project->folder() / std::filesystem::path(folder);
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> items;
    for (const auto& item : std::filesystem::directory_iterator(dir, ec)) {
        const std::string leaf = item.path().filename().string();
        if (leaf.empty() || leaf.front() == '.' || item.path().extension() == ".meta"
            || item.path().extension() == ".tmp") {
            continue;
        }
        items.push_back(item);
    }
    // Folders first, then files, each alphabetically.
    std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
        const bool da = a.is_directory(ec);
        const bool dbir = b.is_directory(ec);
        return da != dbir ? da : a.path().filename() < b.path().filename();
    });
    const AssetDatabase* db = m_editor.assets();
    for (const auto& item : items) {
        Grid::Entry e;
        e.name = item.path().filename().string();
        e.path = (folder.empty() ? "" : folder + "/") + e.name;
        if (item.is_directory(ec)) {
            e.path += "/";
            e.icon = ui::Icon::Folder;
        } else if (item.path().extension() == ".lscene") {
            e.icon = ui::Icon::Rolo;
        } else if (e.name == "project.rolo" || e.name == "project.lantern") {
            e.icon = ui::Icon::Sliders;
        } else if (const AssetRecord* asset = db != nullptr ? db->findByPath(e.path) : nullptr) {
            e.asset = asset->id;
            e.icon = asset->type == AssetType::Tileset ? ui::Icon::Grid : asset->isUsable() ? ui::Icon::Scene : ui::Icon::Warning;
            AssetId imageId = asset->type == AssetType::Image ? asset->id : AssetId{};
            if (asset->type == AssetType::Tileset && asset->tileset) {
                imageId = asset->tileset->image;
            }
            if (asset->type == AssetType::Animation && asset->animation) {
                imageId = asset->animation->image;
                e.icon = ui::Icon::Play;
            }
            if (asset->type == AssetType::Audio && asset->isUsable()) {
                e.icon = ui::Icon::Sound;
            }
            const AssetRecord* image = db->find(imageId);
            if (image != nullptr && image->isUsable()) {
                if (const auto loaded = frame.loadImage(db->absolutePath(*image), image->filter == TextureFilter::Pixel)) {
                    e.thumbnail = loaded->texture;
                    e.thumbnailSize = loaded->size;
                }
            }
        }
        entries.push_back(std::move(e));
    }
}

} // namespace rolo::editor
