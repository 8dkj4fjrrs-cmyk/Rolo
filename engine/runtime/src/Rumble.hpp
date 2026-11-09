#pragma once

// Rumble: makes a gamepad shake, with each system's own controller API.
//
// GLFW (which reads the gamepads) cannot rumble, so this is Rolo's own code,
// one file per system (ADR 0018):
//   * Windows (Rumble_windows.cpp): XInput, built into Windows. Xbox
//     controllers and others that work like them;
//   * macOS (Rumble_apple.mm): the Game Controller framework's haptics
//     (macOS 11 or newer). Xbox, PlayStation and other supported pads;
//   * Linux (Rumble_linux.cpp): the kernel's force feedback (evdev), for any
//     gamepad whose driver supports it;
//   * elsewhere (Rumble_none.cpp): nothing.
// Pads that cannot rumble are simply not shaken.

#include "rolo/core/Log.hpp"
#include "rolo/gameplay/Input.hpp"

#include <array>
#include <memory>
#include <vector>

namespace rolo::detail {

class Rumble {
public:
    static std::unique_ptr<Rumble> create(const Logger& log);
    virtual ~Rumble() = default;

    /// Sets the motors of GLFW joystick `jid`: `low` the heavy one, `high`
    /// the light one, 0 to 1 (both 0 stops). False if it cannot rumble.
    /// Called every frame a rumble changes, and with 0 when it ends.
    virtual bool set(int jid, float low, float high) = 0;
};

/// Keeps time for rumbles: what each of the four pads should be doing, and
/// telling a Rumble only when that changes (separate from GLFW, for tests).
class RumbleTimer {
public:
    /// `jids`: each pad's GLFW joystick (-1: none). `time`: seconds, steady.
    void update(const std::vector<RumbleRequest>& requests, double time, const std::array<int, Input::kMaxGamepads>& jids,
                Rumble& out);
    /// Stops every pad that is rumbling.
    void stop(const std::array<int, Input::kMaxGamepads>& jids, Rumble& out);
    /// Forgets a pad (unplugged).
    void forget(int pad);

private:
    struct Shake {
        float low = 0.0f;
        float high = 0.0f;
        double until = 0.0;
        std::array<float, 2> sent{};
    };
    std::array<Shake, Input::kMaxGamepads> m_shakes{};
};

} // namespace rolo::detail
