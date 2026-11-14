#include "rolo/scene/Scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rolo;
using Catch::Approx;

namespace {
Scene seeded()
{
    return Scene("Test", EntityIdGenerator(42));
}
} // namespace

TEST_CASE("Entity IDs are 16 hex digits and survive a round trip", "[scene][id]")
{
    EntityIdGenerator gen(1);
    const EntityId id = gen.next();
    REQUIRE_FALSE(id.isNull());
    CHECK(id.toString().size() == 16);
    CHECK(EntityId::parse(id.toString()) == id);
    CHECK(EntityId::parse(std::string(16, '0')) == std::nullopt); // zero is "no entity"
    CHECK(EntityId::parse("12345") == std::nullopt);
    CHECK(EntityId::parse("zzzzzzzzzzzzzzzz") == std::nullopt);
    CHECK(EntityId::parse("ABCDEF0123456789")->toString() == "abcdef0123456789");
}

TEST_CASE("Seeded generators repeat; different scenes get different IDs", "[scene][id]")
{
    EntityIdGenerator a(7), b(7);
    CHECK(a.next() == b.next());
    Scene s1, s2;
    CHECK(s1.create("A")->id() != s2.create("A")->id());
}

TEST_CASE("Colours read and write as hex", "[scene][types]")
{
    CHECK(Color{255, 136, 0, 255}.toHex() == "#ff8800ff");
    CHECK(Color::fromHex("#FF8800") == Color{255, 136, 0, 255});
    CHECK(Color::fromHex("ff880080") == Color{255, 136, 0, 128});
    CHECK_FALSE(Color::fromHex("#ff88"));
    CHECK_FALSE(Color::fromHex("#gg8800"));
}

TEST_CASE("Entities form a tree with ordered children", "[scene]")
{
    Scene scene = seeded();
    Entity* a = scene.create("A");
    Entity* b = scene.create("B");
    Entity* child = scene.create("Child", a->id());
    REQUIRE(child != nullptr);
    CHECK(scene.roots() == std::vector<EntityId>{a->id(), b->id()});
    CHECK(scene.childrenOf(a->id()) == std::vector<EntityId>{child->id()});
    CHECK(child->get<Transform>() != nullptr); // every entity has a Transform

    Entity* first = scene.create("First", {}, 0);
    CHECK(scene.roots().front() == first->id());
    CHECK(scene.indexOf(b->id()) == 2u);

    CHECK(scene.create("Orphan", EntityId(12345)) == nullptr); // missing parent
    CHECK(scene.create("Dup", {}, std::nullopt, a->id()) == nullptr); // ID in use

    std::vector<std::string> order;
    scene.forEach([&](const Entity& e, int depth) { order.push_back(std::to_string(depth) + e.name()); });
    CHECK(order == std::vector<std::string>{"0First", "0A", "1Child", "0B"});
}

TEST_CASE("Removing an entity removes everything inside it", "[scene]")
{
    Scene scene = seeded();
    const EntityId a = scene.create("A")->id();
    const EntityId child = scene.create("Child", a)->id();
    scene.create("Grandchild", child);
    const auto before = scene.revision();
    REQUIRE(scene.remove(a));
    CHECK(scene.empty());
    CHECK(scene.revision() > before);
    CHECK_FALSE(scene.remove(a));
}

TEST_CASE("Moving refuses to put an entity inside itself", "[scene]")
{
    Scene scene = seeded();
    const EntityId a = scene.create("A")->id();
    const EntityId b = scene.create("B", a)->id();
    CHECK_FALSE(scene.move(a, b, 0));
    CHECK_FALSE(scene.move(a, a, 0));
    REQUIRE(scene.move(b, {}, 0));
    CHECK(scene.roots() == std::vector<EntityId>{b, a});
    CHECK(scene.find(b)->parent().isNull());
}

