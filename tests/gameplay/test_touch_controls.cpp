#include "rolo/gameplay/Input.hpp"
#include "rolo/gameplay/TouchControls.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace rolo;

namespace {
// The controls of a game with "jump", "interact" (Rolo's own) and "pause", on a 1280 x 720 screen.
struct Fixture {
    Input input;
    TouchControls controls;
    Fixture()
    {
        input.defineAction("jump", {Binding::ofKey(Key::Space)});
        input.defineAction("pause", {Binding::ofKey(Key::Escape)});
        controls.setActions(input);
        controls.layout(1280.0f, 720.0f);
    }
    const TouchButton& button(std::string_view action) const
    {
        const auto& all = controls.buttons();
        const auto found = std::find_if(all.begin(), all.end(), [&](const TouchButton& b) { return b.action == action; });
        INFO("the button for " << action);
        REQUIRE(found != all.end());
        return *found;
    }
};
} // namespace

TEST_CASE("Touches move the pointer and count as the left mouse button", "[input][touch]")
{
    Input input;
    CHECK(input.touches().empty());
    input.touchBegan(7, {100.0f, 50.0f});
    REQUIRE(input.touches().size() == 1);
    CHECK(input.touches()[0].id == 7);
    CHECK(input.touches()[0].began);
    CHECK(input.pointer() == Vec2{100.0f, 50.0f});
    CHECK(input.wasPressed(MouseButton::Left));
    CHECK(input.lastDevice() == InputDevice::Touch);

    // A second finger is listed, but the pointer stays with the first.
    input.touchBegan(8, {300.0f, 60.0f});
    input.touchMoved(8, {310.0f, 60.0f});
    CHECK(input.touches().size() == 2);
    CHECK(input.pointer() == Vec2{100.0f, 50.0f});
    input.endFrame();
    CHECK_FALSE(input.touches()[0].began);
    CHECK(input.isDown(MouseButton::Left));

    input.touchMoved(7, {120.0f, 55.0f});
    CHECK(input.pointer() == Vec2{120.0f, 55.0f});
    // Lifting: listed as ended for one frame, then gone.
    input.touchEnded(7);
    CHECK(input.wasReleased(MouseButton::Left));
    REQUIRE(input.touches().size() == 2);
    CHECK(input.touches()[0].ended);
    input.endFrame();
    REQUIRE(input.touches().size() == 1);
    CHECK(input.touches()[0].id == 8);

    // A key press makes the keyboard the device in use again.
    input.press(Key::A);
    CHECK(input.lastDevice() == InputDevice::Keyboard);
}

TEST_CASE("On-screen buttons are made from the game's actions", "[input][touch]")
{
    Fixture f;
    // Not the four move actions (the stick does those); "interact" is Rolo's own action.
    const std::vector<std::string> expected = {"interact", "jump", "pause"};
    CHECK(f.controls.actions() == expected);
    CHECK(f.button("jump").label == "Jump");

    // On the screen, apart from each other, the main ones on the right, pause small at the top.
    const auto& buttons = f.controls.buttons();
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const TouchButton& b = buttons[i];
        CHECK(b.center.x - b.radius >= 0.0f);
        CHECK(b.center.x + b.radius <= 1280.0f);
        CHECK(b.center.y - b.radius >= 0.0f);
        CHECK(b.center.y + b.radius <= 720.0f);
        for (std::size_t j = i + 1; j < buttons.size(); ++j) {
            const float d = std::hypot(b.center.x - buttons[j].center.x, b.center.y - buttons[j].center.y);
            CHECK(d > b.radius + buttons[j].radius);
        }
    }
    CHECK(f.button("interact").center.x > 900.0f);
    CHECK(f.button("jump").center.x > 900.0f);
    CHECK(f.button("pause").small);
    CHECK(f.button("pause").center.y > 600.0f);
    CHECK(f.controls.stick().rest.x < 300.0f);

    // Six main buttons still fit a phone held upright, apart from each other and from the stick.
    Input many;
    for (const char* name : {"a", "b", "c", "d", "e", "f", "g"}) {
        many.defineAction(name, {});
    }
    TouchControls tall;
    tall.setActions(many, {"a", "b", "c", "d", "e", "f", "g", "no such action", "move left"});
    tall.layout(720.0f, 1280.0f);
    REQUIRE(tall.buttons().size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        const TouchButton& b = tall.buttons()[i];
        CHECK(b.center.x - b.radius >= 0.0f);
        CHECK(b.center.y + b.radius <= 1280.0f);
        CHECK(std::hypot(b.center.x - tall.stick().rest.x, b.center.y - tall.stick().rest.y)
              > b.radius + tall.stick().radius);
        for (std::size_t j = i + 1; j < 6; ++j) {
            CHECK(std::hypot(b.center.x - tall.buttons()[j].center.x, b.center.y - tall.buttons()[j].center.y)
                  > b.radius + tall.buttons()[j].radius);
        }
    }
}

