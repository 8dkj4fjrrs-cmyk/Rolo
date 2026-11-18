#pragma once

// SceneDocument: the scene being edited, where it is saved, and its undo
// history. All edits go through execute(), so every change can be undone and
// the document knows whether it has unsaved changes.

#include "Commands.hpp"

#include "rolo/scene/SceneFile.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rolo::editor {

class SceneDocument {
public:
    /// A document for `scene`, saved at `path` (empty = not saved yet).
    explicit SceneDocument(Scene scene, std::filesystem::path path = {});

    /// Opens a scene file. Problems found while reading go into `issues`.
    static std::optional<SceneDocument> open(const std::filesystem::path& path, const ComponentRegistry& registry,
                                             std::vector<FileIssue>& issues, const PrefabTexts& prefabs = {});

    const Scene& scene() const noexcept { return *m_scene; }
    /// Shared with the runtime, which draws it. Change it only through execute().
    std::shared_ptr<const Scene> sharedScene() const noexcept { return m_scene; }

    const std::filesystem::path& path() const noexcept { return m_path; }
    bool isUntitled() const noexcept { return m_path.empty(); }
    /// "main.lscene", or "Untitled" before the first save.
    std::string displayName() const;
    /// True if there are changes that are not saved.
    bool isDirty() const noexcept { return !m_history.isClean(); }

    bool execute(std::unique_ptr<Command> command);
    bool undo();
    bool redo();
    CommandStack& history() noexcept { return m_history; }
    const CommandStack& history() const noexcept { return m_history; }

    /// Saves to path() (which must be set).
    /// `prefabs`: the project's prefabs, so that linked copies are written
    /// as links (EditorApp gives it; without it they are written in full).
    Status save(const PrefabTexts& prefabs = {});
    /// Saves to a new path and keeps editing there.
    Status saveAs(const std::filesystem::path& path, const PrefabTexts& prefabs = {});

private:
    std::shared_ptr<Scene> m_scene;
    std::filesystem::path m_path;
    CommandStack m_history;
};

} // namespace rolo::editor
