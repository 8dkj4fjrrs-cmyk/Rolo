#include "CommandLine.hpp"

#include <charconv>
#include <limits>

namespace rolo::editor {

EditorParseResult parseEditorCommandLine(std::span<const std::string_view> args)
{
    EditorParseResult result;
    EditorOptions& opt = result.options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];

        auto nextValue = [&](std::string_view& out) -> bool {
            if (i + 1 >= args.size()) {
                result.error = std::string(arg) + " needs a value.";
                return false;
            }
            out = args[++i];
            return true;
        };

        std::string_view value;
        if (arg == "--help" || arg == "-h") {
            opt.showHelp = true;
        } else if (arg == "--version") {
            opt.showVersion = true;
        } else if (arg == "--reset-layout") {
            opt.resetLayout = true;
        } else if (arg == "--no-project") {
            opt.noProject = true;
        } else if (arg == "--play") {
            opt.play = true;
        } else if (arg == "--project" || arg == "--new-project") {
            if (!nextValue(value)) {
                return result;
            }
            if (value.empty()) {
                result.error = std::string(arg) + " needs a folder path.";
                return result;
            }
            (arg == "--project" ? opt.projectPath : opt.newProjectPath) = std::string(value);
        } else if (arg == "--android-package" || arg == "--android-version-name" || arg == "--android-sdk"
                   || arg == "--android-java" || arg == "--android-key" || arg == "--android-key-alias"
                   || arg == "--new-android-key" || arg == "--android-key-owner") {
            if (!nextValue(value)) {
                return result;
            }
            if (value.empty()) {
                result.error = std::string(arg) + " needs a value.";
                return result;
            }
            std::string& into = arg == "--android-package"        ? opt.androidPackage
                                : arg == "--android-version-name" ? opt.androidVersionName
                                : arg == "--android-sdk"          ? opt.androidSdk
                                : arg == "--android-java"         ? opt.androidJava
                                : arg == "--android-key"          ? opt.androidKey
                                : arg == "--android-key-alias"    ? opt.androidKeyAlias
                                : arg == "--new-android-key"      ? opt.newAndroidKey
                                                                  : opt.androidKeyOwner;
            into = std::string(value);
        } else if (arg == "--android-version-code") {
            if (!nextValue(value)) {
                return result;
            }
            int code = 0;
            const char* end = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(value.data(), end, code);
            if (ec != std::errc{} || ptr != end || code < 1) {
                result.error = "--android-version-code must be a whole number of at least 1, got '" + std::string(value) + "'.";
                return result;
            }
            opt.androidVersionCode = code;
        } else if (arg == "--template" || arg == "--export" || arg == "--export-web" || arg == "--export-android") {
            if (!nextValue(value)) {
                return result;
            }
            if (value.empty()) {
                result.error = std::string(arg) + (arg == "--template" ? " needs a template name." : " needs a folder path.");
                return result;
            }
            if (arg != "--template" && !opt.exportPath.empty()) {
                result.error = "Give --export, --export-web or --export-android once: one game is exported at a time.";
                return result;
            }
            (arg == "--template" ? opt.templateId : opt.exportPath) = std::string(value);
            opt.exportWeb = opt.exportWeb || arg == "--export-web";
            opt.exportAndroid = opt.exportAndroid || arg == "--export-android";
        } else if (arg == "--config-dir") {
            if (!nextValue(value)) {
                return result;
            }
            if (value.empty()) {
                result.error = "--config-dir needs a folder path.";
                return result;
            }
            opt.configDirectory = std::string(value);
        } else if (arg == "--frames") {
            if (!nextValue(value)) {
                return result;
            }
            std::uint64_t frames = 0;
            const char* end = value.data() + value.size();
            const auto [ptr, ec] = std::from_chars(value.data(), end, frames);
            if (ec != std::errc{} || ptr != end || frames == 0) {
                result.error = "--frames must be a whole number of at least 1, got '" + std::string(value) + "'.";
                return result;
            }
            opt.maxFrames = frames;
        } else if (arg == "--screenshot") {
            if (!nextValue(value)) {
                return result;
            }
            opt.screenshotPath = std::string(value);
        } else {
            result.error = "Unknown option: " + std::string(arg);
            return result;
        }
    }

    if (!opt.screenshotPath.empty() && opt.maxFrames == 0) {
        result.error = "--screenshot needs --frames N, so the editor knows which frame to capture.";
    }
    const int projectOptions = (opt.projectPath.empty() ? 0 : 1) + (opt.newProjectPath.empty() ? 0 : 1)
                               + (opt.noProject ? 1 : 0);
    if (projectOptions > 1) {
        result.error = "Use only one of --project, --new-project and --no-project.";
    }
    if (!opt.templateId.empty() && opt.newProjectPath.empty()) {
        result.error = "--template goes with --new-project FOLDER.";
    }
    if (!opt.exportPath.empty() && opt.projectPath.empty()) {
        result.error = opt.exportWeb       ? "--export-web needs the project to export: --project FOLDER --export-web OUTPUT."
                       : opt.exportAndroid ? "--export-android needs the project to export: --project FOLDER --export-android OUTPUT."
                                           : "--export needs the project to export: --project FOLDER --export OUTPUT.";
    }
    const bool androidOptions = !opt.androidPackage.empty() || opt.androidVersionCode != 0 || !opt.androidVersionName.empty()
                                || !opt.androidKey.empty();
    if (androidOptions && !opt.exportAndroid) {
        result.error = "--android-package, --android-version-code, --android-version-name and --android-key go with "
                       "--export-android OUTPUT.";
    }
    if (!opt.newAndroidKey.empty() && opt.androidKeyOwner.empty()) {
        result.error = "--new-android-key needs --android-key-owner NAME: your name or your studio's, kept in the key.";
    }
    if (opt.newAndroidKey.empty() && !opt.androidKeyOwner.empty()) {
        result.error = "--android-key-owner goes with --new-android-key FILE.";
    }
    return result;
}

