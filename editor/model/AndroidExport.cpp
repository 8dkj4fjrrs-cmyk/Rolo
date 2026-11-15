#include "AndroidExport.hpp"

#include "GameExport.hpp"

#include "rolo/assets/GamePack.hpp"
#include "rolo/assets/ImageEncode.hpp"
#include "rolo/core/Files.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>

namespace rolo::editor {

namespace fs = std::filesystem;

namespace {

// Written into every export, so a later export may safely replace it (the
// same marker as other exports: GameExport.cpp).
constexpr const char* kMarker = ".rolo-export";

bool isThere(const fs::path& path)
{
    std::error_code ec;
    return !path.empty() && fs::exists(path, ec);
}

bool isDirectory(const fs::path& path)
{
    std::error_code ec;
    return !path.empty() && fs::is_directory(path, ec);
}

void addPlace(std::vector<fs::path>& places, const fs::path& place)
{
    if (!place.empty() && std::find(places.begin(), places.end(), place) == places.end()) {
        places.push_back(place);
    }
}

bool contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

// Copies a folder and everything in it. False, with `error`, if something could not be copied.
bool copyTree(const fs::path& from, const fs::path& to, AndroidProject& counted, std::string& error)
{
    std::error_code ec;
    fs::create_directories(to, ec);
    for (const fs::directory_entry& entry : fs::directory_iterator(from, ec)) {
        const fs::path target = to / entry.path().filename();
        if (fs::is_directory(entry.path(), ec)) {
            if (!copyTree(entry.path(), target, counted, error)) {
                return false;
            }
            continue;
        }
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Could not copy " + entry.path().string() + " to " + target.string() + ": " + ec.message();
            return false;
        }
        counted.files += 1;
        counted.bytes += fs::file_size(target, ec);
    }
    if (ec) {
        error = "Could not read " + from.string() + ": " + ec.message();
        return false;
    }
    return true;
}

// Text for an Android string resource: XML's special characters, and the
// ones Android's resources give a meaning to (quotes, a leading @ or ?).
std::string androidStringResource(const std::string& text)
{
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\'': out += "\\'"; break;
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n':
        case '\r':
        case '\t': out += ' '; break;
        default: out += c;
        }
    }
    if (!out.empty() && (out.front() == '@' || out.front() == '?')) {
        out.insert(out.begin(), '\\');
    }
    return out;
}

std::string androidReadme(const AndroidExportOptions& o, const std::string& name, bool withBundle)
{
    const std::string title = name + " (for Android)";
    std::string text = title + "\n" + std::string(title.size(), '=') + "\n\n";
    if (withBundle) {
        text += name + ".apk\n"
                "    The game as an Android app, signed with your own key: the file to give to players\n"
                "    yourself (on itch.io, your own site, or by sending it). They open it on their\n"
                "    phone, which asks once whether to allow installing apps from files. To try it,\n"
                "    drag it onto a running emulator (Android Studio > Device Manager).\n"
                "    Updates must be signed with the same key and have a higher version code, or\n"
                "    phones refuse to install them over the old one. A copy you installed earlier that\n"
                "    was exported WITHOUT your key has to be removed from the phone first.\n"
                "    Google is bringing in \"developer verification\": phones will only install apps\n"
                "    whose maker has registered with Google. See docs/exporting.md, \"Sharing the\n"
                "    .apk yourself\".\n\n";
    } else {
        text += name + ".apk\n"
                "    The game as an Android app, to try. Drag it onto a running emulator (Android\n"
                "    Studio > Device Manager), or copy it to a phone and open it there (the phone asks\n"
                "    once whether to allow installing apps from files). It is signed with a throwaway\n"
                "    \"debug\" key: fine for trying, not for giving to players (you could never\n"
                "    update it) and not accepted by Google Play.\n\n";
    }
    if (withBundle) {
        text += name + ".aab\n"
                "    The game for Google Play (an Android App Bundle), signed with your upload key.\n"
                "    Upload this file in the Play Console. Every upload needs a higher version code\n"
                "    than the last (" + std::to_string(o.android.versionCode) + " this time). The editor raises it by\n"
                "    itself after each export like this one; on the command line, give --android-version-code.\n"
                "    Keep your key file and its password safe, and keep a copy somewhere else: without\n"
                "    them you cannot update the game.\n"
                "    Rolo's guide from account to release is docs/google-play.md (in the editor: Help >\n"
                "    Publishing on Google Play).\n\n";
    } else {
        text += "No app to share and no .aab (the file Google Play wants) were made: those need your\n"
                "own key. Choose \"Sign with my own key\" in the export dialog.\n\n";
    }
    text += "project/\n"
            "    What both were built from: a Gradle project, which Android Studio can open\n"
            "    (File > Open). Rolo writes it again at every export, so change your game in Rolo,\n"
            "    not here.\n\n"
            "Good to know\n"
            "------------\n"
            "- The app's package name is " + o.android.package + ". Once the game is on Google Play it can\n"
            "  never change.\n"
            "- The game runs on Android 7.0 and newer, on phones and tablets (arm64) and on\n"
            "  emulators and Chromebooks with Intel or AMD processors (x86_64).\n"
            "- It asks for no permissions and does not use the internet.\n"
            "- Saved games are kept on the device, in the app's own folder; Android removes them\n"
            "  with the app.\n\n"
            "Made with Rolo Engine " + o.version + " (MIT License).\n"
            "The app contains Rolo and the libraries it is built on; their licenses are inside it\n"
            "(assets/licenses) and in project/app/src/main/assets/licenses.\n";
    return text;
}

} // namespace

