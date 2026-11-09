#include "rolo/content/ContentRunner.hpp"

#include <algorithm>

namespace rolo {

namespace {

constexpr std::string_view kCategory = "game";
constexpr int kMaxDepth = 32;        // actions starting actions starting actions...
constexpr int kMaxBranchSteps = 64;  // branch nodes followed before giving up (a loop)
constexpr int kMaxQuestPasses = 16;  // quest updates that cause further updates

template <class T>
bool compare(const T& a, const T& b, CompareOp op)
{
    switch (op) {
    case CompareOp::Equal: return a == b;
    case CompareOp::NotEqual: return a != b;
    case CompareOp::Less: return a < b;
    case CompareOp::LessEqual: return a <= b;
    case CompareOp::Greater: return a > b;
    case CompareOp::GreaterEqual: return a >= b;
    }
    return false;
}

} // namespace

ContentRunner::ContentRunner(const ContentLibrary& content, const Logger& log)
    : m_content(content)
    , m_log(log)
    , m_state(GameState::newGame(content))
{
}

void ContentRunner::setState(GameState state)
{
    m_state = std::move(state);
    m_sequences.clear();
    m_line.reset();
}

// --- Conditions and actions ------------------------------------------------------------------------

bool ContentRunner::evaluate(const Condition& c) const
{
    switch (c.kind) {
    case Condition::Kind::Always: return true;
    case Condition::Kind::Compare: {
        const auto value = m_state.variable(c.variable);
        if (!value || value->index() != c.value.index()) {
            return false; // unknown variable or wrong kind: reported by ContentLibrary::check()
        }
        return std::visit(
            [&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                return compare(v, std::get<T>(c.value), c.op);
            },
            *value);
    }
    case Condition::Kind::QuestIs: return m_state.questState(c.quest) == c.state;
    case Condition::Kind::ObjectiveDone: return m_state.isObjectiveDone(c.quest, c.objective);
    case Condition::Kind::All:
        return std::all_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return evaluate(x); });
    case Condition::Kind::Any:
        return std::any_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return evaluate(x); });
    case Condition::Kind::Not: return c.children.empty() ? false : !evaluate(c.children.front());
    }
    return false;
}

void ContentRunner::run(const std::vector<Action>& actions, EntityId self)
{
    if (actions.empty()) {
        return;
    }
    if (m_depth >= kMaxDepth) {
        m_log.error(kCategory, "Actions kept starting more actions (" + std::to_string(kMaxDepth)
                                   + " deep); stopped. Check for content that triggers itself.");
        return;
    }
    Sequence sequence{actions, 0, 0.0f, self};
    runSequence(sequence);
    if (sequence.next < sequence.actions.size() || sequence.wait > 0.0f) {
        m_sequences.push_back(std::move(sequence)); // waiting: carries on in update()
    }
}

void ContentRunner::runSequence(Sequence& sequence)
{
    ++m_depth;
    while (sequence.next < sequence.actions.size()) {
        const Action& action = sequence.actions[sequence.next++];
        if (action.kind == Action::Kind::Wait) {
            if (action.seconds > 0.0f) {
                sequence.wait = action.seconds;
                break;
            }
            continue;
        }
        runOne(action, sequence.self);
    }
    --m_depth;
}

void ContentRunner::runOne(const Action& a, EntityId self)
{
    const auto needHost = [&]() {
        if (m_host == nullptr) {
            m_log.warning(kCategory, "\"" + std::string(actionTypeName(a.kind)) + "\" needs a running game; ignored.");
        }
        return m_host != nullptr;
    };
    switch (a.kind) {
    case Action::Kind::Set: {
        const VariableDefinition* def = m_content.variable(a.variable);
        if (def == nullptr || def->type != typeOf(a.value)) {
            m_log.warning(kCategory, "Cannot set '" + a.variable + "': no such variable of that type.");
            return;
        }
        m_state.setVariable(a.variable, a.value);
        return;
    }
    case Action::Kind::Add: {
        const auto current = m_state.variable(a.variable);
        if (!current || !std::holds_alternative<int>(*current) || !std::holds_alternative<int>(a.value)) {
            m_log.warning(kCategory, "Cannot add to '" + a.variable + "': no such whole-number variable.");
            return;
        }
        m_state.setVariable(a.variable, std::get<int>(*current) + std::get<int>(a.value));
        return;
    }
    case Action::Kind::StartQuest: startQuest(a.quest); return;
    case Action::Kind::CompleteObjective: completeObjective(a.quest, a.objective); return;
    case Action::Kind::FailQuest: failQuest(a.quest); return;
    case Action::Kind::StartDialogue: startDialogue(a.asset); return;
    case Action::Kind::Log: m_log.info(kCategory, fillIn(a.text)); return;
    case Action::Kind::Wait: return; // handled by runSequence
    case Action::Kind::ChangeScene:
        if (needHost()) {
            m_host->changeScene(a.asset);
        }
        return;
    case Action::Kind::SetActive:
        if (needHost()) {
            m_host->setActive(a.entity.isNull() ? self : a.entity, a.active);
        }
        return;
    case Action::Kind::Destroy:
        if (needHost()) {
            m_host->destroy(a.entity.isNull() ? self : a.entity);
        }
        return;
    case Action::Kind::Emit:
        if (needHost()) {
            m_host->emit(a.text);
        }
        return;
    case Action::Kind::SaveGame:
        if (needHost()) {
            m_host->saveGame(a.text);
        }
        return;
    case Action::Kind::LoadGame:
        if (needHost()) {
            m_host->loadGame(a.text);
        }
        return;
    case Action::Kind::PlaySound:
        if (needHost()) {
            m_host->soundAction(a.asset);
        }
        return;
    case Action::Kind::PlayMusic:
        if (needHost()) {
            m_host->musicAction(a.asset);
        }
        return;
    case Action::Kind::StopMusic:
        if (needHost()) {
            m_host->stopMusicAction();
        }
        return;
    }
}

