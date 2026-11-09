#pragma once

// ContentRunner: brings the content to life while a game plays.
//
// It holds a GameState and, as the game asks:
//   - evaluates conditions against it;
//   - runs action lists (setting variables, starting quests and dialogues,
//     waiting, and asking the game for the rest: changing scene, removing
//     objects, saving...);
//   - keeps quests up to date every frame: starts them, completes their
//     objectives, completes or fails them, and runs their rewards;
//   - plays dialogues: the current line, the choices that are available,
//     going on and choosing.
//
// It never touches the scene or the window itself. Whatever only the game
// can do goes through ContentHost, which rolo::Game implements; that keeps
// this class testable on its own.

#include "rolo/content/ContentLibrary.hpp"
#include "rolo/content/GameState.hpp"
#include "rolo/core/Log.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

/// Something that happened to the content, for gameplay code and the log.
struct ContentEvent {
    enum class Kind { QuestStarted, ObjectiveCompleted, QuestCompleted, QuestFailed, DialogueStarted, DialogueEnded };
    Kind kind = Kind::QuestStarted;
    AssetId asset;           // the quest or dialogue
    std::string objective;   // ObjectiveCompleted
};

/// What only the game can do, asked for by actions.
class ContentHost {
public:
    virtual ~ContentHost() = default;
    virtual void changeScene(AssetId scene) = 0;
    /// `entity` is the object an action names, or the object whose trigger
    /// ran the actions when the action names none.
    virtual void setActive(EntityId entity, bool active) = 0;
    virtual void destroy(EntityId entity) = 0;
    virtual void emit(std::string_view event) = 0;
    virtual void saveGame(std::string_view slot) = 0;
    virtual void loadGame(std::string_view slot) = 0;
    /// The playSound, playMusic and stopMusic actions. A host without
    /// audio may leave these out.
    virtual void soundAction(AssetId /*sound*/) {}
    virtual void musicAction(AssetId /*music*/) {}
    virtual void stopMusicAction() {}
    virtual void notify(const ContentEvent& /*event*/) {}
};

/// A dialogue line as players see it, with {variables} filled in.
struct DialogueLine {
    AssetId dialogue;
    std::string node;
    std::string speakerName;       // empty for narration
    Color speakerColor{255, 255, 255, 255};
    AssetId portrait;
    std::string text;
    struct Choice {
        std::string id;
        std::string text;
    };
    std::vector<Choice> choices;   // only those whose condition is true; empty: "continue"
};

class ContentRunner {
public:
    /// Neither `content` nor `log` is owned; both must outlive the runner.
    ContentRunner(const ContentLibrary& content, const Logger& log);

    void setHost(ContentHost* host) noexcept { m_host = host; }
    GameState& state() noexcept { return m_state; }
    const GameState& state() const noexcept { return m_state; }
    /// Replaces the progress (e.g. after loading a save). Ends any dialogue
    /// and waiting actions.
    void setState(GameState state);
    const ContentLibrary& content() const noexcept { return m_content; }

    // --- Conditions and actions ------------------------------------------------------------
    bool evaluate(const Condition& condition) const;
    /// Runs actions in order. A "wait" pauses the rest, which then carry on
    /// in update(). `self` is the object they belong to (e.g. a trigger's).
    void run(const std::vector<Action>& actions, EntityId self = {});
    /// True while actions are waiting to carry on.
    bool isRunningSequences() const noexcept { return !m_sequences.empty(); }

    // --- Quests ------------------------------------------------------------------------------
    /// Starts a quest if it has not started and its prerequisites are complete.
    bool startQuest(AssetId quest);
    bool completeObjective(AssetId quest, std::string_view objective);
    bool failQuest(AssetId quest);

    // --- Dialogue ----------------------------------------------------------------------------
    /// Starts a dialogue (unless one is already being shown). False if it
    /// does not exist or ends at once.
    bool startDialogue(AssetId dialogue);
    bool inDialogue() const noexcept { return m_line.has_value(); }
    const std::optional<DialogueLine>& dialogueLine() const noexcept { return m_line; }
    /// Goes on from a line without choices.
    void continueDialogue();
    /// Picks one of the current line's choices (index into its `choices`).
    void choose(std::size_t index);
    /// Ends the dialogue at once.
    void endDialogue();

    // --- Every frame -----------------------------------------------------------------------
    /// Carries on waiting actions and brings quests up to date.
    void update(float dt);
    /// Fills {variable} placeholders in text with current values.
    std::string fillIn(std::string_view text) const;

private:
    struct Sequence {
        std::vector<Action> actions;
        std::size_t next = 0;
        float wait = 0.0f;
        EntityId self;
    };
    void runSequence(Sequence& sequence);
    void runOne(const Action& action, EntityId self);
    void refreshQuests();
    bool prerequisitesMet(const Quest& quest) const;
    void completeQuest(AssetId id, const Quest& quest);
    void enterNode(AssetId dialogue, std::string_view node);
    void notify(ContentEvent event);

    const ContentLibrary& m_content;
    const Logger& m_log;
    ContentHost* m_host = nullptr;
    GameState m_state;
    std::vector<Sequence> m_sequences;
    std::optional<DialogueLine> m_line;
    int m_depth = 0;   // guards against actions that start each other forever
};

} // namespace rolo
