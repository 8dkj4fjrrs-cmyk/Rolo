// The editor's part in linked copies of prefabs (ADR 0035): Save as Prefab,
// Apply to Prefab, Reset to Prefab, Unlink.

#include "Commands.hpp"
#include "Prefabs.hpp"
#include "SceneDocument.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace rolo;
using namespace rolo::editor;

namespace {

const AssetId kPrefab{0xC0FFEE};

const ComponentRegistry& registry()
{
    static const ComponentRegistry r = ComponentRegistry::withBuiltins();
    return r;
}

// The project's one prefab, as a text that the tests change.
struct Prefabs {
    std::string text;
    PrefabTexts texts()
    {
        return [this](AssetId id) -> std::optional<std::string> {
            return id == kPrefab ? std::optional<std::string>(text) : std::nullopt;
        };
    }
};

const Entity* childNamed(const Scene& scene, EntityId parent, const std::string& name)
{
    for (const EntityId id : scene.find(parent)->children()) {
        if (scene.find(id)->name() == name) {
            return scene.find(id);
        }
    }
    return nullptr;
}

EntityId place(Scene& scene, const Prefabs& project, Vec2 at)
{
    const SceneLoadResult prefab = parsePrefab(project.text, registry());
    REQUIRE(prefab.ok());
    const EntitySnapshot copy = linkedCopyOf(scene, *prefab.scene, kPrefab);
    REQUIRE(scene.restore(copy, EntityId{}, scene.roots().size()));
    scene.setProperty(copy.rootId(), "rolo.Transform", "position", at);
    return copy.rootId();
}

} // namespace

TEST_CASE("Saving an object as a prefab makes it a linked copy of the new file", "[editor][prefabs]")
{
    Scene build("Level", EntityIdGenerator(3));
    const EntityId object = build.create("Coin")->id();
    build.addComponent(object, std::make_unique<Shape>());
    const EntityId shine = build.create("Shine", object)->id();
    SceneDocument document{std::move(build)};

    // What the editor does: the file is the object as it is, and the object is tied to it.
    Prefabs project{serializePrefab(document.scene(), object)};
    REQUIRE(document.execute(std::make_unique<SetPrefabLinks>(linksAfterSaving(document.scene(), object, kPrefab),
                                                              "Save as Prefab")));
    const Scene& scene = document.scene();
    CHECK(scene.find(object)->isPrefabCopy());
    CHECK(scene.find(object)->prefabLink().source == object);
    CHECK(scene.find(shine)->prefabLink().source == shine);
    CHECK_FALSE(scene.find(shine)->isPrefabCopy());
    CHECK(prefabCopyMembers(scene, object).size() == 2);

    // Saved, the scene holds the link and nothing of the object's own.
    const std::string text = serializeScene(scene, project.texts());
    CHECK(text.find("\"prefab\"") != std::string::npos);
    CHECK(text.find("Shine") == std::string::npos);
    CHECK(text.find("\"components\"") == std::string::npos);

    // Unlink is the same command with empty links; undo ties it again.
    std::vector<std::pair<EntityId, PrefabLink>> none;
    for (const auto& [id, inPrefab] : prefabCopyMembers(scene, object)) {
        none.emplace_back(id, PrefabLink{});
    }
    REQUIRE(document.execute(std::make_unique<SetPrefabLinks>(none, "Unlink from Prefab")));
    CHECK_FALSE(document.scene().find(object)->isPrefabCopy());
    CHECK(serializeScene(document.scene(), project.texts()).find("Shine") != std::string::npos);
    document.undo();
    CHECK(document.scene().find(object)->isPrefabCopy());
    document.undo();
    CHECK(document.scene().find(object)->prefabLink().none());
}

