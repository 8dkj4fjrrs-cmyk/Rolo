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

TEST_CASE("What a copy has of its own is found field by field, and can be put back", "[editor][prefabs][own]")
{
    Scene workshop("Workshop", EntityIdGenerator(11));
    const EntityId coin = workshop.create("Coin")->id();
    workshop.addComponent(coin, std::make_unique<Shape>());
    workshop.addComponent(coin, std::make_unique<Collider>());
    const EntityId shine = workshop.create("Shine", coin)->id();
    workshop.addComponent(shine, std::make_unique<Shape>());
    Prefabs project{serializePrefab(workshop, coin)};
    const SceneLoadResult prefab = parsePrefab(project.text, registry());
    REQUIRE(prefab.ok());

    Scene made("Level", EntityIdGenerator(5));
    const EntityId plain = made.create("Floor")->id();
    const EntityId copy = place(made, project, {50.0f, 60.0f});
    const EntityId copyShine = childNamed(made, copy, "Shine")->id();
    const EntityId extra = made.create("Extra", copy)->id();
    SceneDocument document{std::move(made)};

    // Fresh from the prefab, a copy has nothing of its own: where it stands does not count.
    {
        const auto d = differencesFromPrefab(document.scene(), copy, *prefab.scene);
        REQUIRE(d);
        CHECK(d->none());
        CHECK(d->top);
        CHECK(d->root == copy);
        CHECK(d->prefab == kPrefab);
        CHECK(d->summary().empty());
    }
    CHECK(copyRootOf(document.scene(), copyShine) == copy);
    CHECK(copyRootOf(document.scene(), extra).isNull());   // put into the copy here: not the prefab's
    CHECK(copyRootOf(document.scene(), plain).isNull());
    CHECK_FALSE(differencesFromPrefab(document.scene(), extra, *prefab.scene));
    CHECK_FALSE(differencesFromPrefab(document.scene(), plain, *prefab.scene));

    // The copy changes: a colour, a size, a name, a component more and one less; its Shine moves and is switched off.
    REQUIRE(document.execute(std::make_unique<SetProperty>(copy, "rolo.Shape", "color", PropertyValue(Color{255, 0, 0, 255}), "Color")));
    REQUIRE(document.execute(std::make_unique<SetProperty>(copy, "rolo.Shape", "size", PropertyValue(Vec2{9.0f, 9.0f}), "Size")));
    REQUIRE(document.execute(std::make_unique<RenameEntity>(copy, "Red Coin")));
    {
        Scene& s = const_cast<Scene&>(document.scene());   // (set up directly: these have commands of their own in the editor)
        s.removeComponent(copy, "rolo.Collider");
        s.addComponent(copy, std::make_unique<RigidBody>());
        s.setProperty(copyShine, "rolo.Transform", "position", Vec2{3.0f, 4.0f});
        s.setActive(copyShine, false);
    }
    const auto d = differencesFromPrefab(document.scene(), copy, *prefab.scene);
    REQUIRE(d);
    CHECK(d->fields.size() == 2);
    CHECK(std::get<Color>(d->fields.at({"rolo.Shape", "color"})) == Shape().color);
    CHECK(d->fields.count({"rolo.Shape", "size"}) == 1);
    CHECK(d->fields.count({"rolo.Transform", "position"}) == 0);
    CHECK(d->added == std::vector<std::string>{"rolo.RigidBody"});
    CHECK(d->removed == std::vector<std::string>{"rolo.Collider"});
    CHECK(d->name == std::optional<std::string>("Coin"));
    CHECK(d->summary() == "2 fields, 1 component added, 1 component removed, its name");

    const auto inside = differencesFromPrefab(document.scene(), copyShine, *prefab.scene);
    REQUIRE(inside);
    CHECK_FALSE(inside->top);
    CHECK(inside->root == copy);
    CHECK(inside->fields.count({"rolo.Transform", "position"}) == 1);   // inside a copy, a place is a field like any other
    CHECK(inside->active == std::optional<bool>(true));
    CHECK(inside->summary() == "1 field, on / off");

    // Putting one field back is an ordinary change of that property, to the prefab's value: undoable.
    REQUIRE(document.execute(std::make_unique<SetProperty>(copy, "rolo.Shape", "color", d->fields.at({"rolo.Shape", "color"}), "Reset Color")));
    const auto after = differencesFromPrefab(document.scene(), copy, *prefab.scene);
    CHECK(after->fields.size() == 1);
    CHECK(after->fields.count({"rolo.Shape", "size"}) == 1);
    document.undo();
    CHECK(differencesFromPrefab(document.scene(), copy, *prefab.scene)->fields.size() == 2);

    // Named values, name by name.
    CHECK(valuesWithNamePutBack("{\"speed\":50,\"label\":\"x\"}", "speed", DataValue(20.0)) == "{\"label\":\"x\",\"speed\":20}");
    CHECK(valuesWithNamePutBack("{\"speed\":50}", "speed", std::nullopt).empty());
}

