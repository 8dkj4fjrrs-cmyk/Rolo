// Exporting for Android (editor/model/AndroidExport.hpp), with stand-ins for
// Google's tools and for Rolo's Android runtime: nothing here needs Android
// Studio. (The real thing is built and played by the Android workflow.)

#include "support/TempFolder.hpp"
#include "AndroidExport.hpp"

#include "rolo/assets/GamePack.hpp"
#include "rolo/assets/ImageDecode.hpp"
#include "rolo/assets/ImageEncode.hpp"
#include "rolo/core/Files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>

using namespace rolo;
using namespace rolo::editor;
namespace fs = std::filesystem;

namespace {

struct Setup {
    fs::path root = test::uniqueTempFolder("android-export");
    AndroidExportOptions options;

    Setup()
    {
        file("project/project.rolo", R"({"format": "rolo.project", "version": 1, "name": "My Game", "width": 720, "height": 1280})");
        file("project/scenes/main.lscene", "{}");
        file("project/scripts/hero.lua", "-- a script");
        file("project/.git/config", "not for players");
        file("licenses/README.md", "# Licenses");
        file("licenses/axmol.txt", "MIT");
        // A stand-in Android runtime: the files an export looks for.
        file("runtime/settings.gradle", "include ':app'");
        file("runtime/build.gradle", "// top");
        file("runtime/gradle/wrapper/gradle-wrapper.jar", "jar");
        file("runtime/gradle/wrapper/gradle-wrapper.properties", "distributionUrl=x");
        file("runtime/app/build.gradle", "// app");
        file("runtime/app/src/main/AndroidManifest.xml", "<manifest/>");
        file("runtime/app/src/main/jniLibs/arm64-v8a/librolo_player.so", "elf");
        file("runtime/app/src/main/assets/axslc/sprite_fs", "shader");
        file("runtime/app/src/main/res/values/strings.xml", "<resources><string name=\"app_name\">Rolo Player</string></resources>");
        file("runtime/app/src/main/res/mipmap-mdpi/ic_launcher.png", "rolo's icon");
        options.project = root / "project";
        options.output = root / "exports";
        options.gameName = "Tom & Jerry's \"Game\"";
        options.runtime = root / "runtime";
        options.licenses = root / "licenses";
        options.version = "0.9.1";
        options.android.package = "com.example.mygame";
        options.android.versionCode = 3;
        options.android.versionName = "1.2";
    }
    ~Setup()
    {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    void file(const std::string& relative, const std::string& text) const
    {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative, std::ios::binary) << text;
    }
    std::string read(const fs::path& path) const { return readTextFile(path).value_or("<missing>"); }
};

EnvironmentLookup environment(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](const char* name) -> std::optional<std::string> {
        const auto found = values.find(name);
        return found == values.end() ? std::nullopt : std::optional<std::string>(found->second);
    };
}

} // namespace