// --- Google's tools ----------------------------------------------------------------------------

bool isAndroidSdk(const fs::path& folder)
{
    return isDirectory(folder)
           && (isDirectory(folder / "platforms") || isDirectory(folder / "platform-tools")
               || isDirectory(folder / "build-tools") || isDirectory(folder / "cmdline-tools"));
}

fs::path javaProgram(const fs::path& javaHome, HostOs os, std::string_view name)
{
    return javaHome / "bin" / (std::string(name) + (os == HostOs::Windows ? ".exe" : ""));
}

bool isJavaHome(const fs::path& folder, HostOs os)
{
    std::error_code ec;
    return !folder.empty() && fs::is_regular_file(javaProgram(folder, os, "java"), ec);
}

std::vector<fs::path> androidSdkPlaces(HostOs os, const EnvironmentLookup& env)
{
    std::vector<fs::path> places;
    for (const char* name : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        if (const auto value = env(name); value && !value->empty()) {
            addPlace(places, *value);
        }
    }
    // Where Android Studio's setup puts the SDK.
    if (os == HostOs::Windows) {
        if (const auto local = env("LOCALAPPDATA"); local && !local->empty()) {
            addPlace(places, fs::path(*local) / "Android" / "Sdk");
        }
    } else if (const auto home = env("HOME"); home && !home->empty()) {
        addPlace(places, os == HostOs::MacOS ? fs::path(*home) / "Library" / "Android" / "sdk"
                                             : fs::path(*home) / "Android" / "Sdk");
    }
    return places;
}

std::vector<fs::path> javaHomePlaces(HostOs os, const EnvironmentLookup& env)
{
    std::vector<fs::path> places;
    const auto home = env("HOME");
    // The Java inside Android Studio ("jbr").
    switch (os) {
    case HostOs::MacOS:
        addPlace(places, "/Applications/Android Studio.app/Contents/jbr/Contents/Home");
        if (home && !home->empty()) {
            addPlace(places, fs::path(*home) / "Applications" / "Android Studio.app" / "Contents" / "jbr" / "Contents" / "Home");
        }
        break;
    case HostOs::Windows:
        for (const char* name : {"ProgramFiles", "ProgramW6432"}) {
            if (const auto programs = env(name); programs && !programs->empty()) {
                addPlace(places, fs::path(*programs) / "Android" / "Android Studio" / "jbr");
            }
        }
        if (const auto local = env("LOCALAPPDATA"); local && !local->empty()) {
            addPlace(places, fs::path(*local) / "Programs" / "Android Studio" / "jbr");
        }
        break;
    case HostOs::Linux:
        addPlace(places, "/opt/android-studio/jbr");
        addPlace(places, "/usr/local/android-studio/jbr");
        if (home && !home->empty()) {
            addPlace(places, fs::path(*home) / "android-studio" / "jbr");
        }
        addPlace(places, "/snap/android-studio/current/jbr");
        addPlace(places, "/snap/android-studio/current/android-studio/jbr");
        break;
    }
    if (const auto java = env("JAVA_HOME"); java && !java->empty()) {
        addPlace(places, *java);
    }
    return places;
}

AndroidTools findAndroidTools(const fs::path& chosenSdk, const fs::path& chosenJava, HostOs os,
                              const EnvironmentLookup& env)
{
    AndroidTools tools;
    if (isAndroidSdk(chosenSdk)) {
        tools.sdk = chosenSdk;
    } else {
        for (const fs::path& place : androidSdkPlaces(os, env)) {
            if (isAndroidSdk(place)) {
                tools.sdk = place;
                break;
            }
        }
    }
    if (isJavaHome(chosenJava, os)) {
        tools.javaHome = chosenJava;
    } else {
        for (const fs::path& place : javaHomePlaces(os, env)) {
            if (isJavaHome(place, os)) {
                tools.javaHome = place;
                break;
            }
        }
    }
    return tools;
}

