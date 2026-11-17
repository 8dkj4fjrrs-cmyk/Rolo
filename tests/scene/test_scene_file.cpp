#include "rolo/scene/SceneFile.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace rolo;

namespace {

const ComponentRegistry& registry()
{
    static const ComponentRegistry r = ComponentRegistry::withBuiltins();
    return r;
}

bool mentions(const SceneLoadResult& r, std::string_view text)
{
    for (const FileIssue& i : r.issues) {
        if (i.toString().find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string wrap(std::string entities)
{
    return R"({"format": "rolo.scene", "version": 1, "name": "T", "entities": [)" + entities + "]}";
}

} // namespace

TEST_CASE("A scene survives save and load unchanged", "[scene][file]")
{
    const Scene original = makeStartScene();
    const std::string text = serializeScene(original);
    const SceneLoadResult loaded = parseScene(text, registry());
    REQUIRE(loaded.ok());
    CHECK(loaded.issues.empty());
    CHECK(loaded.scene->name() == original.name());
    CHECK(loaded.scene->size() == original.size());
    CHECK(serializeScene(*loaded.scene) == text); // byte-for-byte stable

    // Same IDs, same tree, same values.
    original.forEach([&](const Entity& e, int) {
        const Entity* copy = loaded.scene->find(e.id());
        REQUIRE(copy != nullptr);
        CHECK(copy->name() == e.name());
        CHECK(copy->parent() == e.parent());
        CHECK(copy->children() == e.children());
        CHECK(copy->components().size() == e.components().size());
    });
}

TEST_CASE("Saved numbers are short and readable", "[scene][file]")
{
    Scene scene("Numbers");
    const EntityId id = scene.create("Box")->id();
    scene.setProperty(id, "rolo.Transform", "position", Vec2{0.1f, 640.0f});
    const std::string text = serializeScene(scene);
    CHECK(text.find("0.1,") != std::string::npos);
    CHECK(text.find("640") != std::string::npos);
    CHECK(text.find("0.10000000149") == std::string::npos);
    CHECK(text.find("640.0") == std::string::npos);
}

TEST_CASE("Files from a newer Rolo are refused with advice", "[scene][file]")
{
    const auto r = parseScene(R"({"format": "rolo.scene", "version": 99, "entities": []})", registry());
    CHECK_FALSE(r.ok());
    CHECK(r.firstError().find("newer version of Rolo") != std::string::npos);
}

TEST_CASE("Broken files give a clear error, never a crash", "[scene][file]")
{
    CHECK(parseScene("", registry()).firstError().find("not valid JSON") != std::string::npos);
    CHECK(parseScene("{\"format\": \"rolo.scene\",\n \"version\": 1, }", registry()).firstError().find("line 2")
          != std::string::npos);
    CHECK(parseScene(R"({"format": "something.else", "version": 1})", registry()).firstError().find("not a Rolo scene")
          != std::string::npos);
    CHECK_FALSE(parseScene(R"({"format": "rolo.scene", "version": 1})", registry()).ok()); // no entities
    CHECK_FALSE(parseScene(R"({"format": "rolo.scene", "entities": []})", registry()).ok()); // no version
    CHECK_FALSE(parseScene(wrap("42"), registry()).ok());
}

TEST_CASE("IDs must be present, valid and unique", "[scene][file]")
{
    const auto missing = parseScene(wrap(R"({"name": "A"})"), registry());
    CHECK_FALSE(missing.ok());
    CHECK(missing.firstError().find("16 hex digits") != std::string::npos);

    const auto twice = parseScene(wrap(R"({"id": "00000000000000aa", "name": "A"},
                                         {"id": "00000000000000aa", "name": "B"})"),
                                  registry());
    CHECK_FALSE(twice.ok());
    CHECK(twice.firstError().find("same ID") != std::string::npos);

    const auto orphan = parseScene(wrap(R"({"id": "00000000000000aa", "parent": "00000000000000bb"})"), registry());
    CHECK_FALSE(orphan.ok());
    CHECK(orphan.firstError().find("parent") != std::string::npos);
}

TEST_CASE("Bad values fall back to defaults with a warning", "[scene][file]")
{
    const auto r = parseScene(wrap(R"({"id": "00000000000000aa", "name": "A", "components": [
        {"type": "rolo.Transform", "position": "here", "rotation": 45, "scale": [1, 1]},
        {"type": "rolo.Shape", "kind": "triangle", "size": [-10, 5], "color": "#12345678", "wobble": 3}]})"),
                           registry());
    REQUIRE(r.ok());
    const Entity* e = r.scene->find(*EntityId::parse("00000000000000aa"));
    REQUIRE(e != nullptr);
    CHECK(e->transform().position == Vec2{});      // default
    CHECK(e->transform().rotation == 45.0f);       // the good value is kept
    CHECK(e->get<Shape>()->kind == ShapeKind::Rectangle);
    CHECK(e->get<Shape>()->size == Vec2{0, 5});    // clamped
    CHECK(e->get<Shape>()->color == Color{0x12, 0x34, 0x56, 0x78});
    CHECK(mentions(r, "\"position\" should be two numbers"));
    CHECK(mentions(r, "\"kind\" should be one of"));
    CHECK(mentions(r, "out of range"));
    CHECK(mentions(r, "Unknown property \"wobble\""));
    CHECK(r.warningCount() == 4);
}

TEST_CASE("Unknown component types are kept and saved unchanged", "[scene][file]")
{
    const std::string text = wrap(R"({"id": "00000000000000aa", "name": "A", "components": [
        {"type": "rolo.Transform", "position": [1, 2], "rotation": 0, "scale": [1, 1]},
        {"type": "mygame.Health", "max": 10, "regen": [1, 2, {"deep": true}]}]})");
    const auto r = parseScene(text, registry());
    REQUIRE(r.ok());
    CHECK(mentions(r, "not known to this version"));
    const Entity* e = r.scene->find(*EntityId::parse("00000000000000aa"));
    REQUIRE(e->component("mygame.Health") != nullptr);

    const std::string saved = serializeScene(*r.scene);
    CHECK(saved.find(R"("type": "mygame.Health")") != std::string::npos);
    CHECK(saved.find(R"("deep": true)") != std::string::npos);
    // ...and it can be read again with the same data.
    CHECK(parseScene(saved, registry()).ok());
}

