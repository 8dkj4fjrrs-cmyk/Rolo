#include "rolo/scene/Project.hpp"
#include "rolo/core/Names.hpp"

#include "rolo/core/Files.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace rolo {

namespace {
using Json = nlohmann::ordered_json;

void setError(std::string* error, std::string message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}
} // namespace

bool Project::isValidName(std::string_view name)
{
    if (name.empty() || name.size() > 100) {
        return false;
    }
    if (name.find_first_of("/\\:*?\"<>|") != std::string_view::npos) {
        return false;
    }
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32) {
            return false;
        }
    }
    // Leading/trailing spaces and dots confuse Windows and Finder alike.
    return name.front() != ' ' && name.back() != ' ' && name.front() != '.' && name.back() != '.';
}

bool Project::isValidRelativePath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos
        || path.find(':') != std::string_view::npos) {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

std::optional<ProjectSettings> Project::parseSettings(std::string_view text, std::string* error)
{
    Json j;
    try {
        j = Json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::parse_error&) {
        setError(error, "The project file is not valid JSON.");
        return std::nullopt;
    }
    if (!j.is_object() || !isFormatName(j.value("format", ""), ProjectFormat::kName)) {
        setError(error, "This is not a Rolo project file.");
        return std::nullopt;
    }
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() < 1) {
        setError(error, "The project file has no valid format \"version\".");
        return std::nullopt;
    }
    if (j["version"].get<int>() > ProjectFormat::kVersion) {
        setError(error, "This project was saved by a newer version of Rolo. Update Rolo to open it.");
        return std::nullopt;
    }
    ProjectSettings settings;
    if (j.contains("name") && j["name"].is_string()) {
        settings.name = j["name"].get<std::string>();
    }
    if (j.contains("startScene") && j["startScene"].is_string()) {
        settings.startScene = j["startScene"].get<std::string>();
        if (!isValidRelativePath(settings.startScene)) {
            setError(error, "The project's start scene must be a path inside the project, like "
                            "\"scenes/main.lscene\".");
            return std::nullopt;
        }
    }
    for (const auto& [key, flag] : {std::pair{"escapeQuits", &settings.escapeQuits},
                                     std::pair{"quickSaveKeys", &settings.quickSaveKeys}}) {
        if (j.contains(key)) {
            if (!j[key].is_boolean()) {
                setError(error, std::string("The project's \"") + key + "\" must be true or false.");
                return std::nullopt;
            }
            *flag = j[key].get<bool>();
        }
    }
    return settings;
}

std::string Project::serializeSettings(const ProjectSettings& settings)
{
    Json j;
    j["format"] = ProjectFormat::kName;
    j["version"] = ProjectFormat::kVersion;
    j["name"] = settings.name;
    j["startScene"] = settings.startScene;
    // Written only when changed, so most project files stay as they were.
    if (!settings.escapeQuits) {
        j["escapeQuits"] = false;
    }
    if (!settings.quickSaveKeys) {
        j["quickSaveKeys"] = false;
    }
    return j.dump(2) + "\n";
}

std::filesystem::path projectFileIn(const std::filesystem::path& folder)
{
    std::error_code ec;
    for (const std::string_view name : {ProjectFormat::kFileName, ProjectFormat::kLegacyFileName}) {
        const std::filesystem::path file = folder / name;
        if (std::filesystem::is_regular_file(file, ec)) {
            return file;
        }
    }
    return {};
}

bool isProjectFolder(const std::filesystem::path& folder)
{
    return !projectFileIn(folder).empty();
}

std::optional<Project> Project::open(const std::filesystem::path& folder, std::string* error)
{
    const std::filesystem::path file = projectFileIn(folder);
    const auto text = file.empty() ? std::nullopt : readTextFile(file);
    if (!text) {
        setError(error, "No Rolo project found in " + folder.string() + " (there is no project.rolo file).");
        return std::nullopt;
    }
    auto settings = parseSettings(*text, error);
    if (!settings) {
        return std::nullopt;
    }
    Project project;
    std::error_code ec;
    project.m_folder = std::filesystem::weakly_canonical(std::filesystem::absolute(folder), ec);
    if (ec) {
        project.m_folder = std::filesystem::absolute(folder);
    }
    project.m_settings = std::move(*settings);
    return project;
}

std::optional<Project> Project::create(const std::filesystem::path& folder, std::string name, std::string* error)
{
    if (!isValidName(name)) {
        setError(error, "A project name cannot be empty or contain / \\ : * ? \" < > |.");
        return std::nullopt;
    }
    std::error_code ec;
    if (std::filesystem::exists(folder, ec)) {
        if (!std::filesystem::is_directory(folder, ec) || !std::filesystem::is_empty(folder, ec)) {
            setError(error, "The folder " + folder.string() + " already exists and is not empty. Choose another "
                            "name or location.");
            return std::nullopt;
        }
    }
    Project project;
    project.m_folder = std::filesystem::absolute(folder);
    project.m_settings.name = std::move(name);
    project.m_settings.startScene = std::string(ProjectFormat::kScenesFolder) + "/main.lscene";

    Scene start = makeStartScene();
    start.setName("Main");
    if (const Status s = saveSceneFile(project.absolute(project.m_settings.startScene), start); !s) {
        setError(error, "Could not create the project: " + s.message);
        return std::nullopt;
    }
    if (const Status s = project.save(); !s) {
        setError(error, "Could not create the project: " + s.message);
        return std::nullopt;
    }
    project.m_folder = std::filesystem::weakly_canonical(project.m_folder, ec);
    return project;
}

Status Project::save() const
{
    const Status written = writeTextFileAtomically(m_folder / ProjectFormat::kFileName, serializeSettings(m_settings));
    if (written) {
        // A project made with Lantern now has its project.rolo: the old file
        // would only confuse (two project files that could disagree).
        std::error_code ec;
        std::filesystem::remove(m_folder / ProjectFormat::kLegacyFileName, ec);
    }
    return written;
}

std::filesystem::path Project::absolute(std::string_view relative) const
{
    return (m_folder / std::filesystem::path(std::string(relative))).lexically_normal();
}

std::optional<std::string> Project::relative(const std::filesystem::path& path) const
{
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::weakly_canonical(std::filesystem::absolute(path), ec);
    const std::filesystem::path rel = (ec ? path : full).lexically_relative(m_folder);
    const std::string text = rel.generic_string();
    if (rel.empty() || !isValidRelativePath(text)) {
        return std::nullopt;
    }
    return text;
}

std::vector<std::string> Project::sceneFiles() const
{
    std::vector<std::string> scenes;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(m_folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        const std::string leaf = it->path().filename().string();
        if (!leaf.empty() && leaf.front() == '.') {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending(); // skip .git and other hidden folders
            }
            continue;
        }
        if (it->is_regular_file(ec) && it->path().extension() == SceneFormat::kExtension) {
            if (auto rel = relative(it->path())) {
                scenes.push_back(std::move(*rel));
            }
        }
    }
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

} // namespace rolo
