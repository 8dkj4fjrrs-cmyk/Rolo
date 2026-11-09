#pragma once

// Plays a real Rolo project in tests, the way the player program does:
// its assets read-only, its content checked, its behaviours from the linked
// game modules. Warnings and errors are collected in `errors`.

#include "support/TempFolder.hpp"

#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/content/ContentLibrary.hpp"
#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Components.hpp"
#include "rolo/gameplay/Game.hpp"
#include "rolo/gameplay/GameModule.hpp"
#include "rolo/gameplay/SaveStore.hpp"
#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rolo::test {

struct PlayProject {
    std::filesystem::path folder;
    Logger log;
    mutable std::vector<std::string> errors;   // also filled while loading scenes
    std::vector<std::string> infos;
    ComponentRegistry registry = ComponentRegistry::withBuiltins();
    std::unique_ptr<AssetDatabase> assets;
    ContentLibrary content;
    FolderSaveStore saves{uniqueTempFolder("play-saves")};
    std::unique_ptr<Game> game;

    explicit PlayProject(std::filesystem::path projectFolder)
        : folder(std::move(projectFolder))
    {
        log.addSink([this](const LogMessage& m) {
            (m.level >= LogLevel::Warning ? errors : infos).push_back(std::string(m.text));
        });
        registerGameplayComponents(registry);
        registerGameModules(registry, log);
        assets = std::make_unique<AssetDatabase>(folder, AssetDatabase::Access::ReadOnly);
        for (const FileIssue& issue : assets->refresh()) {
            errors.push_back(issue.toString());
        }
        std::vector<FileIssue> issues;
        content = ContentLibrary::load(*assets, &issues);
        for (FileIssue& issue : content.check()) {
            issues.push_back(std::move(issue));
        }
        for (const FileIssue& issue : issues) {
            errors.push_back(issue.toString());
        }
        GameSetup setup;
        setup.assets = assets.get();
        setup.content = &content;
        setup.saves = &saves;
        setup.loadScene = [this](AssetId id, std::string& error) { return load(id, error); };
        game = std::make_unique<Game>(log, std::move(setup));
    }
    ~PlayProject()
    {
        std::error_code ec;
        std::filesystem::remove_all(saves.folder(), ec);
    }

    std::optional<Scene> load(AssetId id, std::string& error) const
    {
        const AssetRecord* record = assets->find(id);
        if (record == nullptr || record->type != AssetType::Scene) {
            error = "not a scene in this project";
            return std::nullopt;
        }
        SceneLoadResult loaded = loadSceneFile(assets->absolutePath(*record), registry);
        if (!loaded.ok()) {
            error = "could not be read";
            return std::nullopt;
        }
        for (const FileIssue& issue : loaded.issues) {
            errors.push_back(issue.toString());
        }
        return std::move(*loaded.scene);
    }

    /// Starts the project's start scene. False if it cannot be found or read.
    bool start()
    {
        const auto project = Project::open(folder);
        const AssetRecord* first = project ? assets->findByPath(project->settings().startScene) : nullptr;
        std::string error;
        auto scene = first != nullptr ? load(first->id, error) : std::nullopt;
        if (!scene) {
            return false;
        }
        game->start(*scene, first->id);
        frames(1);
        return true;
    }
    void frames(int n)
    {
        for (int i = 0; i < n; ++i) {
            game->update(1.0f / 60.0f);
        }
    }
    void tap(Key key)
    {
        game->input().press(key);
        frames(1);
        game->input().release(key);
        frames(1);
    }
    EntityId player() const { return game->find("Player"); }
    /// What the Text of the object called `name` shows now ("" if none).
    std::string text(std::string_view name) const
    {
        const Entity* e = game->scene().find(game->find(name));
        const Text* t = e != nullptr ? e->get<Text>() : nullptr;
        return t != nullptr ? t->text : std::string();
    }
    /// Puts the player's feet (its collider) on a point, as if it walked there.
    void stand(Vec2 at)
    {
        const Vec2 offset = at - game->colliderBox(player())->center();
        game->transform(player())->position = game->scene().find(player())->transform().position + offset;
        frames(1);
    }
};

} // namespace rolo::test