TEST_CASE("Android: the SDK and Java are found where Android Studio puts them", "[android][export]")
{
    Setup s;
    const std::string home = (s.root / "home").string();
    s.file("home/Library/Android/sdk/platforms/android-36/android.jar", "");
    s.file("home/Android/Sdk/platform-tools/adb", "");
    s.file("local/Android/Sdk/build-tools/36.0.0/aapt2", "");
    s.file("elsewhere/sdk/platforms/android-35/android.jar", "");
    s.file("jdk/bin/java", "");
    s.file("jdk/bin/java.exe", "");

    CHECK(isAndroidSdk(s.root / "home/Library/Android/sdk"));
    CHECK_FALSE(isAndroidSdk(s.root / "home"));
    CHECK_FALSE(isAndroidSdk(s.root / "nothing-here"));
    CHECK(isJavaHome(s.root / "jdk", HostOs::Linux));
    CHECK(isJavaHome(s.root / "jdk", HostOs::Windows));
    CHECK_FALSE(isJavaHome(s.root / "home", HostOs::MacOS));
    CHECK(javaProgram("J", HostOs::Windows, "keytool") == fs::path("J") / "bin" / "keytool.exe");
    CHECK(javaProgram("J", HostOs::MacOS, "java") == fs::path("J") / "bin" / "java");

    const auto env = environment({{"HOME", home}, {"LOCALAPPDATA", (s.root / "local").string()},
                                  {"JAVA_HOME", (s.root / "jdk").string()}});
    CHECK(findAndroidTools({}, {}, HostOs::MacOS, env).sdk == s.root / "home/Library/Android/sdk");
    CHECK(findAndroidTools({}, {}, HostOs::Linux, env).sdk == s.root / "home/Android/Sdk");
    CHECK(findAndroidTools({}, {}, HostOs::Windows, env).sdk == s.root / "local/Android/Sdk");
    CHECK(findAndroidTools({}, {}, HostOs::Linux, env).javaHome == s.root / "jdk");   // JAVA_HOME, with no Android Studio

    // The maker's own choice comes first; a choice that is not an SDK is passed over.
    CHECK(findAndroidTools(s.root / "elsewhere/sdk", {}, HostOs::MacOS, env).sdk == s.root / "elsewhere/sdk");
    CHECK(findAndroidTools(s.root / "nothing-here", {}, HostOs::MacOS, env).sdk == s.root / "home/Library/Android/sdk");
    // ANDROID_HOME comes before the usual place.
    const auto withVariable = environment({{"HOME", home}, {"ANDROID_HOME", (s.root / "elsewhere/sdk").string()}});
    CHECK(findAndroidTools({}, {}, HostOs::Linux, withVariable).sdk == s.root / "elsewhere/sdk");
    // Nothing anywhere: empty, not a guess.
    const AndroidTools none = findAndroidTools({}, {}, HostOs::Linux, environment({{"HOME", (s.root / "nobody").string()}}));
    CHECK(none.sdk.empty());
}

TEST_CASE("Android: a Java's version is read from what it prints", "[android][export]")
{
    CHECK(javaMajorVersion("openjdk version \"21.0.3\" 2024-04-16\nOpenJDK Runtime Environment") == 21);
    CHECK(javaMajorVersion("openjdk version \"17.0.11\" 2024-04-16 LTS") == 17);
    CHECK(javaMajorVersion("java version \"1.8.0_292\"") == 8);
    CHECK(javaMajorVersion("openjdk version \"25\" 2025-09-16") == 25);
    CHECK(javaMajorVersion("command not found") == 0);
    CHECK(javaMajorVersion("") == 0);
}