// --- Quests --------------------------------------------------------------------------------------

bool ContentRunner::prerequisitesMet(const Quest& quest) const
{
    return std::all_of(quest.prerequisites.begin(), quest.prerequisites.end(),
                       [&](AssetId p) { return m_state.questState(p) == QuestState::Completed; });
}

bool ContentRunner::startQuest(AssetId id)
{
    const Quest* quest = m_content.quest(id);
    if (quest == nullptr) {
        m_log.warning(kCategory, "Cannot start quest " + id.toString() + ": it does not exist.");
        return false;
    }
    if (m_state.questState(id) != QuestState::NotStarted || !prerequisitesMet(*quest)) {
        return false;
    }
    m_state.progress(id).state = QuestState::Active;
    notify({ContentEvent::Kind::QuestStarted, id, {}});
    refreshQuests(); // objectives that are already met complete straight away
    return true;
}

bool ContentRunner::completeObjective(AssetId id, std::string_view objective)
{
    const Quest* quest = m_content.quest(id);
    if (quest == nullptr || quest->findObjective(objective) == nullptr
        || m_state.questState(id) != QuestState::Active || m_state.isObjectiveDone(id, objective)) {
        return false;
    }
    m_state.progress(id).objectivesDone.insert(std::string(objective));
    notify({ContentEvent::Kind::ObjectiveCompleted, id, std::string(objective)});
    refreshQuests();
    return true;
}

bool ContentRunner::failQuest(AssetId id)
{
    if (m_content.quest(id) == nullptr || m_state.questState(id) != QuestState::Active) {
        return false;
    }
    m_state.progress(id).state = QuestState::Failed;
    notify({ContentEvent::Kind::QuestFailed, id, {}});
    return true;
}

void ContentRunner::completeQuest(AssetId id, const Quest& quest)
{
    m_state.progress(id).state = QuestState::Completed;
    notify({ContentEvent::Kind::QuestCompleted, id, {}});
    run(quest.rewards);
}

