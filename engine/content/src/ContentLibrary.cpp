#include "rolo/content/ContentLibrary.hpp"

#include "rolo/core/Files.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace rolo {

namespace {

void error(std::vector<FileIssue>& issues, std::string where, std::string message)
{
    issues.push_back({FileIssue::Severity::Error, std::move(where), std::move(message)});
}

void warning(std::vector<FileIssue>& issues, std::string where, std::string message)
{
    issues.push_back({FileIssue::Severity::Warning, std::move(where), std::move(message)});
}

std::string inQuotes(std::string_view id)
{
    return "'" + std::string(id) + "'";
}

// Prefixes each issue's place with the file it came from.
void addFileIssues(std::vector<FileIssue>* out, const std::string& file, std::vector<FileIssue> issues)
{
    if (out == nullptr) {
        return;
    }
    for (FileIssue& issue : issues) {
        issue.where = issue.where == "file" ? file : file + ", " + issue.where;
        out->push_back(std::move(issue));
    }
}

} // namespace

ContentLibrary ContentLibrary::load(const AssetDatabase& assets, std::vector<FileIssue>* issues)
{
    ContentLibrary library;
    for (const AssetRecord* record : assets.all()) {
        if (record->type == AssetType::Scene) {
            library.addScene(record->id, record->path);
            continue;
        }
        if (record->type == AssetType::Audio) {
            library.addSound(record->id, record->path);
            continue;
        }
        if (record->type != AssetType::Variables && record->type != AssetType::Dialogue
            && record->type != AssetType::Quest) {
            continue;
        }
        const auto text = readTextFile(assets.absolutePath(*record));
        if (!text) {
            if (issues != nullptr) {
                issues->push_back({FileIssue::Severity::Error, record->path, "The file could not be read."});
            }
            continue;
        }
        switch (record->type) {
        case AssetType::Variables: {
            auto read = parseVariables(*text);
            addFileIssues(issues, record->path, std::move(read.issues));
            if (read.content) {
                library.addVariables(record->id, record->path, std::move(*read.content));
            }
            break;
        }
        case AssetType::Dialogue: {
            auto read = parseDialogue(*text);
            addFileIssues(issues, record->path, std::move(read.issues));
            if (read.content) {
                library.addDialogue(record->id, record->path, std::move(*read.content));
            }
            break;
        }
        case AssetType::Quest: {
            auto read = parseQuest(*text);
            addFileIssues(issues, record->path, std::move(read.issues));
            if (read.content) {
                library.addQuest(record->id, record->path, std::move(*read.content));
            }
            break;
        }
        default: break;
        }
    }
    return library;
}

void ContentLibrary::addVariables(AssetId id, std::string label, VariableSet variables)
{
    m_variableSets.push_back({{id, std::move(label)}, std::move(variables)});
}

void ContentLibrary::addDialogue(AssetId id, std::string label, Dialogue dialogue)
{
    m_dialogues[id] = {std::move(label), std::move(dialogue)};
}

void ContentLibrary::addQuest(AssetId id, std::string label, Quest quest)
{
    m_quests[id] = {std::move(label), std::move(quest)};
}

void ContentLibrary::addScene(AssetId id, std::string label)
{
    m_scenes[id] = std::move(label);
}

void ContentLibrary::addSound(AssetId id, std::string label)
{
    m_sounds[id] = std::move(label);
}

const VariableDefinition* ContentLibrary::variable(std::string_view id) const
{
    for (const auto& [file, set] : m_variableSets) {
        for (const VariableDefinition& v : set.variables) {
            if (v.id == id) {
                return &v;
            }
        }
    }
    return nullptr;
}

std::vector<const VariableDefinition*> ContentLibrary::variables() const
{
    std::vector<const VariableDefinition*> out;
    for (const auto& [file, set] : m_variableSets) {
        for (const VariableDefinition& v : set.variables) {
            out.push_back(&v);
        }
    }
    return out;
}

const Dialogue* ContentLibrary::dialogue(AssetId id) const
{
    const auto it = m_dialogues.find(id);
    return it != m_dialogues.end() ? &it->second.second : nullptr;
}

const Quest* ContentLibrary::quest(AssetId id) const
{
    const auto it = m_quests.find(id);
    return it != m_quests.end() ? &it->second.second : nullptr;
}

bool ContentLibrary::hasScene(AssetId id) const
{
    return m_scenes.count(id) > 0;
}