TEST_CASE("Android: missing or old tools are named in plain words", "[android][export]")
{
    Setup s;
    s.file("sdk/platforms/android-36/android.jar", "");
    s.file("old-sdk/platforms/android-33/android.jar", "");
    s.file("licensed-sdk/platform-tools/adb", "");
    s.file("licensed-sdk/licenses/android-sdk-license", "hash");
    s.file("jdk/bin/java", "");
    const std::string java21 = "openjdk version \"21.0.3\"";
    const AndroidTools good{s.root / "sdk", s.root / "jdk"};

    CHECK(checkAndroidTools(good, HostOs::Linux, java21, s.options.runtime).ok());
    CHECK(checkAndroidTools(good, HostOs::Linux, java21, s.options.runtime).notes.empty());

    // Nothing installed.
    const AndroidToolsReport nothing = checkAndroidTools({}, HostOs::Linux, "", s.options.runtime);
    REQUIRE(nothing.problems.size() == 2);
    CHECK(nothing.problems[0].find("Android SDK was not found") != std::string::npos);
    CHECK(nothing.problems[0].find("Android Studio") != std::string::npos);
    CHECK(nothing.problems[1].find("Java was not found") != std::string::npos);

    // An SDK without the platform games are built with: a problem, unless Gradle may download it.
    const AndroidToolsReport old = checkAndroidTools({s.root / "old-sdk", s.root / "jdk"}, HostOs::Linux, java21, s.options.runtime);
    REQUIRE(old.problems.size() == 1);
    CHECK(old.problems[0].find("Android SDK Platform 36") != std::string::npos);
    CHECK(old.problems[0].find("SDK Manager") != std::string::npos);
    const AndroidToolsReport licensed =
        checkAndroidTools({s.root / "licensed-sdk", s.root / "jdk"}, HostOs::Linux, java21, s.options.runtime);
    CHECK(licensed.ok());
    REQUIRE(licensed.notes.size() == 1);
    CHECK(licensed.notes[0].find("download") != std::string::npos);

    // A folder that is not an SDK; a Java that is too old, or does not run.
    CHECK(checkAndroidTools({s.root / "jdk", s.root / "jdk"}, HostOs::Linux, java21, s.options.runtime).problems[0].find(
              "is not an Android SDK folder")
          != std::string::npos);
    const AndroidToolsReport java11 = checkAndroidTools(good, HostOs::Linux, "openjdk version \"11.0.2\"", s.options.runtime);
    REQUIRE(java11.problems.size() == 1);
    CHECK(java11.problems[0].find("version 11") != std::string::npos);
    CHECK(java11.problems[0].find("17 or newer") != std::string::npos);
    CHECK(checkAndroidTools(good, HostOs::Linux, "", s.options.runtime).problems[0].find("could not be run") != std::string::npos);

    // Rolo's own part missing.
    const AndroidToolsReport noRuntime = checkAndroidTools(good, HostOs::Linux, java21, s.root / "no-runtime");
    REQUIRE(noRuntime.problems.size() == 1);
    CHECK(noRuntime.problems[0].find("Android runtime was not found") != std::string::npos);
    CHECK_FALSE(isAndroidRuntime(s.root / "licenses"));
    CHECK(isAndroidRuntime(s.options.runtime));
}

TEST_CASE("Android: package names are suggested, and the orientation follows the game's size", "[android][export]")
{
    CHECK(suggestedAndroidPackage("My Game") == "com.yourname.mygame");
    CHECK(suggestedAndroidPackage("The Lantern Keeper!") == "com.yourname.thelanternkeeper");
    CHECK(suggestedAndroidPackage("2048") == "com.yourname.game2048");
    CHECK(suggestedAndroidPackage("???") == "com.yourname.game");
    CHECK(suggestedAndroidPackage("New") == "com.yourname.game");   // "new" is a word of Java
    CHECK(isValidAndroidPackage(suggestedAndroidPackage("Été à Paris")));

    AndroidSettings settings;
    CHECK(androidScreenOrientation(settings, 1280, 720) == "sensorLandscape");
    CHECK(androidScreenOrientation(settings, 720, 1280) == "sensorPortrait");
    CHECK(androidScreenOrientation(settings, 800, 800) == "sensorLandscape");
    settings.orientation = "portrait";
    CHECK(androidScreenOrientation(settings, 1280, 720) == "sensorPortrait");
    settings.orientation = "landscape";
    CHECK(androidScreenOrientation(settings, 720, 1280) == "sensorLandscape");
}

