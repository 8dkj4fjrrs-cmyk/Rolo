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
                                     std::pair{"quickSaveKeys", &settings.quickSaveKeys},
                                     std::pair{"touchControls", &settings.touchControls}}) {
        if (j.contains(key)) {
            if (!j[key].is_boolean()) {
                setError(error, std::string("The project's \"") + key + "\" must be true or false.");
                return std::nullopt;
            }
            *flag = j[key].get<bool>();
        }
    }
    for (const auto& [key, size] : {std::pair{"width", &settings.width}, std::pair{"height", &settings.height}}) {
        if (j.contains(key)) {
            if (!j[key].is_number_integer() || j[key].get<long long>() < ProjectSettings::kMinSize
                || j[key].get<long long>() > ProjectSettings::kMaxSize) {
                setError(error, std::string("The project's \"") + key + "\" (the game's size) must be a whole number from "
                                    + std::to_string(ProjectSettings::kMinSize) + " to "
                                    + std::to_string(ProjectSettings::kMaxSize) + ".");
                return std::nullopt;
            }
            *size = j[key].get<int>();
        }
    }
    if (j.contains("screenFit")) {
        const auto fit = j["screenFit"].is_string() ? screenFitFromName(j["screenFit"].get<std::string>()) : std::nullopt;
        if (!fit) {
            setError(error, "The project's \"screenFit\" must be \"bars\" or \"expand\".");
            return std::nullopt;
        }
        settings.screenFit = *fit;
    }
    if (j.contains("touchButtons")) {
        const Json& list = j["touchButtons"];
        const bool texts = list.is_array()
                           && std::all_of(list.begin(), list.end(), [](const Json& name) { return name.is_string(); });
        if (!texts) {
            setError(error, "The project's \"touchButtons\" must be a list of action names, like [\"jump\", \"pause\"].");
            return std::nullopt;
        }
        settings.touchButtons = list.get<std::vector<std::string>>();
    }
    if (j.contains("android")) {
        const Json& a = j["android"];
        if (!a.is_object()) {
            setError(error, "The project's \"android\" must be a group of settings, like {\"package\": \"com.you.game\"}.");
            return std::nullopt;
        }
        AndroidSettings& android = settings.android;
        for (const auto& [key, value] : {std::pair{"package", &android.package},
                                         std::pair{"versionName", &android.versionName},
                                         std::pair{"orientation", &android.orientation},
                                         std::pair{"icon", &android.icon}}) {
            if (a.contains(key)) {
                if (!a[key].is_string()) {
                    setError(error, std::string("The project's Android \"") + key + "\" must be text.");
                    return std::nullopt;
                }
                *value = a[key].get<std::string>();
            }
        }
        if (a.contains("versionCode")) {
            // Google Play's limit for version codes is 2100000000.
            if (!a["versionCode"].is_number_integer() || a["versionCode"].get<long long>() < 1
                || a["versionCode"].get<long long>() > 2100000000LL) {
                setError(error, "The project's Android \"versionCode\" must be a whole number from 1 to 2100000000.");
                return std::nullopt;
            }
            android.versionCode = a["versionCode"].get<int>();
        }
        std::string why;
        if (!android.package.empty() && !isValidAndroidPackage(android.package, &why)) {
            setError(error, "The project's Android package name \"" + android.package + "\" cannot be used: " + why);
            return std::nullopt;
        }
        if (!android.orientation.empty() && android.orientation != "landscape" && android.orientation != "portrait") {
            setError(error, "The project's Android \"orientation\" must be \"landscape\" or \"portrait\".");
            return std::nullopt;
        }
        if (!android.icon.empty() && !isValidRelativePath(android.icon)) {
            setError(error, "The project's Android \"icon\" must be a file inside the project, like \"icon.png\".");
            return std::nullopt;
        }
    }
    return settings;
}

bool isValidAndroidPackage(std::string_view name, std::string* why)
{
    const auto fail = [&](std::string message) {
        setError(why, std::move(message));
        return false;
    };
    if (name.empty()) {
        return fail("it is empty. It looks like com.yourname.yourgame.");
    }
    // Words Java keeps for itself; Android refuses them as parts of a package name.
    static constexpr std::string_view kKeywords[] = {
        "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue",
        "default", "do", "double", "else", "enum", "extends", "false", "final", "finally", "float", "for", "goto",
        "if", "implements", "import", "instanceof", "int", "interface", "long", "native", "new", "null", "package",
        "private", "protected", "public", "return", "short", "static", "strictfp", "super", "switch",
        "synchronized", "this", "throw", "throws", "transient", "true", "try", "void", "volatile", "while"};
    int parts = 0;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t dot = std::min(name.find('.', start), name.size());
        const std::string_view part = name.substr(start, dot - start);
        if (part.empty()) {
            return fail("it has an empty part (two dots together, or a dot at an end).");
        }
        const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        if (!letter(part.front())) {
            return fail("the part \"" + std::string(part) + "\" must start with a letter.");
        }
        for (const char c : part) {
            if (!letter(c) && !(c >= '0' && c <= '9') && c != '_') {
                return fail("the part \"" + std::string(part)
                            + "\" may only have letters (a to z), digits and _ (no spaces or hyphens).");
            }
        }
        if (std::find(std::begin(kKeywords), std::end(kKeywords), part) != std::end(kKeywords)) {
            return fail("\"" + std::string(part) + "\" is a word of the Java language, which Android does not allow "
                        "as a part.");
        }
        ++parts;
        start = dot + 1;
    }
    if (parts < 2) {
        return fail("it needs at least two parts joined by a dot, like com.yourname.yourgame.");
    }
    return true;
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
    if (!settings.touchControls) {
        j["touchControls"] = false;
    }
    if (!settings.touchButtons.empty()) {
        j["touchButtons"] = settings.touchButtons;
    }
    if (settings.width != 1280 || settings.height != 720) {
        j["width"] = settings.width;
        j["height"] = settings.height;
    }
    if (settings.screenFit != ScreenFit::Bars) {
        j["screenFit"] = std::string(screenFitName(settings.screenFit));
    }
    if (settings.android != AndroidSettings{}) {
        Json a;
        const AndroidSettings& android = settings.android;
        if (!android.package.empty()) {
            a["package"] = android.package;
        }
        a["versionCode"] = android.versionCode;
        a["versionName"] = android.versionName;
        if (!android.orientation.empty()) {
            a["orientation"] = android.orientation;
        }
        if (!android.icon.empty()) {
            a["icon"] = android.icon;
        }
        j["android"] = std::move(a);
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
