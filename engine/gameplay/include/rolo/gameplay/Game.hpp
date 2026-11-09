#pragma once

// Game: a scene being played.
//
// The program running the game (the player, or the editor's play mode)
// creates a Game, starts it with a scene and calls update() once per frame.
// Behaviours receive the Game in start() and update() and use it to read
// input, move and find objects, ask about collisions and change scenes.
//
//     void PlayerController::update(rolo::Game& game, float dt)
//     {
//         const rolo::Vec2 direction = game.input().moveDirection();
//         game.move(entity(), direction * (speed * dt));   // stops at walls
//     }
//
// The game also runs the project's RPG content (see rolo/content):
// Triggers in the scene, dialogues (shown by DialogueView and Text objects),
// quests, game variables, and saving and loading the player's progress.
//
// The game plays a *copy* of the scene it was started with. Whatever the game
// changes stays in that copy, so stopping it can never leave changes behind
// in the scene being edited.
//
// Errors: if a behaviour throws a C++ exception, the error is logged (the
// editor shows it in the Console) and that behaviour is switched off for the
// rest of the run; the rest of the game keeps going. A crash (such as using
// a null pointer) cannot be caught this way and ends the program.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentRunner.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Collision.hpp"
#include "rolo/gameplay/Input.hpp"
#include "rolo/gameplay/SaveStore.hpp"
#include "rolo/gameplay/Sound.hpp"
#include "rolo/scene/Scene.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

class ScriptHost;
struct Script;

/// Loads a scene asset for a scene change. Returns nothing (and says why in
/// `error`) if it cannot.
using SceneLoader = std::function<std::optional<Scene>(AssetId scene, std::string& error)>;

/// What a Game needs from the program running it. Everything is optional.
struct GameSetup {
    const AssetDatabase* assets = nullptr;   // tilesets (for collision)
    SceneLoader loadScene;                   // for scene changes and loading saves
    const ContentLibrary* content = nullptr; // variables, dialogues, quests (none: no content)
    SaveStore* saves = nullptr;              // where save slots go (none: saving is reported, not done)
    std::string gameName = "Game";           // written in save files
    SoundPlayer* sound = nullptr;            // the program's audio (none: the game is silent)
};

class Game : private ContentHost {
public:
    /// The longest frame time a game update uses, in seconds. After a pause
    /// (a slow disk, the debugger) time jumps; without a limit, things would
    /// move so far in one step that they pass through walls.
    static constexpr float kMaxStep = 0.1f;

    /// Nothing in `setup` is owned; it must outlive the game.
    explicit Game(const Logger& log, GameSetup setup = {});
    /// Shorthand for a game without content or saves.
    Game(const Logger& log, const AssetDatabase* assets, SceneLoader loader = {});
    ~Game() override;
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // --- Running the game (the player or the editor) --------------------------------------------
    /// Starts a new game playing a copy of `scene`. `sceneId` is the scene's
    /// asset ID (null if it has none), used to remember progress per scene.
    void start(const Scene& scene, AssetId sceneId = {});
    /// Runs one frame: dialogue input, behaviours, triggers, content (quests,
    /// waiting actions), the dialogue box and texts, then removals, loading
    /// and scene changes. Clears this frame's input flags at the end.
    void update(float dt);
    /// Ends the game; its scene copy is dropped.
    void stop();
    bool isRunning() const noexcept { return m_scene != nullptr; }

    /// The scene being played, for drawing. A different object after a
    /// scene change. Null when not running.
    std::shared_ptr<const Scene> sharedScene() const { return m_scene; }
    /// The asset ID of the scene being played.
    AssetId sceneId() const noexcept { return m_sceneId; }
    /// Frames run since start().
    std::uint64_t frameCount() const noexcept { return m_frames; }
    /// Where the program reports keys and the mouse.
    Input& input() noexcept { return m_input; }

    // --- For behaviours -----------------------------------------------------------------------
    /// The scene being played (a copy; see above). Must be running.
    Scene& scene() { return *m_scene; }
    const Scene& scene() const { return *m_scene; }
    const Input& input() const noexcept { return m_input; }
    /// Seconds since start() (game time: does not advance while paused).
    double time() const noexcept { return m_time; }
    /// The length of the current frame, in seconds.
    float deltaTime() const noexcept { return m_dt; }
    const Logger& log() const noexcept { return m_log; }
    const AssetDatabase* assets() const noexcept { return m_setup.assets; }