TEST_CASE("Properties are set by name, with type checks and limits", "[scene][components]")
{
    Scene scene = seeded();
    const EntityId id = scene.create("Box")->id();
    REQUIRE(scene.addComponent(id, std::make_unique<Shape>()) != nullptr);
    CHECK(scene.addComponent(id, std::make_unique<Shape>()) == nullptr); // one per type

    CHECK(scene.setProperty(id, "rolo.Shape", "size", Vec2{-5.0f, 20.0f}));
    CHECK(scene.find(id)->get<Shape>()->size == Vec2{0.0f, 20.0f}); // clamped at 0
    CHECK(scene.setProperty(id, "rolo.Shape", "lineWidth", 3)); // int accepted for float
    CHECK(scene.find(id)->get<Shape>()->lineWidth == 3.0f);
    CHECK(scene.setProperty(id, "rolo.Shape", "kind", 1));
    CHECK(scene.find(id)->get<Shape>()->kind == ShapeKind::Ellipse);
    CHECK_FALSE(scene.setProperty(id, "rolo.Shape", "kind", 7));            // no such choice
    CHECK_FALSE(scene.setProperty(id, "rolo.Shape", "size", std::string("big"))); // wrong kind
    CHECK_FALSE(scene.setProperty(id, "rolo.Shape", "nope", 1.0f));
    CHECK_FALSE(scene.setProperty(id, "rolo.Sprite", "size", 1.0f));
}

TEST_CASE("Transform cannot be removed; other components can", "[scene][components]")
{
    Scene scene = seeded();
    const EntityId id = scene.create("Box")->id();
    scene.addComponent(id, std::make_unique<Shape>());
    CHECK(scene.removeComponent(id, "rolo.Transform") == nullptr);
    auto removed = scene.removeComponent(id, "rolo.Shape");
    REQUIRE(removed != nullptr);
    CHECK(scene.find(id)->get<Shape>() == nullptr);
    // Put back at the same position.
    CHECK(scene.addComponent(id, std::move(removed), 1) != nullptr);
    CHECK(scene.componentIndex(id, "rolo.Shape") == 1u);
}

TEST_CASE("Snapshots restore deleted entities with the same IDs", "[scene][snapshot]")
{
    Scene scene = seeded();
    const EntityId a = scene.create("A")->id();
    const EntityId b = scene.create("B")->id();
    const EntityId child = scene.create("Child", b)->id();
    scene.addComponent(child, std::make_unique<Shape>());
    scene.setProperty(child, "rolo.Shape", "color", Color{1, 2, 3, 4});

    const EntitySnapshot snap = scene.capture(b);
    CHECK(snap.items.size() == 2);
    CHECK(snap.index == 1);
    scene.remove(b);
    REQUIRE(scene.restore(snap, {}, snap.index));
    CHECK(scene.roots() == std::vector<EntityId>{a, b});
    CHECK(scene.find(child)->get<Shape>()->color == Color{1, 2, 3, 4});
    CHECK_FALSE(scene.restore(snap, {}, 0)); // IDs already present

    const EntitySnapshot copy = scene.withNewIds(snap);
    REQUIRE(scene.restore(copy, {}, 2));
    CHECK(scene.size() == 5);
    const Entity* dup = scene.find(copy.rootId());
    REQUIRE(dup != nullptr);
    CHECK(dup->id() != b);
    REQUIRE(dup->children().size() == 1);
    CHECK(dup->children()[0] != child);
}

TEST_CASE("Copying a scene copies its components", "[scene]")
{
    Scene scene = seeded();
    const EntityId id = scene.create("Box")->id();
    scene.addComponent(id, std::make_unique<Shape>());
    Scene copy = scene;
    scene.setProperty(id, "rolo.Shape", "size", Vec2{1, 1});
    CHECK(copy.find(id)->get<Shape>()->size == Vec2{100, 100});
}

TEST_CASE("World transforms combine parents; hit tests find the top shape", "[scene][geometry]")
{
    Scene scene = seeded();
    const EntityId parent = scene.create("Parent")->id();
    scene.setProperty(parent, "rolo.Transform", "position", Vec2{100, 100});
    scene.setProperty(parent, "rolo.Transform", "scale", Vec2{2, 2});
    const EntityId box = scene.create("Box", parent)->id();
    scene.setProperty(box, "rolo.Transform", "position", Vec2{10, 0});
    scene.addComponent(box, std::make_unique<Shape>()); // 100 x 100

    const Vec2 centre = scene.worldTransform(box).apply({0, 0});
    CHECK(centre.x == Approx(120));
    CHECK(centre.y == Approx(100));
    const auto corners = scene.worldCorners(box);
    REQUIRE(corners);
    CHECK((*corners)[0].x == Approx(20)); // 120 - 50*2
    CHECK((*corners)[2].y == Approx(200));

    CHECK(scene.hitTest({120, 100}) == box);
    CHECK(scene.hitTest({500, 500}).isNull());

    // Clockwise rotation by 90 degrees turns +x into -y.
    scene.setProperty(parent, "rolo.Transform", "rotation", 90.0f);
    const Vec2 rotated = scene.worldTransform(box).apply({0, 0});
    CHECK(rotated.x == Approx(100).margin(1e-3));
    CHECK(rotated.y == Approx(80).margin(1e-3));

    // Inactive parents hide their children from hit tests.
    scene.setActive(parent, false);
    CHECK(scene.hitTest(rotated).isNull());
}

