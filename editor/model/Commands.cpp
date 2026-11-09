#include "Commands.hpp"

#include <algorithm>
#include <cmath>

namespace rolo::editor {

// --- CommandStack ---------------------------------------------------------------------------

bool CommandStack::execute(Scene& scene, std::unique_ptr<Command> command)
{
    if (command == nullptr) {
        return false;
    }
    command->useAssets(m_assets);
    if (!command->apply(scene)) {
        return false;
    }
    // Merge into the newest command if it is still the newest thing that
    // happened and the save point does not sit between them.
    const bool atTop = m_position == m_commands.size() && m_position > 0;
    const bool cleanHere = m_cleanPosition && *m_cleanPosition == m_position;
    if (m_mergeAllowed && atTop && !cleanHere && m_commands.back()->mergeWith(*command)) {
        return true;
    }

    // Anything that could have been redone is gone now.
    if (m_cleanPosition && *m_cleanPosition > m_position) {
        m_cleanPosition.reset();
    }
    m_commands.resize(m_position);
    m_commands.push_back(std::move(command));
    ++m_position;
    if (m_commands.size() > m_limit) {
        m_commands.erase(m_commands.begin());
        --m_position;
        if (m_cleanPosition) {
            if (*m_cleanPosition == 0) {
                m_cleanPosition.reset(); // the saved state can no longer be reached
            } else {
                --*m_cleanPosition;
            }
        }
    }
    m_mergeAllowed = true;
    return true;
}

std::string CommandStack::undoLabel() const
{
    return canUndo() ? m_commands[m_position - 1]->label() : std::string{};
}

std::string CommandStack::redoLabel() const
{
    return canRedo() ? m_commands[m_position]->label() : std::string{};
}

bool CommandStack::undo(Scene& scene)
{
    if (!canUndo()) {
        return false;
    }
    --m_position;
    m_commands[m_position]->revert(scene);
    m_mergeAllowed = false;
    return true;
}

bool CommandStack::redo(Scene& scene)
{
    if (!canRedo()) {
        return false;
    }
    if (!m_commands[m_position]->apply(scene)) {
        // The scene no longer allows it (should not happen): drop the redo history.
        m_commands.resize(m_position);
        return false;
    }
    ++m_position;
    m_mergeAllowed = false;
    return true;
}

bool CommandStack::isClean() const noexcept
{
    if (!m_cleanPosition) {
        return false;
    }
    // Clean if every command between the save point and now changed only
    // assets (which were saved to their own files when they ran).
    const std::size_t from = std::min(*m_cleanPosition, m_position);
    const std::size_t to = std::max(*m_cleanPosition, m_position);
    for (std::size_t i = from; i < to; ++i) {
        if (m_commands[i]->changesScene()) {
            return false;
        }
    }
    return true;
}

void CommandStack::setAssets(AssetDatabase* assets) noexcept
{
    m_assets = assets;
    for (const auto& command : m_commands) {
        command->useAssets(assets);
    }
}

void CommandStack::markClean() noexcept
{
    m_cleanPosition = m_position;
    m_mergeAllowed = false;
}

void CommandStack::clear() noexcept
{
    m_commands.clear();
    m_position = 0;
    m_cleanPosition = 0;
    m_mergeAllowed = false;
}

// --- CreateEntity ------------------------------------------------------------------------------

CreateEntity::CreateEntity(std::string name, EntityId parent, std::optional<std::size_t> index,
                           std::vector<std::unique_ptr<Component>> components)
    : m_name(std::move(name))
    , m_parent(parent)
    , m_index(index)
    , m_components(std::move(components))
{
}

bool CreateEntity::apply(Scene& scene)
{
    // Redo re-creates the entity with the same ID, so later commands that
    // refer to it still work.
    Entity* entity = scene.create(m_name, m_parent, m_index, m_id);
    if (entity == nullptr) {
        return false;
    }
    m_id = entity->id();
    for (const auto& c : m_components) {
        if (c->type().required) {
            // The entity already has one (its Transform): copy the given values in.
            for (const PropertyInfo& p : c->type().properties) {
                scene.setProperty(m_id, c->type().id, p.key, p.get(*c));
            }
        } else {
            scene.addComponent(m_id, c->clone());
        }
    }
    return true;
}

void CreateEntity::revert(Scene& scene)
{
    scene.remove(m_id);
}

// --- DeleteEntity -------------------------------------------------------------------------------

DeleteEntity::DeleteEntity(EntityId id)
    : m_id(id)
{
}

bool DeleteEntity::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    if (entity == nullptr) {
        return false;
    }
    m_name = entity->name();
    m_parent = entity->parent();
    m_snapshot = scene.capture(m_id);
    return scene.remove(m_id);
}