TEST_CASE("Applying a copy to its prefab changes the prefab, and the other copies follow", "[editor][prefabs]")
{
    // The prefab: a Coin (a Shape) with a Shine inside.
    Scene workshop("Workshop", EntityIdGenerator(11));
    const EntityId coin = workshop.create("Coin")->id();
    workshop.addComponent(coin, std::make_unique<Shape>());
    workshop.setProperty(coin, "rolo.Transform", "position", Vec2{1.0f, 1.0f});
    const EntityId shine = workshop.create("Shine", coin)->id();
    Prefabs project{serializePrefab(workshop, coin)};
    const std::string oldText = project.text;

    Scene scene("Level", EntityIdGenerator(5));
    const EntityId a = place(scene, project, {100.0f, 0.0f});
    const EntityId b = place(scene, project, {200.0f, 0.0f});
    const EntityId aShine = childNamed(scene, a, "Shine")->id();
    const EntityId bShine = childNamed(scene, b, "Shine")->id();
    // B has something of its own.
    scene.setProperty(b, "rolo.Shape", "color", Color{255, 0, 0, 255});
    // A is changed: bigger, the Shine renamed, a Sparkle put inside it.
    scene.setProperty(a, "rolo.Shape", "size", Vec2{32.0f, 32.0f});
    scene.rename(aShine, "Glint");
    const EntityId sparkle = scene.create("Sparkle", aShine)->id();

    const std::string newText = prefabTextFromCopy(scene, a, oldText, registry());
    {
        // The prefab is now what A is, under the prefab's own IDs, and stays where it was.
        const SceneLoadResult prefab = parsePrefab(newText, registry());
        REQUIRE(prefab.ok());
        const Entity* top = prefab.scene->find(coin);
        REQUIRE(top != nullptr);
        CHECK(top->get<Shape>()->size == Vec2{32.0f, 32.0f});
        CHECK(top->transform().position == Vec2{1.0f, 1.0f});
        REQUIRE(prefab.scene->find(shine) != nullptr);
        CHECK(prefab.scene->find(shine)->name() == "Glint");
        REQUIRE(prefab.scene->find(sparkle) != nullptr);
        CHECK(prefab.scene->find(sparkle)->parent() == shine);
        CHECK(prefab.scene->size() == 3);
        CHECK(newText.find(a.toString()) == std::string::npos);
        CHECK(newText.find("\"prefab\"") == std::string::npos);
    }

    std::string error;
    project.text = newText;
    const std::optional<Scene> after = sceneAfterApply(scene, a, kPrefab, oldText, newText, registry(), project.texts(), error);
    REQUIRE(after);
    // A is as it was, object for object.
    CHECK(after->find(a)->isPrefabCopy());
    CHECK(after->find(a)->transform().position == Vec2{100.0f, 0.0f});
    CHECK(after->find(aShine)->name() == "Glint");
    REQUIRE(after->find(sparkle) != nullptr);
    CHECK(after->find(sparkle)->parent() == aShine);
    CHECK(after->find(sparkle)->prefabLink().source == sparkle);   // now one of the prefab's objects
    // B follows: bigger, Glint, a Sparkle of its own; and keeps its colour and its place.
    const Entity* bTop = after->find(b);
    CHECK(bTop->get<Shape>()->size == Vec2{32.0f, 32.0f});
    CHECK(bTop->get<Shape>()->color == Color{255, 0, 0, 255});
    CHECK(bTop->transform().position == Vec2{200.0f, 0.0f});
    REQUIRE(after->find(bShine) != nullptr);
    CHECK(after->find(bShine)->name() == "Glint");
    const Entity* bSparkle = childNamed(*after, bShine, "Sparkle");
    REQUIRE(bSparkle != nullptr);
    CHECK(bSparkle->id() != sparkle);
    CHECK(after->size() == 6);

    // Saved: neither copy repeats what the prefab says.
    const std::string saved = serializeScene(*after, project.texts());
    CHECK(saved.find("Glint") == std::string::npos);
    CHECK(saved.find("Sparkle") == std::string::npos);
    CHECK(saved.find("\"size\"") == std::string::npos);
    const SceneLoadResult reread = parseScene(saved, registry(), project.texts());
    REQUIRE(reread.ok());
    CHECK(serializeScene(*reread.scene, project.texts()) == saved);
    CHECK(reread.scene->find(sparkle) != nullptr);
}

TEST_CASE("Resetting a copy gives it back as the prefab has it, where it stands; undo brings its changes back",
          "[editor][prefabs]")
{
    Scene workshop("Workshop", EntityIdGenerator(11));
    const EntityId coin = workshop.create("Coin")->id();
    workshop.addComponent(coin, std::make_unique<Shape>());
    workshop.create("Shine", coin);
    Prefabs project{serializePrefab(workshop, coin)};

    Scene made("Level", EntityIdGenerator(5));
    made.create("Floor");
    const EntityId copy = place(made, project, {50.0f, 60.0f});
    made.create("Wall");
    const EntityId copyShine = childNamed(made, copy, "Shine")->id();
    made.rename(copy, "Big Coin");
    made.setProperty(copy, "rolo.Shape", "size", Vec2{99.0f, 99.0f});
    made.remove(copyShine);
    const EntityId extra = made.create("Extra", copy)->id();
    SceneDocument document{std::move(made)};

    const SceneLoadResult prefab = parsePrefab(project.text, registry());
    REQUIRE(prefab.ok());
    Scene scratch(document.scene());   // (IDs are made from a scene; the editor uses the document's)
    EntitySnapshot fresh = copyReset(scratch, copy, *prefab.scene);
    REQUIRE_FALSE(fresh.empty());
    REQUIRE(document.execute(std::make_unique<ReplaceObject>(copy, std::move(fresh), "Reset to Prefab")));

    const Scene& scene = document.scene();
    const Entity* top = scene.find(copy);   // the same object ID
    REQUIRE(top != nullptr);
    CHECK(top->name() == "Coin");
    CHECK(top->get<Shape>()->size == Shape().size);
    CHECK(top->transform().position == Vec2{50.0f, 60.0f});
    CHECK(top->isPrefabCopy());
    CHECK(childNamed(scene, copy, "Shine") != nullptr);
    CHECK(scene.find(extra) == nullptr);
    CHECK(scene.roots()[1] == copy);   // between the Floor and the Wall, as before
    CHECK(serializeScene(scene, project.texts()).find("\"size\"") == std::string::npos);

    document.undo();
    CHECK(document.scene().find(copy)->name() == "Big Coin");
    CHECK(document.scene().find(extra) != nullptr);
    CHECK(childNamed(document.scene(), copy, "Shine") == nullptr);
    CHECK(document.scene().roots()[1] == copy);

    // An object that is not a linked copy has nothing to be reset to.
    CHECK(copyReset(scratch, document.scene().roots()[0], *prefab.scene).empty());
}
