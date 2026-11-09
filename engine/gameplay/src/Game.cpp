#include "rolo/gameplay/Game.hpp"

#include "rolo/assets/Animation.hpp"

#include "rolo/gameplay/Behaviour.hpp"
#include "rolo/gameplay/Components.hpp"

#include "PhysicsSync.hpp"
#include "ScriptHost.hpp"

#include <algorithm>
#include <cmath>
#include <exception>

namespace rolo {

namespace {
constexpr std::string_view kCategory = "game";
}

Game::Game(const Logger& log, GameSetup setup)
    : m_log(log)
    , m_setup(std::move(setup))
{
    if (m_setup.content == nullptr) {
        m_ownContent = std::make_unique<ContentLibrary>();
        m_setup.content = m_ownContent.get();
    }
    m_runner = std::make_unique<ContentRunner>(*m_setup.content, m_log);
    m_runner->setHost(this);
    m_physics = std::make_unique<PhysicsSync>(m_setup.assets);
}

Game::Game(const Logger& log, const AssetDatabase* assets, SceneLoader loader)
    : Game(log, GameSetup{assets, std::move(loader), nullptr, nullptr, "Game"})
{
}

Game::~Game()
{
    if (m_scene != nullptr) {
        sound().stopAll();   // the game's sounds end with it
    }
}

void Game::start(const Scene& scene, AssetId sceneId)
{
    m_time = 0.0;
    m_dt = 0.0f;
    m_frames = 0;
    m_toDestroy.clear();
    m_nextScene.reset();
    m_arriveAt.clear();
    m_loadSlot.reset();
    m_placePlayerAt.reset();
    m_pendingEvents.clear();
    m_events.clear();
    m_input.releaseAll();
    m_input.endFrame();
    m_scene.reset();
    m_scripts.reset();   // scripts start from scratch, and read their files again
    sound().stopAll();   // a new game starts in silence
    m_soundObjects.clear();
    m_objectSounds.clear();
    m_runner->setState(GameState::newGame(*m_setup.content));   // a new game
    beginScene(scene, sceneId);
}

void Game::stop()
{
    if (m_scene != nullptr) {
        sound().stopAll();
    }
    m_soundObjects.clear();
    m_objectSounds.clear();
    m_physics->reset();
    m_scene.reset();
    m_scripts.reset();
    m_toDestroy.clear();
    m_nextScene.reset();
    m_arriveAt.clear();
    m_loadSlot.reset();
    m_input.releaseAll();
    m_input.endFrame();
}

void Game::beginScene(Scene scene, AssetId id)
{
    m_sceneId = id;
    m_runner->state().scene = id;
    // Leave out what this playthrough removed for good (e.g. collected gems).
    for (const EntityId removed : std::vector<EntityId>(m_runner->state().removed(id).begin(),
                                                         m_runner->state().removed(id).end())) {
        scene.remove(removed);
    }
    m_sceneObjects.clear();
    m_textTemplates.clear();
    scene.forEach([&](const Entity& e, int) {
        m_sceneObjects.insert(e.id());
        if (const Text* text = e.get<Text>()) {
            m_textTemplates[e.id()] = text->text;
        }
    });
    m_triggers.clear();
    m_prompt.clear();
    m_physics->reset();   // the new scene gets its own physics (if it has RigidBodies)
    // The old scene's looping sounds stop; short effects and the music carry
    // on (a new scene with the same music continues it).
    stopObjectSounds();
    scene.forEach([&](const Entity& e, int) {
        if (const Sound* s = e.get<Sound>(); s != nullptr && !s->music && !s->sound.isNull()) {
            sound().preload(s->sound);   // effects: read now, not in the middle of play (music streams)
        }
    });
    // Clips start with the scene (game time keeps counting across scenes).
    scene.forEach([&](const Entity& e, int) {
        if (e.get<Animator>() != nullptr) {
            scene.editAnimatorPlayback(e.id())->startTime = m_time;
        }
    });
    m_scene = std::make_shared<Scene>(std::move(scene));

    // After loading a save, the player stands where they stood; after a
    // door, at the object they arrive at.
    if (!m_arriveAt.empty()) {
        const EntityId target = find(m_arriveAt);
        if (target.isNull()) {
            m_log.warning(kCategory, "Scene '" + m_scene->name() + "' has no object named '" + m_arriveAt
                                         + "' to arrive at; the player starts where the scene puts them.");
        } else {
            m_placePlayerAt = worldPosition(target);
        }
        m_arriveAt.clear();
    }
    if (m_placePlayerAt) {
        if (const EntityId player = firstPlayer(); !player.isNull()) {
            Vec2 local = *m_placePlayerAt;
            if (const Entity* parent = m_scene->find(m_scene->find(player)->parent())) {
                if (const auto inverse = m_scene->worldTransform(parent->id()).inverse()) {
                    local = inverse->apply(local);
                }
            }
            m_scene->edit<Transform>(player)->position = local;
        }
        m_placePlayerAt.reset();
    }
    updatePresentation();   // the dialogue box starts hidden, texts filled in
}

void Game::update(float dt)
{
    if (m_scene == nullptr) {
        return;
    }
    m_dt = std::clamp(dt, 0.0f, kMaxStep);
    m_time += static_cast<double>(m_dt);
    m_events.swap(m_pendingEvents);   // events sent last frame arrive now
    m_pendingEvents.clear();
    const bool talking = m_runner->inDialogue();
    if (talking) {
        handleDialogueInput();
    }
    updateSounds();
    m_physics->sync(*m_scene);   // bodies for new objects, before behaviours push them
    runBehaviours();
    if (m_scene != nullptr) {
        m_physics->step(*m_scene, m_dt);
        deliverCollisions();
    }
    if (m_scene != nullptr) {
        runTriggers(talking);
        m_runner->update(m_dt);
        updatePresentation();
        finishFrame();
    }
    ++m_frames;
    m_input.endFrame();
}

bool Game::isActiveInScene(const Entity& entity) const
{
    for (const Entity* e = &entity; e != nullptr; e = m_scene->find(e->parent())) {
        if (!e->isActive()) {
            return false;
        }
    }
    return true;
}

// --- Behaviours ----------------------------------------------------------------------------------

void Game::runBehaviours()
{
    // Who to run is decided first: behaviours may add or remove objects
    // while they run, which must not disturb the walk through the scene.
    struct Call {
        EntityId entity;
        std::string type;
    };
    std::vector<Call> calls;
    const Scene& scene = *m_scene;
    scene.forEach([&](const Entity& entity, int) {
        for (const auto& component : entity.components()) {
            if (dynamic_cast<const Behaviour*>(component.get()) != nullptr) {
                calls.push_back({entity.id(), component->type().id});
            }
        }
    });

    const std::shared_ptr<Scene> playing = m_scene; // a behaviour may stop the game
    for (const Call& call : calls) {
        if (m_scene != playing) {
            return;
        }
        const Entity* entity = m_scene->find(call.entity);
        if (entity == nullptr || !isActiveInScene(*entity)) {
            continue; // removed or switched off earlier this frame
        }
        // A behaviour's own fields are not drawn, so running it is not a
        // change to the scene (changes it makes through Game or Scene are).
        auto* behaviour = const_cast<Behaviour*>(dynamic_cast<const Behaviour*>(entity->component(call.type)));
        if (behaviour == nullptr || behaviour->m_failed) {
            continue;
        }
        behaviour->m_entity = call.entity;
        const bool starting = !behaviour->m_started;
        try {
            if (starting) {
                behaviour->m_started = true;
                behaviour->start(*this);
            }
            behaviour->update(*this, m_dt);
        } catch (const std::exception& error) {
            behaviour->m_failed = true;
            m_log.error(kCategory, behaviour->type().displayName + " on '" + entity->name() + "' failed in "
                                       + (starting ? "start()" : "update()") + ": " + error.what()
                                       + ". It is switched off until the game is played again.");
        } catch (...) {
            behaviour->m_failed = true;
            m_log.error(kCategory, behaviour->type().displayName + " on '" + entity->name()
                                       + "' failed with an unknown error. It is switched off until the game is "
                                         "played again.");
        }
    }
}

// --- Triggers and dialogue -------------------------------------------------------------------------

void Game::handleDialogueInput()
{
    const auto& line = m_runner->dialogueLine();
    if (!line) {
        return;
    }
    // The player's interact key, Enter or Space go on; up and down pick a choice.
    Key interact = Key::E;
    if (const EntityId player = firstPlayer(); !player.isNull()) {
        interact = toKey(m_scene->find(player)->get<Interactor>()->key);
    }
    const bool confirm = m_input.wasPressed(interact) || m_input.wasPressed(Key::Enter) || m_input.wasPressed(Key::Space);
    if (!line->choices.empty()) {
        const std::size_t count = line->choices.size();
        if (m_input.wasPressed(Key::Down) || m_input.wasPressed(Key::S)) {
            m_choice = (m_choice + 1) % count;
        }
        if (m_input.wasPressed(Key::Up) || m_input.wasPressed(Key::W)) {
            m_choice = (m_choice + count - 1) % count;
        }
        if (confirm) {
            m_runner->choose(std::min(m_choice, count - 1));
        }
    } else if (confirm) {
        m_runner->continueDialogue();
    }
}

void Game::runTriggers(bool talkedThisFrame)
{
    const std::vector<EntityId> players = findAll<Interactor>();
    const auto playerInside = [&](const Box& area) {
        for (const EntityId p : players) {
            const Entity* e = m_scene->find(p);
            if (e == nullptr || !isActiveInScene(*e)) {
                continue;
            }
            if (const auto box = colliderBox(p); box && box->overlaps(area)) {
                return true;
            }
        }
        return false;
    };
    Key interactKey = Key::E;
    if (!players.empty()) {
        interactKey = toKey(m_scene->find(players.front())->get<Interactor>()->key);
    }
    // A key press used by the dialogue this frame (e.g. to close it) must not
    // also start talking again.
    const bool canInteract = !m_runner->inDialogue() && !talkedThisFrame;
    bool interacted = false;
    m_prompt.clear();

    for (const EntityId id : findAll<Trigger>()) {
        const Entity* entity = m_scene->find(id);
        if (entity == nullptr || !isActiveInScene(*entity)) {
            continue;
        }
        const Trigger trigger = *entity->get<Trigger>();   // a copy: actions may change the scene
        TriggerState& state = m_triggers[id];
        if (trigger.once && (state.fired || (!m_sceneId.isNull() && m_runner->state().hasFired(m_sceneId, id)))) {
            continue;
        }
        bool fire = false;
        const auto area = colliderBox(id);
        switch (trigger.on) {
        case TriggerOn::SceneStart:
            fire = !state.started;
            state.started = true;
            break;
        case TriggerOn::Timer:
            state.elapsed += m_dt;
            if (state.elapsed >= trigger.delay) {
                state.elapsed -= std::max(trigger.delay, 0.001f);
                fire = true;
            }
            break;
        case TriggerOn::EnterArea:
        case TriggerOn::LeaveArea: {
            const bool inside = area && playerInside(*area);
            fire = trigger.on == TriggerOn::EnterArea ? (inside && !state.inside) : (!inside && state.inside);
            state.inside = inside;
            break;
        }
        case TriggerOn::Interact: {
            const bool inside = area && playerInside(*area);
            const bool allowed = !trigger.reaction.condition || m_runner->evaluate(*trigger.reaction.condition);
            if (inside && canInteract && allowed && m_prompt.empty() && !interacted) {
                m_prompt = trigger.prompt.empty() ? std::string()
                                                  : "[" + std::string(keyName(interactKey)) + "] " + trigger.prompt;
                if (m_input.wasPressed(interactKey)) {
                    fire = true;
                    interacted = true;   // one interaction per key press
                }
            }
            break;
        }
        case TriggerOn::GameEvent:
            fire = !trigger.event.empty() && wasEmitted(trigger.event);
            break;
        case TriggerOn::ConditionMet: {
            const bool now = trigger.reaction.condition && m_runner->evaluate(*trigger.reaction.condition);
            fire = now && !state.conditionWas;
            state.conditionWas = now;
            break;
        }
        }
        if (!fire) {
            continue;
        }
        if (trigger.on != TriggerOn::ConditionMet && trigger.reaction.condition
            && !m_runner->evaluate(*trigger.reaction.condition)) {
            continue;
        }
        if (trigger.once) {
            state.fired = true;
            if (!m_sceneId.isNull()) {
                m_runner->state().fired(m_sceneId).insert(id);
            }
        }
        m_runner->run(trigger.reaction.actions, id);
        if (m_runner->inDialogue()) {
            m_prompt.clear();
        }
    }
}

void Game::updatePresentation()
{
    if (m_scene == nullptr) {
        return;
    }
    const auto& line = m_runner->dialogueLine();
    // A new line: the first choice is selected again.
    const std::string lineKey = line ? line->dialogue.toString() + "/" + line->node : std::string();
    if (lineKey != m_shownLine) {
        m_shownLine = lineKey;
        m_choice = 0;
    }

    std::string marker = "> ";
    for (const EntityId id : findAll<DialogueView>()) {
        const Entity* e = m_scene->find(id);
        marker = e->get<DialogueView>()->marker;
        if (e->isActive() != line.has_value()) {
            m_scene->setActive(id, line.has_value());
        }
    }

    for (const EntityId id : findAll<Text>()) {
        const Text& text = *m_scene->find(id)->get<Text>();
        const auto known = m_textTemplates.find(id);
        if (known == m_textTemplates.end()) {
            m_textTemplates[id] = text.text;   // made while playing: its text is the template
        }
        const std::string& written = m_textTemplates[id];
        std::string shown;
        Color color = text.color;
        switch (text.source) {
        case TextSource::Text: shown = m_runner->fillIn(written); break;
        case TextSource::DialogueSpeaker:
            if (line) {
                shown = line->speakerName;
                color = line->speakerColor;
            }
            break;
        case TextSource::DialogueLine: shown = line ? line->text : std::string(); break;
        case TextSource::DialogueChoices: shown = dialogueChoicesText(marker); break;
        case TextSource::InteractionPrompt: shown = m_prompt; break;
        case TextSource::QuestLog: shown = questLogText(); break;
        }
        // Only a real change redraws (changing text rebuilds the drawing).
        if (shown != text.text || color != text.color) {
            Text* edit = m_scene->edit<Text>(id);
            edit->text = std::move(shown);
            edit->color = color;
        }
    }
}

std::string Game::dialogueChoicesText(std::string_view marker) const
{
    const auto& line = m_runner->dialogueLine();
    if (!line || line->choices.empty()) {
        return {};
    }
    const std::string blank(marker.size(), ' ');
    std::string out;
    for (std::size_t i = 0; i < line->choices.size(); ++i) {
        out += (i == 0 ? "" : "\n") + (i == m_choice ? std::string(marker) : blank) + line->choices[i].text;
    }
    return out;
}

std::string Game::questLogText() const
{
    std::string out;
    const ContentLibrary& content = *m_setup.content;
    for (const AssetId id : content.questIds()) {
        if (m_runner->state().questState(id) != QuestState::Active) {
            continue;
        }
        const Quest& quest = *content.quest(id);
        out += (out.empty() ? "" : "\n") + m_runner->fillIn(quest.name);
        for (const Objective& o : quest.objectives) {
            const bool done = m_runner->state().isObjectiveDone(id, o.id);
            out += std::string("\n") + (done ? "  [x] " : "  [ ] ") + m_runner->fillIn(o.text.empty() ? o.id : o.text)
                   + (o.optional ? " (optional)" : "");
        }
    }
    return out;
}

// --- End of frame ----------------------------------------------------------------------------------

void Game::finishFrame()
{
    for (const EntityId id : m_toDestroy) {
        if (!m_scene->contains(id)) {
            continue;
        }
        // Objects the scene came with stay removed for this playthrough.
        if (!m_sceneId.isNull() && m_sceneObjects.count(id) > 0) {
            m_runner->state().removed(m_sceneId).insert(id);
        }
        m_scene->remove(id);
    }
    m_toDestroy.clear();

    if (m_loadSlot) {
        const std::string slot = *m_loadSlot;
        m_loadSlot.reset();
        m_nextScene.reset();
        m_arriveAt.clear();
        const auto text = m_setup.saves != nullptr ? m_setup.saves->read(slot) : std::nullopt;
        if (!text) {
            m_log.error(kCategory, "There is no save in slot '" + slot + "'.");
            return;
        }
        SaveLoadResult loaded = parseSave(*text, *m_setup.content);
        for (const FileIssue& issue : loaded.issues) {
            (issue.isError() ? m_log.error(kCategory, issue.toString()) : m_log.warning(kCategory, issue.toString()));
        }
        if (!loaded.ok()) {
            return;
        }
        std::string error = "this program cannot load scenes";
        std::optional<Scene> scene = m_setup.loadScene && !loaded.state->scene.isNull()
                                         ? m_setup.loadScene(loaded.state->scene, error)
                                         : std::nullopt;
        if (!scene) {
            m_log.error(kCategory, "Could not load slot '" + slot + "': its scene could not be opened (" + error + ").");
            return;
        }
        m_placePlayerAt = loaded.state->playerPosition;
        const AssetId sceneId = loaded.state->scene;
        m_runner->setState(std::move(*loaded.state));
        beginScene(std::move(*scene), sceneId);
        m_log.info(kCategory, "Loaded slot '" + slot + "'.");
        return;
    }

    if (!m_nextScene) {
        return;
    }
    const AssetId next = *m_nextScene;
    m_nextScene.reset();
    std::string error = "this program cannot load scenes";
    std::optional<Scene> loaded = m_setup.loadScene ? m_setup.loadScene(next, error) : std::nullopt;
    if (!loaded) {
        m_log.error(kCategory, "Could not change to scene " + next.toString() + ": " + error);
        return;
    }
    m_log.info(kCategory, "Changed to scene '" + loaded->name() + "'.");
    beginScene(std::move(*loaded), next);
}

// --- Objects -----------------------------------------------------------------------------------------

EntityId Game::find(std::string_view name) const
{
    EntityId found;
    if (m_scene != nullptr) {
        m_scene->forEach([&](const Entity& entity, int) {
            if (found.isNull() && entity.name() == name) {
                found = entity.id();
            }
        });
    }
    return found;
}

std::shared_ptr<ScriptHost> Game::scriptHost()
{
    if (m_scripts == nullptr) {
        m_scripts = std::make_shared<ScriptHost>(*this, m_log);
    }
    return m_scripts;
}

EntityId Game::firstPlayer() const
{
    const std::vector<EntityId> players = findAll<Interactor>();
    return players.empty() ? EntityId{} : players.front();
}

std::vector<EntityId> Game::findAllWith(std::string_view typeId) const
{
    std::vector<EntityId> out;
    if (m_scene != nullptr) {
        m_scene->forEach([&](const Entity& entity, int) {
            if (entity.component(typeId) != nullptr) {
                out.push_back(entity.id());
            }
        });
    }
    return out;
}

Vec2 Game::worldPosition(EntityId id) const
{
    return m_scene != nullptr ? m_scene->worldTransform(id).apply({0.0f, 0.0f}) : Vec2{};
}

void Game::destroy(EntityId id)
{
    if (std::find(m_toDestroy.begin(), m_toDestroy.end(), id) == m_toDestroy.end()) {
        m_toDestroy.push_back(id);
    }
}

void Game::changeScene(AssetId scene)
{
    changeScene(scene, {});
}

void Game::changeScene(AssetId scene, std::string arriveAt)
{
    m_nextScene = scene;
    m_arriveAt = std::move(arriveAt);
}

void Game::setActive(EntityId entity, bool active)
{
    if (m_scene != nullptr && !m_scene->setActive(entity, active) && !m_scene->contains(entity)) {
        m_log.warning(kCategory, "Cannot switch object " + entity.toString() + " on or off: it is not in this scene.");
    }
}

void Game::emit(std::string_view event)
{
    m_pendingEvents.emplace_back(event);
}

bool Game::wasEmitted(std::string_view event) const
{
    return std::find(m_events.begin(), m_events.end(), event) != m_events.end();
}

bool Game::save(std::string_view slot)
{
    if (m_setup.saves == nullptr) {
        m_log.warning(kCategory, "Saving is not available here; slot '" + std::string(slot) + "' was not saved.");
        return false;
    }
    if (m_sceneId.isNull()) {
        m_log.warning(kCategory, "This scene is not saved in the project yet, so the game cannot be saved.");
        return false;
    }
    GameState& state = m_runner->state();
    state.scene = m_sceneId;
    state.playerPosition.reset();
    if (const EntityId player = firstPlayer(); !player.isNull()) {
        state.playerPosition = worldPosition(player);
    }
    if (const Status s = m_setup.saves->write(slot, serializeSave(state, m_setup.gameName)); !s) {
        m_log.error(kCategory, s.message);
        return false;
    }
    m_log.info(kCategory, "Saved to slot '" + std::string(slot) + "'.");
    return true;
}

void Game::load(std::string_view slot)
{
    m_loadSlot = std::string(slot);
}

// --- Collision ---------------------------------------------------------------------------------------

std::optional<Box> Game::colliderBox(EntityId id) const
{
    return m_scene != nullptr ? rolo::colliderBox(*m_scene, id) : std::nullopt;
}

std::vector<EntityId> Game::overlapping(EntityId id) const
{
    const auto box = colliderBox(id);
    return box ? overlappingColliders(*m_scene, *box, id) : std::vector<EntityId>{};
}

std::vector<EntityId> Game::overlapping(const Box& box, EntityId ignore) const
{
    return m_scene != nullptr ? overlappingColliders(*m_scene, box, ignore) : std::vector<EntityId>{};
}

bool Game::isBlocked(const Box& box, EntityId ignore) const
{
    return m_scene != nullptr && rolo::isBlocked(*m_scene, m_setup.assets, box, ignore);
}

MoveResult Game::move(EntityId id, Vec2 delta)
{
    MoveResult result;
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    if (entity == nullptr) {
        return result;
    }
    if (const auto box = colliderBox(id)) {
        result = sweepBox(*m_scene, m_setup.assets, *box, delta, id);
    } else {
        result.moved = delta;
    }
    if (result.moved.x == 0.0f && result.moved.y == 0.0f) {
        return result;
    }
    // `moved` is in world points; the position is relative to the parent.
    Vec2 local = result.moved;
    if (const Entity* parent = m_scene->find(entity->parent())) {
        if (const auto inverse = m_scene->worldTransform(parent->id()).inverse()) {
            local = inverse->apply(result.moved) - inverse->apply({0.0f, 0.0f});
        }
    }
    if (Transform* t = m_scene->edit<Transform>(id)) {
        t->position = t->position + local;
    }
    return result;
}

// --- Animation ----------------------------------------------------------------------------------

bool Game::play(EntityId id, std::string_view clip, bool restart)
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    const Animator* current = entity != nullptr ? entity->get<Animator>() : nullptr;
    if (current == nullptr) {
        return false;
    }
    if (current->clip == clip && current->playing && !restart) {
        return true;   // already playing: carry on
    }
    Animator* animator = m_scene->editAnimatorPlayback(id);
    animator->clip = std::string(clip);
    animator->playing = true;
    animator->startTime = m_time;
    return true;
}

