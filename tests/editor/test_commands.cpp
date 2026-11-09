#include "support/TempFolder.hpp"
#include "rolo/assets/Animation.hpp"
#include "Commands.hpp"
#include "SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace rolo;
using namespace rolo::editor;

namespace {

struct Fixture {
    SceneDocument doc{Scene("Test", EntityIdGenerator(9))};
    Scene& scene() { return const_cast<Scene&>(doc.scene()); }

    EntityId make(const char* name, EntityId parent = {})
    {
        auto cmd = std::make_unique<CreateEntity>(name, parent, std::nullopt);
        CreateEntity* raw = cmd.get();
        REQUIRE(doc.execute(std::move(cmd)));
        return raw->id();
    }
};

std::string serialized(const Scene& scene)
{
    return serializeScene(scene);
}

} // namespace

TEST_CASE("Creating can be undone and redone with the same ID", "[editor][commands]")
{
    Fixture f;
    const EntityId id = f.make("Player");
    CHECK(f.doc.isDirty());
    CHECK(f.doc.history().undoLabel() == "Create Player");
    REQUIRE(f.doc.undo());
    CHECK(f.doc.scene().empty());
    CHECK_FALSE(f.doc.isDirty());
    REQUIRE(f.doc.redo());
    CHECK(f.doc.scene().contains(id)); // same ID: later commands still refer to it
}

TEST_CASE("Deleting restores the whole subtree, IDs and order", "[editor][commands]")
{
    Fixture f;
    const EntityId a = f.make("A");
    const EntityId b = f.make("B");
    const EntityId child = f.make("Child", b);
    f.make("C");
    f.doc.execute(std::make_unique<SetProperty>(child, "rolo.Transform", "rotation", 45.0f, "Rotation"));
    const std::string before = serialized(f.doc.scene());

    REQUIRE(f.doc.execute(std::make_unique<DeleteEntity>(b)));
    CHECK_FALSE(f.doc.scene().contains(child));
    CHECK(f.doc.history().undoLabel() == "Delete B");
    REQUIRE(f.doc.undo());
    CHECK(serialized(f.doc.scene()) == before);
    CHECK(f.doc.scene().roots()[1] == b);
    static_cast<void>(a);
}

TEST_CASE("Duplicating copies children with new IDs, right after the original", "[editor][commands]")
{
    Fixture f;
    const EntityId a = f.make("A");
    f.make("Inner", a);
    f.make("Z");
    auto dup = std::make_unique<DuplicateEntity>(a);
    DuplicateEntity* raw = dup.get();
    REQUIRE(f.doc.execute(std::move(dup)));
    const EntityId copy = raw->copyId();
    REQUIRE(f.doc.scene().contains(copy));
    CHECK(copy != a);
    CHECK(f.doc.scene().roots()[1] == copy);
    CHECK(f.doc.scene().find(copy)->children().size() == 1);

    f.doc.undo();
    CHECK_FALSE(f.doc.scene().contains(copy));
    f.doc.redo();
    CHECK(f.doc.scene().contains(copy)); // the same copy comes back
}

TEST_CASE("Property edits undo to the original value and merge while dragging", "[editor][commands]")
{
    Fixture f;
    const EntityId id = f.make("Box");
    f.doc.history().breakMerge();
    for (float x : {1.0f, 2.0f, 3.0f}) {
        f.doc.execute(std::make_unique<SetProperty>(id, "rolo.Transform", "position", Vec2{x, 0}, "Position"));
    }
    CHECK(f.doc.scene().find(id)->transform().position.x == 3.0f);
    CHECK(f.doc.history().size() == 2); // create + one merged edit
    f.doc.undo();
    CHECK(f.doc.scene().find(id)->transform().position.x == 0.0f);

    // After a break, the next edit is its own step.
    f.doc.redo();
    f.doc.history().breakMerge();
    f.doc.execute(std::make_unique<SetProperty>(id, "rolo.Transform", "position", Vec2{9, 0}, "Position"));
    CHECK(f.doc.history().size() == 3);
}

