#include "SceneDocument.hpp"

namespace rolo::editor {

SceneDocument::SceneDocument(Scene scene, std::filesystem::path path)
    : m_scene(std::make_shared<Scene>(std::move(scene)))
    , m_path(std::move(path))
{
}

std::optional<SceneDocument> SceneDocument::open(const std::filesystem::path& path, const ComponentRegistry& registry,
                                                 std::vector<FileIssue>& issues, const PrefabTexts& prefabs)
{
    SceneLoadResult loaded = loadSceneFile(path, registry, prefabs);
    issues = std::move(loaded.issues);
    if (!loaded.ok()) {
        return std::nullopt;
    }
    return SceneDocument(std::move(*loaded.scene), path);
}

std::string SceneDocument::displayName() const
{
    return m_path.empty() ? "Untitled" : m_path.filename().string();
}

bool SceneDocument::execute(std::unique_ptr<Command> command)
{
    return m_history.execute(*m_scene, std::move(command));
}

bool SceneDocument::undo()
{
    return m_history.undo(*m_scene);
}

bool SceneDocument::redo()
{
    return m_history.redo(*m_scene);
}

Status SceneDocument::save(const PrefabTexts& prefabs)
{
    if (m_path.empty()) {
        return Status::failure("This scene has not been saved yet; choose where to save it.");
    }
    if (Status status = saveSceneFile(m_path, *m_scene, prefabs); !status) {
        return status;
    }
    m_history.markClean();
    return Status::success();
}

Status SceneDocument::saveAs(const std::filesystem::path& path, const PrefabTexts& prefabs)
{
    if (Status status = saveSceneFile(path, *m_scene, prefabs); !status) {
        return status;
    }
    m_path = path;
    m_history.markClean();
    return Status::success();
}

} // namespace rolo::editor
