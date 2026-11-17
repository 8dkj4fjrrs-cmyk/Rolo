#include "support/TempFolder.hpp"
#include "rolo/assets/AssetDatabase.hpp"
#include "rolo/assets/ImageDecode.hpp"
#include "rolo/assets/ImageEncode.hpp"
#include "rolo/core/Files.hpp"
#include "rolo/scene/Scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace rolo;
namespace fs = std::filesystem;

namespace {

const fs::path kData = ROLO_TEST_DATA_DIR;

// A fresh, empty project folder for each test.
struct TempProject {
    fs::path path;
    explicit TempProject(const char* name)
        : path(rolo::test::uniqueTempFolder(name))
    {
        fs::remove_all(path);
        fs::create_directories(path / "scenes");
        std::ofstream(path / "project.rolo") << R"({"format": "rolo.project", "version": 1, "name": "T"})";
    }
    ~TempProject() { fs::remove_all(path); }
};

std::string readFile(const fs::path& p)
{
    return readTextFile(p).value_or("");
}

} // namespace

TEST_CASE("Asset types come from file extensions", "[assets]")
{
    CHECK(assetTypeForPath("a/hero.png") == AssetType::Image);
    CHECK(assetTypeForPath("HERO.JPG") == AssetType::Image);
    CHECK(assetTypeForPath("prefabs/Bullet.lprefab") == AssetType::Prefab);
    CHECK(assetTypeName(AssetType::Prefab) == "prefab");
    CHECK(assetTypeFromName("prefab") == AssetType::Prefab);
    CHECK(assetTypeForPath("map.ltileset") == AssetType::Tileset);
    CHECK(assetTypeForPath("jump.ogg") == AssetType::Audio);
    CHECK(assetTypeForPath("ui.ttf") == AssetType::Font);
    CHECK_FALSE(assetTypeForPath("notes.txt").has_value());
    CHECK_FALSE(assetTypeForPath("hero.png.meta").has_value());
    CHECK(assetTypeFromName(assetTypeName(AssetType::Tileset)) == AssetType::Tileset);
}

TEST_CASE("Importing copies the file, writes a .meta with an ID and reads the image size", "[assets]")
{
    TempProject project("assets-import");
    AssetDatabase db(project.path);
    std::string error;
    const auto id = db.import(kData / "hero.png", "assets", &error);
    REQUIRE(id.has_value());
    CHECK(error.empty());
    CHECK(fs::exists(project.path / "assets/hero.png"));
    CHECK(fs::exists(project.path / "assets/hero.png.meta"));

    const AssetRecord* hero = db.find(*id);
    REQUIRE(hero != nullptr);
    CHECK(hero->path == "assets/hero.png");
    CHECK(hero->type == AssetType::Image);
    REQUIRE(hero->image.has_value());
    CHECK(hero->image->width == 24);
    CHECK(hero->image->height == 32);
    CHECK(hero->filter == TextureFilter::Pixel); // small images default to pixel art
    CHECK(hero->isUsable());
    CHECK(readFile(project.path / "assets/hero.png.meta").find(id->toString()) != std::string::npos);

    // A second import of a same-named file gets a new name and a new ID.
    const auto second = db.import(kData / "hero.png", "assets");
    REQUIRE(second.has_value());
    CHECK(*second != *id);
    CHECK(db.find(*second)->path == "assets/hero 2.png");

    // Large photos default to smooth filtering.
    const auto photo = db.import(kData / "photo.jpg", "assets");
    REQUIRE(photo.has_value());
    CHECK(db.find(*photo)->filter == TextureFilter::Smooth);
    CHECK(db.find(*photo)->image->width == 300);

    std::ofstream(project.path / "notes.txt") << "not an asset";
    CHECK_FALSE(db.import(project.path / "notes.txt", "assets", &error).has_value());
    CHECK(error.find("not a supported asset") != std::string::npos);

    // Scenes are assets too (so gameplay can refer to them by ID).
    const auto scene = db.import(kData / "test_card.lscene", "scenes");
    REQUIRE(scene.has_value());
    CHECK(db.find(*scene)->type == AssetType::Scene);
    CHECK(db.find(*scene)->isUsable());
    CHECK(assetTypeForPath("levels/cave.lscene") == AssetType::Scene);
    CHECK(assetTypeFromName("scene") == AssetType::Scene);
}