bool ContentLibrary::hasSound(AssetId id) const
{
    return m_sounds.count(id) > 0;
}

std::vector<AssetId> ContentLibrary::questIds() const
{
    std::vector<AssetId> out;
    for (const auto& [id, entry] : m_quests) {
        out.push_back(id);
    }
    return out;
}

std::vector<AssetId> ContentLibrary::dialogueIds() const
{
    std::vector<AssetId> out;
    for (const auto& [id, entry] : m_dialogues) {
        out.push_back(id);
    }
    return out;
}

std::string ContentLibrary::labelOf(AssetId id) const
{
    if (const auto d = m_dialogues.find(id); d != m_dialogues.end()) {
        return d->second.first;
    }
    if (const auto q = m_quests.find(id); q != m_quests.end()) {
        return q->second.first;
    }
    if (const auto s = m_scenes.find(id); s != m_scenes.end()) {
        return s->second;
    }
    if (const auto s = m_sounds.find(id); s != m_sounds.end()) {
        return s->second;
    }
    for (const auto& [file, set] : m_variableSets) {
        if (file.id == id) {
            return file.label;
        }
    }
    return id.toString();
}

// --- Checking ------------------------------------------------------------------------------------

void ContentLibrary::checkCondition(const Condition& c, const std::string& where, std::vector<FileIssue>& issues) const
{
    switch (c.kind) {
    case Condition::Kind::Always: break;
    case Condition::Kind::Compare: {
        const VariableDefinition* v = variable(c.variable);
        if (v == nullptr) {
            error(issues, where, "There is no variable " + inQuotes(c.variable) + ".");
            break;
        }
        if (typeOf(c.value) != v->type) {
            error(issues, where, "Variable " + inQuotes(c.variable) + " holds " + std::string(valueTypeName(v->type))
                                     + " values; it cannot be compared with " + toDisplayString(c.value) + ".");
        } else if (v->type != ValueType::Int && c.op != CompareOp::Equal && c.op != CompareOp::NotEqual) {
            error(issues, where, "Only whole numbers can be compared with " + std::string(compareOpName(c.op))
                                     + "; use == or != for " + inQuotes(c.variable) + ".");
        }
        break;
    }
    case Condition::Kind::QuestIs:
    case Condition::Kind::ObjectiveDone: {
        const Quest* q = quest(c.quest);
        if (q == nullptr) {
            error(issues, where, "The quest " + c.quest.toString() + " does not exist (was it deleted?).");
        } else if (c.kind == Condition::Kind::ObjectiveDone && q->findObjective(c.objective) == nullptr) {
            error(issues, where, "Quest " + inQuotes(labelOf(c.quest)) + " has no objective " + inQuotes(c.objective) + ".");
        }
        break;
    }
    case Condition::Kind::All:
    case Condition::Kind::Any:
    case Condition::Kind::Not:
        for (const Condition& child : c.children) {
            checkCondition(child, where, issues);
        }
        break;
    }
}

