#include "rolo/gameplay/Input.hpp"

#include <algorithm>
#include <cmath>

namespace rolo {

namespace {

// Sticks never rest at exactly 0; inside this they count as centred.
constexpr float kDeadZone = 0.2f;
// For menus: how far a stick must be pushed to count as a nudge.
constexpr float kNudge = 0.6f;

float length(Vec2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

} // namespace

std::string_view gamepadButtonName(GamepadButton button)
{
    static constexpr std::string_view kNames[] = {"A",  "B",     "X",    "Y",  "LB",    "RB",   "Back", "Start",
                                                  "Guide", "LS", "RS",  "Up", "Right", "Down", "Left"};
    static_assert(std::size(kNames) == static_cast<std::size_t>(GamepadButton::Count), "a name for every button");
    const auto i = static_cast<std::size_t>(button);
    return i < std::size(kNames) ? kNames[i] : std::string_view("Unknown");
}

std::string_view keyName(Key key)
{
    static constexpr std::string_view kNames[] = {
        "Unknown",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V",
        "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "Space", "Enter", "Escape", "Tab", "Backspace",
        "Left", "Right", "Up", "Down",
        "Left Shift", "Right Shift", "Left Control", "Right Control", "Left Alt", "Right Alt",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    };
    static_assert(std::size(kNames) == static_cast<std::size_t>(Key::Count), "a name for every key");
    const auto i = static_cast<std::size_t>(key);
    return i < std::size(kNames) ? kNames[i] : kNames[0];
}

Vec2 Input::moveDirection() const noexcept
{
    float x = 0.0f;
    float y = 0.0f;
    if (isDown(Key::Left) || isDown(Key::A)) {
        x -= 1.0f;
    }
    if (isDown(Key::Right) || isDown(Key::D)) {
        x += 1.0f;
    }
    if (isDown(Key::Down) || isDown(Key::S)) {
        y -= 1.0f;
    }
    if (isDown(Key::Up) || isDown(Key::W)) {
        y += 1.0f;
    }
    if (x == 0.0f && y == 0.0f) {
        // No keys: the d-pad, or else the left stick (any gamepad).
        if (isDown(GamepadButton::Left)) {
            x -= 1.0f;
        }
        if (isDown(GamepadButton::Right)) {
            x += 1.0f;
        }
        if (isDown(GamepadButton::Down)) {
            y -= 1.0f;
        }
        if (isDown(GamepadButton::Up)) {
            y += 1.0f;
        }
        if (x == 0.0f && y == 0.0f) {
            return leftStick();
        }
    }
    if (x != 0.0f && y != 0.0f) {
        // Diagonal: scale to length 1, or it would be ~1.41 times as fast.
        const float s = 1.0f / std::sqrt(2.0f);
        x *= s;
        y *= s;
    }
    return {x, y};
}

void Input::down(State& s) noexcept
{
    if (!s.down) {
        s.down = true;
        s.pressed = true;
    }
}

void Input::up(State& s) noexcept
{
    if (s.down) {
        s.down = false;
        s.released = true;
    }
}

void Input::press(Key key) noexcept
{
    if (key != Key::Unknown) {
        down(m_keys[index(key)]);
    }
}

void Input::release(Key key) noexcept
{
    if (key != Key::Unknown) {
        up(m_keys[index(key)]);
    }
}

void Input::press(MouseButton button) noexcept
{
    if (button != MouseButton::Count) {
        down(m_buttons[static_cast<std::size_t>(button)]);
    }
}

void Input::release(MouseButton button) noexcept
{
    if (button != MouseButton::Count) {
        up(m_buttons[static_cast<std::size_t>(button)]);
    }
}

void Input::releaseAll() noexcept
{
    for (State& s : m_keys) {
        up(s);
    }
    for (State& s : m_buttons) {
        up(s);
    }
    for (Pad& pad : m_pads) {
        for (State& s : pad.buttons) {
            up(s);
        }
    }
}

// --- Gamepads -------------------------------------------------------------------------------------------

void Input::setGamepad(int pad, const GamepadState& state) noexcept
{
    if (pad < 0 || pad >= kMaxGamepads) {
        return;
    }
    Pad& p = m_pads[static_cast<std::size_t>(pad)];
    p.connected = state.connected;
    for (std::size_t i = 0; i < p.buttons.size(); ++i) {
        // A gamepad unplugged with a button held lets go of it.
        (state.connected && state.buttons[i]) ? down(p.buttons[i]) : up(p.buttons[i]);
    }
    for (std::size_t i = 0; i < p.axes.size(); ++i) {
        const float v = state.connected && std::isfinite(state.axes[i]) ? state.axes[i] : 0.0f;
        p.axes[i] = std::clamp(v, -1.0f, 1.0f);
    }
}

const Input::Pad* Input::padFor(int pad) const noexcept
{
    if (pad >= 0 && pad < kMaxGamepads) {
        const Pad& p = m_pads[static_cast<std::size_t>(pad)];
        return p.connected ? &p : nullptr;
    }
    for (const Pad& p : m_pads) {
        if (p.connected) {
            return &p;
        }
    }
    return nullptr;
}

bool Input::isGamepadConnected(int pad) const noexcept
{
    return padFor(pad) != nullptr;
}

bool Input::isDown(GamepadButton button, int pad) const noexcept
{
    const auto b = static_cast<std::size_t>(button);
    if (b >= static_cast<std::size_t>(GamepadButton::Count)) {
        return false;
    }
    if (pad >= 0) {
        const Pad* p = padFor(pad);
        return p != nullptr && p->buttons[b].down;
    }
    return std::any_of(m_pads.begin(), m_pads.end(), [b](const Pad& p) { return p.connected && p.buttons[b].down; });
}

bool Input::wasPressed(GamepadButton button, int pad) const noexcept
{
    const auto b = static_cast<std::size_t>(button);
    if (b >= static_cast<std::size_t>(GamepadButton::Count)) {
        return false;
    }
    if (pad >= 0) {
        const Pad* p = padFor(pad);
        return p != nullptr && p->buttons[b].pressed;
    }
    return std::any_of(m_pads.begin(), m_pads.end(), [b](const Pad& p) { return p.connected && p.buttons[b].pressed; });
}

bool Input::wasReleased(GamepadButton button, int pad) const noexcept
{
    const auto b = static_cast<std::size_t>(button);
    if (b >= static_cast<std::size_t>(GamepadButton::Count)) {
        return false;
    }
    if (pad >= 0) {
        const Pad& p = m_pads[static_cast<std::size_t>(std::min(pad, kMaxGamepads - 1))];
        return p.buttons[b].released;   // also after the pad was unplugged
    }
    return std::any_of(m_pads.begin(), m_pads.end(), [b](const Pad& p) { return p.buttons[b].released; });
}

Vec2 Input::stick(GamepadAxis xAxis, GamepadAxis yAxis, int pad) const noexcept
{
    const Pad* p = padFor(pad);
    if (p == nullptr) {
        return {};
    }
    Vec2 v{p->axes[static_cast<std::size_t>(xAxis)], p->axes[static_cast<std::size_t>(yAxis)]};
    const float len = length(v);
    if (len <= kDeadZone) {
        return {};
    }
    // Past the dead zone the value starts again from 0, so slow walking is
    // possible; and it never goes past 1 (square gates reach 1.41 in corners).
    const float scaled = std::min(1.0f, (len - kDeadZone) / (1.0f - kDeadZone));
    return {v.x / len * scaled, v.y / len * scaled};
}

Vec2 Input::leftStick(int pad) const noexcept
{
    return stick(GamepadAxis::LeftX, GamepadAxis::LeftY, pad);
}

Vec2 Input::rightStick(int pad) const noexcept
{
    return stick(GamepadAxis::RightX, GamepadAxis::RightY, pad);
}

float Input::trigger(GamepadAxis which, int pad) const noexcept
{
    if (which != GamepadAxis::LeftTrigger && which != GamepadAxis::RightTrigger) {
        return 0.0f;
    }
    const Pad* p = padFor(pad);
    return p == nullptr ? 0.0f : std::clamp(p->axes[static_cast<std::size_t>(which)], 0.0f, 1.0f);
}

bool Input::wasNudged(GamepadButton direction) const noexcept
{
    if (wasPressed(direction)) {
        return true;
    }
    Vec2 want;
    switch (direction) {
    case GamepadButton::Up: want = {0.0f, 1.0f}; break;
    case GamepadButton::Down: want = {0.0f, -1.0f}; break;
    case GamepadButton::Left: want = {-1.0f, 0.0f}; break;
    case GamepadButton::Right: want = {1.0f, 0.0f}; break;
    default: return false;
    }
    for (const Pad& p : m_pads) {
        if (!p.connected) {
            continue;
        }
        const Vec2 now{p.axes[static_cast<std::size_t>(GamepadAxis::LeftX)],
                       p.axes[static_cast<std::size_t>(GamepadAxis::LeftY)]};
        const float before = p.previousStick.x * want.x + p.previousStick.y * want.y;
        const float after = now.x * want.x + now.y * want.y;
        if (after >= kNudge && before < kNudge) {
            return true;   // just pushed past the line, this frame
        }
    }
    return false;
}

void Input::endFrame() noexcept
{
    for (State& s : m_keys) {
        s.pressed = false;
        s.released = false;
    }
    for (State& s : m_buttons) {
        s.pressed = false;
        s.released = false;
    }
    for (Pad& pad : m_pads) {
        for (State& s : pad.buttons) {
            s.pressed = false;
            s.released = false;
        }
        pad.previousStick = {pad.axes[static_cast<std::size_t>(GamepadAxis::LeftX)],
                             pad.axes[static_cast<std::size_t>(GamepadAxis::LeftY)]};
    }
}

} // namespace rolo
