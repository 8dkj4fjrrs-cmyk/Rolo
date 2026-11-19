// Exporting a project as a stand-alone game (editor/model/GameExport.hpp),
// with a stand-in runtime for each operating system's layout.

#include "support/TempFolder.hpp"
#include "GameExport.hpp"

#include "rolo/assets/GamePack.hpp"
#include "rolo/core/Files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rolo;
using namespace rolo::editor;
namespace fs = std::filesystem;

namespace {

struct Setup {
    fs::path root = test::uniqueTempFolder("export");
    ExportOptions options;

    Setup()
    {
        file("project/project.rolo", R"({"format": "rolo.project", "version": 1, "name": "My Game"})");
        file("project/scenes/main.lscene", "{}");
        file("project/assets/hero.png", "png");
        file("project/.git/config", "not for players");
        file("licenses/README.md", "# Licenses");
        file("licenses/axmol.txt", "MIT");
        options.project = root / "project";
        options.output = root / "exports";
        options.gameName = "My Game!";
        options.licenses = root / "licenses";
        options.version = "0.8.0";
    }
    ~Setup()
    {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void file(const fs::path& rel, const std::string& text = "x")
    {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << text;
    }
};

} // namespace

TEST_CASE("Export names are safe file names", "[editor][export]")
{
    CHECK(exportFileName("My Game!") == "My Game");
    CHECK(exportFileName("  a/b:c  ") == "abc");
    CHECK(exportFileName("???") == "Game");
    CHECK(exportFileName("Café_2-B") == "Café_2-B");
}

TEST_CASE("A Linux or Windows export is the runtime, renamed, with the game beside it", "[editor][export]")
{
    for (const HostOs os : {HostOs::Linux, HostOs::Windows}) {
        Setup s;
        const std::string exe = os == HostOs::Windows ? "rolo_player.exe" : "rolo_player";
        s.file("runtime/" + exe, "program");
        s.file("runtime/axslc/positionColor_vs", "shader");
        s.file("runtime/libGLESv2.dll", "dll");
        s.file("runtime/rolo_player.pdb", "debug info");
        s.options.player = s.root / "runtime" / exe;
        s.options.os = os;
        const ExportResult r = exportGame(s.options);
        INFO(r.error);
        REQUIRE(r.ok);
        const fs::path game = s.root / "exports/My Game";
        CHECK(r.game == game);
        CHECK(r.program == game / (os == HostOs::Windows ? "My Game.exe" : "My Game"));
        CHECK(*readTextFile(r.program) == "program");
        CHECK(fs::exists(game / "axslc/positionColor_vs"));
        CHECK(fs::exists(game / "libGLESv2.dll"));
        CHECK_FALSE(fs::exists(game / "rolo_player.pdb"));   // build leftovers stay behind
        CHECK(fs::exists(game / "game/project.rolo"));
        CHECK(fs::exists(game / "game/assets/hero.png"));
        CHECK_FALSE(fs::exists(game / "game/.git"));               // hidden files stay behind
        CHECK(fs::exists(game / "licenses/axmol.txt"));
        CHECK(readTextFile(game / "README.txt")->find("Rolo Engine 0.8.0") != std::string::npos);
        CHECK(r.files >= 7);
        if (os == HostOs::Linux) {
            CHECK((fs::status(r.program).permissions() & fs::perms::owner_exec) != fs::perms::none);
        }

        // Exporting again replaces the earlier export.
        s.file("project/scenes/second.lscene", "{}");
        const ExportResult again = exportGame(s.options);
        REQUIRE(again.ok);
        CHECK(fs::exists(game / "game/scenes/second.lscene"));
    }
}

TEST_CASE("A game named like one of the export's own files still exports", "[editor][export]")
{
    // On Linux the program has no ending: a game called "game" would be named like its data folder.
    for (const char* name : {"game", "licenses", "axslc"}) {
        Setup s;
        s.file("runtime/rolo_player", "program");
        s.file("runtime/axslc/positionColor_vs", "shader");
        s.options.player = s.root / "runtime/rolo_player";
        s.options.os = HostOs::Linux;
        s.options.gameName = name;
        const ExportResult r = exportGame(s.options);
        INFO(name << ": " << r.error);
        REQUIRE(r.ok);
        const fs::path game = s.root / "exports" / name;
        CHECK(r.program == game / (std::string(name) + "-program"));
        CHECK(*readTextFile(r.program) == "program");
        CHECK(fs::exists(game / "game/project.rolo"));
        CHECK(fs::exists(game / "licenses/axmol.txt"));
        CHECK(fs::exists(game / "axslc/positionColor_vs"));
        CHECK(readTextFile(game / "README.txt")->find(std::string(name) + "-program") != std::string::npos);
        // Again, over the earlier export.
        REQUIRE(exportGame(s.options).ok);
    }
    // On Windows the program ends in .exe and clashes with nothing.
    Setup s;
    s.file("runtime/rolo_player.exe", "program");
    s.options.player = s.root / "runtime/rolo_player.exe";
    s.options.os = HostOs::Windows;
    s.options.gameName = "game";
    const ExportResult r = exportGame(s.options);
    REQUIRE(r.ok);
    CHECK(r.program == s.root / "exports/game/game.exe");
}

TEST_CASE("A macOS export is the runtime app, renamed, with the game inside", "[editor][export]")
{
    Setup s;
    s.file("runtime/rolo_player.app/Contents/MacOS/rolo_player", "program");
    s.file("runtime/rolo_player.app/Contents/Resources/axslc/positionColor_vs", "shader");
    s.file("runtime/rolo_player.app/Contents/Info.plist",
           "<dict>\n\t<key>CFBundleIdentifier</key>\n\t<string>dev.rolo-engine.player</string>\n"
           "\t<key>CFBundleName</key>\n\t<string>Rolo Player</string>\n</dict>\n");
    s.options.player = s.root / "runtime/rolo_player.app";
    s.options.os = HostOs::MacOS;
    const ExportResult r = exportGame(s.options);
    INFO(r.error);
    REQUIRE(r.ok);
    const fs::path app = s.root / "exports/My Game.app";
    CHECK(r.game == app);
    CHECK(fs::exists(app / "Contents/MacOS/rolo_player"));
    CHECK(fs::exists(app / "Contents/Resources/axslc/positionColor_vs"));
    CHECK(fs::exists(app / "Contents/Resources/game/scenes/main.lscene"));
    CHECK(fs::exists(app / "Contents/Resources/licenses/axmol.txt"));
    const std::string plist = *readTextFile(app / "Contents/Info.plist");
    CHECK(plist.find("<string>My Game</string>") != std::string::npos);
    CHECK(plist.find("dev.rolo-engine.game.mygame") != std::string::npos);
}

TEST_CASE("Exports refuse what they cannot do safely, saying why", "[editor][export]")
{
    Setup s;
    s.file("runtime/rolo_player", "program");
    s.options.player = s.root / "runtime/rolo_player";
    s.options.os = HostOs::Linux;

    ExportOptions noRuntime = s.options;
    noRuntime.player = s.root / "missing/rolo_player";
    CHECK(exportGame(noRuntime).error.find("runtime") != std::string::npos);

    ExportOptions inside = s.options;
    inside.output = s.root / "project/exports";
    CHECK(exportGame(inside).error.find("outside the project") != std::string::npos);

    ExportOptions noLicenses = s.options;
    noLicenses.licenses = s.root / "nowhere";
    CHECK(exportGame(noLicenses).error.find("license") != std::string::npos);

    // A folder of the same name that is not an earlier export is never deleted.
    s.file("exports/My Game/precious.txt", "keep me");
    const ExportResult r = exportGame(s.options);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("not an earlier export") != std::string::npos);
    CHECK(fs::exists(s.root / "exports/My Game/precious.txt"));
}