TEST_CASE("Android: icons are scaled, keeping their shape", "[android][export]")
{
    // A square of one colour stays that colour at any size.
    DecodedImage red;
    red.width = 64;
    red.height = 64;
    red.rgba.assign(64 * 64 * 4, 255);
    for (std::size_t i = 0; i < red.rgba.size(); i += 4) {
        red.rgba[i + 1] = 0;
        red.rgba[i + 2] = 0;
    }
    for (const int size : {48, 64, 192}) {
        const DecodedImage icon = resizedIcon(red, size);
        REQUIRE(icon.width == size);
        REQUIRE(icon.rgba.size() == static_cast<std::size_t>(size * size * 4));
        const std::size_t middle = static_cast<std::size_t>((size / 2) * size + size / 2) * 4;
        CHECK(icon.rgba[middle] == 255);
        CHECK(icon.rgba[middle + 1] == 0);
        CHECK(icon.rgba[middle + 3] == 255);
        CHECK(icon.rgba[3] == 255);   // the corner too
    }

    // Half black, half white, made a quarter of the size: the halves stay apart.
    DecodedImage halves;
    halves.width = 8;
    halves.height = 8;
    halves.rgba.assign(8 * 8 * 4, 255);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 4; ++x) {
            for (int c = 0; c < 3; ++c) {
                halves.rgba[static_cast<std::size_t>((y * 8 + x) * 4 + c)] = 0;
            }
        }
    }
    const DecodedImage small = resizedIcon(halves, 2);
    CHECK(small.rgba[0] == 0);
    CHECK(small.rgba[4] == 255);

    // A wide picture is fitted inside the square, with see-through bands above and below.
    DecodedImage wide;
    wide.width = 40;
    wide.height = 20;
    wide.rgba.assign(40 * 20 * 4, 200);
    const DecodedImage fitted = resizedIcon(wide, 40);
    CHECK(fitted.rgba[3] == 0);                                           // top-left: outside the picture
    CHECK(fitted.rgba[static_cast<std::size_t>(20 * 40 + 20) * 4 + 3] == 200);   // the middle: the picture
    CHECK(fitted.rgba[static_cast<std::size_t>(39 * 40 + 20) * 4 + 3] == 0);     // bottom: outside

    // Nothing sensible in: an empty icon, not a crash.
    CHECK(resizedIcon(DecodedImage{}, 48).rgba.size() == 48u * 48u * 4u);
}

TEST_CASE("Android: an export writes the Gradle project with the game inside", "[android][export]")
{
    Setup s;
    const AndroidProject made = writeAndroidProject(s.options);
    INFO(made.error);
    REQUIRE(made.ok);
    CHECK(made.folder == s.root / "exports" / "Tom  Jerrys Game Android");
    CHECK(made.gradleProject == made.folder / "project");
    const fs::path main = made.gradleProject / "app/src/main";

    // The runtime, as it was.
    CHECK(s.read(made.gradleProject / "settings.gradle") == "include ':app'");
    CHECK(s.read(main / "jniLibs/arm64-v8a/librolo_player.so") == "elf");
    CHECK(s.read(main / "assets/axslc/sprite_fs") == "shader");
    CHECK(s.read(made.gradleProject / "gradle/wrapper/gradle-wrapper.jar") == "jar");

    // The game: one pack, without hidden files.
    const auto index = readGamePackIndex(main / "assets/game.pack");
    REQUIRE(index);
    CHECK(index->name == "Tom & Jerry's \"Game\"");
    REQUIRE(index->files.size() == 3);
    CHECK(index->files[0].path == "project.rolo");
    CHECK(index->files[2].path == "scripts/hero.lua");
    CHECK(extractGamePack(main / "assets/game.pack", s.root / "unpacked"));
    CHECK(s.read(s.root / "unpacked/scripts/hero.lua") == "-- a script");

    // License texts, the app's name (made safe for Android's resource files), and its settings.
    CHECK(s.read(main / "assets/licenses/axmol.txt") == "MIT");
    const std::string strings = s.read(main / "res/values/strings.xml");
    CHECK(strings.find("<string name=\"app_name\">Tom &amp; Jerry\\'s \\\"Game\\\"</string>") != std::string::npos);
    const std::string game = s.read(made.gradleProject / "rolo-game.json");
    CHECK(game.find("\"roloPackage\": \"com.example.mygame\"") != std::string::npos);
    CHECK(game.find("\"roloVersionCode\": 3") != std::string::npos);
    CHECK(game.find("\"roloVersionName\": \"1.2\"") != std::string::npos);
    CHECK(game.find("\"roloOrientation\": \"sensorPortrait\"") != std::string::npos);   // the game is 720 x 1280
    // No icon chosen: Rolo's stays, and the maker is told. And com.example is not for Google Play.
    CHECK(s.read(main / "res/mipmap-mdpi/ic_launcher.png") == "rolo's icon");
    REQUIRE(made.notes.size() == 2);
    CHECK(made.notes[0].find("Rolo's icon") != std::string::npos);
    CHECK(made.notes[1].find("com.example") != std::string::npos);

    // A folder given relative to where the editor runs is made a full path
    // (Gradle is started somewhere else).
    // (Where the temporary folder is on another drive than the tests run
    // on, as on Windows build machines, there is no such relative path.)
    if (const fs::path fromHere = fs::relative(s.root / "exports", fs::current_path()); !fromHere.empty() && fromHere.is_relative()) {
        AndroidExportOptions relative = s.options;
        relative.gameName = "Relative";
        relative.output = fromHere;
        const AndroidProject there = writeAndroidProject(relative);
        REQUIRE(there.ok);
        CHECK(there.gradleProject.is_absolute());
        CHECK(fs::exists(there.gradleProject / "settings.gradle"));
    }

    // Exporting again replaces the earlier export...
    s.file("exports/Tom  Jerrys Game Android/left-over.txt", "old");
    REQUIRE(writeAndroidProject(s.options).ok);
    CHECK_FALSE(fs::exists(made.folder / "left-over.txt"));
    // ...but never a folder that is not one.
    s.options.gameName = "Other";
    s.file("exports/Other Android/holiday.jpg", "precious");
    const AndroidProject refused = writeAndroidProject(s.options);
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.find("not an earlier export") != std::string::npos);
    CHECK(s.read(s.root / "exports/Other Android/holiday.jpg") == "precious");
}