int javaMajorVersion(std::string_view output)
{
    // The version is the first text in quotes: openjdk version "21.0.3" 2024-04-16
    const std::size_t open = output.find('"');
    if (open == std::string_view::npos) {
        return 0;
    }
    const auto number = [&](std::size_t at) {
        int value = 0;
        bool any = false;
        for (; at < output.size() && output[at] >= '0' && output[at] <= '9' && value < 100000; ++at) {
            value = value * 10 + (output[at] - '0');
            any = true;
        }
        return any ? value : 0;
    };
    const int first = number(open + 1);
    if (first == 1) {
        // Java 8 and older called themselves 1.8, 1.7...
        const std::size_t dot = output.find('.', open);
        return dot == std::string_view::npos ? 1 : number(dot + 1);
    }
    return first;
}

ProcessOptions javaVersionCommand(const fs::path& javaHome, HostOs os)
{
    ProcessOptions command;
    command.program = javaProgram(javaHome, os, "java");
    command.arguments = {"-version"};
    return command;
}

bool isAndroidRuntime(const fs::path& runtime)
{
    return isDirectory(runtime) && isThere(runtime / "settings.gradle") && isThere(runtime / "app" / "build.gradle")
           && isThere(runtime / "gradle" / "wrapper" / "gradle-wrapper.jar")
           && isDirectory(runtime / "app" / "src" / "main" / "jniLibs");
}

AndroidToolsReport checkAndroidTools(const AndroidTools& tools, HostOs os, std::string_view javaVersionOutput,
                                     const fs::path& runtime)
{
    AndroidToolsReport report;
    const std::string studio = "Android Studio (free, from developer.android.com/studio)";
    if (!isAndroidRuntime(runtime)) {
        report.problems.push_back(
            "Rolo's Android runtime was not found" + (runtime.empty() ? std::string() : " in " + runtime.string())
            + ". It comes with Rolo's download; when working from source, build it first (docs/building.md, \"For "
              "Android\").");
    }

    if (tools.sdk.empty()) {
        std::string where;
        for (const fs::path& place : androidSdkPlaces(os, realEnvironment)) {
            where += (where.empty() ? "" : " or ") + place.string();
        }
        report.problems.push_back("The Android SDK was not found. Install " + studio
                                  + ", open it once and let its setup finish: it downloads the SDK"
                                  + (where.empty() ? std::string() : " (Rolo looks in " + where + ")")
                                  + ". If yours is somewhere else, choose its folder (in the export dialog; --android-sdk on the command line).");
    } else if (!isAndroidSdk(tools.sdk)) {
        report.problems.push_back(tools.sdk.string() + " is not an Android SDK folder (it has no \"platforms\" or "
                                  "\"platform-tools\" folder inside). In Android Studio, Settings > Languages & "
                                  "Frameworks > Android SDK shows where it is.");
    } else {
        const std::string platform = "android-" + std::to_string(kAndroidPlatform);
        const bool hasPlatform = isDirectory(tools.sdk / "platforms" / platform);
        const bool licensed = isThere(tools.sdk / "licenses" / "android-sdk-license");
        const std::string install = "In Android Studio, open the SDK Manager (More Actions > SDK Manager on its "
                                    "start window), tick \"Android SDK Platform " + std::to_string(kAndroidPlatform)
                                    + "\" (Android 16) and click Apply.";
        if (!hasPlatform && !licensed) {
            report.problems.push_back("The Android SDK in " + tools.sdk.string() + " has no Android SDK Platform "
                                      + std::to_string(kAndroidPlatform) + ", which games are built with. " + install);
        } else if (!hasPlatform) {
            report.notes.push_back("Android SDK Platform " + std::to_string(kAndroidPlatform)
                                   + " is not installed yet. Google's build tool will download it during the export "
                                     "(about 60 MB). If that fails: " + install);
        }
    }

    if (tools.javaHome.empty()) {
        report.problems.push_back("Java was not found. It comes with " + studio + ", in a folder called \"jbr\" "
                                  "inside it. If Android Studio is installed somewhere unusual, choose that folder (in the export dialog; --android-java on the command line).");
    } else if (!isJavaHome(tools.javaHome, os)) {
        report.problems.push_back(tools.javaHome.string() + " is not a Java folder (there is no "
                                  + javaProgram(fs::path(), os, "java").string() + " in it). Android Studio's own "
                                  "Java is the folder \"jbr\" inside Android Studio.");
    } else if (javaVersionOutput.empty()) {
        report.problems.push_back("The Java in " + tools.javaHome.string() + " could not be run. Android Studio's "
                                  "own Java (the folder \"jbr\" inside it) is known to work: choose that one.");
    } else if (const int version = javaMajorVersion(javaVersionOutput); version < kMinimumJava) {
        report.problems.push_back("The Java in " + tools.javaHome.string() + " is "
                                  + (version > 0 ? "version " + std::to_string(version) : std::string("of an unknown version"))
                                  + ", and Google's build tools need " + std::to_string(kMinimumJava)
                                  + " or newer. Android Studio's own Java (the folder \"jbr\" inside it) is new "
                                    "enough: choose that one.");
    }
    return report;
}