void ContentLibrary::checkActions(const std::vector<Action>& actions, const std::string& where,
                                  std::vector<FileIssue>& issues) const
{
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const Action& a = actions[i];
        const std::string at = where + ", action " + std::to_string(i + 1) + " (" + std::string(actionTypeName(a.kind)) + ")";
        switch (a.kind) {
        case Action::Kind::Set:
        case Action::Kind::Add: {
            const VariableDefinition* v = variable(a.variable);
            if (v == nullptr) {
                error(issues, at, "There is no variable " + inQuotes(a.variable) + ".");
            } else if (a.kind == Action::Kind::Add && v->type != ValueType::Int) {
                error(issues, at, "Only whole-number variables can be added to; " + inQuotes(a.variable) + " holds "
                                      + std::string(valueTypeName(v->type)) + " values.");
            } else if (a.kind == Action::Kind::Set && typeOf(a.value) != v->type) {
                error(issues, at, "Variable " + inQuotes(a.variable) + " holds " + std::string(valueTypeName(v->type))
                                      + " values, not " + toDisplayString(a.value) + ".");
            }
            break;
        }
        case Action::Kind::StartQuest:
        case Action::Kind::FailQuest:
        case Action::Kind::CompleteObjective: {
            const Quest* q = quest(a.quest);
            if (q == nullptr) {
                error(issues, at, "The quest " + a.quest.toString() + " does not exist (was it deleted?).");
            } else if (a.kind == Action::Kind::CompleteObjective && q->findObjective(a.objective) == nullptr) {
                error(issues, at, "Quest " + inQuotes(labelOf(a.quest)) + " has no objective " + inQuotes(a.objective) + ".");
            }
            break;
        }
        case Action::Kind::StartDialogue:
            if (dialogue(a.asset) == nullptr) {
                error(issues, at, "The dialogue " + a.asset.toString() + " does not exist (was it deleted?).");
            }
            break;
        case Action::Kind::ChangeScene:
            if (!hasScene(a.asset)) {
                error(issues, at, "The scene " + a.asset.toString() + " does not exist (was it deleted?).");
            }
            break;
        case Action::Kind::PlaySound:
        case Action::Kind::PlayMusic:
            if (!hasSound(a.asset)) {
                error(issues, at, "The sound " + a.asset.toString() + " does not exist (was it deleted?).");
            }
            break;
        case Action::Kind::Emit:
        case Action::Kind::SaveGame:
        case Action::Kind::LoadGame:
            if (a.text.empty()) {
                error(issues, at, a.kind == Action::Kind::Emit ? "The event needs a name." : "The save slot needs a name.");
            }
            break;
        case Action::Kind::SetActive:
        case Action::Kind::Destroy:
        case Action::Kind::Log:
        case Action::Kind::Wait:
        case Action::Kind::StopMusic: break;
        }
    }
}

void ContentLibrary::checkDialogue(AssetId id, const Dialogue& d, std::vector<FileIssue>& issues) const
{
    const std::string file = labelOf(id);
    if (d.nodes.empty()) {
        warning(issues, file, "The dialogue has no lines.");
        return;
    }
    const auto checkNext = [&](const std::string& next, const std::string& where) {
        if (!next.empty() && d.findNode(next) == nullptr) {
            error(issues, where, "It leads to " + inQuotes(next) + ", but there is no such node.");
        }
    };
    if (d.start.empty()) {
        error(issues, file, "The dialogue has no \"start\" node.");
    } else {
        checkNext(d.start, file + ", start");
    }
    for (const DialogueNode& n : d.nodes) {
        const std::string at = file + ", node " + inQuotes(n.id);
        if (!n.speaker.empty() && d.findSpeaker(n.speaker) == nullptr) {
            error(issues, at, "There is no speaker " + inQuotes(n.speaker) + " in this dialogue.");
        }
        if (n.text.empty() && n.branches.empty()) {
            error(issues, at, "A node needs text (a line) or branches.");
        }
        if (!n.text.empty() && !n.branches.empty()) {
            warning(issues, at, "A line with text ignores its branches; use a separate branch node.");
        }
        if (n.isBranch() && !n.choices.empty()) {
            error(issues, at, "A branch node (no text) cannot have choices.");
        }
        if (!n.choices.empty() && !n.next.empty()) {
            warning(issues, at, "A line with choices ignores \"next\"; each choice says where it leads.");
        }
        checkActions(n.actions, at, issues);
        checkNext(n.next, at);
        for (const DialogueChoice& c : n.choices) {
            const std::string cat = at + ", choice " + inQuotes(c.id);
            if (c.text.empty()) {
                warning(issues, cat, "The choice has no text; players will see an empty option.");
            }
            checkCondition(c.condition, cat, issues);
            checkActions(c.actions, cat, issues);
            checkNext(c.next, cat);
        }
        for (std::size_t i = 0; i < n.branches.size(); ++i) {
            const std::string bat = at + ", branch " + std::to_string(i + 1);
            checkCondition(n.branches[i].condition, bat, issues);
            if (n.branches[i].next.empty()) {
                error(issues, bat, "The branch does not say which node to go to.");
            }
            checkNext(n.branches[i].next, bat);
        }
    }

    // Nodes that can never be reached from the start.
    std::set<std::string> reached;
    std::vector<std::string> todo{d.start};
    while (!todo.empty()) {
        const std::string current = todo.back();
        todo.pop_back();
        const DialogueNode* n = d.findNode(current);
        if (n == nullptr || !reached.insert(current).second) {
            continue;
        }
        todo.push_back(n->next);
        for (const DialogueChoice& c : n->choices) {
            todo.push_back(c.next);
        }
        for (const DialogueBranch& b : n->branches) {
            todo.push_back(b.next);
        }
    }
    for (const DialogueNode& n : d.nodes) {
        if (reached.count(n.id) == 0) {
            warning(issues, file + ", node " + inQuotes(n.id), "No other node leads here, so it is never shown.");
        }
    }

    // Branch nodes that only lead to each other would loop forever.
    for (const DialogueNode& n : d.nodes) {
        if (!n.isBranch()) {
            continue;
        }
        std::set<std::string> seen;
        std::vector<std::string> path{n.id};
        bool loops = false;
        while (!path.empty() && !loops) {
            const std::string current = path.back();
            path.pop_back();
            const DialogueNode* node = d.findNode(current);
            if (node == nullptr || !node->isBranch() || !seen.insert(current).second) {
                continue;
            }
            for (const std::string& next : [&] {
                     std::vector<std::string> nexts{node->next};
                     for (const DialogueBranch& b : node->branches) {
                         nexts.push_back(b.next);
                     }
                     return nexts;
                 }()) {
                if (next == n.id) {
                    loops = true;
                }
                path.push_back(next);
            }
        }
        if (loops) {
            error(issues, file + ", node " + inQuotes(n.id),
                  "Branch nodes lead back to this one without a line in between, which would loop forever.");
        }
    }
}

