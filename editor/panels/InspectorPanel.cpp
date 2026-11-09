#include "Panels.hpp"

#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Script.hpp"

#include "../model/EditorPaths.hpp"

#include "rolo/ui/Context.hpp"
#include "rolo/ui/widgets/Dialog.hpp"
#include "rolo/ui/widgets/Menu.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace rolo::editor {

namespace {

ui::Color toUi(Color c)
{
    return ui::Color{c.r, c.g, c.b, c.a};
}

Color fromUi(ui::Color c)
{
    return Color{c.r, c.g, c.b, c.a};
}

std::string capitalized(std::string text)
{
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

std::string componentTypeId(const Component& c)
{
    if (const auto* unknown = dynamic_cast<const UnknownComponent*>(&c)) {
        return unknown->originalTypeId();
    }
    return c.type().id;
}

// The current value of a property in the open scene, if it still exists.
std::optional<PropertyValue> currentValue(const Scene& scene, EntityId id, const std::string& typeId,
                                          const std::string& key)
{
    const Entity* e = scene.find(id);
    const Component* c = e != nullptr ? e->component(typeId) : nullptr;
    return c != nullptr ? c->get(key) : std::nullopt;
}

ui::ValueBox& readOnlyRow(ui::Section& section, const char* label, const std::string& tooltip)
{
    auto& row = section.body().emplace<ui::PropertyField>(label);
    auto& box = row.addValue(std::make_unique<ui::ValueBox>());
    box.setTooltip(tooltip);
    return box;
}

} // namespace

InspectorPanel::InspectorPanel(EditorServices& editor)
    : ui::Column(0.0f)
    , m_editor(editor)
{
    m_empty = &emplace<ui::EmptyState>(ui::Icon::Sliders, "Nothing selected",
                                       "Select an object in the Hierarchy or the Scene to see and edit its "
                                       "properties.");
    auto scroll = std::make_unique<ui::ScrollView>();
    scroll->setStretch(1.0f);
    m_details = &addChild(std::move(scroll));
}

std::string InspectorPanel::signature() const
{
    // The panel is rebuilt when this changes: another object, another scene,
    // or components added or removed.
    const Scene& scene = m_editor.document().scene();
    std::string sig = std::to_string(m_editor.documentGeneration()) + "|";
    if (m_editor.isSceneSelected()) {
        return sig + "scene";
    }
    if (const AssetId asset = m_editor.selectedAsset(); !asset.isNull()) {
        // Assets: rebuilt when anything about the project's assets changes.
        const AssetDatabase* db = m_editor.assets();
        return sig + "asset" + asset.toString() + "|" + (db != nullptr ? std::to_string(db->revision()) : "");
    }
    const Entity* e = scene.find(m_editor.selection());
    if (e == nullptr) {
        return sig + "none";
    }
    sig += e->id().toString();
    for (const auto& c : e->components()) {
        sig += "|" + componentTypeId(*c);
    }
    // A tilemap's layer list is rebuilt when its layers change.
    if (const Tilemap* map = e->get<Tilemap>()) {
        for (const TileLayer& layer : map->layers) {
            sig += "|L" + std::string(layer.visible ? "1" : "0") + layer.name;
        }
        sig += "|S" + std::to_string(map->solid.size());
    }
    return sig;
}

void InspectorPanel::update(UiFrame& frame)
{
    m_frame = &frame;
    if (const std::string sig = signature(); sig != m_builtFor) {
        m_builtFor = sig;
        rebuild();
    }
    for (const auto& refresh : m_refresh) {
        refresh();
    }
    m_frame = nullptr;
}

void InspectorPanel::focusName()
{
    if (m_name != nullptr && context() != nullptr) {
        context()->setFocus(m_name);
        m_name->state().selectAll();
    }
}

void InspectorPanel::rebuild()
{
    // Which keyed field has focus, to give it focus again after rebuilding
    // (pages are rebuilt after each change, e.g. when Tab leaves a field).
    std::string focusedKey;
    for (const auto& [key, widget] : m_keyed) {
        if (widget == m_editor.ui().focused()) {
            focusedKey = key;
        }
    }
    m_keyed.clear();
    m_refresh.clear();
    m_name = nullptr;
    const Scene& scene = m_editor.document().scene();
    const Entity* entity = scene.find(m_editor.selection());
    const bool showScene = m_editor.isSceneSelected();
    const AssetRecord* asset =
        m_editor.assets() != nullptr ? m_editor.assets()->find(m_editor.selectedAsset()) : nullptr;
    m_empty->setVisible(entity == nullptr && !showScene && asset == nullptr);
    m_details->setVisible(entity != nullptr || showScene || asset != nullptr);

    auto content = std::make_unique<ui::Column>(2.0f, ui::Insets{10.0f, 8.0f, 10.0f, 12.0f});
    if (asset != nullptr) {
        buildAsset(*content, *asset);
    } else if (showScene) {
        buildScene(*content);
    } else if (entity != nullptr) {
        buildEntity(*content, *entity);
    }
    m_details->setContent(std::move(content));
    if (const auto it = m_keyed.find(focusedKey); !focusedKey.empty() && it != m_keyed.end()) {
        m_editor.ui().setFocus(it->second);
    }
}

// --- The scene itself --------------------------------------------------------------------------

void InspectorPanel::buildScene(ui::Column& content)
{
    auto& section = content.emplace<ui::Section>("Scene");
    auto& nameRow = section.body().emplace<ui::PropertyField>("Name");
    auto& name = nameRow.addValue(std::make_unique<ui::TextField>(""));
    name.setStretch(1.0f);
    name.setFixedSize({0.0f, 22.0f});
    name.setSelectAllOnFocus(true);
    name.setOnCommit([this](const std::string& text) { m_editor.execute(std::make_unique<RenameScene>(text)); });
    m_name = &name;

    auto& objects = readOnlyRow(section, "Objects", "How many objects the scene contains");
    auto& file = readOnlyRow(section, "File", "Where the scene is saved, inside the project");
    auto& start = readOnlyRow(section, "Start Scene", "The scene the game begins with when it is played");
    auto& makeStart = section.body().emplace<ui::Button>("Make Start Scene");
    makeStart.setTooltip("The game will begin with this scene (saved in the project's settings; undo with Edit > Undo)");
    makeStart.setOnClick([this] {
        const Project* project = m_editor.project();
        const SceneDocument& doc = m_editor.document();
        const auto rel = project != nullptr && !doc.isUntitled() ? project->relative(doc.path()) : std::nullopt;
        if (!rel) {
            return;
        }
        m_editor.endEdit();
        if (!m_editor.execute(std::make_unique<SetStartScene>(*rel))) {
            m_editor.showMessage("Could Not Change the Start Scene",
                                 "Rolo could not write project.rolo. Check that it is not read-only.");
        }
        m_editor.endEdit();
    });
    m_refresh.push_back([this, &start, &makeStart] {
        const Project* project = m_editor.project();
        const SceneDocument& doc = m_editor.document();
        const auto rel = project != nullptr && !doc.isUntitled() ? project->relative(doc.path()) : std::nullopt;
        const bool isStart = rel && project->settings().startScene == *rel;
        start.setText(project == nullptr ? "No project open" : isStart ? "Yes" : project->settings().startScene);
        makeStart.setEnabled(rel.has_value() && !isStart);
    });
    m_refresh.push_back([this, &name, &objects, &file] {
        const SceneDocument& doc = m_editor.document();
        if (!name.isEditing() && name.text() != doc.scene().name()) {
            name.setText(doc.scene().name());
        }
        objects.setText(std::to_string(doc.scene().size()));
        std::string where = "Not saved yet";
        if (!doc.isUntitled()) {
            const Project* project = m_editor.project();
            const auto rel = project != nullptr ? project->relative(doc.path()) : std::nullopt;
            where = rel ? *rel : doc.path().string();
        }
        file.setText(where + (doc.isDirty() ? "  (unsaved changes)" : ""));
    });
}

// --- An entity ---------------------------------------------------------------------------------

void InspectorPanel::buildEntity(ui::Column& content, const Entity& entity)
{
    const EntityId id = entity.id();

    // Header: on/off, and the name.
    auto& header = content.emplace<ui::Row>(6.0f, ui::Insets{0.0f, 0.0f, 0.0f, 6.0f});
    header.setCrossAlign(ui::CrossAlign::Center);
    auto& active = header.emplace<ui::Checkbox>("", entity.isActive(), [this, id](bool on) {
        m_editor.execute(std::make_unique<SetEntityActive>(id, on));
        m_editor.endEdit();
    });
    active.setTooltip("Shown in the game. Hidden objects hide everything inside them too.");
    auto& name = header.emplace<ui::TextField>("Name");
    name.setStretch(1.0f);
    name.setFixedSize({0.0f, 24.0f});
    name.setSelectAllOnFocus(true);
    name.setOnCommit([this, id](const std::string& text) {
        m_editor.execute(std::make_unique<RenameEntity>(id, text));
        m_editor.endEdit();
    });
    m_name = &name;
    m_refresh.push_back([this, id, &active, &name] {
        const Entity* e = m_editor.document().scene().find(id);
        if (e == nullptr) {
            return;
        }
        active.setChecked(e->isActive());
        if (!name.isEditing() && name.text() != e->name()) {
            name.setText(e->name());
        }
    });

    for (const auto& component : entity.components()) {
        buildComponent(content, entity, *component);
    }

    // Add Component.
    auto& addRow = content.emplace<ui::Row>(0.0f, ui::Insets{0.0f, 14.0f, 0.0f, 8.0f});
    addRow.emplace<ui::Spacer>();
    auto& add = addRow.emplace<ui::Button>("Add Component");
    add.setFixedSize({180.0f, 0.0f});
    add.setOnClick([this, &add, id] { openAddComponentMenu(add, id); });
    addRow.emplace<ui::Spacer>();

    // The permanent ID, for reference.
    auto& idRow = content.emplace<ui::Row>(6.0f, ui::Insets{0.0f, 4.0f, 0.0f, 0.0f});
    idRow.emplace<ui::Spacer>();
    auto& idLabel = idRow.emplace<ui::Label>("ID " + id.toString(), ui::Label::Style::Small);
    idLabel.setColor(ui::Color::hex(0x6E6E6E));
    idLabel.setTooltip("This object's permanent ID. Scenes, and later saved games and quests, refer to objects "
                       "by ID, so renaming never breaks anything.");
    idRow.emplace<ui::Spacer>();
}

void InspectorPanel::buildComponent(ui::Column& content, const Entity& entity, const Component& component)
{
    const EntityId id = entity.id();
    const ComponentType& type = component.type();
    const std::string typeId = componentTypeId(component);
    const auto* unknown = dynamic_cast<const UnknownComponent*>(&component);

    auto& section = content.emplace<ui::Section>(unknown != nullptr ? typeId : type.displayName);
    std::string help = type.description;
    for (const PropertyInfo& p : type.properties) {
        if (!p.options.help.empty()) {
            help += "\n\n" + p.label + ": " + p.options.help;
        }
    }
    if (!help.empty()) {
        const std::string title = type.displayName;
        section.setHelpAction([this, title, help] { m_editor.showMessage(title, help); });
    }
    section.setMoreAction([this, &section, id, typeId] { openComponentMenu(section, id, typeId); });

    if (unknown != nullptr) {
        auto& note = section.body().emplace<ui::Label>(
            "This component type is not known to this version of Rolo. Its data is kept unchanged and saved "
            "with the scene.",
            ui::Label::Style::Faint);
        note.setWrap(true);
        return;
    }
    for (const PropertyInfo& info : type.properties) {
        addPropertyEditor(section, id, typeId, info);
    }
    if (dynamic_cast<const Trigger*>(&component) != nullptr) {
        buildTriggerEditor(section, id);
    }
    if (dynamic_cast<const Tilemap*>(&component) != nullptr) {
        buildTilemapLayers(section, id);
    }
    if (dynamic_cast<const Animator*>(&component) != nullptr) {
        auto& buttons = section.body().emplace<ui::Row>(6.0f, ui::Insets{0.0f, 2.0f, 0.0f, 2.0f});
        buttons.emplace<ui::Button>("Edit Clips", [this] { m_editor.showPanel(kAnimationPanel); })
            .setTooltip("Opens the Animation panel for this object's animations");
    }
}

// The layers of a tilemap: which one the tools paint on, visibility, names,
// order; plus the collision layer.
void InspectorPanel::buildTilemapLayers(ui::Section& section, EntityId id)
{
    ui::Column& body = section.body();
    body.emplace<ui::Label>("Layers", ui::Label::Style::Small).setColor(ui::Color::hex(0xA0A0A0));
    auto& list = body.emplace<ui::Column>(2.0f, ui::Insets{0.0f, 2.0f, 0.0f, 4.0f});

    // Applies a change to a copy of the tilemap, as one undoable step.
    auto change = [this, id](std::string label, std::function<void(Tilemap&)> edit) {
        const Entity* e = m_editor.document().scene().find(id);
        const Tilemap* map = e != nullptr ? e->get<Tilemap>() : nullptr;
        if (map == nullptr) {
            return;
        }
        auto copy = std::make_unique<Tilemap>(*map);
        edit(*copy);
        m_editor.execute(std::make_unique<ReplaceComponent>(id, std::move(copy), std::move(label)));
        m_editor.endEdit();
    };

    const Entity* entity = m_editor.document().scene().find(id);
    const Tilemap* map = entity != nullptr ? entity->get<Tilemap>() : nullptr;
    if (map == nullptr) {
        return;
    }
    const auto layerCount = static_cast<int>(map->layers.size());
    if (m_editor.tileLayer() >= layerCount) {
        m_editor.setTileLayer(layerCount - 1);
    }
    for (int i = layerCount - 1; i >= 0; --i) { // top layer first, as drawn
        auto& row = list.emplace<ui::Row>(4.0f);
        row.setCrossAlign(ui::CrossAlign::Center);
        auto& paintHere = row.emplace<ui::Button>("");
        paintHere.setIcon(ui::Icon::Brush);
        paintHere.setStyle(ui::Button::Style::Subtle);
        paintHere.setFixedSize({24.0f, 22.0f});
        paintHere.setTooltip("Paint on this layer");
        paintHere.setOnClick([this, i] { m_editor.setTileLayer(i); });
        auto& visible = row.emplace<ui::Checkbox>("", map->layers[static_cast<std::size_t>(i)].visible);
        visible.setTooltip("Show this layer");
        visible.setOnChange([change, i](bool on) {
            change(on ? "Show Layer" : "Hide Layer", [i, on](Tilemap& m) { m.layers[static_cast<std::size_t>(i)].visible = on; });
        });
        auto& name = row.emplace<ui::TextField>("Layer name");
        name.setStretch(1.0f);
        name.setFixedSize({0.0f, 22.0f});
        name.setText(map->layers[static_cast<std::size_t>(i)].name);
        name.setOnCommit([change, i](const std::string& text) {
            change("Rename Layer", [i, text](Tilemap& m) { m.layers[static_cast<std::size_t>(i)].name = text; });
        });
        auto& up = row.emplace<ui::Button>("");
        up.setIcon(ui::Icon::ChevronDown);
        up.setStyle(ui::Button::Style::Subtle);
        up.setFixedSize({22.0f, 22.0f});
        up.setTooltip("Move down (drawn earlier, underneath)");
        up.setEnabled(i > 0);
        up.setOnClick([this, change, i] {
            change("Reorder Layers", [i](Tilemap& m) { std::swap(m.layers[static_cast<std::size_t>(i)], m.layers[static_cast<std::size_t>(i - 1)]); });
            m_editor.setTileLayer(i - 1);
        });
        auto& remove = row.emplace<ui::Button>("");
        remove.setIcon(ui::Icon::Close);
        remove.setStyle(ui::Button::Style::Subtle);
        remove.setFixedSize({22.0f, 22.0f});
        remove.setTooltip("Delete this layer");
        remove.setEnabled(layerCount > 1);
        remove.setOnClick([change, i] {
            change("Delete Layer", [i](Tilemap& m) { m.layers.erase(m.layers.begin() + i); });
        });
        m_refresh.push_back([this, i, &paintHere] { paintHere.setToggled(m_editor.tileLayer() == i); });
    }
    // Collision: a layer of its own, painted with the same tools.
    auto& collision = list.emplace<ui::Row>(4.0f);
    collision.setCrossAlign(ui::CrossAlign::Center);
    auto& paintCollision = collision.emplace<ui::Button>("");
    paintCollision.setIcon(ui::Icon::Brush);
    paintCollision.setStyle(ui::Button::Style::Subtle);
    paintCollision.setFixedSize({24.0f, 22.0f});
    paintCollision.setTooltip("Paint collision: cells that block movement (shown red in the Scene view)");
    paintCollision.setOnClick([this] { m_editor.setTileLayer(PaintTiles::kCollisionLayer); });
    auto& collisionLabel = collision.emplace<ui::Label>("Collision  ·  " + std::to_string(map->solid.size()) + " cells",
                                                        ui::Label::Style::Dim);
    collisionLabel.setStretch(1.0f);
    m_refresh.push_back([this, &paintCollision] {
        paintCollision.setToggled(m_editor.tileLayer() == PaintTiles::kCollisionLayer);
    });

    auto& buttons = body.emplace<ui::Row>(6.0f, ui::Insets{0.0f, 2.0f, 0.0f, 2.0f});
    buttons.emplace<ui::Button>("Add Layer", [this, change, layerCount] {
        change("Add Layer", [layerCount](Tilemap& m) {
            m.layers.push_back(TileLayer{"Layer " + std::to_string(layerCount + 1), true, {}});
        });
        m_editor.setTileLayer(layerCount);
    });
    buttons.emplace<ui::Button>("Tile Palette", [this] { m_editor.showPanel(kTilePalettePanel); })
        .setTooltip("Choose the tile to paint with");
}

// An Animator's clip: chosen from the clips of its animation set.
void InspectorPanel::addClipChoice(ui::PropertyField& row, EntityId id, std::function<void(PropertyValue)> set,
                                   std::function<void()> end)
{
    auto& field = row.addValue(std::make_unique<ui::ChoiceField>());
    field.setStretch(1.0f);
    auto names = std::make_shared<std::vector<std::string>>();
    field.setOnChoose([set, end, names](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < names->size() && !(*names)[static_cast<std::size_t>(index)].empty()) {
            set((*names)[static_cast<std::size_t>(index)]);
            end();
        }
    });
    m_refresh.push_back([this, id, names, &field] {
        const Entity* e = m_editor.document().scene().find(id);
        const Animator* animator = e != nullptr ? e->get<Animator>() : nullptr;
        if (animator == nullptr) {
            return;
        }
        const AssetDatabase* db = m_editor.assets();
        const AssetRecord* a = db != nullptr ? db->find(animator->animations) : nullptr;
        std::vector<std::string> now;
        std::vector<std::string> labels;
        if (a != nullptr && a->animation) {
            for (const AnimationClip& c : a->animation->clips) {
                now.push_back(c.id);
                labels.push_back(c.id);
            }
        }
        if (!animator->clip.empty() && std::find(now.begin(), now.end(), animator->clip) == now.end()) {
            now.push_back(animator->clip);
            labels.push_back(animator->clip + " (missing)");
        }
        if (now.empty()) {
            now.emplace_back();
            labels.emplace_back(animator->animations.isNull() ? "Choose Animations first" : "No clips");
        }
        if (now != *names) {
            *names = now;
            field.setOptions(std::move(labels));
        }
        const auto it = std::find(names->begin(), names->end(), animator->clip);
        field.setSelected(it != names->end() ? static_cast<int>(it - names->begin()) : 0);
    });
}

