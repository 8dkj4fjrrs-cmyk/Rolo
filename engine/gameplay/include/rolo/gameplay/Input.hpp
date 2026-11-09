#pragma once

// Input: the keyboard, mouse and gamepads as gameplay code sees them.
//
// Gameplay asks questions about the current frame:
//
//     if (game.input().isDown(Key::Right)) { ... }        // held down now
//     if (game.input().wasPressed(Key::Space)) { jump(); } // went down this frame
//     Vec2 direction = game.input().moveDirection();      // arrows or WASD
//
// The program running the game (the player, or the editor's play mode)
// reports key and button changes with press()/release() as they happen, and
// calls endFrame() after each game update, which clears the "this frame"
// flags. Rolo's Key names do not depend on the window library, so game
// code never sees GLFW or Axmol key codes.
//
// Gamepads (up to four) work the same way:
//
//     if (game.input().wasPressed(GamepadButton::A)) { jump(); }   // any gamepad
//     Vec2 stick = game.input().leftStick();                      // y up, at most 1 long
//
// Buttons are named by position, as on an Xbox pad: A is the bottom button
// (Cross on PlayStation, B on Nintendo), B the right one, X the left, Y the
// top. The program reads the pads once a frame and reports them with
// setGamepad(). moveDirection() includes the left stick and the d-pad, and
// the dialogue box and triggers accept the A button, so a game made without
// code is playable with a gamepad too.

#include "rolo/scene/Types.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace rolo {

enum class Key {
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Space, Enter, Escape, Tab, Backspace,
    Left, Right, Up, Down,
    LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count   // not a key: the number of keys
};

/// The key's name, e.g. "Space", "A", "Left".
std::string_view keyName(Key key);

enum class MouseButton { Left, Right, Middle, Count };

/// Gamepad buttons, by position (see above).
enum class GamepadButton {
    A, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftStick, RightStick,   // pressing a stick in
    Up, Right, Down, Left,   // the d-pad
    Count                    // not a button: the number of buttons
};

/// "A", "LB", "Start", "Up"...
std::string_view gamepadButtonName(GamepadButton button);

/// Gamepad axes. Sticks go from -1 to 1 (y up); triggers from 0 to 1.
enum class GamepadAxis { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

/// One gamepad as the program reads it. Values are raw (no dead zone).
struct GamepadState {
    bool connected = false;
    std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
    std::string_view name;   // e.g. "Xbox Controller" (only for messages)
};

class Input {
public:
    // --- Asked by gameplay ---------------------------------------------------------------
    /// The key is held down.
    bool isDown(Key key) const noexcept { return state(key).down; }
    /// The key went down since the last frame.
    bool wasPressed(Key key) const noexcept { return state(key).pressed; }
    /// The key went up since the last frame.
    bool wasReleased(Key key) const noexcept { return state(key).released; }

    bool isDown(MouseButton button) const noexcept { return state(button).down; }
    bool wasPressed(MouseButton button) const noexcept { return state(button).pressed; }
    bool wasReleased(MouseButton button) const noexcept { return state(button).released; }
    /// Where the mouse pointer is, in world coordinates (as last reported).
    Vec2 pointer() const noexcept { return m_pointer; }

    /// The direction the player is steering in: the arrow keys or W A S D
    /// (each axis -1, 0 or 1), or else a gamepad's d-pad or left stick (the
    /// stick gives values in between, for walking slowly). Never longer than
    /// 1: moving diagonally is not faster.
    Vec2 moveDirection() const noexcept;

    // --- Gamepads ---------------------------------------------------------------------------
    static constexpr int kMaxGamepads = 4;
    /// Without `pad`: any gamepad (the usual case: one player). With `pad`
    /// (0 to 3): that one, for games with several players.
    bool isDown(GamepadButton button, int pad = -1) const noexcept;
    bool wasPressed(GamepadButton button, int pad = -1) const noexcept;
    bool wasReleased(GamepadButton button, int pad = -1) const noexcept;
    /// A stick, with a small dead zone in the middle (worn sticks never rest
    /// at exactly 0), at most 1 long, y up. Without `pad`: the first
    /// connected gamepad.
    Vec2 leftStick(int pad = -1) const noexcept;
    Vec2 rightStick(int pad = -1) const noexcept;
    /// A trigger, 0 (released) to 1 (pressed fully).
    float trigger(GamepadAxis trigger, int pad = -1) const noexcept;
    bool isGamepadConnected(int pad = -1) const noexcept;
    /// For menus: the d-pad or the left stick was just pushed this way
    /// (`direction` is Up, Down, Left or Right).
    bool wasNudged(GamepadButton direction) const noexcept;

    // --- Reported by the program running the game -------------------------------------------
    void press(Key key) noexcept;
    void release(Key key) noexcept;
    void press(MouseButton button) noexcept;
    void release(MouseButton button) noexcept;
    void setPointer(Vec2 worldPosition) noexcept { m_pointer = worldPosition; }
    /// A gamepad's state this frame (`pad` 0 to 3). Buttons that changed
    /// since the last report count as pressed or released this frame.
    void setGamepad(int pad, const GamepadState& state) noexcept;
    /// Lets go of everything, e.g. when the window or game view loses focus
    /// (otherwise a key released elsewhere would stay "down").
    void releaseAll() noexcept;
    /// Clears the "pressed/released this frame" flags. Call after each update.
    void endFrame() noexcept;

private:
    struct State {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };
    const State& state(Key key) const noexcept { return m_keys[index(key)]; }
    const State& state(MouseButton b) const noexcept { return m_buttons[static_cast<std::size_t>(b)]; }
    static std::size_t index(Key key) noexcept
    {
        const auto i = static_cast<std::size_t>(key);
        return i < static_cast<std::size_t>(Key::Count) ? i : 0;
    }
    static void down(State& s) noexcept;
    static void up(State& s) noexcept;

    std::array<State, static_cast<std::size_t>(Key::Count)> m_keys{};
    std::array<State, static_cast<std::size_t>(MouseButton::Count)> m_buttons{};
    Vec2 m_pointer;

    struct Pad {
        bool connected = false;
        std::array<State, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
        std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
        Vec2 previousStick;   // the left stick at the last endFrame (for wasNudged)
    };
    // The pad to read: `pad` if valid, else the first connected one (null if none).
    const Pad* padFor(int pad) const noexcept;
    Vec2 stick(GamepadAxis x, GamepadAxis y, int pad) const noexcept;
    std::array<Pad, kMaxGamepads> m_pads{};
};

} // namespace rolo
