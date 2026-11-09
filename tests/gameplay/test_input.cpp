#include "rolo/gameplay/Input.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace rolo;

TEST_CASE("Keys are down, pressed this frame, released this frame", "[gameplay][input]")
{
    Input input;
    CHECK_FALSE(input.isDown(Key::Space));
    input.press(Key::Space);
    CHECK(input.isDown(Key::Space));
    CHECK(input.wasPressed(Key::Space));
    input.press(Key::Space);   // key repeat: still one press
    input.endFrame();
    CHECK(input.isDown(Key::Space));
    CHECK_FALSE(input.wasPressed(Key::Space));
    input.release(Key::Space);
    CHECK(input.wasReleased(Key::Space));
    CHECK_FALSE(input.isDown(Key::Space));
    input.endFrame();
    CHECK_FALSE(input.wasReleased(Key::Space));
}

TEST_CASE("A tap shorter than a frame still counts as a press", "[gameplay][input]")
{
    Input input;
    input.press(Key::Enter);
    input.release(Key::Enter);
    CHECK(input.wasPressed(Key::Enter));
    CHECK(input.wasReleased(Key::Enter));
    CHECK_FALSE(input.isDown(Key::Enter));
}

TEST_CASE("Move direction from arrows or WASD, never faster diagonally", "[gameplay][input]")
{
    Input input;
    CHECK(input.moveDirection() == Vec2{0, 0});
    input.press(Key::Right);
    CHECK(input.moveDirection() == Vec2{1, 0});
    input.press(Key::W);
    const Vec2 d = input.moveDirection();
    CHECK(d.x == Catch::Approx(0.7071f));
    CHECK(d.y == Catch::Approx(0.7071f));
    input.press(Key::A);   // left and right cancel out
    CHECK(input.moveDirection().x == 0.0f);
    CHECK(input.moveDirection().y == 1.0f);
}

TEST_CASE("Releasing everything lets go of held keys and buttons", "[gameplay][input]")
{
    Input input;
    input.press(Key::Left);
    input.press(MouseButton::Left);
    input.setPointer({3, 4});
    input.endFrame();
    input.releaseAll();
    CHECK_FALSE(input.isDown(Key::Left));
    CHECK(input.wasReleased(Key::Left));
    CHECK_FALSE(input.isDown(MouseButton::Left));
    CHECK(input.pointer() == Vec2{3, 4});
    CHECK(keyName(Key::Left) == "Left");
    CHECK(keyName(Key::Num7) == "7");
    CHECK(keyName(Key::Count) == "Unknown");
}

// --- Gamepads ---------------------------------------------------------------------------------------------

namespace {

GamepadState pad(std::initializer_list<GamepadButton> down = {}, Vec2 leftStick = {}, float rightTrigger = 0.0f)
{
    GamepadState s;
    s.connected = true;
    for (const GamepadButton b : down) {
        s.buttons[static_cast<std::size_t>(b)] = true;
    }
    s.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = leftStick.x;
    s.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = leftStick.y;
    s.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = rightTrigger;
    return s;
}

} // namespace

TEST_CASE("Gamepad buttons are down, pressed and released like keys", "[gameplay][input][gamepad]")
{
    Input input;
    CHECK_FALSE(input.isGamepadConnected());
    input.setGamepad(0, pad({GamepadButton::A}));
    CHECK(input.isGamepadConnected());
    CHECK(input.isGamepadConnected(0));
    CHECK_FALSE(input.isGamepadConnected(1));
    CHECK(input.isDown(GamepadButton::A));
    CHECK(input.wasPressed(GamepadButton::A));
    input.endFrame();
    input.setGamepad(0, pad({GamepadButton::A}));   // still held: no new press
    CHECK(input.isDown(GamepadButton::A));
    CHECK_FALSE(input.wasPressed(GamepadButton::A));
    input.endFrame();
    input.setGamepad(0, pad());
    CHECK(input.wasReleased(GamepadButton::A));
    CHECK_FALSE(input.isDown(GamepadButton::A));
    CHECK(gamepadButtonName(GamepadButton::LeftBumper) == "LB");
}