// --- The app -----------------------------------------------------------------------------------

std::string suggestedAndroidPackage(const std::string& gameName)
{
    std::string last;
    for (const char c : gameName) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x80 && std::isalnum(u) != 0) {
            last += static_cast<char>(std::tolower(u));
        }
    }
    if (last.empty() || std::isdigit(static_cast<unsigned char>(last.front())) != 0) {
        last = "game" + last;
    }
    const std::string name = "com.yourname." + last;
    return isValidAndroidPackage(name) ? name : "com.yourname.game";   // (a game called "New" or "Class")
}

std::string androidScreenOrientation(const AndroidSettings& settings, int gameWidth, int gameHeight)
{
    if (settings.orientation == "portrait") {
        return "sensorPortrait";
    }
    if (settings.orientation == "landscape") {
        return "sensorLandscape";
    }
    return gameHeight > gameWidth ? "sensorPortrait" : "sensorLandscape";
}

DecodedImage resizedIcon(const DecodedImage& image, int size)
{
    DecodedImage out;
    out.width = size;
    out.height = size;
    out.hasAlpha = true;
    out.rgba.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4, 0);
    if (image.width <= 0 || image.height <= 0 || size <= 0
        || image.rgba.size() < static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4) {
        return out;
    }
    // The picture keeps its shape: it is fitted inside the square, centred.
    const double scale = static_cast<double>(size) / std::max(image.width, image.height);
    const double shownW = image.width * scale;
    const double shownH = image.height * scale;
    const double left = (size - shownW) * 0.5;
    const double top = (size - shownH) * 0.5;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // The part of the source picture this pixel covers...
            const double sx0 = std::max(0.0, (x - left) / scale);
            const double sx1 = std::min<double>(image.width, (x + 1 - left) / scale);
            const double sy0 = std::max(0.0, (y - top) / scale);
            const double sy1 = std::min<double>(image.height, (y + 1 - top) / scale);
            if (sx1 <= sx0 || sy1 <= sy0) {
                continue;   // outside the picture: stays see-through
            }
            // ...averaged, each source pixel counting by how much of it is
            // covered, and colours counting by how solid they are (so that
            // see-through pixels do not darken the edges).
            double sum[4] = {0.0, 0.0, 0.0, 0.0};
            for (int sy = static_cast<int>(sy0); sy < image.height && sy < sy1; ++sy) {
                const double wy = std::min<double>(sy + 1, sy1) - std::max<double>(sy, sy0);
                for (int sx = static_cast<int>(sx0); sx < image.width && sx < sx1; ++sx) {
                    const double wx = std::min<double>(sx + 1, sx1) - std::max<double>(sx, sx0);
                    const std::uint8_t* p = &image.rgba[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(image.width)
                                                         + static_cast<std::size_t>(sx)) * 4];
                    const double alpha = p[3] / 255.0 * wx * wy;
                    sum[0] += p[0] * alpha;
                    sum[1] += p[1] * alpha;
                    sum[2] += p[2] * alpha;
                    sum[3] += alpha;
                }
            }
            // How much of this pixel the picture covers at all (less than 1 at a non-square picture's edge).
            const double covered = ((sx1 - sx0) * scale) * ((sy1 - sy0) * scale);
            std::uint8_t* d = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4];
            if (sum[3] > 0.0) {
                for (int c = 0; c < 3; ++c) {
                    d[c] = static_cast<std::uint8_t>(std::clamp(sum[c] / sum[3] + 0.5, 0.0, 255.0));
                }
                const double area = (sx1 - sx0) * (sy1 - sy0);
                d[3] = static_cast<std::uint8_t>(std::clamp(sum[3] / area * std::min(1.0, covered) * 255.0 + 0.5, 0.0, 255.0));
            }
        }
    }
    return out;
}