TEST_CASE("Edits that change nothing make no undo step", "[editor][commands]")
{
    Fixture f;
    const EntityId id = f.make("Box");
    CHECK_FALSE(f.doc.execute(std::make_unique<RenameEntity>(id, "Box")));
    CHECK_FALSE(f.doc.execute(std::make_unique<SetProperty>(id, "rolo.Transform", "rotation", 0.0f, "Rotation")));
    CHECK_FALSE(f.doc.execute(std::make_unique<SetEntityActive>(id, true)));
    CHECK_FALSE(f.doc.execute(std::make_unique<SetProperty>(id, "rolo.Shape", "size", Vec2{}, "Size"))); // no Shape
    CHECK(f.doc.history().size() == 1);
}

TEST_CASE("Components can be added and removed, and put back in place", "[editor][commands]")
{
    Fixture f;
    const EntityId id = f.make("Box");
    REQUIRE(f.doc.execute(std::make_unique<AddComponent>(id, std::make_unique<Shape>())));
    f.doc.execute(std::make_unique<SetProperty>(id, "rolo.Shape", "color", Color{1, 2, 3, 4}, "Color"));
    REQUIRE(f.doc.execute(std::make_unique<RemoveComponent>(id, "rolo.Shape")));
    CHECK(f.doc.scene().find(id)->get<Shape>() == nullptr);
    CHECK_FALSE(f.doc.execute(std::make_unique<RemoveComponent>(id, "rolo.Transform"))); // required
    f.doc.undo();
    REQUIRE(f.doc.scene().find(id)->get<Shape>() != nullptr);
    CHECK(f.doc.scene().find(id)->get<Shape>()->color == Color{1, 2, 3, 4}); // values come back too
    CHECK(f.doc.scene().componentIndex(id, "rolo.Shape") == 1u);
}

TEST_CASE("Renaming, hiding and moving undo cleanly", "[editor][commands]")
{
    Fixture f;
    const EntityId a = f.make("A");
    const EntityId b = f.make("B");
    f.doc.execute(std::make_unique<RenameEntity>(a, "Hero"));
    f.doc.execute(std::make_unique<SetEntityActive>(a, false));
    f.doc.execute(std::make_unique<MoveEntity>(b, EntityId{}, 0));
    CHECK(f.doc.scene().roots().front() == b);
    CHECK(f.doc.scene().find(a)->name() == "Hero");
    CHECK_FALSE(f.doc.scene().find(a)->isActive());
    f.doc.undo();
    f.doc.undo();
    f.doc.undo();
    CHECK(f.doc.scene().roots().front() == a);
    CHECK(f.doc.scene().find(a)->name() == "A");
    CHECK(f.doc.scene().find(a)->isActive());
    CHECK_FALSE(f.doc.execute(std::make_unique<MoveEntity>(a, a, 0))); // not inside itself
}

TEST_CASE("A new command clears the redo history", "[editor][commands]")
{
    Fixture f;
    f.make("A");
    f.make("B");
    f.doc.undo();
    CHECK(f.doc.history().canRedo());
    f.make("C");
    CHECK_FALSE(f.doc.history().canRedo());
}

TEST_CASE("The document knows when it has unsaved changes", "[editor][document]")
{
    const auto dir = std::filesystem::temp_directory_path() / "rolo_document_test";
    std::filesystem::remove_all(dir);
    Fixture f;
    CHECK(f.doc.isUntitled());
    CHECK(f.doc.displayName() == "Untitled");
    CHECK_FALSE(f.doc.save()); // no path yet
    f.make("A");
    REQUIRE(f.doc.saveAs(dir / "level.lscene"));
    CHECK_FALSE(f.doc.isDirty());
    CHECK(f.doc.displayName() == "level.lscene");

    f.make("B");
    CHECK(f.doc.isDirty());
    f.doc.undo();
    CHECK_FALSE(f.doc.isDirty()); // back at the saved state

    // The saved state can no longer be reached once history moves on without it.
    f.doc.undo();
    f.make("C");
    CHECK(f.doc.isDirty());

    std::vector<FileIssue> issues;
    const auto reopened = SceneDocument::open(dir / "level.lscene", ComponentRegistry::withBuiltins(), issues);
    REQUIRE(reopened);
    CHECK(reopened->scene().size() == 1);
    CHECK_FALSE(reopened->isDirty());
    std::filesystem::remove_all(dir);
}