TEST_CASE("Android: an export's icon comes from the project, in five sizes", "[android][export]")
{
    Setup s;
    std::vector<std::uint8_t> pixels(96 * 96 * 4, 255);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 10;   // nearly no red: green and blue
    }
    fs::create_directories(s.root / "project/art");
    REQUIRE(writePngFile(s.root / "project/art/icon.png", pixels.data(), 96, 96, 4));
    s.options.android.icon = "art/icon.png";
    s.options.android.package = "com.studio.game";
    const AndroidProject made = writeAndroidProject(s.options);
    INFO(made.error);
    REQUIRE(made.ok);
    CHECK(made.notes.empty());
    const std::pair<const char*, int> sizes[] = {{"mdpi", 48}, {"hdpi", 72}, {"xhdpi", 96}, {"xxhdpi", 144}, {"xxxhdpi", 192}};
    for (const auto& [density, size] : sizes) {
        const auto icon = decodeImageFile(made.gradleProject / "app/src/main/res" / (std::string("mipmap-") + density) / "ic_launcher.png");
        REQUIRE(icon);
        CHECK(icon->width == size);
        CHECK(icon->height == size);
        CHECK(icon->rgba[0] == 10);
        CHECK(icon->rgba[1] == 255);
    }

    s.options.android.icon = "art/missing.png";
    CHECK(writeAndroidProject(s.options).error.find("is not in the project") != std::string::npos);
    s.file("project/art/not-a-picture.png", "text");
    s.options.android.icon = "art/not-a-picture.png";
    CHECK(writeAndroidProject(s.options).error.find("could not be read") != std::string::npos);
}