TEST_CASE("Hierarchy and order come back from the file", "[scene][file]")
{
    const auto r = parseScene(wrap(R"(
        {"id": "00000000000000a1", "name": "Parent"},
        {"id": "00000000000000b2", "parent": "00000000000000a1", "name": "Second"},
        {"id": "00000000000000b1", "parent": "00000000000000a1", "name": "First", "active": false})"),
                              registry());
    REQUIRE(r.ok());
    const Entity* parent = r.scene->find(*EntityId::parse("00000000000000a1"));
    REQUIRE(parent->children().size() == 2);
    CHECK(r.scene->find(parent->children()[0])->name() == "Second"); // file order = drawing order
    CHECK_FALSE(r.scene->find(parent->children()[1])->isActive());
}

TEST_CASE("Scene files are written atomically and read back", "[scene][file]")
{
    const auto dir = std::filesystem::temp_directory_path() / "rolo_scene_file_test";
    std::filesystem::remove_all(dir);
    const auto path = dir / "nested" / "main.lscene";
    REQUIRE(saveSceneFile(path, makeStartScene()));
    CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    const auto r = loadSceneFile(path, registry());
    CHECK(r.ok());
    CHECK_FALSE(loadSceneFile(dir / "missing.lscene", registry()).ok());
    std::filesystem::remove_all(dir);
}

TEST_CASE("Validation accepts scenes built through the API", "[scene][file]")
{
    CHECK(validateScene(makeStartScene()).empty());
    CHECK(validateScene(Scene()).empty());
}

TEST_CASE("The hand-written test card scene loads cleanly", "[scene][file]")
{
    const auto r = loadSceneFile(std::filesystem::path(ROLO_TEST_DATA_DIR) / "test_card.lscene", registry());
    REQUIRE(r.ok());
    CHECK(r.issues.empty());
    CHECK(r.scene->name() == "Test Card");
    CHECK(r.scene->size() == 4);
}

TEST_CASE("Sprites keep their image ID through save and load", "[scene][file][sprite]")
{
    Scene scene("Sprites", EntityIdGenerator(7));
    Entity* hero = scene.create("Hero");
    auto sprite = std::make_unique<Sprite>();
    sprite->image = AssetId(0x8c1f02e94b7d3a60ULL);
    sprite->size = {24.0f, 32.0f};
    sprite->flipX = true;
    REQUIRE(scene.addComponent(hero->id(), std::move(sprite)) != nullptr);
    Entity* empty = scene.create("No image yet");
    REQUIRE(scene.addComponent(empty->id(), std::make_unique<Sprite>()) != nullptr);

    const std::string text = serializeScene(scene);
    CHECK(text.find("\"image\": \"8c1f02e94b7d3a60\"") != std::string::npos);
    CHECK(text.find("\"image\": null") != std::string::npos); // no image chosen

    const SceneLoadResult loaded = parseScene(text, registry());
    REQUIRE(loaded.ok());
    CHECK(loaded.issues.empty());
    const Sprite* back = loaded.scene->find(hero->id())->get<Sprite>();
    REQUIRE(back != nullptr);
    CHECK(back->image == AssetId(0x8c1f02e94b7d3a60ULL));
    CHECK(back->size == Vec2{24.0f, 32.0f});
    CHECK(back->flipX);
    CHECK(loaded.scene->find(empty->id())->get<Sprite>()->image.isNull());
    CHECK(serializeScene(*loaded.scene) == text);
}