TEST_CASE("The history has a size limit", "[editor][commands]")
{
    Scene scene;
    CommandStack stack(3);
    const EntityId id = scene.create("A")->id();
    for (int i = 0; i < 5; ++i) {
        stack.breakMerge();
        stack.execute(scene, std::make_unique<RenameEntity>(id, "Name " + std::to_string(i)));
    }
    CHECK(stack.size() == 3);
    CHECK_FALSE(stack.isClean()); // the clean state fell off the end
    while (stack.undo(scene)) {
    }
    CHECK(scene.find(id)->name() == "Name 1");
}

TEST_CASE("A command group undoes as one step, in reverse order", "[editor][commands]")
{
    Fixture f;
    const EntityId id = f.make("Box");
    auto group = std::make_unique<CommandGroup>("Reset Transform");
    group->add(std::make_unique<SetProperty>(id, "rolo.Transform", "rotation", 10.0f, "Rotation"));
    group->add(std::make_unique<SetProperty>(id, "rolo.Transform", "rotation", 20.0f, "Rotation"));
    group->add(std::make_unique<RenameEntity>(id, "Box")); // no change: skipped
    REQUIRE(f.doc.execute(std::move(group)));
    CHECK(f.doc.scene().find(id)->transform().rotation == 20.0f);
    CHECK(f.doc.history().undoLabel() == "Reset Transform");
    f.doc.undo();
    CHECK(f.doc.scene().find(id)->transform().rotation == 0.0f);
    CHECK_FALSE(f.doc.execute(std::make_unique<CommandGroup>("Nothing")));
}

TEST_CASE("The scene can be renamed and renamed back", "[editor][commands]")
{
    Fixture f;
    REQUIRE(f.doc.execute(std::make_unique<RenameScene>("Level 1")));
    CHECK(f.doc.scene().name() == "Level 1");
    f.doc.undo();
    CHECK(f.doc.scene().name() == "Test");
}

TEST_CASE("The scene's gravity changes as one undo step while dragging", "[editor][commands][physics]")
{
    Fixture f;
    REQUIRE(f.doc.execute(std::make_unique<SetSceneGravity>(Vec2{0, -900})));
    REQUIRE(f.doc.execute(std::make_unique<SetSceneGravity>(Vec2{0, -500})));
    f.doc.history().breakMerge();
    CHECK(f.doc.scene().gravity() == Vec2{0, -500});
    CHECK(f.doc.isDirty());
    CHECK(f.doc.history().undoLabel() == "Change Gravity");
    f.doc.undo();
    CHECK(f.doc.scene().gravity() == Scene::kDefaultGravity);
    CHECK_FALSE(f.doc.execute(std::make_unique<SetSceneGravity>(Scene::kDefaultGravity)));   // no change
}