    /// The first object with this name (in hierarchy order); null ID if none.
    EntityId find(std::string_view name) const;
    /// Every object that has a component of type T, in hierarchy order.
    template <class T>
    std::vector<EntityId> findAll() const
    {
        return findAllWith(T::componentType().id);
    }
    /// A component of an object, to change (null if missing). Changing a
    /// Transform this way only moves things; anything else redraws the object.
    template <class T>
    T* get(EntityId id)
    {
        return m_scene != nullptr ? m_scene->edit<T>(id) : nullptr;
    }
    /// An object's Transform, to move, turn or scale it.
    Transform* transform(EntityId id) { return get<Transform>(id); }
    /// An object's position in the world (parents included).
    Vec2 worldPosition(EntityId id) const;
    /// Removes an object (and its children) at the end of this frame. An
    /// object that was part of the scene stays removed for the rest of the
    /// playthrough (e.g. a collected gem), also in save games.
    void destroy(EntityId id) override;
    /// Switches to another scene at the end of this frame. The current
    /// scene's objects (and their behaviours) are dropped; the new scene's
    /// behaviours start in the next frame.
    void changeScene(AssetId scene) override;
    /// The same, and the player (the object with the Interactor) arrives at
    /// the object named `arriveAt` in the new scene, e.g. the other end of
    /// a road. Empty: wherever the new scene puts the player.
    void changeScene(AssetId scene, std::string arriveAt);

    // --- Collision queries (see Collision.hpp) ----------------------------------------------------
    /// The world box of an object's Collider, if it has one.
    std::optional<Box> colliderBox(EntityId id) const;
    /// Objects whose colliders overlap this object's collider.
    std::vector<EntityId> overlapping(EntityId id) const;
    /// Objects whose colliders overlap a box.
    std::vector<EntityId> overlapping(const Box& box, EntityId ignore = {}) const;
    /// True if something solid (a solid collider or tile) overlaps the box.
    bool isBlocked(const Box& box, EntityId ignore = {}) const;
    /// Moves an object by `delta` (world points), stopping at anything solid
    /// in the way of its collider and sliding along walls. Without a
    /// collider it simply moves.
    MoveResult move(EntityId id, Vec2 delta);

    // --- Animation (see rolo::Animator) ---------------------------------------------------------
    /// Plays a clip on the object's Animator, e.g. play(entity(), "walk_left").
    /// Asking for the clip already playing lets it carry on (so it can be
    /// called every frame), unless `restart` is true. False if the object has
    /// no Animator.
    bool play(EntityId id, std::string_view clip, bool restart = false);
    /// Stops the object's animation on its clip's first frame; play() starts it again.
    void stopAnimation(EntityId id);
    /// The clip the object's Animator plays ("" if it has no Animator).
    std::string currentClip(EntityId id) const;
    /// True while the object's Animator is playing (not stopped).
    bool isAnimationPlaying(EntityId id) const;
    /// True once a clip that does not loop has played to its end (e.g. an
    /// attack). Always false for looping clips and for objects without one.
    bool isAnimationFinished(EntityId id) const;

        // --- Content: variables, quests, dialogue, events, saves ----------------------------------------
    /// Variables, quests, dialogues and actions (see ContentRunner).
    ContentRunner& content() noexcept { return *m_runner; }
    const ContentRunner& content() const noexcept { return *m_runner; }
    /// True while a dialogue is shown (e.g. to stop the player walking).
    bool isInDialogue() const noexcept { return m_runner->inDialogue(); }
    /// Sends a game event; Triggers listening for it run next frame.
    void emit(std::string_view event) override;
    /// True if the event was sent in the previous frame (events reach
    /// triggers and behaviours one frame after they are sent).
    bool wasEmitted(std::string_view event) const;
    /// Saves the progress to a slot now. False (with a log message) if it could not.
    bool save(std::string_view slot);
    /// Loads a slot at the end of this frame (its scene, progress, player position).
    void load(std::string_view slot);
    /// The prompt for what the player can interact with now, e.g. "[E] Talk".
    const std::string& interactionPrompt() const noexcept { return m_prompt; }