TEST_CASE("A bad image ID falls back to no image, with a warning", "[scene][file][sprite]")
{
    const auto r = parseScene(wrap(R"({"id": "0000000000000001", "name": "A", "components": [
        {"type": "rolo.Sprite", "image": "not-an-id"}]})"),
                              registry());
    REQUIRE(r.ok());
    CHECK(mentions(r, "asset ID"));
    CHECK(r.scene->find(EntityId(1))->get<Sprite>()->image.isNull());
}

TEST_CASE("Sprites can be picked and outlined like shapes", "[scene][sprite]")
{
    Scene scene("Pick", EntityIdGenerator(3));
    Entity* e = scene.create("Hero");
    scene.setProperty(e->id(), "rolo.Transform", "position", Vec2{100.0f, 100.0f});
    auto sprite = std::make_unique<Sprite>();
    sprite->size = {20.0f, 40.0f};
    scene.addComponent(e->id(), std::move(sprite));

    CHECK(scene.hitTest({105.0f, 115.0f}) == e->id());
    CHECK(scene.hitTest({115.0f, 100.0f}).isNull()); // outside the 20-point width
    const auto corners = scene.worldCorners(e->id());
    REQUIRE(corners.has_value());
    CHECK((*corners)[0] == Vec2{90.0f, 80.0f});
    CHECK((*corners)[2] == Vec2{110.0f, 120.0f});

    Entity* camera = scene.create("Camera");
    scene.addComponent(camera->id(), std::make_unique<Camera>());
    CHECK_FALSE(scene.worldCorners(camera->id()).has_value()); // a camera draws nothing
}

// --- Prefabs: an object kept as a file (ADR 0033) ------------------------------------------------

TEST_CASE("A prefab file holds one object and what is inside it", "[scene][file][prefab]")
{
    Scene scene("Level", EntityIdGenerator(5));
    scene.create("Floor");
    const EntityId bullet = scene.create("Bullet")->id();
    scene.setProperty(bullet, "rolo.Transform", "position", Vec2{12.0f, 34.0f});
    scene.setActive(bullet, false);
    const EntityId glow = scene.create("Glow", bullet)->id();

    const std::string text = serializePrefab(scene, bullet);
    CHECK(text.find("\"rolo.prefab\"") != std::string::npos);
    CHECK(text.find("Floor") == std::string::npos);
    CHECK(text.find("\"name\": \"Level\"") == std::string::npos);   // no scene name, no gravity

    const SceneLoadResult back = parsePrefab(text, registry());
    REQUIRE(back.ok());
    REQUIRE(back.scene->roots().size() == 1);
    CHECK(back.scene->size() == 2);
    const Entity* root = back.scene->find(back.scene->roots().front());
    CHECK(root->id() == bullet);   // saving it again changes nothing
    CHECK(root->name() == "Bullet");
    CHECK_FALSE(root->isActive());
    CHECK(root->get<Transform>()->position == Vec2{12.0f, 34.0f});
    REQUIRE(root->children().size() == 1);
    CHECK(root->children().front() == glow);
    CHECK(serializePrefab(*back.scene, bullet) == text);

    CHECK(serializePrefab(scene, EntityId{}).empty());
}

TEST_CASE("Prefab files that are not prefabs are refused, saying why", "[scene][file][prefab]")
{
    Scene scene("Level", EntityIdGenerator(5));
    const EntityId a = scene.create("A")->id();
    scene.create("B");

    // A scene is not a prefab, and a prefab is not a scene.
    const SceneLoadResult sceneAsPrefab = parsePrefab(serializeScene(scene), registry());
    CHECK_FALSE(sceneAsPrefab.ok());
    CHECK(mentions(sceneAsPrefab, "not a Rolo prefab file"));
    const SceneLoadResult prefabAsScene = parseScene(serializePrefab(scene, a), registry());
    CHECK_FALSE(prefabAsScene.ok());
    CHECK(mentions(prefabAsScene, "not a Rolo scene file"));

    // Two objects at the top (edited by hand).
    std::string two = serializeScene(scene);
    two.replace(two.find("rolo.scene"), 10, "rolo.prefab");
    const SceneLoadResult twoRoots = parsePrefab(two, registry());
    CHECK_FALSE(twoRoots.ok());
    CHECK(mentions(twoRoots, "this file has 2 objects at the top"));

    const std::string none = "{\"format\": \"rolo.prefab\", \"version\": 1, \"entities\": []}";
    CHECK_FALSE(parsePrefab(none, registry()).ok());
    const std::string newer = "{\"format\": \"rolo.prefab\", \"version\": 99, \"entities\": []}";
    CHECK(mentions(parsePrefab(newer, registry()), "newer version of Rolo"));
    const std::string noFormat = "{\"format\": 3, \"version\": 1, \"entities\": []}";
    CHECK_FALSE(parsePrefab(noFormat, registry()).ok());
}