TEST_CASE("IDs survive a restart and a move; copies get new IDs", "[assets]")
{
    TempProject project("assets-ids");
    AssetId heroId;
    {
        AssetDatabase db(project.path);
        heroId = *db.import(kData / "hero.png", "assets");
        REQUIRE(db.move(heroId, "assets/characters/player.png"));
        CHECK(db.find(heroId)->path == "assets/characters/player.png");
        CHECK(fs::exists(project.path / "assets/characters/player.png.meta"));
        CHECK_FALSE(fs::exists(project.path / "assets/hero.png.meta"));
    }
    // A new database (as after restarting the editor) finds the same ID.
    AssetDatabase db(project.path);
    CHECK(db.refresh().empty());
    REQUIRE(db.find(heroId) != nullptr);
    CHECK(db.find(heroId)->path == "assets/characters/player.png");

    // The user copies the file *with* its .meta in Finder: the copy needs its own ID.
    fs::copy_file(project.path / "assets/characters/player.png", project.path / "assets/copy.png");
    fs::copy_file(project.path / "assets/characters/player.png.meta", project.path / "assets/copy.png.meta");
    const auto issues = db.refresh();
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message.find("same ID") != std::string::npos);
    const AssetRecord* copy = db.findByPath("assets/copy.png");
    REQUIRE(copy != nullptr);
    CHECK(copy->id != heroId);
    CHECK(db.find(heroId) != nullptr);
}

TEST_CASE("Files added by hand get a .meta; damaged and missing files are reported once", "[assets]")
{
    TempProject project("assets-scan");
    fs::create_directories(project.path / "assets");
    fs::copy_file(kData / "tiles_16px.png", project.path / "assets/tiles.png");
    fs::copy_file(kData / "broken.png", project.path / "assets/broken.png");
    std::ofstream(project.path / "assets/readme.txt") << "not an asset";

    AssetDatabase db(project.path);
    const auto issues = db.refresh();
    CHECK(fs::exists(project.path / "assets/tiles.png.meta"));
    CHECK(db.all().size() == 2);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].where == "assets/broken.png");
    CHECK(issues[0].message.find("not a readable PNG or JPEG") != std::string::npos);
    CHECK_FALSE(db.findByPath("assets/broken.png")->isUsable());

    // Nothing changed: nothing new to report, and the revision stays.
    const auto revision = db.revision();
    CHECK(db.refresh().empty());
    CHECK(db.revision() == revision);

    // The image is deleted outside the editor, leaving its .meta behind.
    fs::remove(project.path / "assets/tiles.png");
    const auto gone = db.refresh();
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].message.find("missing") != std::string::npos);
    CHECK(db.findByPath("assets/tiles.png") == nullptr);
    CHECK(db.revision() > revision);
}

TEST_CASE("A filter change is saved in the .meta and kept after a restart", "[assets]")
{
    TempProject project("assets-filter");
    AssetId id;
    {
        AssetDatabase db(project.path);
        id = *db.import(kData / "hero.png");
        REQUIRE(db.setFilter(id, TextureFilter::Smooth));
    }
    AssetDatabase db(project.path);
    db.refresh();
    CHECK(db.find(id)->filter == TextureFilter::Smooth);
}

TEST_CASE("Tilesets: geometry, collision, files", "[assets][tileset]")
{
    Tileset t;
    t.tileWidth = 16;
    t.tileHeight = 16;
    CHECK(t.columns(64) == 4);
    CHECK(t.rows(32) == 2);
    const TileRect r = t.tileRect(5, 64);
    CHECK(r.x == 16);
    CHECK(r.y == 16);

    t.margin = 1;
    t.spacing = 2;
    CHECK(t.columns(1 + 16 + 2 + 16 + 1) == 2);
    CHECK(t.tileRect(1, 36).x == 19);

    t.image = AssetId(0x1234);
    t.solid = {3, 7};
    std::string error;
    const auto back = parseTileset(serializeTileset(t), &error);
    REQUIRE(back.has_value());
    CHECK(*back == t);
    CHECK(back->isSolid(7));
    CHECK_FALSE(back->isSolid(2));

    CHECK_FALSE(parseTileset("{}", &error).has_value());
    CHECK_FALSE(parseTileset(R"({"format": "rolo.tileset", "version": 1, "tileSize": [0, 16]})", &error));
    CHECK(error.find("between 1 and 4096") != std::string::npos);
    CHECK_FALSE(parseTileset(R"({"format": "rolo.tileset", "version": 9, "tileSize": [16, 16]})", &error));
    CHECK(error.find("newer version") != std::string::npos);
}

