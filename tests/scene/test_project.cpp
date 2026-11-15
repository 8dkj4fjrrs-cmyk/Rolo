#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace rolo;

namespace {
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};
} // namespace

TEST_CASE("A new project has a project file and a start scene", "[project]")
{
    TempDir tmp("rolo_project_test");
    const auto folder = tmp.path / "My Game";
    std::string error;
    const auto project = Project::create(folder, "My Game", &error);
    REQUIRE(project);
    CHECK(error.empty());
    CHECK(std::filesystem::exists(folder / "project.rolo"));
    CHECK(project->sceneFiles() == std::vector<std::string>{"scenes/main.lscene"});

    const auto reopened = Project::open(folder, &error);
    REQUIRE(reopened);
    CHECK(reopened->settings().name == "My Game");
    const auto scene = loadSceneFile(reopened->absolute(reopened->settings().startScene),
                                     ComponentRegistry::withBuiltins());
    REQUIRE(scene.ok());
    CHECK(scene.scene->name() == "Main");

    // Creating again in the same (now non-empty) folder is refused.
    CHECK_FALSE(Project::create(folder, "My Game", &error));
    CHECK(error.find("not empty") != std::string::npos);
}

TEST_CASE("Project paths are relative and stay inside the project", "[project]")
{
    TempDir tmp("rolo_project_paths");
    const auto project = Project::create(tmp.path / "P", "P");
    REQUIRE(project);
    CHECK(project->relative(project->folder() / "scenes" / "a.lscene") == "scenes/a.lscene");
    CHECK_FALSE(project->relative(tmp.path / "elsewhere.lscene"));

    CHECK(Project::isValidRelativePath("scenes/main.lscene"));
    CHECK_FALSE(Project::isValidRelativePath("/etc/passwd"));
    CHECK_FALSE(Project::isValidRelativePath("../outside.lscene"));
    CHECK_FALSE(Project::isValidRelativePath("scenes/../../x"));
    CHECK_FALSE(Project::isValidRelativePath("scenes\\main.lscene"));
    CHECK_FALSE(Project::isValidRelativePath("C:/x"));
    CHECK_FALSE(Project::isValidRelativePath(""));
}

TEST_CASE("Names that break on some OS are refused", "[project]")
{
    CHECK(Project::isValidName("My Game 2"));
    CHECK_FALSE(Project::isValidName(""));
    CHECK_FALSE(Project::isValidName("a/b"));
    CHECK_FALSE(Project::isValidName("what?"));
    CHECK_FALSE(Project::isValidName(" padded"));
    CHECK_FALSE(Project::isValidName(".hidden"));
}

TEST_CASE("Project files are validated", "[project]")
{
    std::string error;
    CHECK(Project::parseSettings(Project::serializeSettings({}), &error));
    CHECK_FALSE(Project::parseSettings("{}", &error));
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 5})", &error));
    CHECK(error.find("newer version") != std::string::npos);
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 1, "startScene": "../x"})", &error));
    CHECK_FALSE(Project::open(std::filesystem::temp_directory_path() / "no_such_rolo_project", &error));
    CHECK(error.find("project.rolo") != std::string::npos);
}

TEST_CASE("A project's game options are read and written only when changed", "[project]")
{
    const ProjectSettings defaults;
    CHECK(defaults.escapeQuits);
    CHECK(defaults.quickSaveKeys);
    // Default options are not written, so existing project files stay as they were.
    const std::string plain = Project::serializeSettings(defaults);
    CHECK(plain.find("escapeQuits") == std::string::npos);
    CHECK(plain.find("quickSaveKeys") == std::string::npos);

    ProjectSettings changed;
    changed.escapeQuits = false;
    changed.quickSaveKeys = false;
    const auto back = Project::parseSettings(Project::serializeSettings(changed));
    REQUIRE(back);
    CHECK_FALSE(back->escapeQuits);
    CHECK_FALSE(back->quickSaveKeys);

    std::string error;
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 1, "escapeQuits": "no"})", &error));
    CHECK(error.find("escapeQuits") != std::string::npos);
}

TEST_CASE("A project's touch choices are read and written only when changed", "[project][touch]")
{
    const ProjectSettings defaults;
    CHECK(defaults.touchControls);
    CHECK(defaults.touchButtons.empty());
    const std::string plain = Project::serializeSettings(defaults);
    CHECK(plain.find("touch") == std::string::npos);

    ProjectSettings changed;
    changed.touchControls = false;
    changed.touchButtons = {"jump", "pause"};
    const auto back = Project::parseSettings(Project::serializeSettings(changed));
    REQUIRE(back);
    CHECK_FALSE(back->touchControls);
    CHECK(back->touchButtons == std::vector<std::string>{"jump", "pause"});

    std::string error;
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 1, "touchButtons": "jump"})", &error));
    CHECK(error.find("touchButtons") != std::string::npos);
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 1, "touchButtons": [1, 2]})", &error));
    CHECK_FALSE(Project::parseSettings(R"({"format": "rolo.project", "version": 1, "touchControls": 1})", &error));
}