void DeleteEntity::revert(Scene& scene)
{
    scene.restore(m_snapshot, m_parent, m_snapshot.index);
}

// --- DuplicateEntity -----------------------------------------------------------------------------

DuplicateEntity::DuplicateEntity(EntityId id)
    : m_source(id)
{
}

bool DuplicateEntity::apply(Scene& scene)
{
    if (m_copy.empty()) {
        const Entity* source = scene.find(m_source);
        if (source == nullptr) {
            return false;
        }
        m_name = source->name();
        m_parent = source->parent();
        m_copy = scene.withNewIds(scene.capture(m_source));
        m_copy.index += 1; // right after the original
    }
    return scene.restore(m_copy, m_parent, m_copy.index);
}

void DuplicateEntity::revert(Scene& scene)
{
    scene.remove(m_copy.rootId());
}

// --- RenameEntity / SetEntityActive ------------------------------------------------------------------

RenameEntity::RenameEntity(EntityId id, std::string name)
    : m_id(id)
    , m_name(std::move(name))
{
}

bool RenameEntity::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    if (entity == nullptr || entity->name() == m_name) {
        return false;
    }
    m_previous = entity->name();
    return scene.rename(m_id, m_name);
}

void RenameEntity::revert(Scene& scene)
{
    scene.rename(m_id, m_previous);
}

SetEntityActive::SetEntityActive(EntityId id, bool active)
    : m_id(id)
    , m_active(active)
{
}

bool SetEntityActive::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    if (entity == nullptr || entity->isActive() == m_active) {
        return false;
    }
    m_previous = entity->isActive();
    return scene.setActive(m_id, m_active);
}

void SetEntityActive::revert(Scene& scene)
{
    scene.setActive(m_id, m_previous);
}

// --- SetProperty -----------------------------------------------------------------------------------

SetProperty::SetProperty(EntityId id, std::string componentType, std::string key, PropertyValue value,
                         std::string label)
    : m_id(id)
    , m_type(std::move(componentType))
    , m_key(std::move(key))
    , m_value(std::move(value))
    , m_label(std::move(label))
{
}

bool SetProperty::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    const Component* component = entity != nullptr ? entity->component(m_type) : nullptr;
    if (component == nullptr) {
        return false;
    }
    const std::optional<PropertyValue> current = component->get(m_key);
    const PropertyInfo* info = component->type().findProperty(m_key);
    if (!current || info == nullptr) {
        return false;
    }
    const std::optional<PropertyValue> wanted = info->coerce(m_value);
    if (!wanted || *wanted == *current) {
        return false; // nothing would change: no undo step
    }
    m_previous = current;
    m_value = *wanted;
    return scene.setProperty(m_id, m_type, m_key, m_value);
}

void SetProperty::revert(Scene& scene)
{
    if (m_previous) {
        scene.setProperty(m_id, m_type, m_key, *m_previous);
    }
}

bool SetProperty::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetProperty*>(&next);
    if (other == nullptr || other->m_id != m_id || other->m_type != m_type || other->m_key != m_key) {
        return false;
    }
    m_value = other->m_value; // keep our m_previous: undo goes back to before the first edit
    return true;
}

// --- SetProperties ------------------------------------------------------------------------------

SetProperties::SetProperties(EntityId id, std::vector<Change> changes, std::string label)
    : m_id(id)
    , m_changes(std::move(changes))
    , m_label(std::move(label))
{
}

bool SetProperties::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    if (entity == nullptr) {
        return false;
    }
    m_previous.assign(m_changes.size(), std::nullopt);
    bool changed = false;
    for (std::size_t i = 0; i < m_changes.size(); ++i) {
        Change& c = m_changes[i];
        const Component* component = entity->component(c.componentType);
        const PropertyInfo* info = component != nullptr ? component->type().findProperty(c.key) : nullptr;
        const auto current = component != nullptr ? component->get(c.key) : std::nullopt;
        const auto wanted = info != nullptr ? info->coerce(c.value) : std::nullopt;
        if (!current || !wanted || *wanted == *current) {
            continue;
        }
        c.value = *wanted;
        m_previous[i] = current;
        changed = scene.setProperty(m_id, c.componentType, c.key, c.value) || changed;
    }
    return changed;
}

void SetProperties::revert(Scene& scene)
{
    for (std::size_t i = m_changes.size(); i-- > 0;) {
        if (m_previous[i]) {
            scene.setProperty(m_id, m_changes[i].componentType, m_changes[i].key, *m_previous[i]);
        }
    }
}