void InspectorPanel::addPropertyEditor(ui::Section& section, EntityId id, const std::string& typeId,
                                       const PropertyInfo& info)
{
    auto& row = section.body().emplace<ui::PropertyField>(info.label);
    if (!info.options.help.empty()) {
        row.setTooltip(info.options.help);
    }
    const std::string key = info.key;
    const std::string label = info.label;
    auto set = [this, id, typeId, key, label](PropertyValue value) {
        m_editor.execute(std::make_unique<SetProperty>(id, typeId, key, std::move(value), label));
    };
    auto get = [this, id, typeId, key] { return currentValue(m_editor.document().scene(), id, typeId, key); };
    auto end = [this] { m_editor.endEdit(); };

    const auto numberField = [&](const std::string& suffix) -> ui::NumberField& {
        auto& field = row.addValue(std::make_unique<ui::NumberField>());
        field.setStep(info.options.step);
        field.setRange(info.options.min, info.options.max);
        field.setSuffix(suffix);
        field.setOnEditEnd(end);
        return field;
    };

    switch (info.kind) {
    case PropertyKind::Float:
    case PropertyKind::Int: {
        auto& field = numberField(info.options.unit);
        const bool integer = info.kind == PropertyKind::Int;
        field.setInteger(integer);
        field.setOnValue([set, integer](float v) {
            set(integer ? PropertyValue(static_cast<int>(std::lround(v))) : PropertyValue(v));
        });
        m_refresh.push_back([get, &field] {
            if (const auto v = get()) {
                if (const float* f = std::get_if<float>(&*v)) {
                    field.setValue(*f);
                } else if (const int* i = std::get_if<int>(&*v)) {
                    field.setValue(static_cast<float>(*i));
                }
            }
        });
        break;
    }
    case PropertyKind::Vec2: {
        const bool isSize = key == "size";
        auto& x = numberField(isSize ? "W" : "X");
        auto& y = numberField(isSize ? "H" : "Y");
        x.setOnValue([set, get](float v) {
            if (const auto cur = get(); cur && std::holds_alternative<Vec2>(*cur)) {
                set(Vec2{v, std::get<Vec2>(*cur).y});
            }
        });
        y.setOnValue([set, get](float v) {
            if (const auto cur = get(); cur && std::holds_alternative<Vec2>(*cur)) {
                set(Vec2{std::get<Vec2>(*cur).x, v});
            }
        });
        m_refresh.push_back([get, &x, &y] {
            if (const auto v = get(); v && std::holds_alternative<Vec2>(*v)) {
                x.setValue(std::get<Vec2>(*v).x);
                y.setValue(std::get<Vec2>(*v).y);
            }
        });
        break;
    }
    case PropertyKind::Bool: {
        auto& box = row.addValue(std::make_unique<ui::Checkbox>(""));
        box.setOnChange([set, end](bool on) {
            set(on);
            end();
        });
        m_refresh.push_back([get, &box] {
            if (const auto v = get(); v && std::holds_alternative<bool>(*v)) {
                box.setChecked(std::get<bool>(*v));
            }
        });
        break;
    }
    case PropertyKind::Color: {
        auto& field = row.addValue(std::make_unique<ui::ColorField>());
        field.setOnValue([set](ui::Color c) { set(fromUi(c)); });
        field.setOnEditEnd(end);
        m_refresh.push_back([get, &field] {
            if (const auto v = get(); v && std::holds_alternative<Color>(*v)) {
                field.setValue(toUi(std::get<Color>(*v)));
            }
        });
        break;
    }
    case PropertyKind::Choice: {
        std::vector<std::string> options;
        for (const std::string& choice : info.options.choices) {
            options.push_back(capitalized(choice));
        }
        auto& field = row.addValue(std::make_unique<ui::ChoiceField>(std::move(options)));
        field.setOnChoose([set, end](int index) {
            set(index);
            end();
        });
        m_refresh.push_back([get, &field] {
            if (const auto v = get(); v && std::holds_alternative<int>(*v)) {
                field.setSelected(std::get<int>(*v));
            }
        });
        break;
    }
    case PropertyKind::Asset: {
        // A thumbnail, then a list of the project's assets of the right type.
        auto& thumb = row.addValue(std::make_unique<ui::ImageView>(0, ui::Size{}));
        thumb.setFixedSize({22.0f, 22.0f});
        thumb.setBackground(ui::Color::hex(0x1B1B1B));
        auto& field = row.addValue(std::make_unique<ui::ChoiceField>());
        field.setStretch(1.0f);
        auto ids = std::make_shared<std::vector<AssetId>>();   // ids behind the options (index 0 = None)
        auto shownRevision = std::make_shared<std::uint64_t>(0);
        const std::string assetType = info.options.assetType;
        const bool isSpriteImage = typeId == Sprite::componentType().id && key == "image";
        const bool isTileset = typeId == Tilemap::componentType().id && key == "tileset";
        const bool isAnimations = typeId == Animator::componentType().id && key == "animations";
        field.setOnChoose([this, ids, id, typeId, key, label, isSpriteImage, isTileset, isAnimations, end](int index) {
            const AssetId chosen = index > 0 && static_cast<std::size_t>(index) < ids->size()
                                       ? (*ids)[static_cast<std::size_t>(index)]
                                       : AssetId{};
            std::vector<SetProperties::Change> changes{{typeId, key, chosen}};
            // Choosing a sprite's image sets its size to the image's own size.
            if (isSpriteImage && m_editor.assets() != nullptr) {
                if (const AssetRecord* a = m_editor.assets()->find(chosen); a != nullptr && a->image) {
                    changes.push_back({typeId, "size", Vec2{static_cast<float>(a->image->width),
                                                            static_cast<float>(a->image->height)}});
                }
            }
            // Choosing a tilemap's tileset sizes the cells like its tiles.
            if (isTileset && m_editor.assets() != nullptr) {
                if (const AssetRecord* a = m_editor.assets()->find(chosen); a != nullptr && a->tileset) {
                    changes.push_back({typeId, "cellSize", Vec2{static_cast<float>(a->tileset->tileWidth),
                                                                static_cast<float>(a->tileset->tileHeight)}});
                }
            }
            // Choosing an Animator's animations: the Sprite takes the frame
            // size, and the clip becomes one the set has.
            if (isAnimations && m_editor.assets() != nullptr) {
                const AssetRecord* a = m_editor.assets()->find(chosen);
                const Entity* e = m_editor.document().scene().find(id);
                if (a != nullptr && a->animation && e != nullptr) {
                    if (e->get<Sprite>() != nullptr) {
                        changes.push_back({Sprite::componentType().id, "size",
                                           Vec2{static_cast<float>(a->animation->frameWidth),
                                                static_cast<float>(a->animation->frameHeight)}});
                    }
                    const Animator* animator = e->get<Animator>();
                    if (animator != nullptr && a->animation->findClip(animator->clip) == nullptr) {
                        changes.push_back({typeId, "clip", a->animation->clips.empty()
                                                               ? std::string()
                                                               : a->animation->clips.front().id});
                    }
                }
            }
            m_editor.execute(std::make_unique<SetProperties>(id, std::move(changes), "Change " + label));
            end();
        });
        m_refresh.push_back([this, get, ids, shownRevision, assetType, &field, &thumb] {
            const AssetDatabase* db = m_editor.assets();
            const auto v = get();
            const AssetId current = v && std::holds_alternative<AssetId>(*v) ? std::get<AssetId>(*v) : AssetId{};
            const std::uint64_t revision = db != nullptr ? db->revision() : 0;
            // Rebuild the option list when the assets (or the value) change.
            const bool listed = std::find(ids->begin(), ids->end(), current) != ids->end();
            if (*shownRevision != revision || ids->empty() || !listed) {
                *shownRevision = revision;
                ids->assign(1, AssetId{});
                std::vector<std::string> names{"None"};
                if (db != nullptr) {
                    for (const AssetRecord* a : db->all(assetTypeFromName(assetType))) {
                        ids->push_back(a->id);
                        names.push_back(a->path);
                    }
                }
                if (!current.isNull() && std::find(ids->begin(), ids->end(), current) == ids->end()) {
                    ids->push_back(current);
                    names.push_back("Missing (" + current.toString() + ")");
                }
                field.setOptions(std::move(names));
            }
            const auto it = std::find(ids->begin(), ids->end(), current);
            field.setSelected(static_cast<int>(it - ids->begin()));
            // Thumbnail.
            const AssetRecord* a = db != nullptr ? db->find(current) : nullptr;
            if (a != nullptr && a->type == AssetType::Tileset && a->tileset) {
                a = db->find(a->tileset->image); // a tileset shows its image
            } else if (a != nullptr && a->type == AssetType::Animation && a->animation) {
                a = db->find(a->animation->image); // and animations their sheet
            }
            if (a != nullptr && a->type == AssetType::Image && a->isUsable() && m_frame != nullptr) {
                if (const auto image = m_frame->loadImage(db->absolutePath(*a), a->filter == TextureFilter::Pixel)) {
                    thumb.setTexture(image->texture, image->size);
                    return;
                }
            }
            thumb.setTexture(0, {});
        });
        break;
    }
    case PropertyKind::String: {
        if (typeId == Animator::componentType().id && key == "clip") {
            addClipChoice(row, id, set, end);
            break;
        }
        auto& field = row.addValue(std::make_unique<ui::TextField>(""));
        field.setStretch(1.0f);
        field.setFixedSize({0.0f, 22.0f});
        field.setOnCommit([set, end](const std::string& text) {
            set(text);
            end();
        });
        m_refresh.push_back([get, &field] {
            if (const auto v = get(); v && std::holds_alternative<std::string>(*v) && !field.isEditing()) {
                if (field.text() != std::get<std::string>(*v)) {
                    field.setText(std::get<std::string>(*v));
                }
            }
        });
        break;
    }
    }
}

