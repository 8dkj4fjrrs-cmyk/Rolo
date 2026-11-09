#include "rolo/scene/Project.hpp"
#include "rolo/scene/SceneFile.hpp"

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