bool SetProperties::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetProperties*>(&next);
    if (other == nullptr || other->m_id != m_id || other->m_changes.size() != m_changes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < m_changes.size(); ++i) {
        if (other->m_changes[i].componentType != m_changes[i].componentType
            || other->m_changes[i].key != m_changes[i].key) {
            return false;
        }
    }
    // Take the newest values; keep our "previous" values (where the next
    // command changed something we did not, take its previous value).
    for (std::size_t i = 0; i < m_changes.size(); ++i) {
        if (other->m_previous[i]) {
            m_changes[i].value = other->m_changes[i].value;
            if (!m_previous[i]) {
                m_previous[i] = other->m_previous[i];
            }
        }
    }
    return true;
}

// --- PaintTiles ------------------------------------------------------------------------------------

namespace {
int readCell(const Tilemap& map, int layer, TileCell cell)
{
    if (layer == PaintTiles::kCollisionLayer) {
        return map.solid.count(cell) > 0 ? 1 : 0;
    }
    return map.tileAt(static_cast<std::size_t>(layer), cell);
}

bool writeCell(Tilemap& map, int layer, TileCell cell, int value)
{
    if (layer == PaintTiles::kCollisionLayer) {
        return map.setSolid(cell, value > 0);
    }
    return map.setTile(static_cast<std::size_t>(layer), cell, value);
}
} // namespace

PaintTiles::PaintTiles(EntityId id, int layer, std::vector<Cell> cells, std::string label)
    : m_id(id)
    , m_layer(layer)
    , m_cells(std::move(cells))
    , m_label(std::move(label))
{
}

bool PaintTiles::apply(Scene& scene)
{
    if (!m_changes.empty()) {
        // Redo: put back exactly what changed the first time.
        return scene.modifyComponent(m_id, Tilemap::componentType().id, [this](Component& c) {
            auto& map = static_cast<Tilemap&>(c);
            for (const Change& ch : m_changes) {
                writeCell(map, m_layer, ch.cell, ch.after);
            }
            return true;
        });
    }
    return scene.modifyComponent(m_id, Tilemap::componentType().id, [this](Component& c) {
        auto& map = static_cast<Tilemap&>(c);
        if (m_layer != kCollisionLayer && (m_layer < 0 || static_cast<std::size_t>(m_layer) >= map.layers.size())) {
            return false;
        }
        for (const Cell& cell : m_cells) {
            const int before = readCell(map, m_layer, cell.cell);
            if (writeCell(map, m_layer, cell.cell, cell.value)) {
                m_changes.push_back({cell.cell, before, readCell(map, m_layer, cell.cell)});
            }
        }
        return !m_changes.empty();
    });
}

void PaintTiles::revert(Scene& scene)
{
    scene.modifyComponent(m_id, Tilemap::componentType().id, [this](Component& c) {
        auto& map = static_cast<Tilemap&>(c);
        for (auto it = m_changes.rbegin(); it != m_changes.rend(); ++it) {
            writeCell(map, m_layer, it->cell, it->before);
        }
        return true;
    });
}

bool PaintTiles::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const PaintTiles*>(&next);
    if (other == nullptr || other->m_id != m_id || other->m_layer != m_layer || other->m_label != m_label) {
        return false;
    }
    for (const Change& ch : other->m_changes) {
        // A cell painted twice in one stroke: keep the first "before".
        const auto it = std::find_if(m_changes.begin(), m_changes.end(), [&](const Change& c) { return c.cell == ch.cell; });
        if (it != m_changes.end()) {
            it->after = ch.after;
        } else {
            m_changes.push_back(ch);
        }
    }
    return true;
}

// --- ReplaceComponent ------------------------------------------------------------------------------

ReplaceComponent::ReplaceComponent(EntityId id, std::unique_ptr<Component> replacement, std::string label)
    : m_id(id)
    , m_after(std::move(replacement))
    , m_label(std::move(label))
{
}

bool ReplaceComponent::swapIn(Scene& scene, const Component& version)
{
    const std::string typeId = version.type().id;
    const Entity* e = scene.find(m_id);
    const auto index = scene.componentIndex(m_id, typeId);
    if (e == nullptr || !index) {
        return false;
    }
    const bool required = version.type().required;
    if (required) {
        return false; // a Transform is never replaced wholesale
    }
    auto old = scene.removeComponent(m_id, typeId);
    if (old == nullptr) {
        return false;
    }
    if (m_before == nullptr) {
        m_before = std::move(old);
    }
    return scene.addComponent(m_id, version.clone(), *index) != nullptr;
}