TEST_CASE("A web export is the web runtime, the page with the game's name, and the game as a pack", "[editor][export]")
{
    Setup s;
    s.file("web/index.html", "<title>{{TITLE}}</title><h1>{{TITLE}}</h1>{{WIDTH}}x{{HEIGHT}} {{FIT}}");
    s.file("web/rolo_player.js", "js");
    s.file("web/rolo_player.wasm", "wasm");
    s.file("web/rolo_player.data", "data");
    s.file("web/rolo_player.wasm.map", "not part of the runtime");
    s.options.target = ExportTarget::Web;
    s.options.webPlayer = s.root / "web";
    s.options.gameName = "Cats & <Dogs>";
    // (No desktop runtime is needed for a web export: options.player is empty.)
    const ExportResult r = exportGame(s.options);
    INFO(r.error);
    REQUIRE(r.ok);
    const fs::path game = s.root / "exports/Cats  Dogs Web";
    CHECK(r.game == game);
    CHECK(r.program == game / "index.html");
    // The page gets the game's name, and its size and how it meets other screen shapes.
    CHECK(*readTextFile(game / "index.html")
          == "<title>Cats &amp; &lt;Dogs&gt;</title><h1>Cats &amp; &lt;Dogs&gt;</h1>1280x720 bars");
    CHECK(*readTextFile(game / "rolo_player.js") == "js");
    CHECK(fs::exists(game / "rolo_player.wasm"));
    CHECK(fs::exists(game / "rolo_player.data"));
    CHECK_FALSE(fs::exists(game / "rolo_player.wasm.map"));
    CHECK_FALSE(fs::exists(game / "game"));   // the project is in the pack, not a folder
    CHECK(fs::exists(game / "licenses/axmol.txt"));
    const std::string readme = *readTextFile(game / "README.txt");
    CHECK(readme.find("itch.io") != std::string::npos);
    CHECK(readme.find("python3 -m http.server") != std::string::npos);
    CHECK(readme.find("Rolo Engine 0.8.0") != std::string::npos);

    const auto index = readGamePackIndex(game / "game.pack");
    REQUIRE(index);
    CHECK(index->name == "Cats & <Dogs>");
    CHECK(index->saves == "cats-dogs");
    REQUIRE(index->files.size() == 3);   // hidden files (.git) stay behind
    CHECK(index->files[0].path == "assets/hero.png");
    CHECK(index->files[1].path == "project.rolo");
    CHECK(index->files[2].path == "scenes/main.lscene");
    CHECK(r.files >= 8);

    // Exporting again replaces the earlier export; the desktop export of the same game is another folder.
    s.file("project/scenes/second.lscene", "{}");
    REQUIRE(exportGame(s.options).ok);
    CHECK(readGamePackIndex(game / "game.pack")->files.size() == 4);

    // Without the web runtime, it says what is missing and how to get it.
    fs::remove(s.root / "web/rolo_player.wasm");
    const ExportResult missing = exportGame(s.options);
    CHECK_FALSE(missing.ok);
    CHECK(missing.error.find("web runtime") != std::string::npos);
    CHECK(missing.error.find("rolo_player.wasm") != std::string::npos);
    s.options.webPlayer.clear();
    CHECK(exportGame(s.options).error.find("web runtime") != std::string::npos);
}