// --- Prefabs inside prefabs (ADR 0036) ----------------------------------------------------------------

namespace {

const AssetId kDoor{0xD00A};
const AssetId kHouse{0xA005E};

struct Project {
    std::string door, house;
    PrefabTexts texts()
    {
        return [this](AssetId id) -> std::optional<std::string> {
            const std::string& text = id == kDoor ? door : house;
            return (id == kDoor || id == kHouse) && !text.empty() ? std::optional<std::string>(text) : std::nullopt;
        };
    }
};

EntityId placeOf(Scene& scene, Project& project, AssetId asset, EntityId parent = {})
{
    const SceneLoadResult prefab = parsePrefab(asset == kDoor ? project.door : project.house, registry(), project.texts());
    REQUIRE(prefab.ok());
    const EntitySnapshot copy = linkedCopyOf(scene, *prefab.scene, asset);
    const std::size_t index = parent.isNull() ? scene.roots().size() : scene.find(parent)->children().size();
    REQUIRE(scene.restore(copy, parent, index));
    return copy.rootId();
}

// Door (a Shape) with a Knob (a Shape); House made in a scene from a Wall and a copy of Door, with Save as Prefab.
struct Street {
    Project project;
    EntityId door, knob;   // in the door's file
    Scene scene{"Level", EntityIdGenerator(5)};
    EntityId house;        // the object the house prefab was made from, now its first copy

    Street()
    {
        Scene workshop("Doors", EntityIdGenerator(21));
        door = workshop.create("Door")->id();
        workshop.addComponent(door, std::make_unique<Shape>());
        knob = workshop.create("Knob", door)->id();
        workshop.addComponent(knob, std::make_unique<Shape>());
        project.door = serializePrefab(workshop, door);

        house = scene.create("House")->id();
        scene.create("Wall", house);
        placeOf(scene, project, kDoor, house);
        // Edit > Save as Prefab.
        project.house = serializePrefab(scene, house, project.texts());
        for (auto& [id, link] : linksAfterSaving(scene, house, kHouse)) {
            scene.setPrefabLink(id, std::move(link));
        }
    }
};

} // namespace