Status writeAndroidIcons(const fs::path& png, const fs::path& resFolder)
{
    std::string why;
    const auto image = decodeImageFile(png, &why);
    if (!image) {
        return Status::failure("The icon " + png.string() + " could not be read (" + why + "). Use a square PNG "
                               "picture, 512 x 512 for the best result.");
    }
    // Android's screen densities and the icon's size on each, in pixels.
    const std::pair<const char*, int> sizes[] = {
        {"mdpi", 48}, {"hdpi", 72}, {"xhdpi", 96}, {"xxhdpi", 144}, {"xxxhdpi", 192}};
    for (const auto& [density, size] : sizes) {
        const DecodedImage icon = resizedIcon(*image, size);
        const fs::path folder = resFolder / (std::string("mipmap-") + density);
        std::error_code ec;
        fs::create_directories(folder, ec);
        if (!writePngFile(folder / "ic_launcher.png", icon.rgba.data(), size, size, 4, &why)) {
            return Status::failure("The app's icon could not be written to " + folder.string() + ": " + why + ".");
        }
    }
    return Status::success();
}

AndroidProject writeAndroidProject(const AndroidExportOptions& o)
{
    AndroidProject result;
    const auto fail = [&](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };
    std::error_code ec;
    std::string error;
    const auto project = Project::open(o.project, &error);
    if (!project) {
        return fail(error);
    }
    if (!isAndroidRuntime(o.runtime)) {
        return fail("Rolo's Android runtime was not found" + (o.runtime.empty() ? std::string() : " in " + o.runtime.string())
                    + ". It comes with Rolo's download; when working from source, build it first (docs/building.md, "
                      "\"For Android\").");
    }
    if (o.licenses.empty() || !isThere(o.licenses / "README.md")) {
        return fail("The license texts that must go with every game were not found"
                    + (o.licenses.empty() ? std::string() : " in " + o.licenses.string()) + ". Reinstall Rolo.");
    }
    std::string why;
    if (!isValidAndroidPackage(o.android.package, &why)) {
        return fail("The package name \"" + o.android.package + "\" cannot be used: " + why);
    }
    if (o.android.versionCode < 1) {
        return fail("The version code must be 1 or more.");
    }
    if (o.output.empty()) {
        return fail("Choose a folder to export the game into.");
    }
    {
        // Not inside the project: the export would be packed into the next export.
        const fs::path inner = fs::weakly_canonical(o.output, ec);
        const fs::path outer = fs::weakly_canonical(o.project, ec);
        const auto [mismatch, end] = std::mismatch(outer.begin(), outer.end(), inner.begin(), inner.end());
        if (mismatch == outer.end()) {
            return fail("Export outside the project folder: a game exported inside its own project would be copied "
                        "into the next export.");
        }
    }

    const std::string name = exportFileName(o.gameName);
    // (A full path: Gradle is started inside the project folder, where a
    // path relative to the editor's folder would point somewhere else.)
    result.folder = fs::absolute(o.output, ec) / (name + " Android");
    result.gradleProject = result.folder / "project";
    if (isThere(result.folder)) {
        if (!isThere(result.folder / kMarker)) {
            return fail(result.folder.string() + " already exists and is not an earlier export of a game. Choose "
                                                 "another folder or name.");
        }
        fs::remove_all(result.folder, ec);
        if (ec) {
            return fail("Could not replace the earlier export " + result.folder.string() + ": " + ec.message()
                        + ". Is it open in Android Studio, or still being built?");
        }
    }
    fs::create_directories(result.folder, ec);
    if (ec) {
        return fail("Could not create " + result.folder.string() + ": " + ec.message());
    }
    if (const Status s = writeTextFileAtomically(result.folder / kMarker, "Exported by Rolo " + o.version + "\n"); !s) {
        return fail(s.message);
    }

    // The Gradle project: Rolo's Android runtime, as it is...
    if (!copyTree(o.runtime, result.gradleProject, result, error)) {
        return fail(error);
    }
    const fs::path main = result.gradleProject / "app" / "src" / "main";

    // ...the game, as one pack among the app's assets...
    const auto notHidden = [](const fs::path& p) {
        const std::string file = p.filename().string();
        return file.empty() || file.front() != '.';   // no .git, .DS_Store...
    };
    const std::string appName = o.gameName.empty() ? name : o.gameName;
    const GamePackResult pack =
        writeGamePack(o.project, main / "assets" / "game.pack", appName, gamePackSavesName(name), notHidden);
    if (!pack.status) {
        return fail(pack.status.message);
    }
    result.files += 1;
    result.bytes += pack.bytes;

    // ...the license texts...
    if (!copyTree(o.licenses, main / "assets" / "licenses", result, error)) {
        return fail(error);
    }

    // ...the app's name...
    const std::string strings = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                                "<!-- The name under the app's icon. Written by Rolo's export. -->\n"
                                "<resources>\n"
                                "    <string name=\"app_name\">" + androidStringResource(appName) + "</string>\n"
                                "</resources>\n";
    if (const Status s = writeTextFileAtomically(main / "res" / "values" / "strings.xml", strings); !s) {
        return fail(s.message);
    }

    // ...its icon...
    if (!o.android.icon.empty()) {
        const fs::path icon = project->absolute(o.android.icon);
        if (!isThere(icon)) {
            return fail("The app's icon, " + o.android.icon + ", is not in the project. Choose a PNG picture that "
                        "is, or leave the icon empty for Rolo's.");
        }
        if (const Status s = writeAndroidIcons(icon, main / "res"); !s) {
            return fail(s.message);
        }
    } else {
        result.notes.push_back("The app has Rolo's icon. To give it your own, put a square PNG picture (512 x 512) "
                               "in the project and choose it as the icon.");
    }

    // ...and what Gradle needs to know about the app.
    nlohmann::json game;
    game["roloPackage"] = o.android.package;
    game["roloVersionCode"] = o.android.versionCode;
    game["roloVersionName"] = o.android.versionName.empty() ? std::string("1.0") : o.android.versionName;
    game["roloOrientation"] = androidScreenOrientation(o.android, project->settings().width, project->settings().height);
    if (const Status s = writeTextFileAtomically(result.gradleProject / "rolo-game.json", game.dump(2) + "\n"); !s) {
        return fail(s.message);
    }

    if (o.android.package.rfind("com.example.", 0) == 0) {
        result.notes.push_back("Google Play does not accept package names that start with com.example. Choose your "
                               "own before publishing: it can never change afterwards.");
    } else if (contains(o.android.package, "yourname")) {
        result.notes.push_back("The package name still has \"yourname\" in it (" + o.android.package + "). That is "
                               "fine for trying the game; choose your own before publishing, as it can never change "
                               "afterwards.");
    }
    result.ok = true;
    return result;
}

