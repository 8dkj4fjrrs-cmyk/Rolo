// Finding Rolo's own files (templates, examples, licenses, the game
// runtime) from where the editor program is, and new projects from templates.

#include "support/TempFolder.hpp"
#include "RoloResources.hpp"

#include "rolo/scene/Project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rolo;
using namespace rolo::editor;
namespace fs = std::filesystem;

namespace {

struct Folder {
    fs::path path = test::uniqueTempFolder("resources");
    Folder() { fs::create_directories(path); }
    ~Folder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void file(const fs::path& rel, const std::string& text = "")
    {
        fs::create_directories((path / rel).parent_path());
        std::ofstream(path / rel) << text;
    }
};

void makeTemplate(Folder& f, const fs::path& rel, const std::string& info)
{
    f.file(rel / "template.json", info);
    f.file(rel / "project" / "project.rolo",
           R"({"format": "rolo.project", "version": 1, "name": "Template", "startScene": "scenes/main.lscene"})");
    f.file(rel / "project" / "scenes" / "main.lscene", "{}");
}

} // namespace

TEST_CASE("An installed copy keeps its files beside the editor", "[editor][resources]")
{
    Folder f;
    f.file("Rolo/rolo_editor");
    fs::create_directories(f.path / "Rolo/templates");
    const auto r = findRoloResources(f.path / "Rolo/rolo_editor", HostOs::Linux, "/src", "/build/player");
    CHECK(r.installed);
    CHECK(r.templates == f.path / "Rolo/templates");
    CHECK(r.licenses == f.path / "Rolo/licenses");
    CHECK(r.player == f.path / "Rolo/runtime/rolo_player");
    CHECK(r.webPlayer == f.path / "Rolo/web-runtime");
    const auto w = findRoloResources(f.path / "Rolo/rolo_editor.exe", HostOs::Windows, "/src", "");
    CHECK(w.player == f.path / "Rolo/runtime/rolo_player.exe");
}

TEST_CASE("An installed macOS app keeps them in its Resources", "[editor][resources]")
{
    Folder f;
    const fs::path app = f.path / "Rolo Editor.app/Contents";
    fs::create_directories(app / "Resources/rolo/templates");
    const auto r = findRoloResources(app / "MacOS/rolo_editor", HostOs::MacOS, "/src", "");
    CHECK(r.installed);
    CHECK(r.examples == app / "Resources/rolo/examples");
    CHECK(r.player == app / "Resources/rolo/runtime/rolo_player.app");
    CHECK(r.webPlayer == app / "Resources/rolo/web-runtime");
}

TEST_CASE("A build folder uses the source tree and the player built beside the editor", "[editor][resources]")
{
    Folder f;
    const auto r = findRoloResources(f.path / "build/bin/rolo_editor/rolo_editor", HostOs::Linux,
                                        f.path / "src", f.path / "build/bin/rolo_player/rolo_player");
    CHECK_FALSE(r.installed);
    CHECK(r.templates == f.path / "src/templates");
    CHECK(r.docs == f.path / "src/docs");
    CHECK(r.player == f.path / "build/bin/rolo_player/rolo_player");
    CHECK(r.webPlayer == f.path / "src/build/web/bin/rolo_player");   // where a web build puts it
}

TEST_CASE("Templates and examples are listed in order and copied into new projects", "[editor][resources]")
{
    Folder f;
    makeTemplate(f, "templates/topdown", R"({"name": "Top-Down Starter", "description": "Walk around.", "order": 2})");
    makeTemplate(f, "templates/blank", R"({"name": "Blank", "order": 1})");
    makeTemplate(f, "examples/keeper", R"({"name": "The Lantern Keeper"})");
    f.file("examples/notes/README.md");                         // not a template: ignored
    f.file("templates/broken/template.json", "{}");             // no project: ignored
    RoloResources r;
    r.templates = f.path / "templates";
    r.examples = f.path / "examples";
    const auto list = listProjectTemplates(r);
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "Blank");
    CHECK(list[1].name == "Top-Down Starter");
    CHECK(list[1].description == "Walk around.");
    CHECK(list[2].name == "The Lantern Keeper");
    CHECK(list[2].example);

    std::string error;
    const fs::path made = f.path / "projects/My Game";
    REQUIRE(createProjectFromTemplate(list[1], made, "My Game", &error));
    INFO(error);
    const auto project = Project::open(made);
    REQUIRE(project);
    CHECK(project->settings().name == "My Game");
    CHECK(fs::exists(made / "scenes/main.lscene"));
    CHECK_FALSE(fs::exists(made / "template.json"));

    // Never into a folder that has files in it, and never with an unusable name.
    CHECK_FALSE(createProjectFromTemplate(list[1], made, "Again", &error));
    CHECK(error.find("not empty") != std::string::npos);
    CHECK_FALSE(createProjectFromTemplate(list[1], f.path / "projects/Other", "a/b", &error));
}