TEST_CASE("Saving an object that holds a copy of a prefab keeps the copy linked", "[editor][prefabs][nested]")
{
    Street s;
    CHECK(s.project.house.find("\"prefab\": \"" + kDoor.toString() + "\"") != std::string::npos);
    CHECK(s.project.house.find("Knob") == std::string::npos);
    // In the scene the object is now a copy of the house, the door a part of it.
    CHECK(s.scene.find(s.house)->isPrefabCopy());
    CHECK(prefabCopyMembers(s.scene, s.house).size() == 4);
    const Entity* door = childNamed(s.scene, s.house, "Door");
    CHECK_FALSE(door->isPrefabCopy());
    CHECK(copyRootOf(s.scene, childNamed(s.scene, door->id(), "Knob")->id()) == s.house);
    {
        // The Inspector can say which prefab a part comes from.
        const SceneLoadResult prefab = parsePrefab(s.project.house, registry(), s.project.texts());
        REQUIRE(prefab.ok());
        const EntityId knobHere = childNamed(s.scene, door->id(), "Knob")->id();
        CHECK(differencesFromPrefab(s.scene, knobHere, *prefab.scene)->within == kDoor);
        CHECK(differencesFromPrefab(s.scene, door->id(), *prefab.scene)->within == kDoor);
        CHECK(differencesFromPrefab(s.scene, s.house, *prefab.scene)->within.isNull());
        CHECK(differencesFromPrefab(s.scene, childNamed(s.scene, s.house, "Wall")->id(), *prefab.scene)->within.isNull());
    }

    // The scene is saved and read: the same objects, as one link.
    const std::string text = serializeScene(s.scene, s.project.texts());
    CHECK(text.find("Wall") == std::string::npos);
    const SceneLoadResult back = parseScene(text, registry(), s.project.texts());
    REQUIRE(back.ok());
    CHECK(back.warningCount() == 0);
    CHECK(back.scene->size() == 4);
    CHECK(back.scene->find(door->id()) != nullptr);
    CHECK(serializeScene(*back.scene, s.project.texts()) == text);
    // Nothing of its own: applying changes nothing.
    CHECK(prefabTextFromCopy(s.scene, s.house, s.project.house, registry(), s.project.texts()) == s.project.house);
}

TEST_CASE("Applying a copy keeps the prefabs inside its prefab linked, and other copies follow", "[editor][prefabs][nested]")
{
    Street s;
    Scene& scene = s.scene;
    const EntityId other = placeOf(scene, s.project, kHouse);
    scene.setProperty(other, "rolo.Transform", "position", Vec2{500.0f, 0.0f});
    const EntityId door = childNamed(scene, s.house, "Door")->id();
    const EntityId knobHere = childNamed(scene, door, "Knob")->id();
    const EntityId otherDoor = childNamed(scene, other, "Door")->id();
    const EntityId otherKnob = childNamed(scene, otherDoor, "Knob")->id();

    // In the first house: the door moved, its knob red, a Chimney added.
    scene.setProperty(door, "rolo.Transform", "position", Vec2{40.0f, 0.0f});
    scene.setProperty(knobHere, "rolo.Shape", "color", Color{255, 0, 0, 255});
    scene.create("Chimney", s.house);

    const std::string oldText = s.project.house;
    const std::string newText = prefabTextFromCopy(scene, s.house, oldText, registry(), s.project.texts());
    CHECK(newText != oldText);
    // Still a link to the door, with what this house changes in it.
    CHECK(newText.find("\"prefab\": \"" + kDoor.toString() + "\"") != std::string::npos);
    CHECK(newText.find("Knob") == std::string::npos);
    CHECK(newText.find("Chimney") != std::string::npos);
    std::string error;
    s.project.house = newText;
    const std::optional<Scene> after =
        sceneAfterApply(scene, s.house, kHouse, oldText, newText, registry(), s.project.texts(), error);
    REQUIRE(after);
    CHECK(after->size() == 10);
    CHECK(after->find(knobHere)->get<Shape>()->color == Color{255, 0, 0, 255});
    CHECK(after->find(otherDoor)->transform().position == Vec2{40.0f, 0.0f});
    CHECK(after->find(otherKnob)->get<Shape>()->color == Color{255, 0, 0, 255});
    CHECK(after->find(other)->transform().position == Vec2{500.0f, 0.0f});
    CHECK(childNamed(*after, other, "Chimney") != nullptr);
    // Applied again at once, nothing changes.
    CHECK(prefabTextFromCopy(*after, s.house, newText, registry(), s.project.texts()) == newText);

    // Now the door itself changes, through a copy of it that stands alone.
    Scene level(*after);
    const EntityId loose = placeOf(level, s.project, kDoor);
    level.setProperty(loose, "rolo.Shape", "size", Vec2{20.0f, 50.0f});
    level.create("Bell", loose);
    const std::string oldDoor = s.project.door;
    const std::string newDoor = prefabTextFromCopy(level, loose, oldDoor, registry(), s.project.texts());
    s.project.door = newDoor;
    const std::optional<Scene> later =
        sceneAfterApply(level, loose, kDoor, oldDoor, newDoor, registry(), s.project.texts(), error);
    REQUIRE(later);
    // Both houses' doors follow, and keep what their house says of them.
    for (const EntityId d : {door, otherDoor}) {
        REQUIRE(later->find(d) != nullptr);
        CHECK(later->find(d)->get<Shape>()->size == Vec2{20.0f, 50.0f});
        CHECK(later->find(d)->transform().position == Vec2{40.0f, 0.0f});
        CHECK(childNamed(*later, d, "Bell") != nullptr);
    }
    CHECK(later->find(knobHere)->get<Shape>()->color == Color{255, 0, 0, 255});
    // The house's file did not have to change for that.
    CHECK(s.project.house == newText);
}

