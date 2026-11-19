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

// --- Linked copies of prefabs (ADR 0035) --------------------------------------------------------------

namespace {

const AssetId kBulletPrefab{0xB0B0};

// A project's prefabs for the reader and writer: here, one text that tests change.
struct OnePrefab {
    std::string text;
    bool there = true;
    PrefabTexts texts()
    {
        return [this](AssetId id) -> std::optional<std::string> {
            return there && id == kBulletPrefab ? std::optional<std::string>(text) : std::nullopt;
        };
    }
};

// The prefab used below: Bullet (a Shape) with a Glow and a Trail inside.
Scene bulletWorkshop(EntityId& bullet, EntityId& glow, EntityId& trail)
{
    Scene workshop("Workshop", EntityIdGenerator(77));
    bullet = workshop.create("Bullet")->id();
    workshop.addComponent(bullet, std::make_unique<Shape>());
    workshop.setProperty(bullet, "rolo.Shape", "size", Vec2{8.0f, 4.0f});
    glow = workshop.create("Glow", bullet)->id();
    workshop.addComponent(glow, std::make_unique<Shape>());
    trail = workshop.create("Trail", bullet)->id();
    return workshop;
}

// Puts a linked copy of the prefab text into the scene, as the editor does.
EntityId placeLinked(Scene& scene, const std::string& prefabText)
{
    const SceneLoadResult prefab = parsePrefab(prefabText, registry());
    REQUIRE(prefab.ok());
    const EntitySnapshot copy = linkedCopyOf(scene, *prefab.scene, kBulletPrefab);
    REQUIRE(scene.restore(copy, EntityId{}, scene.roots().size()));
    return copy.rootId();
}

const Entity* childNamed(const Scene& scene, EntityId parent, const std::string& name)
{
    for (const EntityId id : scene.find(parent)->children()) {
        if (scene.find(id)->name() == name) {
            return scene.find(id);
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("A linked copy of a prefab is written as a link and read back whole", "[scene][file][linked]")
{
    EntityId bullet, glow, trail;
    const Scene workshop = bulletWorkshop(bullet, glow, trail);
    OnePrefab prefabs{serializePrefab(workshop, bullet)};

    Scene scene("Level", EntityIdGenerator(5));
    scene.create("Floor");
    const EntityId copy = placeLinked(scene, prefabs.text);
    CHECK(scene.find(copy)->isPrefabCopy());
    CHECK(scene.find(copy)->prefabLink().source == bullet);
    CHECK(copy != bullet);

    const std::string text = serializeScene(scene, prefabs.texts());
    CHECK(text.find("\"version\": 2") != std::string::npos);
    CHECK(text.find("\"prefab\": \"" + kBulletPrefab.toString() + "\"") != std::string::npos);
    CHECK(text.find("\"from\": \"" + glow.toString() + "\"") != std::string::npos);
    // Nothing of the prefab's own is repeated in the scene.
    CHECK(text.find("Glow") == std::string::npos);
    CHECK(text.find("rolo.Shape") == std::string::npos);
    CHECK(text.find("\"components\": [") == text.rfind("\"components\": ["));   // only the Floor's

    const SceneLoadResult back = parseScene(text, registry(), prefabs.texts());
    REQUIRE(back.ok());
    CHECK(back.warningCount() == 0);
    REQUIRE(back.scene->size() == 4);
    const Entity* root = back.scene->find(copy);
    REQUIRE(root != nullptr);
    CHECK(root->name() == "Bullet");
    CHECK(root->prefabLink().prefab == kBulletPrefab);
    CHECK(root->get<Shape>()->size == Vec2{8.0f, 4.0f});
    REQUIRE(root->children().size() == 2);
    // The same IDs as before: other objects and saved games refer to them.
    CHECK(root->children() == scene.find(copy)->children());
    CHECK(back.scene->find(root->children()[0])->prefabLink().source == glow);
    CHECK(serializeScene(*back.scene, prefabs.texts()) == text);

    // A scene without linked copies is a version 1 file still.
    Scene plain("Plain", EntityIdGenerator(6));
    plain.create("Floor");
    CHECK(serializeScene(plain, prefabs.texts()).find("\"version\": 1") != std::string::npos);
    // Written without the project's prefabs at hand, a copy is written in full, as ordinary objects.
    const std::string full = serializeScene(scene);
    CHECK(full.find("\"version\": 1") != std::string::npos);
    CHECK(full.find("Glow") != std::string::npos);
    CHECK(full.find("\"prefab\"") == std::string::npos);
}

TEST_CASE("A linked copy keeps its own changes and follows the prefab in everything else", "[scene][file][linked]")
{
    EntityId bullet, glow, trail;
    Scene workshop = bulletWorkshop(bullet, glow, trail);
    OnePrefab prefabs{serializePrefab(workshop, bullet)};

    Scene scene("Level", EntityIdGenerator(5));
    const EntityId copy = placeLinked(scene, prefabs.text);
    const EntityId copyGlow = childNamed(scene, copy, "Glow")->id();
    const EntityId copyTrail = childNamed(scene, copy, "Trail")->id();
    // The copy's own: a place, a name, a colour, a component more and one less, something inside, something gone.
    scene.setProperty(copy, "rolo.Transform", "position", Vec2{40.0f, 8.0f});
    scene.rename(copy, "Red Bullet");
    scene.setProperty(copy, "rolo.Shape", "color", Color{255, 0, 0, 255});
    scene.addComponent(copy, std::make_unique<Collider>());
    scene.setActive(copyGlow, false);
    scene.removeComponent(copyGlow, "rolo.Shape");
    const EntityId spark = scene.create("Spark", copyGlow)->id();
    scene.remove(copyTrail);

    const std::string text = serializeScene(scene, prefabs.texts());
    CHECK(text.find("\"removed\"") != std::string::npos);
    CHECK(text.find("\"removedComponents\"") != std::string::npos);
    CHECK(text.find("\"filled\"") == std::string::npos);   // what the Shape has from the prefab is not written

    {
        const SceneLoadResult back = parseScene(text, registry(), prefabs.texts());
        REQUIRE(back.ok());
        CHECK(back.warningCount() == 0);
        CHECK(serializeScene(*back.scene, prefabs.texts()) == text);
        CHECK(back.scene->size() == 3);
    }

    // The prefab changes: bigger, the Glow renamed and moved, the Trail too, and a Flash more.
    workshop.setProperty(bullet, "rolo.Shape", "size", Vec2{16.0f, 8.0f});
    workshop.setProperty(bullet, "rolo.Shape", "color", Color{0, 255, 0, 255});
    workshop.setProperty(bullet, "rolo.Transform", "position", Vec2{-5.0f, -5.0f});
    workshop.rename(glow, "Halo");
    workshop.setProperty(glow, "rolo.Transform", "position", Vec2{1.0f, 2.0f});
    const EntityId flash = workshop.create("Flash", bullet)->id();
    prefabs.text = serializePrefab(workshop, bullet);

    const SceneLoadResult now = parseScene(text, registry(), prefabs.texts());
    REQUIRE(now.ok());
    const Scene& s = *now.scene;
    const Entity* root = s.find(copy);
    REQUIRE(root != nullptr);
    CHECK(root->name() == "Red Bullet");                                    // its own
    CHECK(root->transform().position == Vec2{40.0f, 8.0f});                 // its own
    CHECK(root->get<Shape>()->color == Color{255, 0, 0, 255});              // its own
    CHECK(root->get<Shape>()->size == Vec2{16.0f, 8.0f});                   // follows the prefab
    CHECK(root->get<Collider>() != nullptr);                                // its own
    const Entity* halo = s.find(copyGlow);
    REQUIRE(halo != nullptr);
    CHECK(halo->name() == "Halo");                                          // follows
    CHECK(halo->transform().position == Vec2{1.0f, 2.0f});                  // follows
    CHECK_FALSE(halo->isActive());                                          // its own
    CHECK(halo->get<Shape>() == nullptr);                                   // its own
    REQUIRE(s.find(spark) != nullptr);
    CHECK(s.find(spark)->parent() == copyGlow);                             // what the copy put inside stays
    CHECK(s.find(spark)->prefabLink().none());
    CHECK(childNamed(s, copy, "Trail") == nullptr);                         // gone stays gone
    const Entity* newFlash = childNamed(s, copy, "Flash");
    REQUIRE(newFlash != nullptr);                                           // what the prefab gained arrives
    CHECK(newFlash->prefabLink().source == flash);
    // ...with the same ID every time the scene is read, until it is saved.
    const SceneLoadResult again = parseScene(text, registry(), prefabs.texts());
    CHECK(childNamed(*again.scene, copy, "Flash")->id() == newFlash->id());
    CHECK(newFlash->id() != flash);

    // Saved again and read again: the same scene.
    const std::string saved = serializeScene(s, prefabs.texts());
    const SceneLoadResult reread = parseScene(saved, registry(), prefabs.texts());
    REQUIRE(reread.ok());
    CHECK(serializeScene(*reread.scene, prefabs.texts()) == saved);
    CHECK(childNamed(*reread.scene, copy, "Flash")->id() == newFlash->id());

    // The prefab loses the Glow that the copy had put a Spark into: the Spark goes to the copy's top object.
    workshop.remove(glow);
    prefabs.text = serializePrefab(workshop, bullet);
    const SceneLoadResult without = parseScene(text, registry(), prefabs.texts());
    REQUIRE(without.ok());
    CHECK(without.scene->find(copyGlow) == nullptr);
    REQUIRE(without.scene->find(spark) != nullptr);
    CHECK(without.scene->find(spark)->parent() == copy);
}

TEST_CASE("A copy whose prefab is missing is kept as it was, and comes back with the prefab", "[scene][file][linked]")
{
    EntityId bullet, glow, trail;
    const Scene workshop = bulletWorkshop(bullet, glow, trail);
    OnePrefab prefabs{serializePrefab(workshop, bullet)};
    Scene scene("Level", EntityIdGenerator(5));
    const EntityId copy = placeLinked(scene, prefabs.text);
    scene.setProperty(copy, "rolo.Transform", "position", Vec2{40.0f, 8.0f});
    const EntityId copyGlow = childNamed(scene, copy, "Glow")->id();
    const EntityId spark = scene.create("Spark", copyGlow)->id();
    const std::string text = serializeScene(scene, prefabs.texts());

    prefabs.there = false;   // the prefab file was deleted
    const SceneLoadResult missing = parseScene(text, registry(), prefabs.texts());
    REQUIRE(missing.ok());   // the scene still opens
    CHECK(mentions(missing, "copy of a prefab that could not be read"));
    const Entity* standIn = missing.scene->find(copy);
    REQUIRE(standIn != nullptr);
    CHECK(standIn->transform().position == Vec2{40.0f, 8.0f});   // it stands where the copy stood
    CHECK(missing.scene->find(spark) != nullptr);
    // Saved like that, the file says what it said.
    CHECK(serializeScene(*missing.scene, prefabs.texts()) == text);
    // Read with no way to read prefabs at all (a tool that gives none): the same.
    const SceneLoadResult blind = parseScene(text, registry());
    REQUIRE(blind.ok());
    CHECK(serializeScene(*blind.scene) == text);

    prefabs.there = true;    // and it is back
    const SceneLoadResult back = parseScene(serializeScene(*missing.scene, prefabs.texts()), registry(), prefabs.texts());
    REQUIRE(back.ok());
    CHECK(back.warningCount() == 0);
    CHECK(back.scene->size() == 4);
    CHECK(back.scene->find(copy)->get<Shape>() != nullptr);
    CHECK(back.scene->find(spark)->parent() == copyGlow);

    // A damaged prefab file is a missing one.
    prefabs.text = "{ not json";
    CHECK(mentions(parseScene(text, registry(), prefabs.texts()), "it is not a prefab file"));
}

TEST_CASE("Copies of a linked copy are linked copies, and older scene files still open", "[scene][file][linked]")
{
    EntityId bullet, glow, trail;
    const Scene workshop = bulletWorkshop(bullet, glow, trail);
    OnePrefab prefabs{serializePrefab(workshop, bullet)};
    Scene scene("Level", EntityIdGenerator(5));
    const EntityId first = placeLinked(scene, prefabs.text);
    // Duplicate, as the editor does it.
    const EntitySnapshot twin = scene.withNewIds(scene.capture(first));
    REQUIRE(scene.restore(twin, EntityId{}, 1));
    scene.setProperty(twin.rootId(), "rolo.Transform", "position", Vec2{9.0f, 9.0f});

    const std::string text = serializeScene(scene, prefabs.texts());
    const SceneLoadResult back = parseScene(text, registry(), prefabs.texts());
    REQUIRE(back.ok());
    CHECK(back.scene->size() == 6);
    CHECK(back.scene->find(first)->isPrefabCopy());
    CHECK(back.scene->find(twin.rootId())->isPrefabCopy());
    CHECK(back.scene->find(twin.rootId())->transform().position == Vec2{9.0f, 9.0f});
    CHECK(text.find("Glow") == std::string::npos);

    // A version 1 file (before linked copies) reads as it always did, and is written as version 1.
    const std::string old = "{\"format\": \"rolo.scene\", \"version\": 1, \"name\": \"Old\", \"entities\": ["
                            "{\"id\": \"00000000000000a1\", \"name\": \"Rock\", \"active\": true, \"components\": []}]}";
    const SceneLoadResult oldScene = parseScene(old, registry(), prefabs.texts());
    REQUIRE(oldScene.ok());
    CHECK(oldScene.warningCount() == 0);
    CHECK(oldScene.scene->size() == 1);
    CHECK(serializeScene(*oldScene.scene, prefabs.texts()).find("\"version\": 1") != std::string::npos);
    // A file from a newer Rolo is refused.
    std::string newer = old;
    newer.replace(newer.find("\"version\": 1"), 12, "\"version\": 4");
    CHECK(mentions(parseScene(newer, registry()), "newer version of Rolo"));
}

// --- Prefabs inside prefabs (ADR 0036) ----------------------------------------------------------------

namespace {

const AssetId kDoorPrefab{0xD00A};
const AssetId kHousePrefab{0xA005E};

// A project with several prefabs, as texts that tests change.
struct ManyPrefabs {
    std::vector<std::pair<AssetId, std::string>> all;
    std::string& text(AssetId id)
    {
        for (auto& [known, held] : all) {
            if (known == id) {
                return held;
            }
        }
        all.emplace_back(id, std::string());
        return all.back().second;
    }
    PrefabTexts texts()
    {
        return [this](AssetId id) -> std::optional<std::string> {
            for (const auto& [known, held] : all) {
                if (known == id && !held.empty()) {
                    return held;
                }
            }
            return std::nullopt;
        };
    }
};

EntityId placeCopy(Scene& scene, ManyPrefabs& project, AssetId asset, EntityId parent = {})
{
    const SceneLoadResult prefab = parsePrefab(project.text(asset), registry(), project.texts());
    REQUIRE(prefab.ok());
    const EntitySnapshot copy = linkedCopyOf(scene, *prefab.scene, asset);
    const std::size_t index = parent.isNull() ? scene.roots().size() : scene.find(parent)->children().size();
    REQUIRE(scene.restore(copy, parent, index));
    return copy.rootId();
}

// Door (a Shape) with a Knob (a Shape) inside; House with a Wall and a linked copy of Door inside.
struct Village {
    ManyPrefabs project;
    EntityId door, knob, house, wall, houseDoor;
    Scene doorWorkshop{"Doors", EntityIdGenerator(21)};
    Scene houseWorkshop{"Houses", EntityIdGenerator(22)};

    Village()
    {
        door = doorWorkshop.create("Door")->id();
        doorWorkshop.addComponent(door, std::make_unique<Shape>());
        knob = doorWorkshop.create("Knob", door)->id();
        doorWorkshop.addComponent(knob, std::make_unique<Shape>());
        project.text(kDoorPrefab) = serializePrefab(doorWorkshop, door);

        house = houseWorkshop.create("House")->id();
        wall = houseWorkshop.create("Wall", house)->id();
        houseDoor = placeCopy(houseWorkshop, project, kDoorPrefab, house);
        houseWorkshop.setProperty(houseDoor, "rolo.Transform", "position", Vec2{30.0f, 0.0f});
        project.text(kHousePrefab) = serializePrefab(houseWorkshop, house, project.texts());
    }
};

} // namespace

TEST_CASE("A prefab keeps a copy of another prefab inside it as a link", "[scene][file][nested]")
{
    Village v;
    const std::string& houseText = v.project.text(kHousePrefab);
    // The door is a link in the house's file: its Knob is not written there.
    CHECK(houseText.find("\"prefab\": \"" + kDoorPrefab.toString() + "\"") != std::string::npos);
    CHECK(houseText.find("Knob") == std::string::npos);
    CHECK(houseText.find("\"version\": 3") != std::string::npos);
    // A prefab without one is a version 1 file still.
    CHECK(v.project.text(kDoorPrefab).find("\"version\": 1") != std::string::npos);

    const SceneLoadResult house = parsePrefab(houseText, registry(), v.project.texts());
    REQUIRE(house.ok());
    CHECK(house.warningCount() == 0);
    CHECK(house.scene->size() == 4);
    const Entity* top = house.scene->find(house.scene->roots().front());
    CHECK(top->id() == v.house);
    CHECK_FALSE(top->isPrefabCopy());
    const Entity* door = childNamed(*house.scene, v.house, "Door");
    REQUIRE(door != nullptr);
    CHECK(door->id() == v.houseDoor);
    CHECK(door->isPrefabCopy());
    CHECK(door->prefabLink().prefab == kDoorPrefab);
    CHECK(door->transform().position == Vec2{30.0f, 0.0f});
    REQUIRE(childNamed(*house.scene, door->id(), "Knob") != nullptr);
    // Written again, it is the same text.
    CHECK(serializePrefab(*house.scene, v.house, v.project.texts()) == houseText);

    // The door changes: the house has the new door.
    v.doorWorkshop.setProperty(v.knob, "rolo.Shape", "size", Vec2{3.0f, 3.0f});
    v.doorWorkshop.create("Bell", v.door);
    v.project.text(kDoorPrefab) = serializePrefab(v.doorWorkshop, v.door);
    const SceneLoadResult later = parsePrefab(houseText, registry(), v.project.texts());
    REQUIRE(later.ok());
    CHECK(later.scene->size() == 5);
    const Entity* laterDoor = childNamed(*later.scene, v.house, "Door");
    CHECK(childNamed(*later.scene, laterDoor->id(), "Knob")->get<Shape>()->size == Vec2{3.0f, 3.0f});
    CHECK(childNamed(*later.scene, laterDoor->id(), "Bell") != nullptr);

    // Without the project's prefabs, or without the door, the house has a stand-in and says so.
    const SceneLoadResult alone = parsePrefab(houseText, registry());
    REQUIRE(alone.ok());
    CHECK(mentions(alone, "could not be read"));
    v.project.text(kDoorPrefab).clear();
    const SceneLoadResult doorless = parsePrefab(houseText, registry(), v.project.texts());
    REQUIRE(doorless.ok());
    CHECK(mentions(doorless, "could not be read"));
    // ...and writes the link back as it was.
    CHECK(serializePrefab(*doorless.scene, v.house, v.project.texts()) == houseText);
}

TEST_CASE("A scene's copy of a prefab follows the prefabs inside that prefab", "[scene][file][nested]")
{
    Village v;
    Scene scene("Level", EntityIdGenerator(5));
    const EntityId copy = placeCopy(scene, v.project, kHousePrefab);
    REQUIRE(scene.size() == 4);
    const Entity* door = childNamed(scene, copy, "Door");
    REQUIRE(door != nullptr);
    const EntityId doorId = door->id();
    // In the scene the door is simply part of the house's copy.
    CHECK_FALSE(door->isPrefabCopy());
    CHECK(door->prefabLink().source == v.houseDoor);
    const EntityId knobId = childNamed(scene, doorId, "Knob")->id();
    // This copy's knob has a colour of its own.
    scene.setProperty(knobId, "rolo.Shape", "color", Color{0, 255, 0, 255});

    const std::string text = serializeScene(scene, v.project.texts());
    CHECK(text.find("Wall") == std::string::npos);
    CHECK(text.find("Knob") == std::string::npos);
    CHECK(text.find(kDoorPrefab.toString()) == std::string::npos);   // the scene knows the house only
    CHECK(text.find("\"version\": 2") != std::string::npos);

    // The door prefab changes: bigger knob, and a Bell.
    v.doorWorkshop.setProperty(v.knob, "rolo.Shape", "size", Vec2{3.0f, 3.0f});
    v.doorWorkshop.create("Bell", v.door);
    v.project.text(kDoorPrefab) = serializePrefab(v.doorWorkshop, v.door);

    const SceneLoadResult back = parseScene(text, registry(), v.project.texts());
    REQUIRE(back.ok());
    CHECK(back.warningCount() == 0);
    CHECK(back.scene->size() == 5);
    REQUIRE(back.scene->find(doorId) != nullptr);
    REQUIRE(back.scene->find(knobId) != nullptr);
    CHECK(back.scene->find(doorId)->transform().position == Vec2{30.0f, 0.0f});
    CHECK(back.scene->find(knobId)->get<Shape>()->size == Vec2{3.0f, 3.0f});
    CHECK(back.scene->find(knobId)->get<Shape>()->color == Color{0, 255, 0, 255});
    const Entity* bell = childNamed(*back.scene, doorId, "Bell");
    REQUIRE(bell != nullptr);
    // The Bell has the same ID every time the scene is read.
    const SceneLoadResult again = parseScene(text, registry(), v.project.texts());
    REQUIRE(again.ok());
    CHECK(again.scene->find(bell->id()) != nullptr);
    // Saved and read once more, nothing moves.
    const std::string saved = serializeScene(*back.scene, v.project.texts());
    const SceneLoadResult third = parseScene(saved, registry(), v.project.texts());
    REQUIRE(third.ok());
    CHECK(third.scene->size() == 5);
    CHECK(third.scene->find(bell->id()) != nullptr);
    CHECK(serializeScene(*third.scene, v.project.texts()) == saved);

    // The door prefab goes missing: the copy has an empty object there, with a warning, and loses nothing.
    const std::string doorText = v.project.text(kDoorPrefab);
    v.project.text(kDoorPrefab).clear();
    const SceneLoadResult doorless = parseScene(saved, registry(), v.project.texts());
    REQUIRE(doorless.ok());
    CHECK(mentions(doorless, "holds a copy of another prefab that could not be read"));
    CHECK(doorless.scene->find(doorId) != nullptr);
    const std::string keptText = serializeScene(*doorless.scene, v.project.texts());
    v.project.text(kDoorPrefab) = doorText;
    const SceneLoadResult returned = parseScene(keptText, registry(), v.project.texts());
    REQUIRE(returned.ok());
    REQUIRE(returned.scene->find(knobId) != nullptr);
    CHECK(returned.scene->find(knobId)->get<Shape>()->color == Color{0, 255, 0, 255});
}

TEST_CASE("A prefab that holds itself is not read without end", "[scene][file][nested]")
{
    Village v;
    CHECK(prefabHolds(v.project.texts(), kHousePrefab, kDoorPrefab));
    CHECK(prefabHolds(v.project.texts(), kHousePrefab, kHousePrefab));
    CHECK_FALSE(prefabHolds(v.project.texts(), kDoorPrefab, kHousePrefab));

    // By hand: the door is given a copy of the house, which holds the door.
    Scene loop(v.doorWorkshop);
    placeCopy(loop, v.project, kHousePrefab, v.door);
    v.project.text(kDoorPrefab) = serializePrefab(loop, v.door, v.project.texts());
    CHECK(prefabHolds(v.project.texts(), kDoorPrefab, kHousePrefab));
    const SceneLoadResult door = parsePrefab(v.project.text(kDoorPrefab), registry(), v.project.texts());
    REQUIRE(door.ok());
    CHECK(door.scene->size() < 20);
    Scene scene("Level", EntityIdGenerator(5));
    placeCopy(scene, v.project, kHousePrefab);
    const SceneLoadResult back = parseScene(serializeScene(scene, v.project.texts()), registry(), v.project.texts());
    REQUIRE(back.ok());
    CHECK(back.scene->size() < 20);
}

TEST_CASE("Files from before prefabs inside prefabs still open", "[scene][file][nested]")
{
    Village v;
    // A version 1 prefab.
    const std::string oldPrefab = "{\"format\": \"rolo.prefab\", \"version\": 1, \"entities\": ["
                                  "{\"id\": \"00000000000000b1\", \"name\": \"Rock\", \"active\": true, \"components\": []}]}";
    const SceneLoadResult rock = parsePrefab(oldPrefab, registry(), v.project.texts());
    REQUIRE(rock.ok());
    CHECK(rock.warningCount() == 0);
    CHECK(serializePrefab(*rock.scene, rock.scene->roots().front(), v.project.texts()).find("\"version\": 1")
          != std::string::npos);
    // A version 2 scene with a linked copy.
    const std::string oldScene = "{\"format\": \"rolo.scene\", \"version\": 2, \"name\": \"Old\", \"entities\": ["
                                 "{\"id\": \"00000000000000c1\", \"prefab\": \"" + kDoorPrefab.toString() + "\"}]}";
    const SceneLoadResult level = parseScene(oldScene, registry(), v.project.texts());
    REQUIRE(level.ok());
    CHECK(level.warningCount() == 0);
    CHECK(level.scene->size() == 2);
    CHECK(serializeScene(*level.scene, v.project.texts()).find("\"version\": 2") != std::string::npos);
}