TEST_CASE("Android: an export refuses what cannot work, saying why", "[android][export]")
{
    Setup s;
    const auto errorWith = [&](const std::function<void(AndroidExportOptions&)>& change) {
        AndroidExportOptions o = s.options;
        change(o);
        const AndroidProject made = writeAndroidProject(o);
        CHECK_FALSE(made.ok);
        return made.error;
    };
    CHECK(errorWith([](auto& o) { o.android.package = "My Game"; }).find("package name") != std::string::npos);
    CHECK(errorWith([](auto& o) { o.android.package.clear(); }).find("package name") != std::string::npos);
    CHECK(errorWith([](auto& o) { o.android.versionCode = 0; }).find("version code") != std::string::npos);
    CHECK(errorWith([&](auto& o) { o.runtime = s.root / "licenses"; }).find("Android runtime was not found") != std::string::npos);
    CHECK(errorWith([&](auto& o) { o.licenses = s.root / "runtime"; }).find("license texts") != std::string::npos);
    CHECK(errorWith([&](auto& o) { o.project = s.root / "licenses"; }).empty() == false);
    CHECK(errorWith([](auto& o) { o.output.clear(); }).find("Choose a folder") != std::string::npos);
    CHECK(errorWith([&](auto& o) { o.output = s.root / "project" / "out"; }).find("outside the project") != std::string::npos);
}

TEST_CASE("Android: the commands for Google's tools", "[android][export]")
{
    const AndroidTools tools{"/sdk", "/jdk"};
    AndroidSigning key;
    key.keystore = "/keys/upload.jks";
    key.password = "s3cret pass";

    const ProcessOptions build = gradleCommand("/out/project", tools, HostOs::Linux, {":app:assembleDebug", ":app:bundleRelease"}, key);
    CHECK(build.program == fs::path("/jdk/bin/java"));
    CHECK(build.workingFolder == fs::path("/out/project"));
    const auto has = [](const ProcessOptions& command, const std::string& argument) {
        return std::find(command.arguments.begin(), command.arguments.end(), argument) != command.arguments.end();
    };
    CHECK(has(build, "org.gradle.wrapper.GradleWrapperMain"));
    CHECK(has(build, (fs::path("/out/project") / "gradle" / "wrapper" / "gradle-wrapper.jar").string()));
    CHECK(has(build, "--no-daemon"));
    CHECK(build.arguments.back() == ":app:bundleRelease");
    std::map<std::string, std::string> env(build.environment.begin(), build.environment.end());
    CHECK(env["ANDROID_HOME"] == fs::path("/sdk").string());
    CHECK(env["JAVA_HOME"] == fs::path("/jdk").string());
    CHECK(env[kKeystorePasswordVariable] == "s3cret pass");
    CHECK(env[kKeyAliasVariable] == "upload");
    // The password travels in the environment only: never as an argument.
    for (const std::string& argument : build.arguments) {
        CHECK(argument.find("s3cret") == std::string::npos);
    }
    // Without a key, nothing about signing is passed on.
    const ProcessOptions plain = gradleCommand("/out/project", tools, HostOs::Windows, {":app:assembleDebug"});
    CHECK(plain.program == fs::path("/jdk") / "bin" / "java.exe");
    CHECK(plain.environment.size() == 3);

    const ProcessOptions keytool = newKeyCommand("/jdk", HostOs::Linux, key, "Ada, Countess of \"Lovelace\"");
    CHECK(keytool.program == fs::path("/jdk/bin/keytool"));
    CHECK(has(keytool, "-genkeypair"));
    CHECK(has(keytool, "-storepass:env"));
    CHECK(has(keytool, "CN=Ada\\, Countess of \\\"Lovelace\\\""));
    for (const std::string& argument : keytool.arguments) {
        CHECK(argument.find("s3cret") == std::string::npos);
    }
    REQUIRE(keytool.environment.size() == 1);
    CHECK(keytool.environment[0].second == "s3cret pass");
}

TEST_CASE("Android: a new key's details are checked first", "[android][export]")
{
    Setup s;
    AndroidSigning key;
    CHECK(newKeyProblem(key, "Me").find("where to keep") != std::string::npos);
    key.keystore = s.root / "keys" / "upload.jks";
    key.password = "12345";
    CHECK(newKeyProblem(key, "Me").find("6 characters") != std::string::npos);
    key.password = "123456";
    CHECK(newKeyProblem(key, "").find("whose key") != std::string::npos);
    CHECK(newKeyProblem(key, "Me").empty());
    // An existing key file is never replaced.
    s.file("keys/upload.jks", "a key");
    CHECK(newKeyProblem(key, "Me").find("never replaced") != std::string::npos);
}