TEST_CASE("A prefab cannot be applied into itself", "[editor][prefabs][nested]")
{
    Street s;
    Scene& scene = s.scene;
    const EntityId other = placeOf(scene, s.project, kHouse);
    CHECK_FALSE(applyWouldLoop(scene, s.house, s.project.texts()));
    // A second door inside the house is fine.
    placeOf(scene, s.project, kDoor, s.house);
    CHECK_FALSE(applyWouldLoop(scene, s.house, s.project.texts()));
    // A house inside the house is not.
    placeOf(scene, s.project, kHouse, other);
    CHECK(applyWouldLoop(scene, other, s.project.texts()));
    // Nor a house inside a door: the house holds the door.
    const EntityId loose = placeOf(scene, s.project, kDoor);
    CHECK_FALSE(applyWouldLoop(scene, loose, s.project.texts()));
    placeOf(scene, s.project, kHouse, loose);
    CHECK(applyWouldLoop(scene, loose, s.project.texts()));
}

TEST_CASE("Unlinking a copy leaves the prefabs inside it linked", "[editor][prefabs][nested]")
{
    Street s;
    Scene& scene = s.scene;
    const EntityId door = childNamed(scene, s.house, "Door")->id();
    const EntityId knobHere = childNamed(scene, door, "Knob")->id();
    scene.setProperty(door, "rolo.Transform", "position", Vec2{40.0f, 0.0f});
    const SceneLoadResult prefab = parsePrefab(s.project.house, registry(), s.project.texts());
    REQUIRE(prefab.ok());
    for (auto& [id, link] : linksAfterUnlinking(scene, s.house, &*prefab.scene)) {
        scene.setPrefabLink(id, std::move(link));
    }
    CHECK_FALSE(scene.find(s.house)->isPrefabCopy());
    CHECK(scene.find(door)->isPrefabCopy());
    CHECK(scene.find(door)->prefabLink().prefab == kDoor);
    CHECK(copyRootOf(scene, knobHere) == door);

    // The house is ordinary objects in the file now, the door a link that follows its prefab.
    const std::string text = serializeScene(scene, s.project.texts());
    CHECK(text.find("Wall") != std::string::npos);
    CHECK(text.find("Knob") == std::string::npos);
    CHECK(text.find(kHouse.toString()) == std::string::npos);
    const SceneLoadResult back = parseScene(text, registry(), s.project.texts());
    REQUIRE(back.ok());
    CHECK(back.scene->find(door)->transform().position == Vec2{40.0f, 0.0f});
    CHECK(back.scene->find(knobHere) != nullptr);

    // Without the prefab to ask, everything becomes ordinary.
    Street t;
    for (auto& [id, link] : linksAfterUnlinking(t.scene, t.house, nullptr)) {
        t.scene.setPrefabLink(id, std::move(link));
    }
    t.scene.forEach([](const Entity& e, int) { CHECK(e.prefabLink().none()); });
}
