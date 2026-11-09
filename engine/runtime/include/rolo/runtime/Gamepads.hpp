#pragma once

// Gamepads: reads the computer's game controllers into rolo::Input.
//
// Built on GLFW's gamepad support (GLFW comes with Axmol). GLFW carries a
// database of controller layouts (from SDL's GameControllerDB), so Xbox,
// PlayStation, Nintendo Switch Pro and most other pads report their buttons
// in the same places: A is always the bottom face button.
//
// The player and the editor's play mode create one GamepadReader and call
// read() once a frame, before the game updates.

#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Input.hpp"

#include <array>
#include <memory>
#include <string>

namespace rolo {

namespace detail {
class Rumble;
class RumbleTimer;
}

/// A gamepad's buttons and axes as GLFW reports them (GLFWgamepadstate:
/// 15 buttons, then 6 axes with y pointing down and triggers from -1 to 1),
/// turned into Rolo's GamepadState (y up, triggers 0 to 1). Exposed for tests.
GamepadState gamepadFromGlfw(const unsigned char (&buttons)[15], const float (&axes)[6]);

class GamepadReader {
public:
    explicit GamepadReader(const Logger& log);
    ~GamepadReader();   // stops any rumble
    GamepadReader(const GamepadReader&) = delete;
    GamepadReader& operator=(const GamepadReader&) = delete;

    /// Reads every connected gamepad into `input` (up to Input::kMaxGamepads),
    /// and says in the log when one is plugged in or out. Also sends the
    /// rumbles the game asked for (Input::rumble) and ends them on time.
    void read(Input& input);
    /// Stops every rumble at once (the game was paused or stopped).
    void stopRumble();

private:
    void updateRumble(Input& input);

    const Logger& m_log;
    std::unique_ptr<detail::Rumble> m_rumble;
    std::unique_ptr<detail::RumbleTimer> m_timer;
    // Which GLFW joystick each of Rolo's pads is (-1: none). A pad keeps its
    // number while it stays plugged in, even if one before it is unplugged.
    std::array<int, Input::kMaxGamepads> m_joystickOf{-1, -1, -1, -1};
    std::array<bool, 16> m_reportedUnknown{};   // joysticks without a layout, already mentioned
    std::array<std::string, Input::kMaxGamepads> m_names;   // each pad's name, e.g. "PS5 Controller"
};

} // namespace rolo
