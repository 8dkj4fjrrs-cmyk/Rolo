#include "CommandLine.hpp"
#include "support/TempFolder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using rolo::player::bundledGameFolder;
using rolo::player::frameTimesReport;
using rolo::player::parseCommandLine;
using rolo::player::ParseResult;

namespace {
ParseResult parse(std::initializer_list<std::string_view> args)
{
    const std::vector<std::string_view> list(args);
    return parseCommandLine(list);
}
} // namespace

TEST_CASE("No arguments gives the defaults", "[player][cli]")
{
    const ParseResult r = parse({});
    REQUIRE(r.ok());
    CHECK(r.options.width == 1280);
    CHECK(r.options.height == 720);
    CHECK(r.options.maxFrames == 0);
    CHECK(r.options.showStats);
    CHECK(r.options.screenshotPath.empty());
}

TEST_CASE("Window options are read", "[player][cli]")
{
    const ParseResult r = parse({"--title", "My Game", "--width", "800", "--height", "600", "--no-stats"});
    REQUIRE(r.ok());
    CHECK(r.options.title == "My Game");
    CHECK(r.options.width == 800);
    CHECK(r.options.height == 600);
    CHECK_FALSE(r.options.showStats);
}

TEST_CASE("Frame limit and screenshot are read", "[player][cli]")
{
    const ParseResult r = parse({"--frames", "90", "--screenshot", "shot.png"});
    REQUIRE(r.ok());
    CHECK(r.options.maxFrames == 90);
    CHECK(r.options.screenshotPath == "shot.png");
}

TEST_CASE("A screenshot without a frame limit is rejected", "[player][cli]")
{
    const ParseResult r = parse({"--screenshot", "shot.png"});
    CHECK_FALSE(r.ok());
    CHECK(r.error.find("--frames") != std::string::npos);
}

TEST_CASE("Bad numbers are rejected with a helpful message", "[player][cli]")
{
    CHECK_FALSE(parse({"--frames", "zero"}).ok());
    CHECK_FALSE(parse({"--frames", "0"}).ok());
    CHECK_FALSE(parse({"--frames", "-5"}).ok());
    CHECK_FALSE(parse({"--width", "10"}).ok());       // below minimum
    CHECK_FALSE(parse({"--width", "800px"}).ok());    // trailing junk
    CHECK(parse({"--width", "abc"}).error.find("--width") != std::string::npos);
}

TEST_CASE("Options missing their value are rejected", "[player][cli]")
{
    const ParseResult r = parse({"--title"});
    CHECK_FALSE(r.ok());
    CHECK(r.error == "--title needs a value.");
}

TEST_CASE("Unknown options are rejected", "[player][cli]")
{
    const ParseResult r = parse({"--fullscreen"});
    CHECK_FALSE(r.ok());
    CHECK(r.error == "Unknown option: --fullscreen");
}

TEST_CASE("Help and version flags are recognised", "[player][cli]")
{
    CHECK(parse({"--help"}).options.showHelp);
    CHECK(parse({"-h"}).options.showHelp);
    CHECK(parse({"--version"}).options.showVersion);
}

TEST_CASE("A project or a scene can be chosen, not both", "[player][cli]")
{
    CHECK(parse({"--project", "My Game"}).options.projectPath == "My Game");
    CHECK(parse({"--scene", "a.lscene"}).options.scenePath == "a.lscene");
    const auto both = parse({"--project", "p", "--scene", "s"});
    CHECK_FALSE(both.ok());
    CHECK(both.error.find("not both") != std::string::npos);
}

TEST_CASE("Held keys and fixed steps for gameplay tests", "[player][cli]")
{
    CHECK(parse({"--frame-times"}).options.frameTimes);
    CHECK_FALSE(parse({}).options.frameTimes);
    const ParseResult r = parse({"--hold", "Right", "--hold", "space", "--fixed-step"});
    REQUIRE(r.ok());
    CHECK(r.options.heldKeys == std::vector<rolo::Key>{rolo::Key::Right, rolo::Key::Space});
    CHECK(r.options.fixedStep);

    const ParseResult bad = parse({"--hold", "Rigth"});
    CHECK_FALSE(bad.ok());
    CHECK(bad.error.find("Rigth") != std::string::npos);
    CHECK(rolo::player::keyFromName("left shift") == rolo::Key::LeftShift);
    CHECK_FALSE(rolo::player::keyFromName("Unknown"));
}

TEST_CASE("Tapped keys and the save folder", "[player][cli]")
{
    const ParseResult r = parse({"--tap", "E@120", "--tap", "down@5-40", "--save-dir", "saves"});
    REQUIRE(r.ok());
    REQUIRE(r.options.tappedKeys.size() == 2);
    using Tap = rolo::player::PlayerOptions::Tap;
    CHECK(r.options.tappedKeys[0] == Tap{rolo::Key::E, 120, 121});
    CHECK(r.options.tappedKeys[1] == Tap{rolo::Key::Down, 5, 40});
    CHECK_FALSE(parse({"--tap", "E@9-3"}).ok());
    CHECK(r.options.saveDir == "saves");
    CHECK_FALSE(parse({"--tap", "E"}).ok());
    CHECK_FALSE(parse({"--tap", "Nope@3"}).ok());
}

TEST_CASE("An exported game is found beside the player", "[player][cli]")
{
    const auto root = rolo::test::uniqueTempFolder("bundled");
    std::filesystem::create_directories(root / "My Game/game");
    std::ofstream(root / "My Game/game/project.rolo") << "{}";
    CHECK(bundledGameFolder(root / "My Game/My Game") == root / "My Game/game");
    // macOS: inside the app's Resources.
    std::filesystem::create_directories(root / "My Game.app/Contents/Resources/game");
    std::filesystem::create_directories(root / "My Game.app/Contents/MacOS");
    std::ofstream(root / "My Game.app/Contents/Resources/game/project.rolo") << "{}";
    CHECK(bundledGameFolder(root / "My Game.app/Contents/MacOS/rolo_player")
          == root / "My Game.app/Contents/Resources/game");
    // A player on its own (a build folder): nothing bundled.
    CHECK_FALSE(bundledGameFolder(root / "bin/rolo_player").has_value());
    std::filesystem::remove_all(root);

    const auto stats = parse({"--stats"});
    CHECK(stats.options.showStats);
    CHECK(stats.options.statsChosen);
    CHECK_FALSE(parse({}).options.statsChosen);
}

TEST_CASE("The mute option plays without sound", "[player][cli]")
{
    CHECK(parse({}).options.sound);
    const ParseResult r = parse({"--mute"});
    REQUIRE(r.ok());
    CHECK_FALSE(r.options.sound);
}

TEST_CASE("The frame-times report gives the mean, the slow frames and the worst", "[player][cli]")
{
    std::vector<double> frames(100, 16.0);
    frames[50] = 40.0;   // one hitch
    const std::vector<double> gameplay(100, 2.0);
    const std::string report = frameTimesReport(frames, gameplay);
    CHECK(report.find("over 100 frames") != std::string::npos);
    CHECK(report.find("mean 16.24 ms (62 fps)") != std::string::npos);
    CHECK(report.find("worst 40.00 ms; 1 frames slower than 48 fps") != std::string::npos);
    CHECK(report.find("Gameplay: mean 2.00 ms") != std::string::npos);
    CHECK(frameTimesReport({}, {}).find("too few frames") != std::string::npos);
}