TEST_CASE("Pressing an on-screen button is pressing its action", "[input][touch]")
{
    Fixture f;
    const Vec2 jump = f.button("jump").center;
    REQUIRE(f.controls.touchBegan(1, jump));
    CHECK(f.controls.owns(1));
    f.controls.apply(f.input);
    CHECK(f.input.isActionDown("jump"));
    CHECK(f.input.wasActionPressed("jump"));
    CHECK_FALSE(f.input.isActionDown("interact"));
    CHECK(f.input.lastDevice() == InputDevice::Touch);
    CHECK(f.input.touches().empty());           // the game does not see fingers on controls
    CHECK_FALSE(f.input.isDown(Key::Space));    // the key itself is not pressed

    f.input.endFrame();
    f.controls.apply(f.input);
    CHECK(f.input.isActionDown("jump"));
    CHECK_FALSE(f.input.wasActionPressed("jump"));   // held, not pressed again

    f.controls.touchEnded(1);
    f.controls.apply(f.input);
    CHECK_FALSE(f.input.isActionDown("jump"));
    CHECK(f.input.wasActionReleased("jump"));

    // The keyboard still works beside it.
    f.input.endFrame();
    f.input.press(Key::Space);
    CHECK(f.input.wasActionPressed("jump"));

    // A finger on empty screen is not taken.
    CHECK_FALSE(f.controls.touchBegan(2, {640.0f, 400.0f}));
}

TEST_CASE("The on-screen stick steers", "[input][touch]")
{
    Fixture f;
    // Anywhere bottom-left: the stick's base comes to the finger.
    REQUIRE(f.controls.touchBegan(1, {200.0f, 200.0f}));
    CHECK(f.controls.stick().held);
    CHECK(f.controls.stick().base == Vec2{200.0f, 200.0f});
    f.controls.apply(f.input);
    CHECK(f.input.moveDirection() == Vec2{0.0f, 0.0f});   // not pushed yet

    // Pushed fully right (further than the stick reaches): 1 long, and "move right" is down.
    f.controls.touchMoved(1, {600.0f, 200.0f});
    f.controls.apply(f.input);
    CHECK(f.input.moveDirection().x == Catch::Approx(1.0f));
    CHECK(f.input.moveDirection().y == Catch::Approx(0.0f).margin(0.001));
    CHECK(f.input.isActionDown("move right"));
    CHECK(f.input.wasActionPressed("move right"));   // menus can ask for it
    CHECK_FALSE(f.input.isActionDown("move up"));

    // A little way up: walking slowly, no action held.
    const float r = f.controls.stick().radius;
    f.input.endFrame();
    f.controls.touchMoved(1, {200.0f, 200.0f + r * 0.4f});
    f.controls.apply(f.input);
    CHECK(f.input.moveDirection().y == Catch::Approx(0.4f).margin(0.01));
    CHECK_FALSE(f.input.isActionDown("move up"));
    CHECK(f.input.wasActionReleased("move right"));

    // A second finger can press a button meanwhile.
    REQUIRE(f.controls.touchBegan(2, f.button("jump").center));
    f.controls.apply(f.input);
    CHECK(f.input.isActionDown("jump"));

    // Let go: back to rest.
    f.controls.touchEnded(1);
    f.controls.apply(f.input);
    CHECK_FALSE(f.controls.stick().held);
    CHECK(f.input.moveDirection() == Vec2{0.0f, 0.0f});
    CHECK(f.input.isActionDown("jump"));

    f.controls.releaseAll();
    f.controls.apply(f.input);
    CHECK_FALSE(f.input.isActionDown("jump"));
}
