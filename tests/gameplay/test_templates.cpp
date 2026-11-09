// The New Project wizard's templates (templates/) and example games: listed,
// copied into a new project as the wizard does, and played.

#include "support/PlayProject.hpp"
#include "RoloResources.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace rolo;
using namespace rolo::editor;
namespace fs = std::filesystem;

namespace {

const fs::path kSource = ROLO_SOURCE_DIR;

ProjectTemplate find(const std::vector<ProjectTemplate>& all, std::string_view id)
{
    const auto it = std::find_if(all.begin(), all.end(), [&](const ProjectTemplate& t) { return t.id == id; });
    REQUIRE(it != all.end());
    return *it;
}

struct NewProject {
    fs::path folder = test::uniqueTempFolder("from-template") / "My Game";
    ~NewProject()
    {
        std::error_code ec;
        fs::remove_all(folder.parent_path(), ec);
    }
};

std::string all(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) {
        out += l + "\n";
    }
    return out;
}

} // namespace

TEST_CASE("The wizard offers the templates and the example game", "[templates]")
{
    const auto r = findRoloResources("/nowhere/rolo_editor", HostOs::Linux, kSource, {});
    const auto list = listProjectTemplates(r);
    REQUIRE(list.size() == 4);
    CHECK(list[0].name == "Empty Project");
    CHECK(list[1].name == "Top-Down Starter");
    CHECK(list[2].name == "Platformer Starter");
    CHECK(list[3].name == "Example: The Lantern Keeper");
    CHECK(list[3].example);
}

TEST_CASE("A project made from the Top-Down Starter plays without any C++", "[templates]")
{
    const auto list = listProjectTemplates(findRoloResources("/x/rolo_editor", HostOs::Linux, kSource, {}));
    NewProject made;
    std::string error;
    REQUIRE(createProjectFromTemplate(find(list, "topdown"), made.folder, "My Game", &error));

    test::PlayProject p(made.folder);
    REQUIRE(p.start());
    Game& game = *p.game;
    CHECK(game.scene().name() == "Meadow");
    CHECK(game.content().state().questState(p.assets->findByPath("content/hello.lquest")->id) == QuestState::Active);

    // Walking: the hero plays its walk clip and the camera stays on the map.
    game.input().press(Key::Right);
    p.frames(20);
    CHECK(game.currentClip(p.player()) == "walk_right");
    game.input().release(Key::Right);
    p.frames(1);
    CHECK(game.currentClip(p.player()) == "idle_right");
    CHECK(game.worldPosition(game.find("Camera")).x == Catch::Approx(640.0f));

    // Talking to the friend finishes the first quest.
    const EntityId friendArea = game.scene().childrenOf(game.find("Friend")).front();
    p.stand(game.colliderBox(friendArea)->center());
    p.tap(Key::E);
    REQUIRE(game.isInDialogue());
    for (int i = 0; i < 6 && game.isInDialogue(); ++i) {
        p.tap(Key::Down);   // choose "Bye!", the fourth answer
        p.tap(Key::Down);
        p.tap(Key::Down);
        p.tap(Key::E);
    }
    CHECK_FALSE(game.isInDialogue());
    CHECK(game.content().state().questState(p.assets->findByPath("content/hello.lquest")->id) == QuestState::Completed);

    // The path east leads to the grove, arriving at "From Meadow"; and back.
    p.stand(game.colliderBox(game.find("Path to the Grove"))->center());
    p.frames(2);
    REQUIRE(game.scene().name() == "Grove");
    CHECK(game.worldPosition(p.player()).x == Catch::Approx(game.worldPosition(game.find("From Meadow")).x));

    // The fireflies fly: a Lua script (scripts/firefly.lua) moves them.
    const EntityId firefly = game.find("Firefly 1");
    REQUIRE_FALSE(firefly.isNull());
    const Vec2 before = game.worldPosition(firefly);
    p.frames(30);
    const Vec2 after = game.worldPosition(firefly);
    CHECK((after.x != before.x || after.y != before.y));
    p.stand(game.colliderBox(game.find("Path to the Meadow"))->center());
    p.frames(2);
    REQUIRE(game.scene().name() == "Meadow");
    CHECK(game.worldPosition(p.player()).x == Catch::Approx(game.worldPosition(game.find("From Grove")).x));

    INFO(all(p.errors));
    CHECK(p.errors.empty());
}