TEST_CASE("Creating and saving a tileset asset", "[assets][tileset]")
{
    TempProject project("assets-tileset");
    AssetDatabase db(project.path);
    const AssetId image = *db.import(kData / "tiles_16px.png");
    std::string error;
    const auto tileset = db.createTileset(image, {16, 16}, "Grass", &error);
    REQUIRE(tileset.has_value());
    CHECK(db.find(*tileset)->path == "assets/Grass.ltileset");
    REQUIRE(db.find(*tileset)->tileset.has_value());

    Tileset changed = *db.find(*tileset)->tileset;
    changed.solid.insert(6);
    REQUIRE(db.saveTileset(*tileset, changed));

    AssetDatabase reopened(project.path);
    CHECK(reopened.refresh().empty());
    REQUIRE(reopened.find(*tileset) != nullptr);
    CHECK(reopened.find(*tileset)->tileset->isSolid(6));
    CHECK(reopened.find(*tileset)->tileset->image == image);
}

TEST_CASE("Scenes are checked for missing assets and found by reference", "[assets]")
{
    TempProject project("assets-refs");
    AssetDatabase db(project.path);
    const AssetId hero = *db.import(kData / "hero.png");

    // A scene file that mentions the asset's ID.
    std::ofstream(project.path / "scenes/level.lscene") << "{ \"image\": \"" + hero.toString() + "\" }";
    std::ofstream(project.path / "scenes/other.lscene") << "{ }";
    const auto users = db.scenesReferencing(hero);
    REQUIRE(users.size() == 1);
    CHECK(users[0] == "scenes/level.lscene");

    // A scene with no asset properties has nothing to report.
    const Scene scene = makeStartScene();
    CHECK(checkAssetReferences(scene, db).empty());
}

TEST_CASE("Small images copied in by hand also default to pixel-art filtering", "[assets]")
{
    TempProject project("assets-default-filter");
    fs::create_directories(project.path / "assets");
    fs::copy_file(kData / "hero.png", project.path / "assets/hero.png");
    fs::copy_file(kData / "photo.jpg", project.path / "assets/photo.jpg");
    AssetDatabase db(project.path);
    db.refresh();
    CHECK(db.findByPath("assets/hero.png")->filter == TextureFilter::Pixel);
    CHECK(db.findByPath("assets/photo.jpg")->filter == TextureFilter::Smooth);
}

TEST_CASE("A read-only database (games) never writes .meta files", "[assets]")
{
    TempProject project("assets-readonly");
    fs::create_directories(project.path / "assets");
    fs::copy_file(kData / "hero.png", project.path / "assets/hero.png");
    AssetDatabase db(project.path, AssetDatabase::Access::ReadOnly);
    CHECK(db.refresh().empty());
    REQUIRE(db.findByPath("assets/hero.png") != nullptr); // usable for this run
    CHECK_FALSE(fs::exists(project.path / "assets/hero.png.meta"));
}

