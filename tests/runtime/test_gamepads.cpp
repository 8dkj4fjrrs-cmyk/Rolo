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

#include "Rumble.hpp"

#include <vector>

namespace {
struct FakeRumble final : detail::Rumble {
    struct Call {
        int jid;
        float low;
        float high;
    };
    std::vector<Call> calls;
    bool set(int jid, float low, float high) override
    {
        calls.push_back({jid, low, high});
        return true;
    }
};
} // namespace

TEST_CASE("Rumbles start, are replaced, and end on time", "[runtime][gamepad]")
{
    FakeRumble device;
    detail::RumbleTimer timer;
    const std::array<int, Input::kMaxGamepads> jids{3, -1, 7, -1};   // pads 1 and 3 plugged in

    timer.update({{-1, 0.5f, 0.25f, 0.2f}}, 10.0, jids, device);   // every pad, 0.2 s
    REQUIRE(device.calls.size() == 2);
    CHECK(device.calls[0].jid == 3);
    CHECK(device.calls[0].low == 0.5f);
    CHECK(device.calls[0].high == 0.25f);
    CHECK(device.calls[1].jid == 7);

    timer.update({}, 10.1, jids, device);   // still going: nothing to tell
    CHECK(device.calls.size() == 2);

    timer.update({{2, 1.0f, 1.0f, 1.0f}}, 10.15, jids, device);   // pad 3 harder and longer
    REQUIRE(device.calls.size() == 3);
    CHECK(device.calls[2].jid == 7);
    CHECK(device.calls[2].low == 1.0f);

    timer.update({}, 10.25, jids, device);   // pad 1's time is up
    REQUIRE(device.calls.size() == 4);
    CHECK(device.calls[3].jid == 3);
    CHECK(device.calls[3].low == 0.0f);

    timer.stop(jids, device);   // the game was stopped
    REQUIRE(device.calls.size() == 5);
    CHECK(device.calls[4].jid == 7);
    CHECK(device.calls[4].high == 0.0f);
    timer.stop(jids, device);
    CHECK(device.calls.size() == 5);   // nothing left rumbling

    timer.update({{0, 0.5f, 0.5f, 0.0f}}, 20.0, jids, device);   // zero seconds: no rumble at all
    CHECK(device.calls.size() == 5);
}