TEST_CASE("Without a number any gamepad counts; with one, only that pad", "[gameplay][input][gamepad]")
{
    Input input;
    input.setGamepad(0, pad());
    input.setGamepad(2, pad({GamepadButton::Start}));
    CHECK(input.isDown(GamepadButton::Start));
    CHECK_FALSE(input.isDown(GamepadButton::Start, 0));
    CHECK(input.isDown(GamepadButton::Start, 2));
    CHECK_FALSE(input.isDown(GamepadButton::Start, 1));   // not plugged in
    CHECK_FALSE(input.isDown(GamepadButton::Count));      // not a button

    // Unplugged while held: let go.
    input.endFrame();
    input.setGamepad(2, GamepadState{});
    CHECK_FALSE(input.isDown(GamepadButton::Start));
    CHECK(input.wasReleased(GamepadButton::Start));
    CHECK(input.wasReleased(GamepadButton::Start, 2));
    CHECK_FALSE(input.isGamepadConnected(2));
}

TEST_CASE("Sticks have a dead zone, start again from 0 past it, and never exceed 1", "[gameplay][input][gamepad]")
{
    Input input;
    CHECK(input.leftStick() == Vec2{});   // no gamepad
    input.setGamepad(0, pad({}, {0.1f, -0.1f}));
    CHECK(input.leftStick() == Vec2{});   // resting, a little off centre
    input.setGamepad(0, pad({}, {0.6f, 0.0f}));
    CHECK(input.leftStick().x == Catch::Approx(0.5f));   // (0.6 - 0.2) / 0.8
    input.setGamepad(0, pad({}, {1.0f, 1.0f}));          // a square gate's corner
    const Vec2 corner = input.leftStick();
    CHECK(std::sqrt(corner.x * corner.x + corner.y * corner.y) == Catch::Approx(1.0f));
    CHECK(input.rightStick() == Vec2{});
}

TEST_CASE("Triggers go from 0 to 1", "[gameplay][input][gamepad]")
{
    Input input;
    input.setGamepad(1, pad({}, {}, 0.75f));
    CHECK(input.trigger(GamepadAxis::RightTrigger) == Catch::Approx(0.75f));
    CHECK(input.trigger(GamepadAxis::LeftTrigger) == 0.0f);
    CHECK(input.trigger(GamepadAxis::LeftX) == 0.0f);   // not a trigger
}

TEST_CASE("moveDirection steers with the keys, else the d-pad, else the left stick", "[gameplay][input][gamepad]")
{
    Input input;
    input.setGamepad(0, pad({}, {0.6f, 0.0f}));
    CHECK(input.moveDirection().x == Catch::Approx(0.5f));   // the stick: slower than full speed
    input.setGamepad(0, pad({GamepadButton::Up, GamepadButton::Left}, {0.6f, 0.0f}));
    const Vec2 dpad = input.moveDirection();
    CHECK(dpad.x == Catch::Approx(-0.7071f).margin(0.001f));
    CHECK(dpad.y == Catch::Approx(0.7071f).margin(0.001f));
    input.press(Key::Right);   // keys and buttons combine: right and left cancel out, up stays
    CHECK(input.moveDirection() == Vec2{0.0f, 1.0f});
}

TEST_CASE("A stick pushed past halfway counts as a nudge once, for menus", "[gameplay][input][gamepad]")
{
    Input input;
    input.setGamepad(0, pad({}, {0.0f, -0.9f}));
    CHECK(input.wasNudged(GamepadButton::Down));
    CHECK_FALSE(input.wasNudged(GamepadButton::Up));
    input.endFrame();
    input.setGamepad(0, pad({}, {0.0f, -0.95f}));   // still held down: not again
    CHECK_FALSE(input.wasNudged(GamepadButton::Down));
    input.endFrame();
    input.setGamepad(0, pad({GamepadButton::Up}));   // the d-pad counts too
    CHECK(input.wasNudged(GamepadButton::Up));
    CHECK_FALSE(input.wasNudged(GamepadButton::A));
}