TEST_CASE("Rolo decodes JPEG and PNG files itself", "[assets][image]")
{
    // The renderer draws from these pixels: no separate JPEG library is in the engine.
    std::string error;
    const auto photo = decodeImageFile(kData / "photo.jpg", &error);
    INFO(error);
    REQUIRE(photo);
    CHECK(photo->width > 0);
    CHECK(photo->height > 0);
    CHECK(photo->rgba.size() == static_cast<std::size_t>(photo->width) * static_cast<std::size_t>(photo->height) * 4);
    CHECK(photo->rgba[3] == 255);   // JPEG has no transparency

    // A progressive JPEG (the kind cameras and web sites often make): a
    // gradient, red growing to the right, green downwards, top row first.
    const auto gradient = decodeImageFile(kData / "gradient_progressive.jpg", &error);
    INFO(error);
    REQUIRE(gradient);
    REQUIRE(gradient->width == 48);
    REQUIRE(gradient->height == 32);
    const auto pixel = [&](int x, int y, int channel) {
        return static_cast<int>(gradient->rgba[static_cast<std::size_t>((y * 48 + x) * 4 + channel)]);
    };
    CHECK(pixel(40, 2, 0) > pixel(4, 2, 0) + 120);    // red: 200 against 20
    CHECK(pixel(4, 30, 1) > pixel(4, 2, 1) + 150);    // green: 240 against 16
    CHECK(std::abs(pixel(20, 16, 2) - 128) < 12);     // blue: 128 everywhere (JPEG is not exact)
    CHECK(pixel(20, 16, 3) == 255);

    // Greyscale JPEGs come out as grey pixels.
    const auto grey = decodeImageFile(kData / "grey.jpg", &error);
    REQUIRE(grey);
    CHECK(grey->rgba[0] == grey->rgba[1]);
    CHECK(grey->rgba[1] == grey->rgba[2]);

    const auto hero = decodeImageFile(kData / "hero.png", &error);
    REQUIRE(hero);
    CHECK(hero->width > 0);

    CHECK_FALSE(decodeImageFile(kData / "broken.png", &error));
    CHECK(error.find("not a readable") != std::string::npos);
    CHECK_FALSE(decodeImageFile(kData / "no_such_file.jpg", &error));
    CHECK(error.find("could not be opened") != std::string::npos);
}

TEST_CASE("Rolo decodes every kind of PNG file", "[assets][image]")
{
    // The renderer draws PNG files from these pixels: no PNG library is in
    // the engine (ADR 0024). The files are made by tests/data/make_png_project.py:
    // a 128 x 128 colour wheel that fades out towards its edge.
    const fs::path folder = kData / "png_project/assets";
    const auto load = [&](const char* name) {
        std::string error;
        auto image = decodeImageFile(folder / name, &error);
        INFO(name << ": " << error);
        REQUIRE(image);
        REQUIRE(image->width == 128);
        REQUIRE(image->height == 128);
        REQUIRE(image->rgba.size() == 128u * 128u * 4u);
        return *image;
    };
    const auto at = [](const DecodedImage& image, int x, int y, int channel) {
        return static_cast<int>(image.rgba[static_cast<std::size_t>((y * 128 + x) * 4 + channel)]);
    };

    const DecodedImage rgba = load("rgba.png");
    CHECK(rgba.hasAlpha);
    CHECK(at(rgba, 64, 64, 3) == 255);   // solid in the middle
    CHECK(at(rgba, 0, 0, 3) == 0);       // clear in the corners
    CHECK(at(rgba, 120, 64, 0) > 200);   // red to the right

    const DecodedImage rgb = load("rgb.png");
    CHECK_FALSE(rgb.hasAlpha);
    CHECK(at(rgb, 0, 0, 3) == 255);
    CHECK(at(rgb, 120, 64, 0) == at(rgba, 120, 64, 0));

    // Greyscale files come out as grey pixels, not as red ones.
    const DecodedImage grey = load("grey.png");
    CHECK_FALSE(grey.hasAlpha);
    CHECK(at(grey, 100, 30, 0) == at(grey, 100, 30, 1));
    CHECK(at(grey, 100, 30, 1) == at(grey, 100, 30, 2));
    CHECK(at(grey, 100, 30, 3) == 255);

    const DecodedImage greyAlpha = load("grey_alpha.png");
    CHECK(greyAlpha.hasAlpha);
    CHECK(at(greyAlpha, 64, 64, 0) == at(greyAlpha, 64, 64, 2));
    CHECK(at(greyAlpha, 0, 0, 3) == 0);
    CHECK(at(greyAlpha, 64, 64, 3) == 255);

    // Files with a palette, without and with see-through entries.
    const DecodedImage palette = load("palette.png");
    CHECK_FALSE(palette.hasAlpha);
    CHECK(std::abs(at(palette, 120, 64, 0) - at(rgb, 120, 64, 0)) < 40);   // 32 colours: near, not equal
    const DecodedImage paletteAlpha = load("palette_alpha.png");
    CHECK(paletteAlpha.hasAlpha);
    CHECK(at(paletteAlpha, 0, 0, 3) < 40);
    CHECK(at(paletteAlpha, 64, 64, 3) > 215);

    // 16 bits per channel: black on the left to white on the right.
    const DecodedImage grey16 = load("grey16.png");
    CHECK_FALSE(grey16.hasAlpha);
    CHECK(at(grey16, 0, 5, 0) == 0);
    CHECK(at(grey16, 127, 5, 0) == 255);
    CHECK(std::abs(at(grey16, 64, 5, 1) - 128) <= 2);

    // An interlaced file holds the same picture as the plain one.
    const DecodedImage interlaced = load("interlaced.PNG");
    CHECK(interlaced.hasAlpha);
    CHECK(interlaced.rgba == rgba.rgba);

    // JPEG files never have transparency.
    const auto photo = decodeImageFile(kData / "photo.jpg");
    REQUIRE(photo);
    CHECK_FALSE(photo->hasAlpha);
}