TEST_CASE("A tool drag changing several properties is one undo step", "[editor][commands]")
{
    Fixture f;
    const EntityId box = f.make("Box");
    f.scene().addComponent(box, std::make_unique<Shape>());
    const std::string before = serialized(f.scene());

    // A resize drag: size and position change together, many times.
    for (int step = 1; step <= 5; ++step) {
        const float w = 100.0f + 10.0f * static_cast<float>(step);
        REQUIRE(f.doc.execute(std::make_unique<SetProperties>(
            box,
            std::vector<SetProperties::Change>{{"rolo.Shape", "size", Vec2{w, 100.0f}},
                                               {"rolo.Transform", "position", Vec2{(w - 100.0f) * 0.5f, 0.0f}}},
            "Resize Box")));
    }
    f.doc.history().breakMerge();
    CHECK(f.scene().find(box)->get<Shape>()->size == Vec2{150.0f, 100.0f});
    CHECK(f.scene().find(box)->transform().position == Vec2{25.0f, 0.0f});
    CHECK(f.doc.history().undoLabel() == "Resize Box");

    REQUIRE(f.doc.undo());
    CHECK(serialized(f.scene()) == before);
    REQUIRE(f.doc.redo());
    CHECK(f.scene().find(box)->get<Shape>()->size == Vec2{150.0f, 100.0f});

    // Changing nothing makes no step.
    CHECK_FALSE(f.doc.execute(std::make_unique<SetProperties>(
        box, std::vector<SetProperties::Change>{{"rolo.Shape", "size", Vec2{150.0f, 100.0f}}}, "Resize Box")));
}

TEST_CASE("A brush stroke is one undo step; erasing and collision too", "[editor][commands][tilemap]")
{
    Fixture f;
    const EntityId id = f.make("Map");
    f.scene().addComponent(id, std::make_unique<Tilemap>());
    f.doc.history().breakMerge();
    const auto tile = [&](int x) { return f.scene().find(id)->get<Tilemap>()->tileAt(0, {x, 0}); };

    // One stroke over three cells (the middle one twice).
    for (int x : {0, 1, 1, 2}) {
        f.doc.execute(std::make_unique<PaintTiles>(id, 0, std::vector<PaintTiles::Cell>{{{x, 0}, 4}}, "Paint Tiles"));
    }
    f.doc.history().breakMerge();
    CHECK(tile(0) == 4);
    CHECK(tile(2) == 4);
    const std::size_t steps = f.doc.history().size();

    // Erase one in a second stroke, paint collision in a third.
    f.doc.execute(std::make_unique<PaintTiles>(id, 0, std::vector<PaintTiles::Cell>{{{1, 0}, -1}}, "Erase Tiles"));
    f.doc.history().breakMerge();
    f.doc.execute(std::make_unique<PaintTiles>(id, PaintTiles::kCollisionLayer,
                                               std::vector<PaintTiles::Cell>{{{5, 5}, 1}}, "Paint Collision"));
    CHECK(f.doc.history().size() == steps + 2);
    CHECK(tile(1) == -1);
    CHECK(f.scene().find(id)->get<Tilemap>()->solid.count({5, 5}) == 1);

    f.doc.undo(); // collision
    CHECK(f.scene().find(id)->get<Tilemap>()->solid.empty());
    f.doc.undo(); // erase
    CHECK(tile(1) == 4);
    f.doc.undo(); // the whole first stroke
    CHECK(tile(0) == -1);
    CHECK(tile(1) == -1);
    f.doc.redo();
    CHECK(tile(1) == 4);
    CHECK(tile(2) == 4);

    // Painting the same tile again changes nothing and adds no step.
    f.doc.history().breakMerge();
    CHECK_FALSE(f.doc.execute(std::make_unique<PaintTiles>(id, 0, std::vector<PaintTiles::Cell>{{{0, 0}, 4}}, "Paint Tiles")));
}

TEST_CASE("Replacing a component (e.g. adding a layer) undoes in place", "[editor][commands][tilemap]")
{
    Fixture f;
    const EntityId id = f.make("Map");
    f.scene().addComponent(id, std::make_unique<Shape>());
    f.scene().addComponent(id, std::make_unique<Tilemap>());
    auto changed = std::make_unique<Tilemap>(*f.scene().find(id)->get<Tilemap>());
    changed->layers.push_back(TileLayer{"Decor", true, {}});
    REQUIRE(f.doc.execute(std::make_unique<ReplaceComponent>(id, std::move(changed), "Add Layer")));
    CHECK(f.scene().find(id)->get<Tilemap>()->layers.size() == 2);
    CHECK(f.scene().componentIndex(id, "rolo.Tilemap") == 2u); // same place in the list
    f.doc.undo();
    CHECK(f.scene().find(id)->get<Tilemap>()->layers.size() == 1);
    CHECK(f.scene().componentIndex(id, "rolo.Tilemap") == 2u);
    f.doc.redo();
    CHECK(f.scene().find(id)->get<Tilemap>()->layers.size() == 2);
}