TEST_CASE("Android: package names Google Play would refuse, or that are nobody's", "[android][export]")
{
    CHECK(playPackageProblem("com.studio.game").empty());
    CHECK(playPackageProblem("io.itch.ada.platformer").empty());
    CHECK(playPackageProblem("com.example.game").find("com.example") != std::string::npos);
    CHECK(playPackageProblem("com.yourname.game").find("yourname") != std::string::npos);
}

TEST_CASE("Android: Gradle's known failures are explained", "[android][export]")
{
    CHECK(explainGradleFailure({"> Task :app:compileDebugJavaWithJavac", "BUILD FAILED in 3s"}).empty());
    CHECK(explainGradleFailure({"   > Keystore was tampered with, or password was incorrect"}).find("password is wrong") != std::string::npos);
    CHECK(explainGradleFailure({"SDK location not found. Define a valid SDK location with an ANDROID_HOME"}).find("Android SDK") != std::string::npos);
    CHECK(explainGradleFailure({"> Failed to install the following Android SDK packages as some licences have not been accepted."})
              .find("SDK Manager") != std::string::npos);
    CHECK(explainGradleFailure({"java.net.UnknownHostException: services.gradle.org"}).find("internet") != std::string::npos);
    CHECK(explainGradleFailure({"java.io.IOException: No space left on device"}).find("disk is full") != std::string::npos);
    CHECK(explainGradleFailure({"Unsupported class file major version 69"}).find("Java") != std::string::npos);
}

