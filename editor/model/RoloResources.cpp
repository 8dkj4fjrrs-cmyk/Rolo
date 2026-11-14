#include "RoloResources.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/scene/Project.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace rolo::editor {

namespace fs = std::filesystem;

namespace {

void setError(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool isDirectory(const fs::path& p)
{
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

RoloResources resourcesIn(const fs::path& root, const fs::path& player, bool installed)
{
    RoloResources r;
    r.root = root;
    r.templates = root / "templates";
    r.examples = root / "examples";
    r.licenses = root / "licenses";
    r.docs = root / "docs";
    r.player = player;
    r.webPlayer = installed ? root / "web-runtime" : root / "build" / "web" / "bin" / "rolo_player";
    r.installed = installed;
    return r;
}

} // namespace

RoloResources findRoloResources(const fs::path& executable, HostOs os, const fs::path& sourceDir,
                                      const fs::path& devPlayer)
{
    const fs::path exeDir = executable.parent_path();
    const std::string playerName = os == HostOs::Windows ? "rolo_player.exe" : "rolo_player";
    // An installed copy: everything beside the program.
    if (isDirectory(exeDir / "templates")) {
        const fs::path player = os == HostOs::MacOS ? exeDir / "runtime" / "rolo_player.app"
                                                    : exeDir / "runtime" / playerName;
        return resourcesIn(exeDir, player, true);
    }
    // An installed macOS app: inside its Resources folder.
    const fs::path bundled = exeDir.parent_path() / "Resources" / "rolo";
    if (isDirectory(bundled / "templates")) {
        return resourcesIn(bundled, bundled / "runtime" / "rolo_player.app", true);
    }
    // A build folder: the source tree, and the player built with the editor.
    return resourcesIn(sourceDir, devPlayer, false);
}

std::vector<ProjectTemplate> listProjectTemplates(const RoloResources& resources)
{
    std::vector<ProjectTemplate> found;
    const auto scan = [&](const fs::path& folder, bool example) {
        std::error_code ec;
        if (!isDirectory(folder)) {
            return;
        }
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
            const fs::path info = entry.path() / "template.json";
            const fs::path project = entry.path() / "project";
            if (!fs::exists(info, ec) || !isProjectFolder(project)) {
                continue;
            }
            ProjectTemplate t;
            t.id = entry.path().filename().string();
            t.name = t.id;
            t.project = project;
            t.example = example;
            if (const auto text = readTextFile(info)) {
                const auto j = nlohmann::json::parse(*text, nullptr, false);
                if (j.is_object()) {
                    t.name = j.value("name", t.name);
                    t.description = j.value("description", std::string());
                    t.order = j.value("order", 100);
                }
            }
            found.push_back(std::move(t));
        }
    };
    scan(resources.templates, false);
    scan(resources.examples, true);
    std::sort(found.begin(), found.end(), [](const ProjectTemplate& a, const ProjectTemplate& b) {
        if (a.example != b.example) {
            return !a.example;
        }
        return a.order != b.order ? a.order < b.order : a.name < b.name;
    });
    return found;
}

bool createProjectFromTemplate(const ProjectTemplate& from, const fs::path& folder, const std::string& name,
                               std::string* error)
{
    if (!Project::isValidName(name)) {
        setError(error, "A project name cannot be empty or contain / \\ : * ? \" < > |.");
        return false;
    }
    std::error_code ec;
    if (fs::exists(folder, ec) && !fs::is_empty(folder, ec)) {
        setError(error, "The folder " + folder.string() + " already exists and is not empty. Choose another name or "
                            "location.");
        return false;
    }
    if (!isProjectFolder(from.project)) {
        setError(error, "The template \"" + from.name + "\" is incomplete: " + from.project.string()
                            + " has no project.rolo. Reinstall Rolo.");
        return false;
    }
    fs::create_directories(folder, ec);
    fs::copy(from.project, folder, fs::copy_options::recursive, ec);
    if (ec) {
        setError(error, "Could not copy the template into " + folder.string() + ": " + ec.message());
        return false;
    }
    std::string why;
    auto project = Project::open(folder, &why);
    if (!project) {
        setError(error, "The copied template could not be opened: " + why);
        return false;
    }
    ProjectSettings settings = project->settings();
    settings.name = name;
    project->setSettings(std::move(settings));
    if (const Status s = project->save(); !s) {
        setError(error, s.message);
        return false;
    }
    return true;
}

} // namespace rolo::editor