    // --- Sound (see Sound.hpp) ---------------------------------------------------------------------
    /// The program's audio: channel volumes, stopping effects. Silent if the
    /// program has none (the game works the same, without sound).
    SoundPlayer& sound() noexcept { return m_setup.sound != nullptr ? *m_setup.sound : m_silence; }
    /// Plays a sound effect, e.g. playSound(coinSound, {.pitch = 1.2f}).
    /// Returns a handle for sound().stop(), or 0 if it could not play.
    SoundHandle playSound(AssetId sound, const SoundOptions& options = {});
    /// Plays the object's Sound component (as music if it is set to). Playing
    /// a looping sound that is already playing lets it carry on. 0 if the
    /// object has no Sound, or it is music.
    SoundHandle playSound(EntityId id);
    /// Stops what the object's Sound component is playing.
    void stopSound(EntityId id);
    /// Plays music, fading from what played before (see SoundPlayer::playMusic).
    void playMusic(AssetId music, float fadeSeconds = 1.0f, float volume = 1.0f);
    void stopMusic(float fadeSeconds = 1.0f);

private:
    friend class ScriptHost;   // Lua scripts reach setActive and the player through it
    friend struct Script;

    struct TriggerState {
        bool started = false;
        bool inside = false;
        bool conditionWas = false;
        float elapsed = 0.0f;
        bool fired = false;
    };

    // ContentHost (changeScene, destroy and emit are public above).
    void setActive(EntityId entity, bool active) override;
    void saveGame(std::string_view slot) override { save(slot); }
    void loadGame(std::string_view slot) override { load(slot); }
    void soundAction(AssetId sound) override { playSound(sound); }
    void musicAction(AssetId music) override { playMusic(music); }
    void stopMusicAction() override { stopMusic(); }

    std::vector<EntityId> findAllWith(std::string_view typeId) const;
    // The player: the first object with an Interactor, or a null id.
    EntityId firstPlayer() const;
    // The Lua state of this run, made when the first script starts.
    std::shared_ptr<ScriptHost> scriptHost();
    void beginScene(Scene scene, AssetId id);
    void handleDialogueInput();
    void updateSounds();
    void stopObjectSounds();
    void runBehaviours();
    void runTriggers(bool talkedThisFrame);
    void updatePresentation();
    void finishFrame();
    bool isActiveInScene(const Entity& entity) const;
    std::string dialogueChoicesText(std::string_view marker) const;
    std::string questLogText() const;

    const Logger& m_log;
    GameSetup m_setup;
    std::unique_ptr<ContentLibrary> m_ownContent;   // used when the setup has none
    std::unique_ptr<ContentRunner> m_runner;
    std::shared_ptr<Scene> m_scene;
    AssetId m_sceneId;
    Input m_input;
    double m_time = 0.0;
    float m_dt = 0.0f;
    std::uint64_t m_frames = 0;

    std::vector<EntityId> m_toDestroy;
    std::optional<AssetId> m_nextScene;
    std::optional<std::string> m_loadSlot;
    std::string m_arriveAt;                          // with m_nextScene: an object's name
    std::optional<Vec2> m_placePlayerAt;             // after loading a save
    std::set<EntityId> m_sceneObjects;                // objects the scene started with
    std::map<EntityId, TriggerState> m_triggers;
    std::map<EntityId, std::string> m_textTemplates;  // each Text's own text, as written
    std::vector<std::string> m_pendingEvents;         // sent this frame
    std::vector<std::string> m_events;                // sent last frame: delivered now
    std::string m_prompt;
    std::size_t m_choice = 0;                         // the selected dialogue choice
    std::string m_shownLine;                          // to notice a new line (resets the choice)
    std::shared_ptr<ScriptHost> m_scripts;            // Lua scripts' state; dropped when the game stops
    SilentSoundPlayer m_silence;                      // sound() when the program has no audio
    std::set<EntityId> m_soundObjects;                // objects whose Sound has started (active ones)
    struct ObjectSound {
        SoundHandle handle = 0;
        bool loop = false;   // remembered: the object may be gone when it is stopped
    };
    std::map<EntityId, ObjectSound> m_objectSounds;   // the effect each object's Sound plays
};

} // namespace rolo