#if !defined(_WIN32)
// The whole export, with a stand-in for Java that does what Gradle would:
// leave an .apk (and an .aab) in the project's build folder. (A shell
// script, so not on Windows; the real tools run in the Android workflow.)
TEST_CASE("Android: the whole export, with a stand-in for Google's tools", "[android][export]")
{
    Setup s;
    const auto fakeJava = [&](const std::string& script) {
        s.file("jdk/bin/java", "#!/bin/sh\n" + script);
        fs::permissions(s.root / "jdk/bin/java", fs::perms::owner_all);
    };
    s.options.tools = {s.root / "sdk", s.root / "jdk"};
    s.options.gameName = "My Game";
    std::vector<std::string> said;
    const auto say = [&](std::string_view line) { said.emplace_back(line); };
    const auto saidAny = [&](const std::string& part) {
        return std::any_of(said.begin(), said.end(), [&](const std::string& line) { return line.find(part) != std::string::npos; });
    };

    fakeJava("echo \"building in $PWD with $ANDROID_HOME\"\n"
             "mkdir -p app/build/outputs/apk/debug app/build/outputs/apk/release app/build/outputs/bundle/release\n"
             "case \"$*\" in *assembleDebug*) echo apk > app/build/outputs/apk/debug/app-debug.apk;; esac\n"
             "case \"$*\" in *assembleRelease*) echo \"apk signed with $ROLO_ANDROID_KEY_ALIAS\" > app/build/outputs/apk/release/app-release.apk;; esac\n"
             "if [ -n \"$ROLO_ANDROID_KEYSTORE\" ]; then echo \"signed with $ROLO_ANDROID_KEY_ALIAS\" > app/build/outputs/bundle/release/app-release.aab; fi\n"
             "echo 'BUILD SUCCESSFUL'\n");
    const AndroidExportResult tried = exportAndroidGame(s.options, {}, say);
    INFO(tried.message);
    REQUIRE(tried.ok);
    CHECK(tried.apk == s.root / "exports/My Game Android/My Game.apk");
    CHECK(s.read(tried.apk) == "apk\n");
    CHECK_FALSE(tried.apkSignedWithOwnKey);
    CHECK(tried.aab.empty());
    CHECK_FALSE(fs::exists(s.root / "exports/My Game Android/My Game.aab"));
    CHECK(tried.message.find("Exported My Game.apk") != std::string::npos);
    CHECK(saidAny("BUILD SUCCESSFUL"));
    CHECK(saidAny((s.root / "sdk").string()));
    CHECK(s.read(tried.project.folder / "README.txt").find("com.example.mygame") != std::string::npos);

    CHECK(tried.storeIcon.empty());   // the project has no icon of its own

    // With an upload key: the file for Google Play too. Not with a package
    // name that is nobody's: Google Play keeps it for ever.
    s.file("keys/upload.jks", "key");
    AndroidSigning key;
    key.keystore = s.root / "keys/upload.jks";
    key.password = "secret1";
    const AndroidExportResult nobodys = exportAndroidGame(s.options, key, say);
    CHECK_FALSE(nobodys.ok);
    CHECK(nobodys.message.find("com.example") != std::string::npos);
    s.options.android.package = "com.studio.mygame";
    // ...and with an icon of the project's, the store's 512 x 512 one is made as well.
    {
        std::vector<std::uint8_t> pixels(64 * 64 * 4, 255);
        REQUIRE(writePngFile(s.root / "project/icon.png", pixels.data(), 64, 64, 4));
        s.options.android.icon = "icon.png";
    }
    const AndroidExportResult published = exportAndroidGame(s.options, key, say);
    INFO(published.message);
    REQUIRE(published.ok);
    CHECK(s.read(published.aab) == "signed with upload\n");
    // ...and the .apk is now the one to share: signed with the same key, not the debug one.
    CHECK(published.apkSignedWithOwnKey);
    CHECK(s.read(published.apk) == "apk signed with upload\n");
    CHECK(s.read(published.project.folder / "README.txt").find("signed with your own key") != std::string::npos);
    REQUIRE(published.storeIcon == published.project.folder / "Google Play icon (512).png");
    const auto storeIcon = decodeImageFile(published.storeIcon);
    REQUIRE(storeIcon);
    CHECK(storeIcon->width == 512);
    CHECK(storeIcon->height == 512);
    CHECK(published.message.find("for Google Play") != std::string::npos);
    CHECK(s.read(published.project.folder / "README.txt").find("Upload this file in the Play Console") != std::string::npos);
    // The password is nowhere in the export.
    for (const auto& entry : fs::recursive_directory_iterator(published.project.folder)) {
        if (entry.is_regular_file()) {
            CHECK(s.read(entry.path()).find("secret1") == std::string::npos);
        }
    }

    // A key file that is not there, or no password: said before anything is built.
    AndroidSigning missing = key;
    missing.keystore = s.root / "keys/none.jks";
    CHECK(exportAndroidGame(s.options, missing, say).message.find("was not found") != std::string::npos);
    AndroidSigning noPassword = key;
    noPassword.password.clear();
    CHECK(exportAndroidGame(s.options, noPassword, say).message.find("password") != std::string::npos);

    // Gradle fails: a known reason is put in plain words...
    fakeJava("echo '   > Keystore was tampered with, or password was incorrect'\necho 'BUILD FAILED in 2s'\nexit 1\n");
    const AndroidExportResult wrong = exportAndroidGame(s.options, key, say);
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.message.find("password is wrong") != std::string::npos);
    // ...and an unknown one points at Gradle's own words.
    fakeJava("echo 'error: something new'\necho 'BUILD FAILED in 2s'\nexit 1\n");
    CHECK(exportAndroidGame(s.options, {}, say).message.find("BUILD FAILED") != std::string::npos);
    // Java missing altogether.
    fs::remove(s.root / "jdk/bin/java");
    CHECK(exportAndroidGame(s.options, {}, say).message.find("Java could not be started") != std::string::npos);
}
#endif
