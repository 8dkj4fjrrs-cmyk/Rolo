#include "Panels.hpp"

#include "rolo/ui/Context.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rolo::editor {

namespace {

constexpr std::uint64_t kSceneRow = EditorState::kSceneSelected;

bool containsIgnoringCase(std::string_view text, std::string_view needle)
{
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                       [&](char a, char b) { return lower(a) == lower(b); })
           != text.end();
}

} // namespace

// The tree, plus the keys that act on the selected object.
class HierarchyPanel::SceneTree final : public ui::TreeView {
public:
    explicit SceneTree(EditorServices& editor)
        : m_editor(editor)
    {
    }

    bool onKeyDown(ui::Context& ctx, const ui::KeyEvent& event) override
    {
        if (!m_editor.selection().isNull() && event.modifiers.none()) {
            if (event.key == ui::Key::Delete || event.key == ui::Key::Backspace) {
                m_editor.deleteSelection();
                return true;
            }
            if (event.key == ui::Key::F2) {
                m_editor.renameSelection();
                return true;
            }
        }
        return TreeView::onKeyDown(ctx, event);
    }

private:
    EditorServices& m_editor;
};

HierarchyPanel::HierarchyPanel(EditorServices& editor)
    : ui::Column(0.0f)
    , m_editor(editor)
{
    auto& toolbar = makePanelToolbar(*this);
    m_create = &addToolButton(toolbar, ui::Icon::Plus, "Create an object", [this] {
        const bool hasSelection = !m_editor.selection().isNull();
        const auto item = [this, hasSelection](const char* label, NewEntityKind kind, bool child) {
            ui::MenuItem m;
            m.label = label;
            m.action = [this, kind, child] { m_editor.createEntity(kind, child); };
            if (child) {
                m.isEnabled = [hasSelection] { return hasSelection; }; // needs something to go inside
            }
            return m;
        };
        std::vector<ui::MenuItem> items{item("Empty Object", NewEntityKind::Empty, false),
                                        item("Rectangle", NewEntityKind::Rectangle, false),
                                        item("Ellipse", NewEntityKind::Ellipse, false),
                                        item("Sprite", NewEntityKind::Sprite, false),
                                        item("Camera", NewEntityKind::Camera, false),
                                        item("Tilemap", NewEntityKind::Tilemap, false), ui::MenuItem::divider(),
                                        item("Empty Child of Selection", NewEntityKind::Empty, true),
                                        item("Rectangle Child of Selection", NewEntityKind::Rectangle, true)};
        openMenuAt(m_editor.ui(), m_create->bounds(), std::move(items));
    });
    auto& search = toolbar.emplace<ui::TextField>("Search name");
    search.setLeadingIcon(ui::Icon::Search);
    search.setClearButton(true);
    search.setStretch(1.0f);
    search.setOnChange([this](const std::string& text) { m_filter = text; });

    auto tree = std::make_unique<SceneTree>(editor);
    tree->setStretch(1.0f);
    tree->setAutoExpandDepth(2); // the scene and its top-level objects start open
    tree->setEmptyText("The scene is empty.");
    tree->setOnSelect([this](std::uint64_t id) {
        if (id == kSceneRow) {
            m_editor.selectScene();
        } else {
            m_editor.select(EntityId(id));
        }
    });
    tree->setOnActivate([this](std::uint64_t id) {
        if (id != kSceneRow) {
            m_editor.renameSelection();
        }
    });
    m_tree = &addChild(std::move(tree));
}

void HierarchyPanel::update(UiFrame&)
{
    const Scene& scene = m_editor.document().scene();
    if (m_editor.documentGeneration() != m_shownGeneration || scene.revision() != m_shownRevision
        || m_filter != m_shownFilter) {
        rebuild();
        m_shownGeneration = m_editor.documentGeneration();
        m_shownRevision = scene.revision();
        m_shownFilter = m_filter;
    }
    // The selection can change elsewhere (Inspector, viewport, undo).
    const std::uint64_t selected = m_editor.isSceneSelected() ? kSceneRow : m_editor.selection().value();
    if (m_tree->selectedId() != selected) {
        m_tree->setSelectedId(selected);
        m_tree->reveal(selected);
    }
}

void HierarchyPanel::rebuild()
{
    const Scene& scene = m_editor.document().scene();

    // With a search: matching objects plus the objects they sit inside.
    std::unordered_set<EntityId> keep;
    if (!m_filter.empty()) {
        scene.forEach([&](const Entity& e, int) {
            if (containsIgnoringCase(e.name(), m_filter)) {
                for (const Entity* p = &e; p != nullptr && keep.insert(p->id()).second; p = scene.find(p->parent())) {
                }
            }
        });
    }

    std::vector<ui::TreeItem> items;
    ui::TreeItem root;
    root.id = kSceneRow;
    root.label = scene.name();
    root.icon = ui::Icon::Rolo;
    root.hasChildren = !scene.empty();
    items.push_back(root);

    std::unordered_map<EntityId, bool> active; // effectively active (all parents active too)
    scene.forEach([&](const Entity& e, int depth) {
        const auto parent = active.find(e.parent());
        const bool on = e.isActive() && (parent == active.end() || parent->second);
        active[e.id()] = on;
        if (!m_filter.empty() && keep.count(e.id()) == 0) {
            return;
        }
        ui::TreeItem item;
        item.id = e.id().value();
        item.parentId = e.parent().isNull() ? kSceneRow : e.parent().value();
        item.depth = depth + 1;
        item.label = e.name().empty() ? "(unnamed)" : e.name();
        item.hasChildren = !e.children().empty();
        item.dimmed = !on;
        if (e.isPrefabCopy()) {
            item.icon = ui::Icon::Layers;   // a linked copy of a prefab
        }
        items.push_back(std::move(item));
    });
    m_tree->setEmptyText(m_filter.empty() ? "The scene is empty." : "No objects match “" + m_filter + "”.");
    m_tree->setItems(std::move(items));
}

} // namespace rolo::editor
