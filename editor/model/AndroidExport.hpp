#pragma once

// Exporting a project as an Android app (ADR 0028, step 2).
//
// Android apps are built by Google's tools, as in Godot and Cocos Creator:
// the maker installs Android Studio once (it brings the Android SDK and a
// Java), and Rolo does the rest. An export is
//
//   My Game Android/
//     My Game.apk        the app, to try on a phone or an emulator
//     My Game.aab        the app for Google Play, signed with the maker's
//                        upload key (only when a key was given)
//     project/           the Gradle project both were built from (it opens
//                        in Android Studio): a copy of Rolo's Android
//                        runtime, which has the player built in, plus
//       rolo-game.json     the app's name, package, version and orientation
//       app/src/main/assets/game.pack   the project's files as one game pack
//       app/src/main/assets/licenses/   license texts
//       app/src/main/res/...            the app's name and icon
//     README.txt
//
// Writing the project (writeAndroidProject) and building it (the commands
// for Java, Gradle and keytool) are separate, and nothing here opens a
// window or logs: the editor runs the building on a worker thread, and
// `rolo_editor --export-android` in the terminal. See docs/exporting.md.

#include "EditorPaths.hpp"
#include "Process.hpp"

#include "rolo/assets/ImageDecode.hpp"
#include "rolo/core/Engine.hpp"
#include "rolo/scene/Project.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo::editor {

// --- Google's tools on this computer -------------------------------------------------------------

struct AndroidTools {
    std::filesystem::path sdk;        // the Android SDK's folder
    std::filesystem::path javaHome;   // a Java's folder: bin/java and bin/keytool are in it
};

/// True if `folder` looks like an Android SDK (it has the folders the SDK's parts are installed in).
bool isAndroidSdk(const std::filesystem::path& folder);
/// True if `folder` is a Java: it has bin/java (bin\java.exe on Windows).
bool isJavaHome(const std::filesystem::path& folder, HostOs os);
/// A program of a Java, e.g. "java" or "keytool".
std::filesystem::path javaProgram(const std::filesystem::path& javaHome, HostOs os, std::string_view name);

/// Where the SDK is looked for, in order: where ANDROID_HOME (or the older
/// ANDROID_SDK_ROOT) says, then where Android Studio puts it on this system.
std::vector<std::filesystem::path> androidSdkPlaces(HostOs os, const EnvironmentLookup& env);
/// Where Java is looked for: the one inside Android Studio first (it is the
/// version Google's tools are tested with), then where JAVA_HOME says.
std::vector<std::filesystem::path> javaHomePlaces(HostOs os, const EnvironmentLookup& env);

/// The tools to use: the folders the maker chose (if they are what they
/// should be), or else the first of the usual places that is. A member is
/// empty if nothing was found.
AndroidTools findAndroidTools(const std::filesystem::path& chosenSdk, const std::filesystem::path& chosenJava, HostOs os,
                              const EnvironmentLookup& env);

/// The first number of a Java's version, from what `java -version` prints:
/// 21 from `openjdk version "21.0.3"`, 8 from `java version "1.8.0_292"`. 0 if there is none.
int javaMajorVersion(std::string_view versionOutput);

/// The Java Google's build tools need at least.
inline constexpr int kMinimumJava = 17;
/// The Android version exported games are built for (SDK Platform 36 is Android 16).
inline constexpr int kAndroidPlatform = 36;

struct AndroidToolsReport {
    std::vector<std::string> problems;   // exporting cannot work until these are fixed
    std::vector<std::string> notes;      // worth knowing; exporting can go on
    bool ok() const noexcept { return problems.empty(); }
};

/// Says in plain words what is missing or too old. `javaVersionOutput` is
/// what `<javaHome>/bin/java -version` printed (empty: it was not run, or
/// printed nothing). `runtime` is Rolo's Android runtime folder.
AndroidToolsReport checkAndroidTools(const AndroidTools& tools, HostOs os, std::string_view javaVersionOutput,
                                     const std::filesystem::path& runtime);

/// The command that prints a Java's version.
ProcessOptions javaVersionCommand(const std::filesystem::path& javaHome, HostOs os);

// --- The app ---------------------------------------------------------------------------------

/// A package name to start from for a game: "com.yourname." and the game's
/// name in small letters and digits ("com.yourname.mygame").
std::string suggestedAndroidPackage(const std::string& gameName);

/// What Android is told about how the app is held: "sensorLandscape" or
/// "sensorPortrait" (either way up). From the setting, or else the game's
/// size: wider than tall is landscape.
std::string androidScreenOrientation(const AndroidSettings& settings, int gameWidth, int gameHeight);

/// `image` scaled to fit a `size` × `size` square, centred, with see-through
/// edges if it is not square.
DecodedImage resizedIcon(const DecodedImage& image, int size);