ProcessOptions gradleCommand(const fs::path& gradleProject, const AndroidTools& tools, HostOs os,
                             const std::vector<std::string>& tasks, const AndroidSigning& signing)
{
    // What Gradle's own "gradlew" scripts do, without a shell: Java runs the
    // small starter program, which fetches the Gradle named in
    // gradle/wrapper/gradle-wrapper.properties the first time and runs it.
    ProcessOptions command;
    command.program = javaProgram(tools.javaHome, os, "java");
    command.workingFolder = gradleProject;
    command.arguments = {"-Xmx64m",
                         "-Xms64m",
                         "-Dorg.gradle.appname=gradlew",
                         "-classpath",
                         (gradleProject / "gradle" / "wrapper" / "gradle-wrapper.jar").string(),
                         "org.gradle.wrapper.GradleWrapperMain",
                         "--console=plain",   // plain lines, no progress bars
                         "--no-daemon"};      // nothing left running (and using memory) afterwards
    command.arguments.insert(command.arguments.end(), tasks.begin(), tasks.end());
    command.environment = {{"JAVA_HOME", tools.javaHome.string()},
                           {"ANDROID_HOME", tools.sdk.string()},
                           {"ANDROID_SDK_ROOT", tools.sdk.string()}};
    if (signing.wanted()) {
        std::error_code ec;
        command.environment.emplace_back(kKeystoreVariable, fs::absolute(signing.keystore, ec).string());
        command.environment.emplace_back(kKeystorePasswordVariable, signing.password);
        command.environment.emplace_back(kKeyAliasVariable, signing.alias);
    }
    return command;
}

std::string newKeyProblem(const AndroidSigning& key, const std::string& owner)
{
    if (key.keystore.empty()) {
        return "Choose where to keep the key file.";
    }
    if (isThere(key.keystore)) {
        return key.keystore.string() + " already exists. A key file is never replaced: losing a key means losing "
                                       "the right to update the games signed with it. Choose another name.";
    }
    if (key.password.size() < 6) {
        return "The password needs at least 6 characters.";
    }
    if (key.alias.empty()) {
        return "The key needs a name (\"upload\" is the usual one).";
    }
    if (owner.empty()) {
        return "Say whose key it is: your name or your studio's.";
    }
    return {};
}

ProcessOptions newKeyCommand(const fs::path& javaHome, HostOs os, const AndroidSigning& key, const std::string& owner)
{
    // The owner's name goes into the key as "CN=name"; these characters have
    // a meaning there and are written with a backslash before them.
    std::string name;
    for (const char c : owner) {
        if (std::string_view(",+\"\\<>;=").find(c) != std::string_view::npos) {
            name += '\\';
        }
        name += (c == '\n' || c == '\r') ? ' ' : c;
    }
    ProcessOptions command;
    command.program = javaProgram(javaHome, os, "keytool");
    std::error_code ec;
    command.arguments = {"-genkeypair",
                         "-keystore", fs::absolute(key.keystore, ec).string(),
                         "-storetype", "PKCS12",
                         // The password is read from a variable: it is on no command line.
                         std::string("-storepass:env"), kKeystorePasswordVariable,
                         "-alias", key.alias,
                         "-keyalg", "RSA",
                         "-keysize", "2048",
                         "-validity", "10000",   // days: about 27 years (Google Play asks for 25 or more)
                         "-dname", "CN=" + name};
    command.environment = {{kKeystorePasswordVariable, key.password}};
    return command;
}