TEST_CASE("Colours are multiplied by alpha as the renderer expects", "[assets][image]")
{
    DecodedImage image;
    image.width = 4;
    image.height = 1;
    image.hasAlpha = true;
    image.rgba = {200, 100, 50, 128,   255, 255, 255, 255,   255, 128, 7, 0,   10, 20, 30, 1};
    premultiplyAlpha(image);
    // Each colour becomes colour * (alpha + 1) / 256, rounded down: the
    // same sum the engine's PNG library path used, so pictures look as before.
    const std::vector<std::uint8_t> expected = {100, 50, 25, 128,   255, 255, 255, 255,   0, 0, 0, 0,   0, 0, 0, 1};
    CHECK(image.rgba == expected);
}

TEST_CASE("Rolo writes PNG files itself", "[assets][image]")
{
    // Screenshots are written with this. What is written must read back exactly.
    const int w = 37;
    const int h = 21;
    for (const int channels : {3, 4}) {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * channels));
        std::uint32_t seed = 12345u + static_cast<std::uint32_t>(channels);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            // Half noise (hard to compress), half flat (easy): both paths of the writer.
            seed = seed * 1664525u + 1013904223u;
            pixels[i] = i < pixels.size() / 2 ? static_cast<std::uint8_t>(seed >> 24) : std::uint8_t{90};
        }
        const std::vector<std::uint8_t> png = encodePng(pixels.data(), w, h, channels);
        REQUIRE(png.size() > 8);
        CHECK(png[1] == 'P');
        CHECK(png[2] == 'N');
        CHECK(png[3] == 'G');
        std::string error;
        const auto back = decodeImage(png.data(), png.size(), &error);
        INFO(error);
        REQUIRE(back);
        REQUIRE(back->width == w);
        REQUIRE(back->height == h);
        CHECK(back->hasAlpha == (channels == 4));
        bool same = true;
        for (int i = 0; i < w * h && same; ++i) {
            for (int c = 0; c < 4; ++c) {
                const int wrote = c < channels ? pixels[static_cast<std::size_t>(i * channels + c)] : 255;
                same = same && back->rgba[static_cast<std::size_t>(i * 4 + c)] == wrote;
            }
        }
        CHECK(same);
    }

    const std::vector<std::uint8_t> red(4 * 4 * 3, 200);
    CHECK(encodePng(nullptr, 4, 4, 3).empty());
    CHECK(encodePng(red.data(), 0, 4, 3).empty());
    CHECK(encodePng(red.data(), 4, 4, 2).empty());

    const fs::path folder = rolo::test::uniqueTempFolder("png-write");
    fs::create_directories(folder);
    std::string error;
    REQUIRE(writePngFile(folder / "shot.png", red.data(), 4, 4, 3, &error));
    const auto shot = decodeImageFile(folder / "shot.png", &error);
    REQUIRE(shot);
    CHECK(shot->width == 4);
    CHECK(shot->rgba[0] == 200);
    CHECK_FALSE(writePngFile(folder / "no/such/folder/shot.png", red.data(), 4, 4, 3, &error));
    CHECK(error.find("could not be opened") != std::string::npos);
    fs::remove_all(folder);
}
