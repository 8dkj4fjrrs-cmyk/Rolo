#include "support/TempFolder.hpp"
#include "rolo/assets/GamePack.hpp"
#include "rolo/core/Files.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rolo;
namespace fs = std::filesystem;

namespace {

struct Temp {
    fs::path path;
    explicit Temp(const char* name)
        : path(rolo::test::uniqueTempFolder(name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Temp() { fs::remove_all(path); }
};

void write(const fs::path& file, const std::string& bytes)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}

std::string read(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("A game pack holds a folder's files and gives them back", "[assets][pack]")
{
    Temp t("pack");
    const fs::path game = t.path / "game";
    const std::string binary("\x00\x01\xff\n\r\x7f", 6);
    write(game / "project.rolo", R"({"name": "T"})");
    write(game / "scenes/main.lscene", "scene");
    write(game / "assets/art/hero.png", binary);
    write(game / "assets/empty.txt", "");
    write(game / ".git/config", "not packed");        // hidden folders and files stay out
    write(game / "scenes/.DS_Store", "not packed");

    const fs::path pack = t.path / "out" / "game.pack";
    const GamePackResult made = writeGamePack(game, pack, "My Game", "my-game");
    REQUIRE(made.status);
    CHECK(made.files == 4);
    CHECK(made.bytes == fs::file_size(pack));
    CHECK(read(pack).substr(0, 10) == "ROLOPACK1\n");
    CHECK_FALSE(fs::exists(t.path / "out" / "game.pack.tmp"));

    std::string error;
    const auto index = readGamePackIndex(pack, &error);
    REQUIRE(index);
    CHECK(index->name == "My Game");
    CHECK(index->saves == "my-game");
    REQUIRE(index->files.size() == 4);
    // Alphabetical, folders' files in place: the same folder always gives the same pack.
    CHECK(index->files[0].path == "assets/art/hero.png");
    CHECK(index->files[0].size == 6);
    CHECK(index->files[1].path == "assets/empty.txt");
    CHECK(index->files[2].path == "project.rolo");
    CHECK(index->files[3].path == "scenes/main.lscene");

    const fs::path back = t.path / "back";
    REQUIRE(extractGamePack(pack, back));
    CHECK(read(back / "assets/art/hero.png") == binary);
    CHECK(read(back / "assets/empty.txt").empty());
    CHECK(read(back / "project.rolo") == R"({"name": "T"})");
    CHECK(read(back / "scenes/main.lscene") == "scene");
    CHECK_FALSE(fs::exists(back / ".git"));

    // Packing again gives the same bytes.
    const fs::path again = t.path / "again.pack";
    REQUIRE(writeGamePack(game, again, "My Game", "my-game").status);
    CHECK(read(again) == read(pack));

    // A caller's own choice of files.
    const fs::path some = t.path / "some.pack";
    const auto noArt = [](const fs::path& p) { return p.filename() != "art" && p.filename().string().front() != '.'; };
    REQUIRE(writeGamePack(game, some, "My Game", "my-game", noArt).status);
    CHECK(readGamePackIndex(some)->files.size() == 3);
}

TEST_CASE("Damaged or dangerous game packs are refused", "[assets][pack]")
{
    Temp t("badpack");
    std::string error;

    write(t.path / "not.pack", "PNG\x89 something else entirely");
    CHECK_FALSE(readGamePackIndex(t.path / "not.pack", &error));
    CHECK(error.find("not a Rolo game pack") != std::string::npos);
    CHECK_FALSE(readGamePackIndex(t.path / "missing.pack", &error));

    const auto pack = [&](const std::string& index, const std::string& data) {
        const auto n = static_cast<unsigned>(index.size());
        std::string bytes = "ROLOPACK1\n";
        bytes += static_cast<char>(n & 0xff);
        bytes += static_cast<char>((n >> 8) & 0xff);
        bytes += static_cast<char>((n >> 16) & 0xff);
        bytes += static_cast<char>((n >> 24) & 0xff);
        write(t.path / "made.pack", bytes + index + data);
        return t.path / "made.pack";
    };
    // A path that would leave the folder it is unpacked into.
    for (const char* path : {"../evil.txt", "/etc/evil", "a/../../evil", "C:/evil", "a\\\\b", ""}) {
        const std::string index = std::string(R"({"format":"rolo.pack","version":1,"name":"x","saves":"x","files":[{"path":")")
                                  + path + R"(","size":1}]})";
        INFO(path);
        CHECK_FALSE(readGamePackIndex(pack(index, "x"), &error));
        CHECK_FALSE(extractGamePack(t.path / "made.pack", t.path / "out"));
    }
    CHECK_FALSE(fs::exists(t.path / "evil.txt"));

    // Cut short: the index promises more than the file has.
    const std::string good = R"({"format":"rolo.pack","version":1,"name":"x","saves":"x","files":[{"path":"a.txt","size":10}]})";
    REQUIRE(readGamePackIndex(pack(good, "short")));
    const Status cut = extractGamePack(t.path / "made.pack", t.path / "out");
    CHECK_FALSE(cut);
    CHECK(cut.message.find("cut short") != std::string::npos);

    // From a newer Rolo.
    const std::string newer = R"({"format":"rolo.pack","version":99,"files":[]})";
    CHECK_FALSE(readGamePackIndex(pack(newer, ""), &error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK_FALSE(readGamePackIndex(pack("{not json", ""), &error));
}

TEST_CASE("A game's saves folder name is safe anywhere", "[assets][pack]")
{
    CHECK(gamePackSavesName("The Lantern Keeper") == "the-lantern-keeper");
    CHECK(gamePackSavesName("  My  Game!! (v2) ") == "my-game-v2");
    CHECK(gamePackSavesName("../..") == "game");
    CHECK(gamePackSavesName("") == "game");
    CHECK(gamePackSavesName("Café 3") == "caf-3");
}