TEST_CASE("releaseAll lets go of gamepad buttons too", "[gameplay][input][gamepad]")
{
    Input input;
    input.setGamepad(0, pad({GamepadButton::B}));
    input.endFrame();
    input.releaseAll();
    CHECK_FALSE(input.isDown(GamepadButton::B));
    CHECK(input.wasReleased(GamepadButton::B));
}

// --- Actions and remapping ------------------------------------------------------------------------------

TEST_CASE("Rolo's own actions are there from the start", "[gameplay][input][actions]")
{
    Input input;
    CHECK(input.actions() == std::vector<std::string>{"move left", "move right", "move up", "move down", "interact"});
    input.press(Key::E);
    CHECK(input.wasActionPressed("interact"));
    input.endFrame();
    input.setGamepad(0, pad({GamepadButton::A}));
    CHECK(input.wasActionPressed("interact"));
    CHECK(input.isActionDown("interact"));
    CHECK_FALSE(input.isActionDown("no such action"));
}

TEST_CASE("Games define actions; any of their bindings counts", "[gameplay][input][actions]")
{
    Input input;
    input.defineAction("jump", {Binding::ofKey(Key::Space), Binding::ofPad(GamepadButton::A), Binding::ofMouse(MouseButton::Right)});
    CHECK(input.hasAction("jump"));
    input.press(MouseButton::Right);
    CHECK(input.wasActionPressed("jump"));
    input.release(MouseButton::Right);
    CHECK(input.wasActionReleased("jump"));
    CHECK(input.bindingFor("jump", InputDevice::Gamepad) == Binding::ofPad(GamepadButton::A));
    CHECK(input.bindingFor("jump", InputDevice::Keyboard) == Binding::ofKey(Key::Space));
}

TEST_CASE("Binding names are read and written", "[gameplay][input][actions]")
{
    CHECK(bindingName(Binding::ofKey(Key::LeftShift)) == "Left Shift");
    CHECK(bindingName(Binding::ofMouse(MouseButton::Middle)) == "Mouse Middle");
    CHECK(bindingName(Binding::ofPad(GamepadButton::RightBumper)) == "Pad RB");
    CHECK(parseBinding("left_shift") == Binding::ofKey(Key::LeftShift));
    CHECK(parseBinding("Gamepad Start") == Binding::ofPad(GamepadButton::Start));
    CHECK(parseBinding("pad up") == Binding::ofPad(GamepadButton::Up));
    CHECK(parseBinding("mouse left") == Binding::ofMouse(MouseButton::Left));
    CHECK(parseBinding("return") == Binding::ofKey(Key::Enter));
    CHECK_FALSE(parseBinding("pad q"));
    CHECK_FALSE(parseBinding("hyperspace"));
}

TEST_CASE("Remapping: the next press becomes the binding for its kind of device", "[gameplay][input][actions]")
{
    Input input;
    input.defineAction("jump", {Binding::ofKey(Key::Space), Binding::ofPad(GamepadButton::A)});
    const auto revision = input.bindingsRevision();

    input.startRebind("jump");
    CHECK(input.isRebinding());
    CHECK(input.rebindingAction() == "jump");
    input.press(Key::J);
    CHECK_FALSE(input.isRebinding());
    CHECK_FALSE(input.wasPressed(Key::J));   // used up by the remapping
    CHECK(input.bindings("jump") == std::vector<Binding>{Binding::ofKey(Key::J), Binding::ofPad(GamepadButton::A)});
    CHECK(input.bindingsRevision() != revision);
    input.release(Key::J);
    input.endFrame();

    // A gamepad button replaces only the gamepad binding.
    input.startRebind("jump");
    input.setGamepad(0, pad({GamepadButton::RightBumper}));
    CHECK_FALSE(input.wasPressed(GamepadButton::RightBumper));
    CHECK(input.bindings("jump") == std::vector<Binding>{Binding::ofKey(Key::J), Binding::ofPad(GamepadButton::RightBumper)});
    input.endFrame();
    input.setGamepad(0, pad());

    // Escape or Backspace gives up; unknown actions cannot be remapped.
    input.startRebind("jump");
    input.press(Key::Escape);
    CHECK_FALSE(input.isRebinding());
    input.startRebind("jump");
    input.press(Key::Backspace);
    CHECK_FALSE(input.isRebinding());
    CHECK(input.bindings("jump").front() == Binding::ofKey(Key::J));
    input.startRebind("fly");
    CHECK_FALSE(input.isRebinding());

    input.resetBindings("jump");
    CHECK(input.bindings("jump").front() == Binding::ofKey(Key::Space));
}