bool ReplaceComponent::apply(Scene& scene)
{
    return m_after != nullptr && swapIn(scene, *m_after);
}

void ReplaceComponent::revert(Scene& scene)
{
    if (m_before != nullptr) {
        const auto before = m_before->clone();
        swapIn(scene, *before);
    }
}

// --- AddComponent / RemoveComponent ---------------------------------------------------------------------

AddComponent::AddComponent(EntityId id, std::unique_ptr<Component> component)
    : m_id(id)
    , m_component(std::move(component))
{
    if (m_component != nullptr) {
        m_typeId = m_component->type().id;
        m_name = m_component->type().displayName;
    }
}

bool AddComponent::apply(Scene& scene)
{
    return m_component != nullptr && scene.addComponent(m_id, m_component->clone()) != nullptr;
}

void AddComponent::revert(Scene& scene)
{
    scene.removeComponent(m_id, m_typeId);
}

RemoveComponent::RemoveComponent(EntityId id, std::string typeId)
    : m_id(id)
    , m_typeId(std::move(typeId))
{
}

bool RemoveComponent::apply(Scene& scene)
{
    const auto index = scene.componentIndex(m_id, m_typeId);
    if (!index) {
        return false;
    }
    m_removed = scene.removeComponent(m_id, m_typeId);
    if (m_removed == nullptr) {
        return false; // required components cannot be removed
    }
    m_index = *index;
    m_name = m_removed->type().displayName;
    return true;
}

void RemoveComponent::revert(Scene& scene)
{
    if (m_removed != nullptr) {
        scene.addComponent(m_id, std::move(m_removed), m_index);
    }
}

// --- CommandGroup / RenameScene ---------------------------------------------------------------------------

bool CommandGroup::apply(Scene& scene)
{
    m_applied.assign(m_commands.size(), false);
    bool any = false;
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        m_applied[i] = m_commands[i]->apply(scene);
        any = any || m_applied[i];
    }
    return any;
}

void CommandGroup::revert(Scene& scene)
{
    for (std::size_t i = m_commands.size(); i-- > 0;) {
        if (m_applied[i]) {
            m_commands[i]->revert(scene);
        }
    }
}

bool CommandGroup::changesScene() const
{
    return std::any_of(m_commands.begin(), m_commands.end(), [](const auto& c) { return c->changesScene(); });
}

void CommandGroup::useAssets(AssetDatabase* assets)
{
    for (const auto& c : m_commands) {
        c->useAssets(assets);
    }
}

bool RenameScene::apply(Scene& scene)
{
    if (scene.name() == m_name) {
        return false;
    }
    m_previous = scene.name();
    scene.setName(m_name);
    return true;
}

void RenameScene::revert(Scene& scene)
{
    scene.setName(m_previous);
}

// --- SetSceneGravity --------------------------------------------------------------------------------------

bool SetSceneGravity::apply(Scene& scene)
{
    if (scene.gravity() == m_gravity || !std::isfinite(m_gravity.x) || !std::isfinite(m_gravity.y)) {
        return false;
    }
    m_previous = scene.gravity();
    scene.setGravity(m_gravity);
    return true;
}

void SetSceneGravity::revert(Scene& scene)
{
    scene.setGravity(m_previous);
}

bool SetSceneGravity::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const SetSceneGravity*>(&next);
    if (other == nullptr) {
        return false;
    }
    m_gravity = other->m_gravity;   // undo still goes back to before the first change
    return true;
}

// --- MoveEntity -------------------------------------------------------------------------------------------

MoveEntity::MoveEntity(EntityId id, EntityId newParent, std::size_t index)
    : m_id(id)
    , m_newParent(newParent)
    , m_index(index)
{
}

bool MoveEntity::apply(Scene& scene)
{
    const Entity* entity = scene.find(m_id);
    if (entity == nullptr) {
        return false;
    }
    m_name = entity->name();
    m_oldParent = entity->parent();
    m_oldIndex = scene.indexOf(m_id).value_or(0);
    if (m_oldParent == m_newParent && m_oldIndex == m_index) {
        return false;
    }
    return scene.move(m_id, m_newParent, m_index);
}

void MoveEntity::revert(Scene& scene)
{
    scene.move(m_id, m_oldParent, m_oldIndex);
}

// --- Asset commands ---------------------------------------------------------------------------

ChangeTileset::ChangeTileset(AssetId tileset, Tileset definition, std::string label)
    : m_id(tileset)
    , m_after(std::move(definition))
    , m_label(std::move(label))
{
}

