#pragma once

// ContentLibrary: all the RPG content of a project, loaded and checked.
//
// It reads every variables, dialogue and quest asset from the project's
// AssetDatabase, then checks that the pieces fit together: every variable a
// condition uses exists and holds that kind of value, every dialogue line
// leads somewhere that exists, quests do not require each other in a circle,
// referenced dialogues, quests and scenes exist, and so on. Problems come
// back as FileIssues naming the file and the place in it, so the editor's
// Console (and the player's log) can say exactly what to fix.

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/Content.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rolo {

class ContentLibrary {
public:
    ContentLibrary() = default;

    /// Reads the project's content. Problems in the files themselves go to
    /// `issues` (a file that cannot be read is left out).
    static ContentLibrary load(const AssetDatabase& assets, std::vector<FileIssue>* issues = nullptr);

    // --- Building by hand (tests, tools) ---------------------------------------------------------
    /// `label` names the content in messages, e.g. its file path.
    void addVariables(AssetId id, std::string label, VariableSet variables);
    void addDialogue(AssetId id, std::string label, Dialogue dialogue);
    void addQuest(AssetId id, std::string label, Quest quest);
    /// A scene that exists (for checking "change scene" actions).
    void addScene(AssetId id, std::string label);
    /// A sound that exists (for checking "play sound" and "play music" actions).
    void addSound(AssetId id, std::string label);

    // --- Looking things up -----------------------------------------------------------------
    const VariableDefinition* variable(std::string_view id) const;
    /// Every variable, in the order of their files and definitions.
    std::vector<const VariableDefinition*> variables() const;
    const Dialogue* dialogue(AssetId id) const;
    const Quest* quest(AssetId id) const;
    bool hasScene(AssetId id) const;
    bool hasSound(AssetId id) const;
    std::vector<AssetId> questIds() const;
    std::vector<AssetId> dialogueIds() const;
    /// The label given for an asset (its path), or its ID as text.
    std::string labelOf(AssetId id) const;

    // --- Checking ----------------------------------------------------------------------------
    /// Checks how the content fits together (see above). Empty: all good.
    std::vector<FileIssue> check() const;
    /// Checks a condition or action list used somewhere else (e.g. by a
    /// trigger in a scene). `where` starts each message.
    void checkCondition(const Condition& condition, const std::string& where, std::vector<FileIssue>& issues) const;
    void checkActions(const std::vector<Action>& actions, const std::string& where, std::vector<FileIssue>& issues) const;

private:
    struct Labelled {
        AssetId id;
        std::string label;
    };
    void checkDialogue(AssetId id, const Dialogue& dialogue, std::vector<FileIssue>& issues) const;
    void checkQuest(AssetId id, const Quest& quest, std::vector<FileIssue>& issues) const;

    std::vector<std::pair<Labelled, VariableSet>> m_variableSets;
    std::map<AssetId, std::pair<std::string, Dialogue>> m_dialogues;
    std::map<AssetId, std::pair<std::string, Quest>> m_quests;
    std::map<AssetId, std::string> m_scenes;
    std::map<AssetId, std::string> m_sounds;
};

} // namespace rolo
