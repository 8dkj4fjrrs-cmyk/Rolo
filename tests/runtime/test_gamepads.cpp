// Turning GLFW's gamepad state into Rolo's (engine/runtime/src/Gamepads.cpp):
// the same buttons, sticks with y up, triggers from 0 to 1.

#include "rolo/runtime/Gamepads.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rolo;

TEST_CASE("GLFW's gamepad state becomes Rolo's: y up, triggers 0 to 1", "[runtime][gamepad]")
{
    unsigned char buttons[15] = {};
    buttons[0] = 1;    // GLFW_GAMEPAD_BUTTON_A, pressed
    buttons[7] = 1;    // START
    buttons[13] = 1;   // DPAD_DOWN
    const float axes[6] = {0.5f, -1.0f, 0.0f, 0.25f, -1.0f, 1.0f};   // left stick pushed up; triggers released, full
    const GamepadState state = gamepadFromGlfw(buttons, axes);
    CHECK(state.connected);
    CHECK(state.buttons[static_cast<std::size_t>(GamepadButton::A)]);
    CHECK(state.buttons[static_cast<std::size_t>(GamepadButton::Start)]);
    CHECK(state.buttons[static_cast<std::size_t>(GamepadButton::Down)]);
    CHECK_FALSE(state.buttons[static_cast<std::size_t>(GamepadButton::B)]);
    CHECK(state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] == 0.5f);
    CHECK(state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] == 1.0f);     // up is positive in Rolo
    CHECK(state.axes[static_cast<std::size_t>(GamepadAxis::RightY)] == -0.25f);
    CHECK(state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] == 0.0f);
    CHECK(state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] == 1.0f);

    // Through Input: the stick reads as pushed up.
    Input input;
    input.setGamepad(0, state);
    CHECK(input.leftStick().y > 0.8f);
    CHECK(input.wasPressed(GamepadButton::A));
}

TEST_CASE("Reading gamepads with none plugged in leaves them unplugged", "[runtime][gamepad]")
{
    // On the test machine there is no gamepad (and possibly no GLFW window):
    // reading must work and report nothing.
    Logger log;
    GamepadReader reader(log);
    Input input;
    reader.read(input);
    CHECK_FALSE(input.isGamepadConnected());
}