std::string editorHelpText()
{
    return "Usage: rolo_editor [options]\n"
           "\n"
           "Options:\n"
           "  --help              Show this message\n"
           "  --version           Print version information and exit\n"
           "  --project FOLDER    Open the Rolo project in FOLDER\n"
           "  --new-project FOLDER  Create a new project in FOLDER and open it\n"
           "  --template NAME     With --new-project: start from a template, e.g. topdown\n"
           "  --export OUTPUT     With --project: export the game into OUTPUT and quit\n"
           "  --export-web OUTPUT With --project: export the game for web browsers into OUTPUT and quit\n"
           "  --export-android OUTPUT  With --project: export the game for Android into OUTPUT and quit\n"
           "                      (needs Android Studio installed; docs/exporting.md, \"For Android\")\n"
           "      --android-package NAME       the app's package name, instead of the project's\n"
           "      --android-version-code N     its version code (a whole number), instead of the project's\n"
           "      --android-version-name TEXT  the version players see, instead of the project's\n"
           "      --android-sdk DIR            the Android SDK, if not where Android Studio puts it\n"
           "      --android-java DIR           the Java to use, if not Android Studio's own\n"
           "      --android-key FILE           your key: sign the .apk with it (to share), and also\n"
           "                                   make the .aab for Google Play. Its\n"
           "                                   password is read from the environment variable\n"
           "                                   ROLO_ANDROID_KEYSTORE_PASSWORD\n"
           "      --android-key-alias NAME     the key's name inside the file (default: upload)\n"
           "  --new-android-key FILE  Make a new upload key for Google Play in FILE and quit\n"
           "      --android-key-owner NAME     whose key it is (required); the password is read from\n"
           "                                   ROLO_ANDROID_KEYSTORE_PASSWORD\n"
           "  --no-project        Start without opening the last project\n"
           "  --reset-layout      Start with the default panel layout\n"
           "  --play              Start playing the scene at once (tests)\n"
           "  --config-dir DIR    Keep preferences and layout in DIR instead of the\n"
           "                      per-user settings folder\n"
           "  --frames N          Quit automatically after N frames (used by tests)\n"
           "  --screenshot FILE   Save the last frame as a PNG (needs --frames)\n";
}

} // namespace rolo::editor