// --- Menus -------------------------------------------------------------------------------------

void InspectorPanel::openComponentMenu(ui::Section& section, EntityId id, const std::string& typeId)
{
    // Look the component up now: it may have changed since the panel was built.
    const Entity* entity = m_editor.document().scene().find(id);
    const Component* found = entity != nullptr ? entity->component(typeId) : nullptr;
    if (found == nullptr) {
        return;
    }
    const Component& component = *found;
    const ComponentType& type = component.type();
    const bool required = type.required;
    const bool unknown = dynamic_cast<const UnknownComponent*>(&component) != nullptr;

    ui::MenuItem reset;
    reset.label = "Reset to Defaults";
    reset.isEnabled = [unknown] { return !unknown; };
    reset.action = [this, id, typeId, &type] {
        if (!type.create) {
            return;
        }
        const std::unique_ptr<Component> defaults = type.create();
        auto group = std::make_unique<CommandGroup>("Reset " + type.displayName);
        for (const PropertyInfo& p : type.properties) {
            group->add(std::make_unique<SetProperty>(id, typeId, p.key, p.get(*defaults), p.label));
        }
        m_editor.execute(std::move(group));
        m_editor.endEdit();
    };

    ui::MenuItem remove;
    remove.label = "Remove Component";
    remove.isEnabled = [required] { return !required; };
    remove.action = [this, id, typeId] {
        m_editor.execute(std::make_unique<RemoveComponent>(id, typeId));
        m_editor.endEdit();
    };

    std::vector<ui::MenuItem> items{std::move(reset), ui::MenuItem::divider(), std::move(remove)};
    const ui::Rect anchor = section.moreButton();
    openMenuAt(m_editor.ui(), ui::Rect{anchor.right() - 180.0f, anchor.y, 180.0f, anchor.height}, std::move(items));
}