TEST_CASE("Ellipses are hit inside the curve only", "[scene][geometry]")
{
    Scene scene = seeded();
    const EntityId id = scene.create("Ball")->id();
    auto shape = std::make_unique<Shape>();
    shape->kind = ShapeKind::Ellipse;
    scene.addComponent(id, std::move(shape));
    CHECK(scene.hitTest({0, 49}) == id);
    CHECK(scene.hitTest({45, 45}).isNull()); // inside the box, outside the circle
}

TEST_CASE("The start scene is Rolo, made of shapes", "[scene]")
{
    const Scene scene = makeStartScene();
    CHECK(scene.size() > 10);
    CHECK(scene.find(scene.roots().front())->name() == "Background");
    CHECK(scene.hitTest({640, 352}) != EntityId{}); // Rolo, centre of the world
}

TEST_CASE("The scene lists entities by component and by name, in tree order", "[scene][index]")
{
    Scene scene = seeded();
    const EntityId a = scene.create("Box")->id();
    const EntityId b = scene.create("Box", a)->id();   // a second "Box", inside the first
    const EntityId c = scene.create("Lamp")->id();
    scene.addComponent(c, std::make_unique<Shape>());
    scene.addComponent(b, std::make_unique<Shape>());

    CHECK(scene.inOrder() == std::vector<EntityId>{a, b, c});
    CHECK(scene.all<Shape>() == std::vector<EntityId>{b, c});          // tree order, not the order added
    CHECK(scene.all<Transform>() == std::vector<EntityId>{a, b, c});
    CHECK(scene.entitiesWith("no.SuchType").empty());
    CHECK(scene.findByName("Box") == a);                               // the first in tree order
    CHECK(scene.findByName("Lamp") == c);
    CHECK(scene.findByName("Nobody").isNull());

    // Values changing does not change the lists (or structureRevision).
    const std::uint64_t structure = scene.structureRevision();
    scene.setProperty(c, "rolo.Transform", "position", Vec2{5.0f, 6.0f});
    scene.setActive(b, false);
    CHECK(scene.structureRevision() == structure);
    CHECK(scene.all<Shape>() == std::vector<EntityId>{b, c});          // switched off is still listed

    // Structure changing does: components, entities, names, places in the tree.
    scene.removeComponent(b, "rolo.Shape");
    CHECK(scene.structureRevision() > structure);
    CHECK(scene.all<Shape>() == std::vector<EntityId>{c});
    scene.rename(c, "Box");
    scene.rename(a, "Crate");
    CHECK(scene.findByName("Box") == b);
    CHECK(scene.findByName("Lamp").isNull());
    scene.move(c, {}, 0);                                              // to the front
    CHECK(scene.inOrder() == std::vector<EntityId>{c, a, b});
    scene.remove(a);                                                   // and b inside it
    CHECK(scene.inOrder() == std::vector<EntityId>{c});
    CHECK(scene.findByName("Crate").isNull());
    const EntityId d = scene.create("New")->id();
    CHECK(scene.all<Transform>() == std::vector<EntityId>{c, d});

    // A copy has its own lists.
    Scene copy = scene;
    copy.remove(d);
    CHECK(copy.inOrder() == std::vector<EntityId>{c});
    CHECK(scene.inOrder() == std::vector<EntityId>{c, d});

    // Components this program does not know are found by the type they had in the file.
    scene.addComponent(d, std::make_unique<UnknownComponent>("future.Thing", "{}"));
    CHECK(scene.entitiesWith("future.Thing") == std::vector<EntityId>{d});
    CHECK(scene.find(d)->component("future.Thing") != nullptr);
    CHECK(scene.find(d)->get<Shape>() == nullptr);
    CHECK(scene.find(c)->get<Shape>() != nullptr);
}