bool ChangeTileset::apply(Scene&)
{
    const AssetRecord* asset = m_assets != nullptr ? m_assets->find(m_id) : nullptr;
    if (asset == nullptr || !asset->tileset || *asset->tileset == m_after) {
        return false;
    }
    const Tileset before = *asset->tileset;   // copy: saving updates the record
    if (!m_assets->saveTileset(m_id, m_after)) {
        return false;
    }
    m_before = before;
    return true;
}

void ChangeTileset::revert(Scene&)
{
    if (m_assets != nullptr && m_before) {
        (void)m_assets->saveTileset(m_id, *m_before);   // the file may have gone; nothing else to undo
    }
}

ChangeAssetText::ChangeAssetText(AssetId asset, std::string text, std::string label)
    : m_id(asset)
    , m_after(std::move(text))
    , m_label(std::move(label))
{
}

bool ChangeAssetText::apply(Scene&)
{
    if (m_assets == nullptr) {
        return false;
    }
    const auto current = m_assets->readText(m_id);
    if (!current || *current == m_after) {
        return false;
    }
    if (!m_assets->writeText(m_id, m_after)) {
        return false;
    }
    m_before = current;
    return true;
}

void ChangeAssetText::revert(Scene&)
{
    if (m_assets != nullptr && m_before) {
        (void)m_assets->writeText(m_id, *m_before);
    }
}

bool ChangeAssetText::mergeWith(const Command& next)
{
    const auto* other = dynamic_cast<const ChangeAssetText*>(&next);
    if (other == nullptr || other->m_id != m_id || other->m_label != m_label) {
        return false;
    }
    m_after = other->m_after;   // keep m_before: undo goes back before the first edit
    return true;
}

ChangeImageFilter::ChangeImageFilter(AssetId image, TextureFilter filter)
    : m_id(image)
    , m_after(filter)
{
}

bool ChangeImageFilter::apply(Scene&)
{
    const AssetRecord* asset = m_assets != nullptr ? m_assets->find(m_id) : nullptr;
    if (asset == nullptr || asset->type != AssetType::Image || asset->filter == m_after) {
        return false;
    }
    const TextureFilter before = asset->filter;
    if (!m_assets->setFilter(m_id, m_after)) {
        return false;
    }
    m_before = before;
    return true;
}

void ChangeImageFilter::revert(Scene&)
{
    if (m_assets != nullptr) {
        (void)m_assets->setFilter(m_id, m_before);
    }
}

// --- SetStartScene ---------------------------------------------------------------------------------

bool SetStartScene::write(const std::string& path) const
{
    auto project = m_assets != nullptr ? Project::open(m_assets->projectFolder()) : std::nullopt;
    if (!project || !Project::isValidRelativePath(path)) {
        return false;
    }
    ProjectSettings settings = project->settings();
    settings.startScene = path;
    project->setSettings(std::move(settings));
    return static_cast<bool>(project->save());
}

bool SetStartScene::apply(Scene&)
{
    const auto project = m_assets != nullptr ? Project::open(m_assets->projectFolder()) : std::nullopt;
    if (!project || project->settings().startScene == m_after) {
        return false;
    }
    m_before = project->settings().startScene;
    return write(m_after);
}

void SetStartScene::revert(Scene&)
{
    static_cast<void>(write(m_before));
}

// --- SetGameOption ---------------------------------------------------------------------------------

std::string SetGameOption::label() const
{
    return m_option == Option::EscapeQuits ? "Escape Quits the Game" : "F5 / F9 Quick Save";
}

bool& SetGameOption::in(ProjectSettings& settings, Option option)
{
    return option == Option::EscapeQuits ? settings.escapeQuits : settings.quickSaveKeys;
}

bool SetGameOption::write(bool on) const
{
    auto project = m_assets != nullptr ? Project::open(m_assets->projectFolder()) : std::nullopt;
    if (!project) {
        return false;
    }
    ProjectSettings settings = project->settings();
    in(settings, m_option) = on;
    project->setSettings(std::move(settings));
    return static_cast<bool>(project->save());
}

bool SetGameOption::apply(Scene&)
{
    auto project = m_assets != nullptr ? Project::open(m_assets->projectFolder()) : std::nullopt;
    if (!project) {
        return false;
    }
    ProjectSettings settings = project->settings();
    if (in(settings, m_option) == m_after) {
        return false;
    }
    return write(m_after);
}

void SetGameOption::revert(Scene&)
{
    static_cast<void>(write(!m_after));
}

} // namespace rolo::editor