Status createAndroidKey(const fs::path& javaHome, HostOs os, const AndroidSigning& key, const std::string& owner)
{
    if (const std::string problem = newKeyProblem(key, owner); !problem.empty()) {
        return Status::failure(problem);
    }
    if (!isJavaHome(javaHome, os)) {
        return Status::failure("Java was not found, and making a key needs its keytool program. It comes with "
                               "Android Studio (the folder \"jbr\" inside it).");
    }
    std::error_code ec;
    fs::create_directories(fs::absolute(key.keystore, ec).parent_path(), ec);
    std::string output;
    const ProcessResult made = runProcess(newKeyCommand(javaHome, os, key, owner), [&](std::string_view line) {
        output += (output.empty() ? "" : " ") + std::string(line);
    });
    if (!made.started) {
        return Status::failure("Java's keytool could not be started. " + made.error);
    }
    if (made.exitCode != 0 || !isThere(key.keystore)) {
        return Status::failure("The key could not be made. Java's keytool said: " + (output.empty() ? "nothing." : output));
    }
    return Status::success();
}

std::string playPackageProblem(const std::string& package)
{
    if (package.rfind("com.example.", 0) == 0 || package == "com.example") {
        return "Google Play does not accept package names that start with com.example. Choose your own (your name, "
               "or a web address you own backwards, then the game): it stays the game's for ever.";
    }
    if (contains(package, "yourname")) {
        return "The package name still has \"yourname\" in it (" + package + "). Put your own name there before making "
               "the file for Google Play: once a game is uploaded, its package name can never change.";
    }
    return {};
}

std::string explainGradleFailure(const std::vector<std::string>& output)
{
    const auto said = [&](std::string_view part) {
        return std::any_of(output.begin(), output.end(), [&](const std::string& line) { return contains(line, part); });
    };
    if (said("password was incorrect") || said("Keystore was tampered with") || said("keystore password was incorrect")
        || said("Failed to read key") || said("Wrong password")) {
        return "The key file could not be opened: the password is wrong (or the key's name inside the file is not "
               "the one given). Nothing is wrong with the game; export again with the right password.";
    }
    if (said("No key with alias")) {
        return "The key file has no key with that name. The name is the one chosen when the key was made (Rolo "
               "calls it \"upload\").";
    }
    if (said("SDK location not found")) {
        return "Google's build tool did not find the Android SDK. Choose its folder in the export dialog (in "
               "Android Studio, Settings > Languages & Frameworks > Android SDK shows where it is).";
    }
    if (said("licences have not been accepted") || said("licenses have not been accepted")
        || said("License for package")) {
        return "A part of the Android SDK is missing, and its license has not been accepted, so it could not be "
               "downloaded. In Android Studio, open the SDK Manager, tick \"Android SDK Platform "
               + std::to_string(kAndroidPlatform) + "\" and \"Android SDK Build-Tools\" and click Apply; then export again.";
    }
    if (said("No space left on device") || said("There is not enough space on the disk")) {
        return "The disk is full. Google's build tools need about 1 GB the first time; free some space and export again.";
    }
    if (said("UnknownHostException") || said("Could not resolve all") || said("Could not GET ")
        || said("Connection refused") || said("Connection timed out") || said("Could not resolve com.android")
        || said("Network is unreachable")) {
        return "Google's build tools could not be downloaded. The first export fetches them from the internet (a "
               "few hundred MB, once); check the connection and export again.";
    }
    if (said("Unsupported class file major version") || said("requires Java") || said("compatible with Java")) {
        return "The Java used is the wrong version for Google's build tools. Android Studio's own Java (the folder "
               "\"jbr\" inside it) is the one they are tested with: choose it in the export dialog.";
    }
    return {};
}