/// Writes the app's icon in Android's five sizes (res/mipmap-*/ic_launcher.png)
/// from a PNG file.
Status writeAndroidIcons(const std::filesystem::path& png, const std::filesystem::path& resFolder);

struct AndroidExportOptions {
    std::filesystem::path project;    // the project folder
    std::filesystem::path output;     // the folder the export's folder is created in
    std::string gameName;             // the app's name, and the export's folder and file names
    std::filesystem::path runtime;    // Rolo's Android runtime (RoloResources::androidRuntime)
    std::filesystem::path licenses;   // Rolo's licenses/ folder
    std::string version;              // Rolo's version, for README.txt
    AndroidSettings android;          // package, version, orientation, icon
    AndroidTools tools;
    HostOs os = currentOs();
};

struct AndroidProject {
    bool ok = false;
    std::string error;
    std::filesystem::path folder;          // "<output>/<Game> Android"
    std::filesystem::path gradleProject;   // its "project" folder
    std::vector<std::string> notes;        // things the maker should know (not errors)
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
};

/// True if `runtime` is a complete Android runtime (the Gradle project with the player's libraries in it).
bool isAndroidRuntime(const std::filesystem::path& runtime);

/// Writes the export's folder and the Gradle project in it. An earlier
/// export of the same game there is replaced; any other folder of that
/// name is left alone (an error).
AndroidProject writeAndroidProject(const AndroidExportOptions& options);

/// The maker's upload key for Google Play. The password is kept only in
/// memory and handed to Google's tools through their environment.
struct AndroidSigning {
    std::filesystem::path keystore;   // the key file (.jks); empty: no .aab is made
    std::string alias = "upload";     // the key's name inside the file
    std::string password;
    bool wanted() const noexcept { return !keystore.empty(); }
};

/// The names of the variables the password and key travel in.
inline constexpr const char* kKeystoreVariable = "ROLO_ANDROID_KEYSTORE";
inline constexpr const char* kKeystorePasswordVariable = "ROLO_ANDROID_KEYSTORE_PASSWORD";
inline constexpr const char* kKeyAliasVariable = "ROLO_ANDROID_KEY_ALIAS";

/// The command that builds the Gradle project: Gradle's small starter
/// (gradle/wrapper/gradle-wrapper.jar in the project) run with Java. It
/// fetches the right Gradle the first time. `tasks` are Gradle's names for
/// what to build, e.g. ":app:assembleDebug".
ProcessOptions gradleCommand(const std::filesystem::path& gradleProject, const AndroidTools& tools, HostOs os,
                             const std::vector<std::string>& tasks, const AndroidSigning& signing = {});

/// The command that makes a new upload key (Java's keytool), good for about
/// 27 years. `owner` is the maker's or studio's name, kept inside the key.
ProcessOptions newKeyCommand(const std::filesystem::path& javaHome, HostOs os, const AndroidSigning& key,
                             const std::string& owner);
/// What is wrong with a new key's details, or empty: the file must not exist yet, the password needs six characters...
std::string newKeyProblem(const AndroidSigning& key, const std::string& owner);
/// Makes a new upload key file with Java's keytool (checking the details
/// first). An existing file is never replaced.
Status createAndroidKey(const std::filesystem::path& javaHome, HostOs os, const AndroidSigning& key,
                        const std::string& owner);

/// Why a package name will not do for Google Play (which keeps it for
/// ever), or empty: names starting with com.example are refused there, and
/// Rolo's own suggestion, com.yourname..., is nobody's name.
std::string playPackageProblem(const std::string& package);

/// If Gradle failed for a reason Rolo knows, says so in plain words, with
/// what to do. Empty if the output shows nothing known.
std::string explainGradleFailure(const std::vector<std::string>& output);

struct AndroidExportResult {
    bool ok = false;
    bool cancelled = false;
    std::string message;              // what happened, for the maker
    AndroidProject project;
    std::filesystem::path apk;        // "<folder>/<Game>.apk"
    std::filesystem::path aab;        // "<folder>/<Game>.aab", if a key was given
    /// "<folder>/Google Play icon (512).png": the icon in the size Google
    /// Play's store listing asks for, if the project has an icon of its own.
    std::filesystem::path storeIcon;
};

/// The whole export: writes the project, builds it with Gradle (this takes
/// minutes, and the first time downloads Google's build tools), and puts
/// the .apk (and .aab) beside the project. `say` gets every line of
/// progress (Gradle's output too); it is called on the calling thread.
/// Blocks: the editor calls it from a worker thread.
AndroidExportResult exportAndroidGame(const AndroidExportOptions& options, const AndroidSigning& signing,
                                      const std::function<void(std::string_view)>& say,
                                      const std::atomic<bool>* cancel = nullptr);

} // namespace rolo::editor