void Game::stopAnimation(EntityId id)
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    if (entity != nullptr && entity->get<Animator>() != nullptr && entity->get<Animator>()->playing) {
        m_scene->editAnimatorPlayback(id)->playing = false;
    }
}

std::string Game::currentClip(EntityId id) const
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    const Animator* animator = entity != nullptr ? entity->get<Animator>() : nullptr;
    return animator != nullptr ? animator->clip : std::string();
}

bool Game::isAnimationPlaying(EntityId id) const
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    const Animator* animator = entity != nullptr ? entity->get<Animator>() : nullptr;
    return animator != nullptr && animator->playing;
}

bool Game::isAnimationFinished(EntityId id) const
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    const Animator* animator = entity != nullptr ? entity->get<Animator>() : nullptr;
    if (animator == nullptr || !animator->playing || m_setup.assets == nullptr) {
        return false;
    }
    const AssetRecord* asset = m_setup.assets->find(animator->animations);
    if (asset == nullptr || !asset->animation) {
        return false;
    }
    const AnimationClip* clip = animatorClip(*asset->animation, *animator);
    return clip != nullptr && clip->finishedAt(animatorTime(*animator, m_time));
}

} // namespace rolo

namespace rolo {

// --- Sound -------------------------------------------------------------------------------------------------

SoundHandle Game::playSound(AssetId soundId, const SoundOptions& options)
{
    return soundId.isNull() ? 0 : sound().play(soundId, options);
}

SoundHandle Game::playSound(EntityId id)
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    const Sound* s = entity != nullptr ? entity->get<Sound>() : nullptr;
    if (s == nullptr || s->sound.isNull()) {
        return 0;
    }
    if (s->music) {
        sound().playMusic(s->sound, s->fade, s->volume);
        return 0;
    }
    // A loop already going carries on: playing it every frame does not stack it.
    if (const auto playing = m_objectSounds.find(id);
        playing != m_objectSounds.end() && s->loop && sound().isPlaying(playing->second.handle)) {
        return playing->second.handle;
    }
    const SoundHandle handle = sound().play(s->sound, s->options());
    m_objectSounds[id] = {handle, s->loop};
    return handle;
}