AndroidExportResult exportAndroidGame(const AndroidExportOptions& options, const AndroidSigning& signing,
                                      const std::function<void(std::string_view)>& say, const std::atomic<bool>* cancel)
{
    AndroidExportResult result;
    const auto tell = [&](const std::string& line) {
        if (say) {
            say(line);
        }
    };
    if (signing.wanted()) {
        std::error_code ec;
        if (!fs::is_regular_file(signing.keystore, ec)) {
            result.message = "The key file " + signing.keystore.string() + " was not found.";
            return result;
        }
        if (signing.password.empty()) {
            result.message = "Give the key file's password (it is not stored anywhere, so it is asked for at every export).";
            return result;
        }
        if (const std::string problem = playPackageProblem(options.android.package); !problem.empty()) {
            result.message = "The game was not exported. " + problem;
            return result;
        }
    }

    tell("Writing the Android project...");
    result.project = writeAndroidProject(options);
    if (!result.project.ok) {
        result.message = "The game was not exported: " + result.project.error;
        return result;
    }
    for (const std::string& note : result.project.notes) {
        tell("Note: " + note);
    }

    // Without a key: an app to try, signed with Google's throwaway debug
    // key. With the maker's key: the app to share and the bundle for Google
    // Play, both signed with it by Google's tools.
    std::vector<std::string> tasks = {":app:assembleDebug"};
    if (signing.wanted()) {
        tasks = {":app:assembleRelease", ":app:bundleRelease"};
    }
    tell("Building the app with Google's tools (Gradle). This takes a few minutes; the first time it also "
         "downloads the tools, a few hundred MB.");
    std::deque<std::string> lastLines;   // enough of Gradle's output to find out why it failed
    const ProcessResult built = runProcess(
        gradleCommand(result.project.gradleProject, options.tools, options.os, tasks, signing),
        [&](std::string_view line) {
            lastLines.emplace_back(line);
            if (lastLines.size() > 600) {
                lastLines.pop_front();
            }
            tell(std::string(line));
        },
        cancel);
    if (built.cancelled) {
        result.cancelled = true;
        result.message = "The export was cancelled.";
        return result;
    }
    if (!built.started) {
        result.message = "The game was not exported: Java could not be started. " + built.error;
        return result;
    }
    if (built.exitCode != 0) {
        const std::string known = explainGradleFailure({lastLines.begin(), lastLines.end()});
        result.message = "The game was not exported. "
                         + (known.empty() ? "Google's build tool (Gradle) stopped with an error; its own words are "
                                            "the lines above \"BUILD FAILED\" in the Console (or above, in the terminal)."
                                          : known);
        return result;
    }

    // The results, out of Gradle's folders and under the game's name.
    const std::string name = exportFileName(options.gameName);
    const fs::path outputs = result.project.gradleProject / "app" / "build" / "outputs";
    std::error_code ec;
    result.apk = result.project.folder / (name + ".apk");
    result.apkSignedWithOwnKey = signing.wanted();
    const fs::path builtApk = signing.wanted() ? outputs / "apk" / "release" / "app-release.apk"
                                               : outputs / "apk" / "debug" / "app-debug.apk";
    fs::copy_file(builtApk, result.apk, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.message = "Gradle finished, but the app was not where it should be (" + builtApk.string()
                         + "): " + ec.message();
        return result;
    }
    if (signing.wanted()) {
        result.aab = result.project.folder / (name + ".aab");
        fs::copy_file(outputs / "bundle" / "release" / "app-release.aab", result.aab, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            result.message = "Gradle finished, but the file for Google Play was not where it should be ("
                             + (outputs / "bundle" / "release" / "app-release.aab").string() + "): " + ec.message();
            return result;
        }
    }
    // For the store listing: the project's icon at 512 x 512, as Google Play asks for it.
    if (!options.android.icon.empty()) {
        if (const auto icon = decodeImageFile(options.project / options.android.icon)) {
            const DecodedImage large = resizedIcon(*icon, 512);
            const fs::path file = result.project.folder / "Google Play icon (512).png";
            if (writePngFile(file, large.rgba.data(), 512, 512, 4)) {
                result.storeIcon = file;
                std::error_code sizeError;
                if (fs::file_size(file, sizeError) > 1024u * 1024u && !sizeError) {
                    say("Note: " + file.filename().string() + " is larger than the 1024 KB Google Play allows for "
                        "the store's icon. A simpler picture (fewer colours, less detail) makes a smaller file.");
                }
            }
        }
    }
    static_cast<void>(writeTextFileAtomically(result.project.folder / "README.txt",
                                              androidReadme(options, name, signing.wanted())));

    char size[32];
    std::snprintf(size, sizeof size, "%.1f MB", static_cast<double>(fs::file_size(result.apk, ec)) / (1024.0 * 1024.0));
    result.ok = true;
    result.message = "Exported " + result.apk.filename().string() + " (" + size
                     + (signing.wanted() ? ", signed with your key, to share) and " + result.aab.filename().string()
                                               + " for Google Play"
                                         : std::string(", to try)"))
                     + " to " + result.project.folder.string() + ".";
    return result;
}

} // namespace rolo::editor