TEST_CASE("A project's game size and screen fit are read and written only when changed", "[project][screen]")
{
    const ProjectSettings defaults;
    CHECK(defaults.width == 1280);
    CHECK(defaults.height == 720);
    CHECK(defaults.screenFit == ScreenFit::Bars);
    const std::string plain = Project::serializeSettings(defaults);
    CHECK(plain.find("width") == std::string::npos);
    CHECK(plain.find("screenFit") == std::string::npos);

    ProjectSettings upright;
    upright.width = 720;
    upright.height = 1280;
    upright.screenFit = ScreenFit::Expand;
    const auto back = Project::parseSettings(Project::serializeSettings(upright));
    REQUIRE(back);
    CHECK(back->width == 720);
    CHECK(back->height == 1280);
    CHECK(back->screenFit == ScreenFit::Expand);

    std::string error;
    for (const char* bad : {R"("width": 20)", R"("height": 99999)", R"("width": "wide")", R"("width": 12.5)",
                            R"("screenFit": "stretch")", R"("screenFit": true)"}) {
        INFO(bad);
        CHECK_FALSE(Project::parseSettings(std::string(R"({"format": "rolo.project", "version": 1, )") + bad + "}", &error));
    }
    CHECK(error.find("screenFit") != std::string::npos);
}

TEST_CASE("A project's Android settings are read and written only when set", "[project][android]")
{
    ProjectSettings settings;
    CHECK(Project::serializeSettings(settings).find("android") == std::string::npos);

    settings.android.package = "com.example.platformer";
    settings.android.versionCode = 7;
    settings.android.versionName = "1.2";
    settings.android.orientation = "portrait";
    settings.android.icon = "art/icon.png";
    const auto back = Project::parseSettings(Project::serializeSettings(settings));
    REQUIRE(back);
    CHECK(back->android == settings.android);

    // A project file without the group has the defaults.
    const auto plain = Project::parseSettings(R"({"format": "rolo.project", "version": 1, "name": "G"})");
    REQUIRE(plain);
    CHECK(plain->android == AndroidSettings{});
    CHECK(plain->android.versionCode == 1);

    const auto with = [](const std::string& android) {
        std::string error;
        const auto parsed = Project::parseSettings(
            R"({"format": "rolo.project", "version": 1, "name": "G", "android": )" + android + "}", &error);
        return parsed ? std::string() : error;
    };
    CHECK(with(R"({"package": "com.example.game"})").empty());
    CHECK(with(R"({"package": "My Game"})").find("package name") != std::string::npos);
    CHECK(with(R"({"versionCode": 0})").find("versionCode") != std::string::npos);
    CHECK(with(R"({"orientation": "sideways"})").find("orientation") != std::string::npos);
    CHECK(with(R"({"icon": "../icon.png"})").find("icon") != std::string::npos);
    CHECK(with(R"("yes")").find("android") != std::string::npos);
}

TEST_CASE("Android package names are checked, with a reason", "[project][android]")
{
    CHECK(isValidAndroidPackage("com.example.game"));
    CHECK(isValidAndroidPackage("io.itch.my_name.Game2"));
    std::string why;
    CHECK_FALSE(isValidAndroidPackage("", &why));
    CHECK_FALSE(isValidAndroidPackage("game", &why));
    CHECK(why.find("two parts") != std::string::npos);
    CHECK_FALSE(isValidAndroidPackage("com.my game", &why));
    CHECK_FALSE(isValidAndroidPackage("com.my-game", &why));
    CHECK(why.find("hyphens") != std::string::npos);
    CHECK_FALSE(isValidAndroidPackage("com.2048.game", &why));
    CHECK(why.find("start with a letter") != std::string::npos);
    CHECK_FALSE(isValidAndroidPackage("com..game", &why));
    CHECK_FALSE(isValidAndroidPackage("com.game.", &why));
    CHECK_FALSE(isValidAndroidPackage("com.new.game", &why));
    CHECK(why.find("Java") != std::string::npos);
}

TEST_CASE("How much of the world a window shows", "[project][screen]")
{
    const Vec2 game{1280.0f, 720.0f};
    // Bars: always the game's size.
    CHECK(visibleSize(game, {2000.0f, 720.0f}, ScreenFit::Bars) == game);
    // Expand: a window of the game's shape shows the game's size...
    CHECK(visibleSize(game, {640.0f, 360.0f}, ScreenFit::Expand) == game);
    // ...a wider one more to the sides, a taller one more above and below. The game's size is always in view.
    CHECK(visibleSize(game, {1700.0f, 720.0f}, ScreenFit::Expand) == Vec2{1700.0f, 720.0f});
    CHECK(visibleSize(game, {844.0f, 390.0f}, ScreenFit::Expand).y == 720.0f);
    CHECK(visibleSize(game, {844.0f, 390.0f}, ScreenFit::Expand).x > 1550.0f);
    CHECK(visibleSize(game, {720.0f, 720.0f}, ScreenFit::Expand) == Vec2{1280.0f, 1280.0f});
    // Never more than three times the game's size (a very long window).
    CHECK(visibleSize(game, {10000.0f, 100.0f}, ScreenFit::Expand) == Vec2{3840.0f, 720.0f});
    // Nonsense in: the game's size out.
    CHECK(visibleSize(game, {0.0f, 100.0f}, ScreenFit::Expand) == game);

    CHECK(screenFitFromName("expand") == ScreenFit::Expand);
    CHECK(screenFitFromName(screenFitName(ScreenFit::Bars)) == ScreenFit::Bars);
    CHECK_FALSE(screenFitFromName("fill"));
}