TEST_CASE("A project made from the Platformer Starter runs, jumps and pushes with physics", "[templates][physics]")
{
    const auto list = listProjectTemplates(findRoloResources("/x/rolo_editor", HostOs::Linux, kSource, {}));
    NewProject made;
    std::string error;
    REQUIRE(createProjectFromTemplate(find(list, "platformer"), made.folder, "My Game", &error));

    test::PlayProject p(made.folder);
    REQUIRE(p.start());
    Game& game = *p.game;
    CHECK(game.scene().name() == "Level 1");
    CHECK(game.hasPhysics());

    // The hero lands on the grass.
    p.frames(60);
    const EntityId hero = p.player();
    CHECK(game.isOnGround(hero));
    const Vec2 start = game.worldPosition(hero);

    // Running right (scripts/player.lua sets its velocity) plays the walk clip.
    game.input().press(Key::Right);
    p.frames(30);
    CHECK(game.worldPosition(hero).x > start.x + 100.0f);
    CHECK(game.currentClip(hero) == "walk_right");
    game.input().release(Key::Right);
    p.frames(30);

    // Jumping: up it goes, and comes back down to the ground.
    const float groundY = game.worldPosition(hero).y;
    p.tap(Key::Space);
    p.frames(15);
    CHECK(game.worldPosition(hero).y > groundY + 80.0f);
    CHECK_FALSE(game.isOnGround(hero));
    p.frames(90);
    CHECK(game.worldPosition(hero).y == Catch::Approx(groundY).margin(2.0f));
    CHECK(game.isOnGround(hero));

    // Pushing a crate: walk into it from the left.
    const EntityId crate = game.find("Crate 3");   // alone on flat ground
    REQUIRE_FALSE(crate.isNull());
    const Vec2 crateBefore = game.worldPosition(crate);
    game.transform(hero)->position = {crateBefore.x - 80.0f, groundY};
    game.input().press(Key::Right);
    p.frames(40);
    game.input().release(Key::Right);
    CHECK(game.worldPosition(crate).x > crateBefore.x + 40.0f);

    // A coin: touching it collects it (a Trigger: a sound, +1 coin, gone).
    const EntityId coin = game.find("Coin 1");
    REQUIRE_FALSE(coin.isNull());
    game.transform(hero)->position = game.worldPosition(coin);
    p.frames(3);
    CHECK(game.find("Coin 1").isNull());
    CHECK(std::get<int>(*game.content().state().variable("coins")) == 1);

    // The lift goes up and down by itself (scripts/lift.lua).
    const EntityId lift = game.find("Lift 1");
    const float liftY = game.worldPosition(lift).y;
    p.frames(60);
    CHECK(game.worldPosition(lift).y > liftY + 50.0f);

    INFO(all(p.errors));
    CHECK(p.errors.empty());
}

TEST_CASE("Projects made from the other templates open and start cleanly", "[templates]")
{
    const auto list = listProjectTemplates(findRoloResources("/x/rolo_editor", HostOs::Linux, kSource, {}));
    for (const char* id : {"empty", "lantern_keeper"}) {
        INFO(id);
        NewProject made;
        std::string error;
        REQUIRE(createProjectFromTemplate(find(list, id), made.folder, "My Game", &error));
        test::PlayProject p(made.folder);
        REQUIRE(p.start());
        INFO(all(p.errors));
        CHECK(p.errors.empty());
    }
}