TEST_CASE("Remapped controls are written and read back, even before the game defines them", "[gameplay][input][actions]")
{
    Input before;
    before.defineAction("jump", {Binding::ofKey(Key::Space)});
    before.setBindings("jump", {Binding::ofKey(Key::K), Binding::ofPad(GamepadButton::B)});
    before.setBindings("move left", {Binding::ofKey(Key::J)});
    const std::string text = before.saveBindings();
    CHECK(text == "move left=J\njump=K,Pad B\n");   // only the changed ones, in the order defined

    Input after;
    after.loadBindings(text + "broken line\n=nothing\n");
    CHECK(after.bindings("move left") == std::vector<Binding>{Binding::ofKey(Key::J)});
    after.press(Key::J);
    CHECK(after.moveDirection() == Vec2{-1.0f, 0.0f});
    after.defineAction("jump", {Binding::ofKey(Key::Space)});   // defined later: the player's choice wins
    CHECK(after.bindings("jump") == std::vector<Binding>{Binding::ofKey(Key::K), Binding::ofPad(GamepadButton::B)});
    after.resetBindings();
    CHECK(after.bindings("jump") == std::vector<Binding>{Binding::ofKey(Key::Space)});
    CHECK(after.saveBindings().empty());
}

TEST_CASE("The input knows which device the player used last, and the pad's style", "[gameplay][input][gamepad]")
{
    Input input;
    CHECK(input.lastDevice() == InputDevice::Keyboard);
    GamepadState ps = pad({GamepadButton::X});
    ps.style = gamepadStyleFromName("DualSense Wireless Controller");
    input.setGamepad(1, ps);
    CHECK(input.lastDevice() == InputDevice::Gamepad);
    CHECK(input.gamepadStyle() == GamepadStyle::PlayStation);
    input.press(Key::Space);
    CHECK(input.lastDevice() == InputDevice::Keyboard);
    CHECK(gamepadStyleFromName("Xbox Wireless Controller") == GamepadStyle::Xbox);
    CHECK(gamepadStyleFromName("Nintendo Switch Pro Controller") == GamepadStyle::Nintendo);
    CHECK(gamepadStyleFromName("8BitDo SN30") == GamepadStyle::Generic);
}

TEST_CASE("Rumbles are queued for the program, clamped to sensible values", "[gameplay][input][gamepad]")
{
    Input input;
    input.rumble(0.5f, 0.3f);
    input.rumble(2.0f, -1.0f, 20.0f, 1);
    input.rumble(0.0f, 0.0f, 0.0f, 9);
    const auto r = input.takeRumbles();
    REQUIRE(r.size() == 3);
    CHECK(r[0].pad == -1);
    CHECK(r[0].low == 0.5f);
    CHECK(r[0].high == 0.5f);
    CHECK(r[0].seconds == Catch::Approx(0.3f));
    CHECK(r[1].pad == 1);
    CHECK(r[1].low == 1.0f);
    CHECK(r[1].high == 0.0f);
    CHECK(r[1].seconds == 10.0f);
    CHECK(r[2].pad == -1);   // no gamepad 9: every gamepad
    CHECK(input.takeRumbles().empty());
}