void Game::stopSound(EntityId id)
{
    const Entity* entity = m_scene != nullptr ? m_scene->find(id) : nullptr;
    if (const Sound* s = entity != nullptr ? entity->get<Sound>() : nullptr; s != nullptr && s->music) {
        if (sound().music() == s->sound) {
            sound().stopMusic(s->fade);
        }
        return;
    }
    if (const auto playing = m_objectSounds.find(id); playing != m_objectSounds.end()) {
        sound().stop(playing->second.handle);
        m_objectSounds.erase(playing);
    }
}

void Game::playMusic(AssetId music, float fadeSeconds, float volume)
{
    if (!music.isNull()) {
        sound().playMusic(music, fadeSeconds, volume);
    }
}

void Game::stopMusic(float fadeSeconds)
{
    sound().stopMusic(fadeSeconds);
}

// Plays the Sound of objects that have just appeared, and stops the loops of
// objects that went away. Runs at the start of each frame, so a scene's sounds
// begin in its first frame.
void Game::updateSounds()
{
    // Gone, switched off, or the component removed: a looping effect stops
    // (a short one plays to its end), and the sound starts again if the
    // object comes back.
    for (auto it = m_soundObjects.begin(); it != m_soundObjects.end();) {
        const Entity* entity = m_scene->find(*it);
        const Sound* s = entity != nullptr ? entity->get<Sound>() : nullptr;
        if (s != nullptr && isActiveInScene(*entity)) {
            ++it;
            continue;
        }
        if (const auto playing = m_objectSounds.find(*it); playing != m_objectSounds.end()) {
            if (playing->second.loop) {
                sound().stop(playing->second.handle);
            }
            m_objectSounds.erase(playing);
        }
        it = m_soundObjects.erase(it);
    }
    for (const EntityId id : findAllWith(Sound::componentType().id)) {
        const Entity* entity = m_scene->find(id);
        if (entity == nullptr || !isActiveInScene(*entity) || !m_soundObjects.insert(id).second) {
            continue;
        }
        if (entity->get<Sound>()->playOnStart) {
            playSound(id);
        }
    }
}