void ContentRunner::refreshQuests()
{
    // Each change can make another condition true (a reward starting the
    // next quest...), so go round until nothing changes, within a limit.
    for (int pass = 0; pass < kMaxQuestPasses; ++pass) {
        bool changed = false;
        for (const AssetId id : m_content.questIds()) {
            const Quest& quest = *m_content.quest(id);
            const QuestState state = m_state.questState(id);
            if (state == QuestState::NotStarted) {
                if (quest.startWhen && prerequisitesMet(quest) && evaluate(*quest.startWhen)) {
                    m_state.progress(id).state = QuestState::Active;
                    notify({ContentEvent::Kind::QuestStarted, id, {}});
                    changed = true;
                }
                continue;
            }
            if (state != QuestState::Active) {
                continue;
            }
            for (const Objective& o : quest.objectives) {
                if (o.completeWhen && !m_state.isObjectiveDone(id, o.id) && evaluate(*o.completeWhen)) {
                    m_state.progress(id).objectivesDone.insert(o.id);
                    notify({ContentEvent::Kind::ObjectiveCompleted, id, o.id});
                    changed = true;
                }
            }
            if (quest.failWhen && evaluate(*quest.failWhen)) {
                m_state.progress(id).state = QuestState::Failed;
                notify({ContentEvent::Kind::QuestFailed, id, {}});
                changed = true;
                continue;
            }
            const bool done = std::all_of(quest.objectives.begin(), quest.objectives.end(), [&](const Objective& o) {
                return o.optional || m_state.isObjectiveDone(id, o.id);
            });
            if (done) {
                completeQuest(id, quest);
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
    }
}

// --- Dialogue ------------------------------------------------------------------------------------

bool ContentRunner::startDialogue(AssetId id)
{
    const Dialogue* dialogue = m_content.dialogue(id);
    if (dialogue == nullptr) {
        m_log.warning(kCategory, "Cannot start dialogue " + id.toString() + ": it does not exist.");
        return false;
    }
    if (m_line) {
        return false; // one conversation at a time
    }
    notify({ContentEvent::Kind::DialogueStarted, id, {}});
    enterNode(id, dialogue->start);
    if (!m_line) {
        notify({ContentEvent::Kind::DialogueEnded, id, {}});
        return false;
    }
    return true;
}

void ContentRunner::enterNode(AssetId id, std::string_view start)
{
    const Dialogue* dialogue = m_content.dialogue(id);
    std::string current(start);
    for (int step = 0; dialogue != nullptr && step < kMaxBranchSteps; ++step) {
        const DialogueNode* node = dialogue->findNode(current);
        if (node == nullptr) {
            break;
        }
        run(node->actions);
        if (node->isBranch()) {
            current = node->next;
            for (const DialogueBranch& b : node->branches) {
                if (evaluate(b.condition)) {
                    current = b.next;
                    break;
                }
            }
            continue;
        }
        if (node->text.empty()) {
            current = node->next; // an empty node: pass through
            continue;
        }
        DialogueLine line;
        line.dialogue = id;
        line.node = node->id;
        if (const Speaker* speaker = dialogue->findSpeaker(node->speaker)) {
            line.speakerName = fillIn(speaker->name);
            line.speakerColor = speaker->color;
            line.portrait = speaker->portrait;
        }
        line.text = fillIn(node->text);
        for (const DialogueChoice& c : node->choices) {
            if (evaluate(c.condition)) {
                line.choices.push_back({c.id, fillIn(c.text)});
            }
        }
        m_line = std::move(line);
        return;
    }
    m_line.reset();
}

void ContentRunner::continueDialogue()
{
    if (!m_line || !m_line->choices.empty()) {
        return;
    }
    const Dialogue* dialogue = m_content.dialogue(m_line->dialogue);
    const DialogueNode* node = dialogue != nullptr ? dialogue->findNode(m_line->node) : nullptr;
    const AssetId id = m_line->dialogue;
    if (node == nullptr || node->next.empty()) {
        endDialogue();
        return;
    }
    enterNode(id, node->next);
    if (!m_line) {
        notify({ContentEvent::Kind::DialogueEnded, id, {}});
    }
}

void ContentRunner::choose(std::size_t index)
{
    if (!m_line || index >= m_line->choices.size()) {
        return;
    }
    const AssetId id = m_line->dialogue;
    const Dialogue* dialogue = m_content.dialogue(id);
    const DialogueNode* node = dialogue != nullptr ? dialogue->findNode(m_line->node) : nullptr;
    const std::string choiceId = m_line->choices[index].id;
    const DialogueChoice* choice = nullptr;
    if (node != nullptr) {
        for (const DialogueChoice& c : node->choices) {
            if (c.id == choiceId) {
                choice = &c;
            }
        }
    }
    if (choice == nullptr) {
        endDialogue();
        return;
    }
    const std::string next = choice->next;
    run(choice->actions);
    if (next.empty()) {
        endDialogue();
        return;
    }
    enterNode(id, next);
    if (!m_line) {
        notify({ContentEvent::Kind::DialogueEnded, id, {}});
    }
}

void ContentRunner::endDialogue()
{
    if (!m_line) {
        return;
    }
    const AssetId id = m_line->dialogue;
    m_line.reset();
    notify({ContentEvent::Kind::DialogueEnded, id, {}});
}

// --- Every frame ---------------------------------------------------------------------------------

void ContentRunner::update(float dt)
{
    if (!m_sequences.empty()) {
        std::vector<Sequence> waiting;
        waiting.swap(m_sequences);   // actions run below may add new sequences
        for (Sequence& s : waiting) {
            s.wait -= dt;
            if (s.wait <= 0.0f) {
                s.wait = 0.0f;
                runSequence(s);
            }
            if (s.next < s.actions.size() || s.wait > 0.0f) {
                m_sequences.push_back(std::move(s));
            }
        }
    }
    refreshQuests();
}

std::string ContentRunner::fillIn(std::string_view text) const
{
    return substituteVariables(text, [&](std::string_view name) -> std::optional<std::string> {
        const auto value = m_state.variable(name);
        return value ? std::optional<std::string>(toDisplayString(*value)) : std::nullopt;
    });
}

void ContentRunner::notify(ContentEvent event)
{
    std::string name;
    std::string text;
    if (const Quest* quest = m_content.quest(event.asset)) {
        name = quest->name.empty() ? m_content.labelOf(event.asset) : quest->name;
        if (const Objective* o = quest->findObjective(event.objective)) {
            text = fillIn(o->text.empty() ? o->id : o->text);
        }
    }
    switch (event.kind) {
    case ContentEvent::Kind::QuestStarted: m_log.info(kCategory, "Quest started: " + name); break;
    case ContentEvent::Kind::ObjectiveCompleted: m_log.info(kCategory, "Objective done (" + name + "): " + text); break;
    case ContentEvent::Kind::QuestCompleted: m_log.info(kCategory, "Quest completed: " + name); break;
    case ContentEvent::Kind::QuestFailed: m_log.info(kCategory, "Quest failed: " + name); break;
    case ContentEvent::Kind::DialogueStarted:
    case ContentEvent::Kind::DialogueEnded: break;
    }
    if (m_host != nullptr) {
        m_host->notify(event);
    }
}

} // namespace rolo