void InspectorPanel::openAddComponentMenu(ui::Button& button, EntityId id)
{
    const Entity* entity = m_editor.document().scene().find(id);
    if (entity == nullptr) {
        return;
    }
    // Rolo's own components first, then each category of the game's
    // (e.g. "Gameplay": behaviours from the linked game modules).
    std::vector<std::string> categories{""};
    for (const ComponentType* type : m_editor.registry().types()) {
        if (std::find(categories.begin(), categories.end(), type->category) == categories.end()) {
            categories.push_back(type->category);
        }
    }
    std::vector<ui::MenuItem> items;
    const auto header = [&items](const std::string& text) {
        ui::MenuItem label;
        label.label = text;
        label.isEnabled = [] { return false; };
        items.push_back(std::move(label));
    };
    for (const std::string& category : categories) {
        bool first = true;
        for (const ComponentType* type : m_editor.registry().types()) {
            if (type->category != category || type->required || entity->component(type->id) != nullptr || !type->create) {
                continue;
            }
            if (first && !category.empty()) {
                items.push_back(ui::MenuItem::divider());
                header(category);
            }
            first = false;
            ui::MenuItem item;
            item.label = type->displayName;
            item.action = [this, id, type] {
                m_editor.execute(std::make_unique<AddComponent>(id, type->create()));
                m_editor.endEdit();
            };
            items.push_back(std::move(item));
        }
    }
    if (items.empty()) {
        header("Every component is already added");
    }
    if (categories.size() == 1) {
        items.push_back(ui::MenuItem::divider());
        header("No game module is linked: your C++ behaviours appear here (see docs/gameplay.md)");
    }
    openMenuAt(m_editor.ui(), button.bounds(), std::move(items));
}

} // namespace rolo::editor