// --- Asset commands ------------------------------------------------------------------------------

namespace {

// A project folder with hero.png imported and a tileset made from it.
struct AssetFixture {
    std::filesystem::path folder = rolo::test::uniqueTempFolder("asset-commands");
    std::unique_ptr<AssetDatabase> db;
    AssetId image;
    AssetId tileset;

    AssetFixture()
    {
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        db = std::make_unique<AssetDatabase>(folder);
        image = *db->import(std::filesystem::path(ROLO_TEST_DATA_DIR) / "hero.png", "assets");
        tileset = *db->createTileset(image, {8.0f, 8.0f}, "hero");
    }
    ~AssetFixture() { std::filesystem::remove_all(folder); }

    Tileset current() const { return *db->find(tileset)->tileset; }
    Tileset onDisk() const
    {
        AssetDatabase fresh(folder, AssetDatabase::Access::ReadOnly);
        fresh.refresh();
        return *fresh.find(tileset)->tileset;
    }
};

} // namespace

TEST_CASE("Tileset changes are undoable and saved to the tileset file", "[editor][commands][assets]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());

    Tileset solid = a.current();
    solid.solid.insert(3);
    REQUIRE(f.doc.execute(std::make_unique<ChangeTileset>(a.tileset, solid, "Make Tile Solid")));
    CHECK(f.doc.history().undoLabel() == "Make Tile Solid");
    CHECK(a.current().isSolid(3));
    CHECK(a.onDisk().isSolid(3));              // written straight away
    CHECK_FALSE(f.doc.isDirty());              // the scene itself has nothing to save

    REQUIRE(f.doc.undo());
    CHECK_FALSE(a.current().isSolid(3));
    CHECK_FALSE(a.onDisk().isSolid(3));
    REQUIRE(f.doc.redo());
    CHECK(a.onDisk().isSolid(3));

    // Setting what is already there is not an undo step.
    CHECK_FALSE(f.doc.execute(std::make_unique<ChangeTileset>(a.tileset, solid, "Again")));
}

TEST_CASE("Asset changes mixed with scene edits keep the unsaved state right", "[editor][commands][assets]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());

    f.make("Player");                          // a scene change: unsaved
    CHECK(f.doc.isDirty());
    f.doc.history().markClean();               // "saved"
    Tileset bigger = a.current();
    bigger.tileWidth = 12;
    REQUIRE(f.doc.execute(std::make_unique<ChangeTileset>(a.tileset, bigger, "Change Tile Size")));
    REQUIRE(f.doc.execute(std::make_unique<ChangeImageFilter>(a.image, TextureFilter::Smooth)));
    CHECK_FALSE(f.doc.isDirty());

    // Undoing both asset steps and then the creation does change the scene.
    REQUIRE(f.doc.undo());
    REQUIRE(f.doc.undo());
    CHECK(a.current().tileWidth == 8);
    CHECK(a.db->find(a.image)->filter == TextureFilter::Pixel);   // small images import as pixel art
    CHECK_FALSE(f.doc.isDirty());
    REQUIRE(f.doc.undo());
    CHECK(f.doc.isDirty());
}

