#include "CommandLine.hpp"
#include "EditorPaths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

using namespace rolo::editor;

namespace {

EnvironmentLookup fakeEnvironment(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](const char* name) -> std::optional<std::string> {
        if (auto it = values.find(name); it != values.end()) {
            return it->second;
        }
        return std::nullopt;
    };
}

EditorParseResult parse(std::initializer_list<std::string_view> args)
{
    const std::vector<std::string_view> list(args);
    return parseEditorCommandLine(list);
}

} // namespace

TEST_CASE("Settings folder follows each operating system's convention", "[editor][paths]")
{
    const auto env = fakeEnvironment({{"HOME", "/Users/oscar"}, {"APPDATA", "C:/Users/oscar/AppData/Roaming"}});
    CHECK(userConfigDirectory(HostOs::MacOS, env)
          == std::filesystem::path("/Users/oscar/Library/Application Support/Rolo Engine/Editor"));
    CHECK(userConfigDirectory(HostOs::Windows, env)
          == std::filesystem::path("C:/Users/oscar/AppData/Roaming") / "Rolo Engine" / "Editor");
    CHECK(userConfigDirectory(HostOs::Linux, env)
          == std::filesystem::path("/Users/oscar/.config/rolo-engine/editor"));
}

TEST_CASE("Linux respects XDG_CONFIG_HOME", "[editor][paths]")
{
    const auto env = fakeEnvironment({{"HOME", "/home/oscar"}, {"XDG_CONFIG_HOME", "/data/config"}});
    CHECK(userConfigDirectory(HostOs::Linux, env) == std::filesystem::path("/data/config/rolo-engine/editor"));
}

TEST_CASE("No home folder gives an empty path instead of guessing", "[editor][paths]")
{
    const auto env = fakeEnvironment({});
    CHECK(userConfigDirectory(HostOs::MacOS, env).empty());
    CHECK(userConfigDirectory(HostOs::Linux, env).empty());
    CHECK(userConfigDirectory(HostOs::Windows, env).empty());
}

TEST_CASE("Editor command line: defaults and options", "[editor][cli]")
{
    CHECK(parse({}).ok());
    const auto r = parse({"--config-dir", "/tmp/cfg", "--reset-layout", "--frames", "60", "--screenshot", "a.png"});
    REQUIRE(r.ok());
    CHECK(r.options.configDirectory == "/tmp/cfg");
    CHECK(r.options.resetLayout);
    CHECK(r.options.maxFrames == 60);
    CHECK(r.options.screenshotPath == "a.png");
}

TEST_CASE("Editor command line: mistakes are reported", "[editor][cli]")
{
    CHECK(parse({"--frames", "0"}).error.find("--frames") != std::string::npos);
    CHECK(parse({"--screenshot", "a.png"}).error.find("--frames") != std::string::npos);
    CHECK(parse({"--config-dir"}).error == "--config-dir needs a value.");
    CHECK(parse({"--smoke-test"}).error == "Unknown option: --smoke-test");
    CHECK(parse({"--version"}).options.showVersion);
}

TEST_CASE("Editor command line: project options", "[editor][cli]")
{
    CHECK(parse({"--project", "My Game"}).options.projectPath == "My Game");
    CHECK(parse({"--new-project", "/tmp/x"}).options.newProjectPath == "/tmp/x");
    CHECK(parse({"--no-project"}).options.noProject);
    CHECK_FALSE(parse({"--project", "a", "--no-project"}).ok());
    CHECK_FALSE(parse({"--project"}).ok());
}

TEST_CASE("New projects are suggested in Documents/Rolo Projects", "[editor][paths]")
{
    const auto mac = fakeEnvironment({{"HOME", "/Users/oscar"}});
    CHECK(defaultProjectsDirectory(HostOs::MacOS, mac) == "/Users/oscar/Documents/Rolo Projects");
    const auto win = fakeEnvironment({{"USERPROFILE", "C:/Users/oscar"}});
    CHECK(defaultProjectsDirectory(HostOs::Windows, win) == std::filesystem::path("C:/Users/oscar") / "Documents"
                                                                / "Rolo Projects");
    CHECK(defaultProjectsDirectory(HostOs::Linux, fakeEnvironment({})).empty());
}

TEST_CASE("Editor command line: play straight away", "[editor][cli]")
{
    CHECK_FALSE(parse({}).options.play);
    const auto r = parse({"--project", "Game", "--play", "--frames", "60"});
    REQUIRE(r.ok());
    CHECK(r.options.play);
}

TEST_CASE("Editor command line: templates and exporting", "[editor][cli]")
{
    const auto made = parse({"--new-project", "/tmp/x", "--template", "topdown"});
    REQUIRE(made.ok());
    CHECK(made.options.templateId == "topdown");
    CHECK_FALSE(parse({"--template", "topdown"}).ok());   // needs --new-project
    const auto exported = parse({"--project", "Game", "--export", "/tmp/out"});
    REQUIRE(exported.ok());
    CHECK(exported.options.exportPath == "/tmp/out");
    CHECK_FALSE(exported.options.exportWeb);
    const auto web = parse({"--project", "Game", "--export-web", "/tmp/web"});
    REQUIRE(web.ok());
    CHECK(web.options.exportPath == "/tmp/web");
    CHECK(web.options.exportWeb);
    CHECK(parse({"--export-web", "/tmp/web"}).error.find("--project") != std::string::npos);
    CHECK_FALSE(parse({"--project", "Game", "--export", "/a", "--export-web", "/b"}).ok());
    CHECK(parse({"--export", "/tmp/out"}).error.find("--project") != std::string::npos);
    CHECK_FALSE(parse({"--project", "Game", "--export", ""}).ok());
}