TEST_CASE("Things fixed to the camera follow the screen's edges", "[project][screen]")
{
    const Vec2 game{1280.0f, 720.0f};
    const Vec2 wide{1700.0f, 720.0f};   // 210 more on each side
    // A score in the top-left corner stays as far from the left edge as it was made.
    CHECK(pinToScreen({-616.0f, 340.0f}, {}, game, wide).offset == Vec2{-210.0f, 0.0f});
    // Help in the top-right corner goes right; a dialogue box at the bottom middle stays put.
    CHECK(pinToScreen({616.0f, 340.0f}, {}, game, wide).offset == Vec2{210.0f, 0.0f});
    CHECK(pinToScreen({0.0f, -260.0f}, {1040.0f, 180.0f}, game, wide).offset == Vec2{0.0f, 0.0f});
    CHECK(pinToScreen({0.0f, -260.0f}, {1040.0f, 180.0f}, game, wide).scale == Vec2{1.0f, 1.0f});
    // On a taller screen, things at the top and bottom move with those edges.
    const Vec2 tall{1280.0f, 1000.0f};
    CHECK(pinToScreen({-616.0f, 340.0f}, {}, game, tall).offset == Vec2{0.0f, 140.0f});
    CHECK(pinToScreen({0.0f, -260.0f}, {}, game, tall).offset == Vec2{0.0f, -140.0f});
    // A veil over the whole game (night, a fade) is stretched over the whole screen.
    const ScreenPin veil = pinToScreen({0.0f, 0.0f}, {1280.0f, 720.0f}, game, wide);
    CHECK(veil.offset == Vec2{0.0f, 0.0f});
    CHECK(veil.scale.x == Catch::Approx(1700.0f / 1280.0f));
    CHECK(veil.scale.y == 1.0f);
    // The same shape of screen as the game: nothing moves.
    CHECK(pinToScreen({-616.0f, 340.0f}, {}, game, game).offset == Vec2{0.0f, 0.0f});
}

TEST_CASE("A camera notch: things at that edge stay clear of it", "[project][screen]")
{
    const Vec2 game{1280.0f, 720.0f};
    const Vec2 wide{1600.0f, 720.0f};   // 160 more on each side
    // A phone of 2400 x 1080 pixels with a notch of 120 pixels on its left: 80 of the game's points.
    const ScreenInsets notch = insetsInView({120.0f, 0.0f, 0.0f, 0.0f}, {2400.0f, 1080.0f}, wide);
    CHECK(notch == ScreenInsets{80.0f, 0.0f, 0.0f, 0.0f});
    CHECK(notch.any());
    CHECK_FALSE(ScreenInsets{}.any());

    // The score at the top-left goes to the left edge, but not under the notch;
    // the help at the top-right is not moved by a notch on the other side.
    CHECK(pinToScreen({-616.0f, 340.0f}, {}, game, wide, notch).offset == Vec2{-80.0f, 0.0f});
    CHECK(pinToScreen({616.0f, 340.0f}, {}, game, wide, notch).offset == Vec2{160.0f, 0.0f});
    // What is in the middle stays there, and a veil over everything still covers everything.
    CHECK(pinToScreen({0.0f, -260.0f}, {1040.0f, 180.0f}, game, wide, notch).offset == Vec2{0.0f, 0.0f});
    const ScreenPin veil = pinToScreen({0.0f, 0.0f}, {1280.0f, 720.0f}, game, wide, notch);
    CHECK(veil.scale == Vec2{1.25f, 1.0f});
    CHECK(veil.offset == Vec2{0.0f, 0.0f});
    // Held upright with the notch at the top: 60 points down from the top edge.
    const ScreenInsets top = insetsInView({0.0f, 0.0f, 0.0f, 90.0f}, {1080.0f, 2400.0f}, {720.0f, 1600.0f});
    CHECK(top == ScreenInsets{0.0f, 0.0f, 0.0f, 60.0f});
    CHECK(pinToScreen({-300.0f, 600.0f}, {}, {720.0f, 1280.0f}, {720.0f, 1600.0f}, top).offset == Vec2{0.0f, 100.0f});

    // With bars beside the game (it is shown whole), a notch under a bar needs nothing...
    CHECK(insetsInView({120.0f, 0.0f, 0.0f, 0.0f}, {2400.0f, 1080.0f}, game) == ScreenInsets{});
    // ...and one wider than the bar only what sticks out: (300 - 240) pixels at 1.5 a point.
    CHECK(insetsInView({300.0f, 0.0f, 0.0f, 0.0f}, {2400.0f, 1080.0f}, game) == ScreenInsets{40.0f, 0.0f, 0.0f, 0.0f});
    CHECK(insetsInView({120.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, game) == ScreenInsets{});
}