// Stops the looping effects of the scene's objects (leaving the scene).
void Game::stopObjectSounds()
{
    for (const auto& [id, playing] : m_objectSounds) {
        if (playing.loop) {
            sound().stop(playing.handle);
        }
    }
    m_objectSounds.clear();
    m_soundObjects.clear();
}

} // namespace rolo

namespace rolo {

// --- Physics -----------------------------------------------------------------------------------------------

namespace {

Vec2 normalised(Vec2 v)
{
    const float length = std::sqrt(v.x * v.x + v.y * v.y);
    return length > 1e-6f ? Vec2{v.x / length, v.y / length} : Vec2{};
}

} // namespace

bool Game::hasPhysics() const noexcept
{
    return m_physics->isRunning();
}

Vec2 Game::velocity(EntityId id) const
{
    const BodyHandle body = m_physics->bodyOf(id);
    return body.isNull() ? Vec2{} : m_physics->world()->velocity(body);
}

void Game::setVelocity(EntityId id, Vec2 velocity)
{
    if (const BodyHandle body = m_physics->bodyOf(id); !body.isNull()) {
        m_physics->world()->setVelocity(body, velocity);
    }
}

float Game::spin(EntityId id) const
{
    const BodyHandle body = m_physics->bodyOf(id);
    return body.isNull() ? 0.0f : m_physics->world()->angularVelocity(body);
}

void Game::setSpin(EntityId id, float degreesPerSecond)
{
    if (const BodyHandle body = m_physics->bodyOf(id); !body.isNull()) {
        m_physics->world()->setAngularVelocity(body, degreesPerSecond);
    }
}

void Game::applyImpulse(EntityId id, Vec2 impulse)
{
    if (const BodyHandle body = m_physics->bodyOf(id); !body.isNull()) {
        m_physics->world()->applyImpulse(body, impulse);
    }
}

void Game::applyForce(EntityId id, Vec2 force)
{
    if (const BodyHandle body = m_physics->bodyOf(id); !body.isNull()) {
        m_physics->world()->applyForce(body, force);
    }
}

bool Game::isOnGround(EntityId id) const
{
    const BodyHandle body = m_physics->bodyOf(id);
    if (body.isNull() || m_scene == nullptr) {
        return false;
    }
    // "Up" is against gravity (and its scale on this body: a body falling
    // upwards stands on ceilings).
    float scale = 1.0f;
    if (const Entity* e = m_scene->find(id); e != nullptr && e->get<RigidBody>() != nullptr) {
        scale = e->get<RigidBody>()->gravityScale;
    }
    const Vec2 g = m_scene->gravity();
    const Vec2 up = normalised({-g.x * scale, -g.y * scale});
    if (up.x == 0.0f && up.y == 0.0f) {
        return false;   // no gravity: nothing is "below"
    }
    for (const BodyContact& contact : m_physics->world()->contacts(body)) {
        // The normal points from what it touches towards it: from the ground, upwards.
        if (contact.normal.x * up.x + contact.normal.y * up.y > 0.7f) {   // surfaces up to about 45° count
            return true;
        }
    }
    return false;
}

std::vector<EntityId> Game::touching(EntityId id) const
{
    std::vector<EntityId> out;
    const BodyHandle body = m_physics->bodyOf(id);
    if (body.isNull()) {
        return out;
    }
    for (const BodyContact& contact : m_physics->world()->contacts(body)) {
        const EntityId other{contact.other};
        if (!other.isNull() && std::find(out.begin(), out.end(), other) == out.end()) {
            out.push_back(other);
        }
    }
    return out;
}

Vec2 Game::gravity() const
{
    return m_scene != nullptr ? m_scene->gravity() : Scene::kDefaultGravity;
}

void Game::setGravity(Vec2 gravity)
{
    if (m_scene != nullptr) {
        m_scene->setGravity(gravity);   // the game's copy; physics picks it up next frame
    }
}

// Tells the behaviours of both objects that they started touching.
void Game::deliverCollisions()
{
    const auto deliver = [this](EntityId self, EntityId other) {
        const Entity* entity = m_scene != nullptr ? m_scene->find(self) : nullptr;
        if (entity == nullptr || !isActiveInScene(*entity)) {
            return;
        }
        std::vector<std::string> types;
        for (const auto& component : entity->components()) {
            if (dynamic_cast<const Behaviour*>(component.get()) != nullptr) {
                types.push_back(component->type().id);
            }
        }
        for (const std::string& type : types) {
            entity = m_scene != nullptr ? m_scene->find(self) : nullptr;
            auto* behaviour = entity != nullptr
                                  ? const_cast<Behaviour*>(dynamic_cast<const Behaviour*>(entity->component(type)))
                                  : nullptr;
            if (behaviour == nullptr || behaviour->m_failed || !behaviour->m_started) {
                continue;
            }
            try {
                behaviour->collide(*this, other);
            } catch (const std::exception& error) {
                behaviour->m_failed = true;
                m_log.error(kCategory, behaviour->type().displayName + " on '" + entity->name()
                                           + "' failed in collide(): " + error.what()
                                           + ". It is switched off until the game is played again.");
            }
        }
    };
    const auto hits = m_physics->hits();   // a copy: behaviours may change the scene
    for (const auto& [a, b] : hits) {
        deliver(a, b);
        deliver(b, a);
    }
}

} // namespace rolo