TEST_CASE("Asset undo steps never use a database that has gone", "[editor][commands][assets]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());
    Tileset solid = a.current();
    solid.solid.insert(1);
    REQUIRE(f.doc.execute(std::make_unique<ChangeTileset>(a.tileset, solid, "Make Tile Solid")));

    // The project closes: the editor tells the history, then the database goes.
    f.doc.history().setAssets(nullptr);
    a.db.reset();
    CHECK(f.doc.undo());                       // does nothing to any file, and does not crash
    CHECK_FALSE(f.doc.redo());                 // cannot be redone without the project
    CHECK_FALSE(f.doc.history().canRedo());

    // Without assets, asset commands do nothing.
    CHECK_FALSE(f.doc.execute(std::make_unique<ChangeImageFilter>(a.image, TextureFilter::Smooth)));
}

TEST_CASE("Animation edits are undoable and read again at once", "[editor][commands][assets][animation]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());
    const auto id = a.db->createAnimationSet(a.image, {8.0f, 8.0f}, "hero");
    REQUIRE(id);
    AnimationSet changed = *a.db->find(*id)->animation;
    changed.clips.push_back(AnimationClip{"walk", {1, 2, 3}, 12.0f, true});
    REQUIRE(f.doc.execute(std::make_unique<ChangeAssetText>(*id, serializeAnimationSet(changed), "Add Clip")));
    REQUIRE(a.db->find(*id)->animation->findClip("walk") != nullptr);   // the record is up to date
    REQUIRE(f.doc.undo());
    CHECK(a.db->find(*id)->animation->findClip("walk") == nullptr);
    REQUIRE(f.doc.redo());
    CHECK(a.db->find(*id)->animation->findClip("walk")->fps == 12.0f);
}

TEST_CASE("The start scene is changed as an undoable step", "[editor][commands]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());
    std::ofstream(a.folder / "project.rolo")
        << R"({"format": "rolo.project", "version": 1, "name": "T", "startScene": "scenes/main.lscene"})";
    const auto startScene = [&] { return Project::open(a.folder)->settings().startScene; };
    REQUIRE(f.doc.execute(std::make_unique<SetStartScene>("scenes/village.lscene")));
    CHECK(startScene() == "scenes/village.lscene");
    CHECK(Project::open(a.folder)->settings().name == "T");   // the rest is kept
    CHECK_FALSE(f.doc.isDirty());                              // a project change, not a scene change
    REQUIRE(f.doc.undo());
    CHECK(startScene() == "scenes/main.lscene");
    REQUIRE(f.doc.redo());
    CHECK(startScene() == "scenes/village.lscene");
    CHECK_FALSE(f.doc.execute(std::make_unique<SetStartScene>("scenes/village.lscene")));   // already
    CHECK_FALSE(f.doc.execute(std::make_unique<SetStartScene>("../outside.lscene")));       // not in the project
}

TEST_CASE("Content files are edited through undoable steps", "[editor][commands][assets]")
{
    AssetFixture a;
    Fixture f;
    f.doc.history().setAssets(a.db.get());
    std::string error;
    const auto id = a.db->createTextAsset("content", "talk", ".ldialogue", "{\"v\":1}\n", &error);
    REQUIRE(id);
    CHECK(a.db->find(*id)->type == AssetType::Dialogue);
    CHECK(a.db->find(*id)->path == "content/talk.ldialogue");
    CHECK(std::filesystem::exists(a.folder / "content/talk.ldialogue.meta"));

    REQUIRE(f.doc.execute(std::make_unique<ChangeAssetText>(*id, "{\"v\":2}\n", "Edit Line")));
    REQUIRE(f.doc.execute(std::make_unique<ChangeAssetText>(*id, "{\"v\":3}\n", "Edit Line")));   // merges
    CHECK(f.doc.history().size() == 1);
    CHECK(*a.db->readText(*id) == "{\"v\":3}\n");
    CHECK_FALSE(f.doc.isDirty());
    REQUIRE(f.doc.undo());
    CHECK(*a.db->readText(*id) == "{\"v\":1}\n");
    REQUIRE(f.doc.redo());
    CHECK(*a.db->readText(*id) == "{\"v\":3}\n");
    CHECK_FALSE(f.doc.execute(std::make_unique<ChangeAssetText>(*id, "{\"v\":3}\n", "Edit Line")));   // no change
}