void ContentLibrary::checkQuest(AssetId id, const Quest& q, std::vector<FileIssue>& issues) const
{
    const std::string file = labelOf(id);
    if (q.name.empty()) {
        warning(issues, file, "The quest has no name to show players.");
    }
    if (q.objectives.empty()) {
        warning(issues, file, "The quest has no objectives, so it completes as soon as it starts.");
    } else if (std::all_of(q.objectives.begin(), q.objectives.end(), [](const Objective& o) { return o.optional; })) {
        warning(issues, file, "Every objective is optional, so the quest completes as soon as it starts.");
    }
    for (AssetId p : q.prerequisites) {
        if (p == id) {
            error(issues, file + ", prerequisites", "A quest cannot require itself.");
        } else if (quest(p) == nullptr) {
            error(issues, file + ", prerequisites", "The quest " + p.toString() + " does not exist (was it deleted?).");
        }
    }
    if (q.startWhen) {
        checkCondition(*q.startWhen, file + ", startWhen", issues);
    }
    if (q.failWhen) {
        checkCondition(*q.failWhen, file + ", failWhen", issues);
    }
    for (const Objective& o : q.objectives) {
        if (o.completeWhen) {
            checkCondition(*o.completeWhen, file + ", objective " + inQuotes(o.id), issues);
        }
    }
    checkActions(q.rewards, file + ", rewards", issues);
}

std::vector<FileIssue> ContentLibrary::check() const
{
    std::vector<FileIssue> issues;

    // Variable ids are shared by the whole game.
    std::map<std::string, std::string> firstFile;
    for (const auto& [file, set] : m_variableSets) {
        for (const VariableDefinition& v : set.variables) {
            const auto [it, added] = firstFile.emplace(v.id, file.label);
            if (!added) {
                error(issues, file.label + ", variable " + inQuotes(v.id),
                      "The variable is also defined in " + it->second + "; each variable needs its own id.");
            }
        }
    }
    for (const auto& [id, entry] : m_dialogues) {
        checkDialogue(id, entry.second, issues);
    }
    for (const auto& [id, entry] : m_quests) {
        checkQuest(id, entry.second, issues);
    }

    // Quests that require each other in a circle could never start.
    for (const auto& [id, entry] : m_quests) {
        std::set<AssetId> seen;
        std::vector<AssetId> todo(entry.second.prerequisites.begin(), entry.second.prerequisites.end());
        bool circle = false;
        while (!todo.empty() && !circle) {
            const AssetId current = todo.back();
            todo.pop_back();
            if (current == id) {
                // A quest listing itself is reported by checkQuest already.
                const auto& direct = entry.second.prerequisites;
                circle = std::find(direct.begin(), direct.end(), id) == direct.end();
                break;
            }
            if (!seen.insert(current).second) {
                continue;
            }
            if (const Quest* q = quest(current)) {
                todo.insert(todo.end(), q->prerequisites.begin(), q->prerequisites.end());
            }
        }
        if (circle) {
            error(issues, entry.first + ", prerequisites",
                  "The quest's prerequisites lead back to itself, so it can never start.");
        }
    }
    return issues;
}

} // namespace rolo