// --- An asset ------------------------------------------------------------------------------------

namespace rolo::editor {

void InspectorPanel::buildAsset(ui::Column& content, const AssetRecord& asset)
{
    AssetDatabase* db = m_editor.assets();
    const AssetId id = asset.id;
    const std::string typeName(assetTypeName(asset.type));

    // Header: the file name (editable: renames the file and its .meta).
    auto& header = content.emplace<ui::Row>(6.0f, ui::Insets{0.0f, 0.0f, 0.0f, 6.0f});
    header.setCrossAlign(ui::CrossAlign::Center);
    header.emplace<ui::IconView>(asset.type == AssetType::Tileset     ? ui::Icon::Grid
                                 : asset.type == AssetType::Animation ? ui::Icon::Play
                                 : asset.type == AssetType::Image ? ui::Icon::Scene
                                 : asset.type == AssetType::Scene ? ui::Icon::Rolo
                                 : asset.type == AssetType::Variables ? ui::Icon::Sliders
                                 : asset.type == AssetType::Dialogue ? ui::Icon::Terminal
                                 : asset.type == AssetType::Quest    ? ui::Icon::Check
                                 : asset.type == AssetType::Script   ? ui::Icon::Code
                                 : asset.type == AssetType::Audio    ? ui::Icon::Sound
                                                                   : ui::Icon::Folder,
                                 16.0f, ui::Color::hex(0xF2A93B));
    auto& name = header.emplace<ui::TextField>("Name");
    name.setStretch(1.0f);
    name.setFixedSize({0.0f, 24.0f});
    name.setSelectAllOnFocus(true);
    name.setText(asset.fileName());
    name.setTooltip("Renaming changes the file name; scenes keep working because they use the asset's ID.");
    // A scene file may be open, or be the project's start scene (stored by
    // path in project.rolo), so scenes are renamed and deleted outside the
    // editor for now.
    const bool isScene = asset.type == AssetType::Scene;
    if (isScene) {
        name.setEnabled(false);
        name.setTooltip("Scenes cannot be renamed in the editor yet (the project's start scene is saved by name).");
    }
    name.setOnCommit([this, id](const std::string& text) {
        AssetDatabase* assets = m_editor.assets();
        const AssetRecord* a = assets != nullptr ? assets->find(id) : nullptr;
        if (a == nullptr || text.empty() || text == a->fileName()) {
            return;
        }
        if (!Project::isValidName(text)) {
            m_editor.showMessage("Invalid Name", "A file name cannot be empty or contain / \\ : * ? \" < > |.");
            return;
        }
        const std::size_t slash = a->path.rfind('/');
        const std::string folder = slash == std::string::npos ? std::string() : a->path.substr(0, slash + 1);
        if (const Status s = assets->move(id, folder + text); !s) {
            m_editor.showMessage("Could Not Rename", s.message);
        }
    });

    if (!asset.isUsable()) {
        auto& problem = content.emplace<ui::Label>(asset.problem);
        problem.setColor(ui::Color::hex(0xF26B65));
        problem.setWrap(true);
    }

    // A preview of images (and the image of a tileset).
    AssetId previewImage = asset.type == AssetType::Image ? id : AssetId{};
    if (asset.type == AssetType::Tileset && asset.tileset) {
        previewImage = asset.tileset->image;
    }
    if (asset.type == AssetType::Animation && asset.animation) {
        previewImage = asset.animation->image;
    }
    if (!previewImage.isNull()) {
        auto& preview = content.emplace<ui::ImageView>(0, ui::Size{});
        preview.setFixedSize({0.0f, 150.0f});
        preview.setBackground(ui::Color::hex(0x1B1B1B));
        m_refresh.push_back([this, previewImage, &preview] {
            AssetDatabase* assets = m_editor.assets();
            const AssetRecord* a = assets != nullptr ? assets->find(previewImage) : nullptr;
            if (a != nullptr && a->isUsable() && m_frame != nullptr) {
                if (const auto image = m_frame->loadImage(assets->absolutePath(*a), a->filter == TextureFilter::Pixel)) {
                    preview.setTexture(image->texture, image->size);
                    return;
                }
            }
            preview.setTexture(0, {});
        });
    }

    auto& info = content.emplace<ui::Section>(typeName == "image"     ? "Image"
                                              : typeName == "tileset" ? "Tileset"
                                              : typeName == "audio"   ? "Sound"
                                              : typeName == "scene"   ? "Scene"
                                              : typeName == "variables" ? "Variables"
                                              : typeName == "dialogue" ? "Dialogue"
                                              : typeName == "quest"   ? "Quest"
                                              : typeName == "animation" ? "Animations"
                                                                      : "Font");
    auto row = [&](const char* label, const std::string& value, const std::string& tip) {
        auto& r = info.body().emplace<ui::PropertyField>(label);
        auto& box = r.addValue(std::make_unique<ui::ValueBox>(value));
        box.setTooltip(tip);
    };
    row("File", asset.path, "Where the file is, inside the project");
    if (asset.image) {
        row("Size", std::to_string(asset.image->width) + " × " + std::to_string(asset.image->height) + " px",
            "The image's size in pixels; a sprite shows it at this size in points by default");
    }

    if (asset.type == AssetType::Image) {
        auto& filterRow = info.body().emplace<ui::PropertyField>("Filtering");
        auto& filter = filterRow.addValue(std::make_unique<ui::ChoiceField>(
            std::vector<std::string>{"Smooth", "Pixel art"}));
        filter.setStretch(1.0f);
        filter.setSelected(asset.filter == TextureFilter::Pixel ? 1 : 0);
        filter.setTooltip("How the image looks when drawn larger or smaller: Smooth blends pixels (photos, "
                          "painted art), Pixel art keeps crisp square pixels.");
        filter.setOnChoose([this, id](int index) {
            const TextureFilter chosen = index == 1 ? TextureFilter::Pixel : TextureFilter::Smooth;
            const AssetRecord* a = m_editor.assets() != nullptr ? m_editor.assets()->find(id) : nullptr;
            if (a == nullptr || a->filter == chosen) {
                return;
            }
            const std::string path = a->path;
            m_editor.endEdit();
            if (!m_editor.execute(std::make_unique<ChangeImageFilter>(id, chosen))) {
                m_editor.showMessage("Could Not Save", "Rolo could not write " + path + ".meta. Check that it is not read-only.");
            }
        });
    }

    if (asset.type == AssetType::Tileset && asset.tileset) {
        const Tileset tileset = *asset.tileset;
        const AssetRecord* image = db != nullptr ? db->find(tileset.image) : nullptr;
        // Edits are saved to the tileset file straight away, as undoable steps.
        auto save = [this, id](const char* label, const std::function<void(Tileset&)>& change) {
            AssetDatabase* assets = m_editor.assets();
            const AssetRecord* a = assets != nullptr ? assets->find(id) : nullptr;
            if (a == nullptr || !a->tileset) {
                return;
            }
            Tileset t = *a->tileset;
            change(t);
            if (t == *a->tileset) {
                return;
            }
            const std::string path = a->path;
            m_editor.endEdit();
            if (!m_editor.execute(std::make_unique<ChangeTileset>(id, std::move(t), label))) {
                m_editor.showMessage("Could Not Save the Tileset",
                                     "Rolo could not write " + path + ". Check that the file is not read-only.");
            }
        };
        auto& imageRow = info.body().emplace<ui::PropertyField>("Image");
        std::vector<std::string> names{"None"};
        auto ids = std::make_shared<std::vector<AssetId>>(1, AssetId{});
        int selected = 0;
        if (db != nullptr) {
            for (const AssetRecord* a : db->all(AssetType::Image)) {
                if (a->id == tileset.image) {
                    selected = static_cast<int>(ids->size());
                }
                ids->push_back(a->id);
                names.push_back(a->path);
            }
        }
        auto& imageField = imageRow.addValue(std::make_unique<ui::ChoiceField>(std::move(names)));
        imageField.setStretch(1.0f);
        imageField.setSelected(selected);
        imageField.setOnChoose([save, ids](int index) {
            const AssetId chosen = index > 0 ? (*ids)[static_cast<std::size_t>(index)] : AssetId{};
            save("Change Tileset Image", [chosen](Tileset& t) { t.image = chosen; });
        });

        const auto numberRow = [&](const char* label, const char* sx, const char* sy, int x, int y,
                                   std::function<void(Tileset&, int, int)> apply, const char* tip) {
            auto& r = info.body().emplace<ui::PropertyField>(label);
            r.setTooltip(tip);
            const std::string undoLabel = std::string("Change ") + label;
            auto values = std::make_shared<std::pair<int, int>>(x, y);
            const auto make = [&](const char* suffix, bool first) -> ui::NumberField& {
                auto& f = r.addValue(std::make_unique<ui::NumberField>());
                f.setInteger(true);
                f.setRange(sy == nullptr ? 0.0f : 1.0f, 4096.0f);
                f.setSuffix(suffix);
                f.setValue(static_cast<float>(first ? x : y));
                f.setOnValue([values, first](float v) {
                    (first ? values->first : values->second) = static_cast<int>(std::lround(v));
                });
                f.setOnEditEnd([save, values, apply, undoLabel] {
                    save(undoLabel.c_str(), [&](Tileset& t) { apply(t, values->first, values->second); });
                });
                return f;
            };
            make(sx, true);
            if (sy != nullptr) {
                make(sy, false);
            }
        };
        numberRow("Tile Size", "W", "H", tileset.tileWidth, tileset.tileHeight,
                  [](Tileset& t, int w, int h) {
                      t.tileWidth = std::max(1, w);
                      t.tileHeight = std::max(1, h);
                  },
                  "The size of one tile in the image, in pixels");
        numberRow("Margin", "px", nullptr, tileset.margin, 0, [](Tileset& t, int m, int) { t.margin = std::max(0, m); },
                  "Empty pixels around the edge of the image");
        numberRow("Spacing", "px", nullptr, tileset.spacing, 0,
                  [](Tileset& t, int sp, int) { t.spacing = std::max(0, sp); }, "Empty pixels between neighbouring tiles");
        if (image != nullptr && image->image) {
            const int cols = tileset.columns(image->image->width);
            const int rows = tileset.rows(image->image->height);
            row("Tiles", std::to_string(cols) + " × " + std::to_string(rows) + " = " + std::to_string(cols * rows),
                "How many tiles the image is cut into");
        }
        row("Solid Tiles", std::to_string(tileset.solid.size()),
            "Tiles that block movement. Choose them in the Tile Palette with \"Edit solid tiles\" on.");
    }

    if (asset.type == AssetType::Animation && asset.animation) {
        buildAnimationAsset(info.body(), asset);
    }

    if (asset.type == AssetType::Variables || asset.type == AssetType::Dialogue || asset.type == AssetType::Quest) {
        buildContentAsset(content, asset);
    }

    if (asset.type == AssetType::Script && db != nullptr) {
        // Scripts are edited in a code editor; here: whether the code is
        // valid, and its first lines.
        const auto code = db->readText(id);
        const auto problem = code ? checkScriptSyntax(*code, asset.path) : std::optional<std::string>("could not read the file");
        auto& check = content.emplace<ui::Label>(problem ? "Problem: " + *problem : "No syntax errors.",
                                                 ui::Label::Style::Small);
        check.setWrap(true);
        if (problem) {
            check.setColor(ui::Color::hex(0xF26B65));
        }
        if (code) {
            auto& preview = content.emplace<ui::Column>(0.0f, ui::Insets{8.0f, 6.0f, 8.0f, 6.0f});
            std::size_t start = 0;
            int shown = 0;
            constexpr int kMaxLines = 40;
            while (start <= code->size() && shown < kMaxLines) {
                const std::size_t end = code->find('\n', start);
                std::string line = code->substr(start, end == std::string::npos ? std::string::npos : end - start);
                std::string expanded;   // tabs as four spaces
                for (const char c : line) {
                    expanded += c == '\t' ? std::string("    ") : std::string(1, c);
                }
                if (!expanded.empty() && expanded.back() == '\r') {
                    expanded.pop_back();
                }
                preview.emplace<ui::Label>(expanded.empty() ? " " : expanded, ui::Label::Style::Small);
                ++shown;
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            if (shown == kMaxLines) {
                preview.emplace<ui::Label>("…", ui::Label::Style::Faint);
            }
        }
    }

    // Where it is used.
    if (db != nullptr) {
        const std::vector<std::string> scenes = db->scenesReferencing(id);
        std::string used = scenes.empty() ? "No saved scene" : std::to_string(scenes.size()) + (scenes.size() == 1 ? " scene" : " scenes");
        std::string tip = "Saved scenes that use this asset:";
        for (const std::string& scene : scenes) {
            tip += "\n" + scene;
        }
        row("Used In", used, scenes.empty() ? "No saved scene uses this asset." : tip);
    }

    // Actions.
    auto& actions = content.emplace<ui::Flow>(6.0f, 6.0f, ui::Insets{0.0f, 12.0f, 0.0f, 4.0f});
    if (asset.type == AssetType::Image && asset.isUsable()) {
        actions.emplace<ui::Button>("Place in Scene", [this, id] {
            m_editor.placeAsset(id, {640.0f, 360.0f});
        }).setTooltip("Adds a sprite showing this image (or drag the image into the Scene view)");
        actions.emplace<ui::Button>("Create Tileset…", [this, id] { showCreateTilesetDialog(id); })
            .setTooltip("Cut this image into tiles for painting tilemaps");
        actions.emplace<ui::Button>("Create Animations…", [this, id] { showCreateAnimationsDialog(id); })
            .setTooltip("Use this image as a sprite sheet: cut it into frames and make animation clips from them");
    }
    if (asset.type == AssetType::Audio && asset.isUsable()) {
        actions.emplace<ui::Button>("Play", [this, id] { m_editor.previewSound(id); })
            .setTooltip("Listen to the sound. In a game, a Sound component, a playSound action or audio.play in a "
                        "script plays it.");
        actions.emplace<ui::Button>("Stop", [this] { m_editor.stopSoundPreview(); });
        actions.emplace<ui::Button>("Place in Scene", [this, id] { m_editor.placeAsset(id, {640.0f, 360.0f}); })
            .setTooltip("Adds an object with a Sound component that plays it when the scene starts (or drag the "
                        "sound into the Scene view)");
    }
    actions.emplace<ui::Button>("Show in Folder", [this, id] {
        AssetDatabase* assets = m_editor.assets();
        if (const AssetRecord* a = assets != nullptr ? assets->find(id) : nullptr) {
            revealInFileManager(assets->absolutePath(*a).parent_path());
        }
    });
    if (asset.type == AssetType::Script) {
        actions.emplace<ui::Button>("Open in Code Editor", [this, id] {
            AssetDatabase* assets = m_editor.assets();
            if (const AssetRecord* a = assets != nullptr ? assets->find(id) : nullptr) {
                openWithDefaultApp(assets->absolutePath(*a));
            }
        }).setTooltip("Opens the script in the program your computer uses for .lua files, e.g. Visual Studio Code. "
                      "Save it there, then press Play.");
    }
    if (asset.type == AssetType::Animation) {
        actions.emplace<ui::Button>("Edit Clips", [this] { m_editor.showPanel(kAnimationPanel); })
            .setTooltip("Opens the Animation panel: choose frames, speed and looping, with a preview");
    }
    if (isScene) {
        actions.emplace<ui::Button>("Open Scene", [this, id] {
            AssetDatabase* assets = m_editor.assets();
            if (const AssetRecord* a = assets != nullptr ? assets->find(id) : nullptr) {
                m_editor.openScene(assets->absolutePath(*a));
            }
        });
    }
    auto& del = actions.emplace<ui::Button>("Delete…", [this, id] { confirmDeleteAsset(id); });
    del.setTooltip("Deletes the file from the project");
    if (isScene) {
        del.setEnabled(false);
        del.setTooltip("Scenes cannot be deleted in the editor yet: delete the file in your file manager");
    }

    auto& idRow = content.emplace<ui::Row>(6.0f, ui::Insets{0.0f, 6.0f, 0.0f, 0.0f});
    idRow.emplace<ui::Spacer>();
    auto& idLabel = idRow.emplace<ui::Label>("ID " + id.toString(), ui::Label::Style::Small);
    idLabel.setColor(ui::Color::hex(0x6E6E6E));
    idLabel.setTooltip("This asset's permanent ID, kept in its .meta file. Scenes refer to assets by ID.");
    idRow.emplace<ui::Spacer>();
}

void InspectorPanel::showCreateTilesetDialog(AssetId imageId)
{
    AssetDatabase* db = m_editor.assets();
    const AssetRecord* image = db != nullptr ? db->find(imageId) : nullptr;
    if (image == nullptr) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Create Tileset", 400.0f);
    ui::Column& body = dialog->body();
    auto& intro = body.emplace<ui::Label>("A tileset cuts " + image->fileName()
                                          + " into equal tiles that you paint tilemaps with.",
                                          ui::Label::Style::Dim);
    intro.setWrap(true);
    std::string stem = image->fileName();
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
        stem.resize(dot);
    }
    auto& nameRow = body.emplace<ui::PropertyField>("Name");
    auto& name = nameRow.addValue(std::make_unique<ui::TextField>(""));
    name.setStretch(1.0f);
    name.setFixedSize({0.0f, 24.0f});
    name.setText(stem);
    auto size = std::make_shared<Vec2>(Vec2{16.0f, 16.0f});   // braces: Apple Clang 15 cannot build aggregates from ( )
    auto& sizeRow = body.emplace<ui::PropertyField>("Tile Size");
    for (int axis = 0; axis < 2; ++axis) {
        auto& f = sizeRow.addValue(std::make_unique<ui::NumberField>());
        f.setInteger(true);
        f.setRange(1.0f, 4096.0f);
        f.setSuffix(axis == 0 ? "W" : "H");
        f.setValue(16.0f);
        f.setOnValue([size, axis](float v) { (axis == 0 ? size->x : size->y) = std::round(v); });
    }
    auto& note = body.emplace<ui::Label>("You can change the tile size, margins and spacing later in the Inspector.",
                                         ui::Label::Style::Faint);
    note.setWrap(true);
    dialog->addButton("Cancel");
    dialog->addButton("Create", ui::Button::Style::Primary, [this, imageId, size, &name] {
        AssetDatabase* assets = m_editor.assets();
        if (assets == nullptr) {
            return;
        }
        const std::string text = name.text().empty() ? std::string("Tileset") : name.text();
        if (!Project::isValidName(text)) {
            m_editor.showMessage("Invalid Name", "A file name cannot contain / \\ : * ? \" < > |.");
            return;
        }
        std::string error;
        if (const auto created = assets->createTileset(imageId, *size, text, &error)) {
            m_editor.selectAsset(*created);
        } else {
            m_editor.showMessage("Could Not Create the Tileset", error);
        }
    });
    ui::showDialog(m_editor.ui(), std::move(dialog));
}

void InspectorPanel::buildAnimationAsset(ui::Column& body, const AssetRecord& asset)
{
    AssetDatabase* db = m_editor.assets();
    const AssetId id = asset.id;
    const AnimationSet set = *asset.animation;
    const AssetRecord* image = db != nullptr ? db->find(set.image) : nullptr;
    auto save = [this, id](const std::string& label, const std::function<void(AnimationSet&)>& change) {
        editAnimationSet(m_editor, id, label, change);
    };

    auto& imageRow = body.emplace<ui::PropertyField>("Sheet");
    imageRow.setTooltip("The image the frames are cut from");
    std::vector<std::string> names{"None"};
    auto ids = std::make_shared<std::vector<AssetId>>(1, AssetId{});
    int selected = 0;
    if (db != nullptr) {
        for (const AssetRecord* a : db->all(AssetType::Image)) {
            if (a->id == set.image) {
                selected = static_cast<int>(ids->size());
            }
            ids->push_back(a->id);
            names.push_back(a->path);
        }
    }
    auto& imageField = imageRow.addValue(std::make_unique<ui::ChoiceField>(std::move(names)));
    imageField.setStretch(1.0f);
    imageField.setSelected(selected);
    imageField.setOnChoose([save, ids](int index) {
        const AssetId chosen = index > 0 ? (*ids)[static_cast<std::size_t>(index)] : AssetId{};
        save("Change Sprite Sheet", [chosen](AnimationSet& s) { s.image = chosen; });
    });

    const auto numberRow = [&](const char* label, const char* sx, const char* sy, int x, int y,
                               std::function<void(AnimationSet&, int, int)> apply, const char* tip) {
        auto& r = body.emplace<ui::PropertyField>(label);
        r.setTooltip(tip);
        const std::string undoLabel = std::string("Change ") + label;
        auto values = std::make_shared<std::pair<int, int>>(x, y);
        const auto make = [&](const char* suffix, bool first) {
            auto& f = r.addValue(std::make_unique<ui::NumberField>());
            f.setInteger(true);
            f.setRange(sy == nullptr ? 0.0f : 1.0f, 4096.0f);
            f.setSuffix(suffix);
            f.setValue(static_cast<float>(first ? x : y));
            f.setOnValue([values, first](float v) {
                (first ? values->first : values->second) = static_cast<int>(std::lround(v));
            });
            f.setOnEditEnd([save, values, apply, undoLabel] {
                save(undoLabel, [&](AnimationSet& s) { apply(s, values->first, values->second); });
            });
        };
        make(sx, true);
        if (sy != nullptr) {
            make(sy, false);
        }
    };
    numberRow("Frame Size", "W", "H", set.frameWidth, set.frameHeight,
              [](AnimationSet& s, int w, int h) {
                  s.frameWidth = std::max(1, w);
                  s.frameHeight = std::max(1, h);
              },
              "The size of one frame in the sheet, in pixels");
    numberRow("Margin", "px", nullptr, set.margin, 0, [](AnimationSet& s, int m, int) { s.margin = std::max(0, m); },
              "Empty pixels around the edge of the sheet");
    numberRow("Spacing", "px", nullptr, set.spacing, 0,
              [](AnimationSet& s, int sp, int) { s.spacing = std::max(0, sp); }, "Empty pixels between neighbouring frames");
    auto info = [&](const char* label, const std::string& value, const std::string& tip) {
        auto& r = body.emplace<ui::PropertyField>(label);
        r.addValue(std::make_unique<ui::ValueBox>(value)).setTooltip(tip);
    };
    if (image != nullptr && image->image) {
        const int cols = set.columns(image->image->width);
        const int rows = set.rows(image->image->height);
        info("Frames", std::to_string(cols) + " × " + std::to_string(rows) + " = " + std::to_string(cols * rows),
             "How many frames the sheet is cut into, numbered from 0 left to right, then top to bottom");
    }
    std::string clips;
    for (const AnimationClip& c : set.clips) {
        clips += (clips.empty() ? "" : ", ") + c.id;
    }
    info("Clips", set.clips.empty() ? "None yet" : clips,
         "Edit clips in the Animation panel (Edit Clips below). Gameplay code plays them by name.");
}

void InspectorPanel::showCreateAnimationsDialog(AssetId imageId)
{
    AssetDatabase* db = m_editor.assets();
    const AssetRecord* image = db != nullptr ? db->find(imageId) : nullptr;
    if (image == nullptr) {
        return;
    }
    auto dialog = std::make_unique<ui::Dialog>("Create Animations", 400.0f);
    ui::Column& body = dialog->body();
    auto& intro = body.emplace<ui::Label>("Animations use " + image->fileName()
                                              + " as a sprite sheet: it is cut into equal frames, and clips (such as "
                                                "walk_left) show some of them one after another.",
                                          ui::Label::Style::Dim);
    intro.setWrap(true);
    std::string stem = image->fileName();
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
        stem.resize(dot);
    }
    auto& nameRow = body.emplace<ui::PropertyField>("Name");
    auto& name = nameRow.addValue(std::make_unique<ui::TextField>(""));
    name.setStretch(1.0f);
    name.setFixedSize({0.0f, 24.0f});
    name.setText(stem);
    auto size = std::make_shared<Vec2>(Vec2{16.0f, 16.0f});   // braces: Apple Clang 15 cannot build aggregates from ( )
    auto& sizeRow = body.emplace<ui::PropertyField>("Frame Size");
    for (int axis = 0; axis < 2; ++axis) {
        auto& f = sizeRow.addValue(std::make_unique<ui::NumberField>());
        f.setInteger(true);
        f.setRange(1.0f, 4096.0f);
        f.setSuffix(axis == 0 ? "W" : "H");
        f.setValue(16.0f);
        f.setOnValue([size, axis](float v) { (axis == 0 ? size->x : size->y) = std::round(v); });
    }
    auto& note = body.emplace<ui::Label>("Then make clips in the Animation panel, and give objects an Animator.",
                                         ui::Label::Style::Faint);
    note.setWrap(true);
    dialog->addButton("Cancel");
    dialog->addButton("Create", ui::Button::Style::Primary, [this, imageId, size, &name] {
        AssetDatabase* assets = m_editor.assets();
        if (assets == nullptr) {
            return;
        }
        const std::string text = name.text().empty() ? std::string("Animations") : name.text();
        if (!Project::isValidName(text)) {
            m_editor.showMessage("Invalid Name", "A file name cannot contain / \\ : * ? \" < > |.");
            return;
        }
        std::string error;
        if (const auto created = assets->createAnimationSet(imageId, *size, text, &error)) {
            m_editor.selectAsset(*created);
            m_editor.showPanel(kAnimationPanel);
        } else {
            m_editor.showMessage("Could Not Create the Animations", error);
        }
    });
    ui::showDialog(m_editor.ui(), std::move(dialog));
}

void InspectorPanel::confirmDeleteAsset(AssetId id)
{
    AssetDatabase* db = m_editor.assets();
    const AssetRecord* asset = db != nullptr ? db->find(id) : nullptr;
    if (asset == nullptr) {
        return;
    }
    const std::vector<std::string> scenes = db->scenesReferencing(id);
    std::string text = "Delete " + asset->path + " from the project? This cannot be undone.";
    if (!scenes.empty()) {
        text += " It is used in " + std::to_string(scenes.size()) + (scenes.size() == 1 ? " scene" : " scenes")
                + ", which will show a pink placeholder instead.";
    }
    auto dialog = std::make_unique<ui::Dialog>("Delete Asset?", 400.0f);
    dialog->body().emplace<ui::Label>(text).setWrap(true);
    dialog->addButton("Cancel");
    dialog->addButton("Delete", ui::Button::Style::Primary, [this, id] {
        AssetDatabase* assets = m_editor.assets();
        if (assets == nullptr) {
            return;
        }
        if (const Status s = assets->remove(id); !s) {
            m_editor.showMessage("Could Not Delete", s.message);
            return;
        }
        m_editor.selectAsset({});
    });
    ui::showDialog(m_editor.ui(), std::move(dialog));
}

} // namespace rolo::editor
