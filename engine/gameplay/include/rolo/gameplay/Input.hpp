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
//
// Actions: games name what the player does ("jump", "interact") and bind
// keys and buttons to it; the player can change those bindings (remapping):
//
//     input.defineAction("jump", {Binding::key(Key::Space), Binding::pad(GamepadButton::A)});
//     if (input.wasActionPressed("jump")) { ... }
//     input.startRebind("jump");   // the next key or button pressed takes over
//
// Rolo's own controls are actions too, so they can be remapped: "move left",
// "move right", "move up", "move down" (moveDirection) and "interact"
// (talking, going on in dialogue). Bindings the player chose are kept with
// their save games (saveBindings/loadBindings).

#include "rolo/scene/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

/// Which family a gamepad belongs to, for showing its buttons as they look.
enum class GamepadStyle { Generic, Xbox, PlayStation, Nintendo };

/// A gamepad's style from its name (as GLFW reports it), e.g. "PS5 Controller".
GamepadStyle gamepadStyleFromName(std::string_view name);

/// One gamepad as the program reads it. Values are raw (no dead zone).
struct GamepadState {
    bool connected = false;
    std::array<bool, static_cast<std::size_t>(GamepadButton::Count)> buttons{};
    std::array<float, static_cast<std::size_t>(GamepadAxis::Count)> axes{};
    std::string_view name;   // e.g. "Xbox Controller" (only for messages)
    GamepadStyle style = GamepadStyle::Generic;
};

/// What an action can be bound to: a key, a mouse button or a gamepad button.
struct Binding {
    enum class Kind { Key, Mouse, Pad };
    Kind kind = Kind::Key;
    Key key = Key::Unknown;
    MouseButton mouse = MouseButton::Left;
    GamepadButton pad = GamepadButton::A;

    static Binding ofKey(Key k) { return {Kind::Key, k, MouseButton::Left, GamepadButton::A}; }
    static Binding ofMouse(MouseButton b) { return {Kind::Mouse, Key::Unknown, b, GamepadButton::A}; }
    static Binding ofPad(GamepadButton b) { return {Kind::Pad, Key::Unknown, MouseButton::Left, b}; }
    bool isGamepad() const noexcept { return kind == Kind::Pad; }
    friend bool operator==(const Binding&, const Binding&) = default;
};

/// "Space", "Mouse Left", "Pad A".
std::string bindingName(const Binding& binding);
/// Reads a name like the above (any case; spaces optional): "space",
/// "left shift", "mouse left", "pad a", "gamepad start". Nothing if unknown.
std::optional<Binding> parseBinding(std::string_view name);

/// A rumble asked for by the game, for the program to send to the gamepad.
struct RumbleRequest {
    int pad = -1;             // -1: every gamepad
    float low = 0.0f;         // the heavy, slow motor, 0 to 1
    float high = 0.0f;        // the light, fast motor, 0 to 1
    float seconds = 0.0f;     // 0: stop
};

/// The device the player used last, for prompts.
enum class InputDevice { Keyboard, Gamepad };

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
    /// What the player used last: the keyboard (or mouse), or a gamepad.
    InputDevice lastDevice() const noexcept { return m_lastDevice; }
    /// The style of the gamepad used last (or the first one plugged in).
    GamepadStyle gamepadStyle() const noexcept;

    // --- Rumble -----------------------------------------------------------------------------
    /// Shakes the gamepad: `strength` 0 to 1 for `seconds` (0 stops it).
    /// Without `pad`, every gamepad. Not every gamepad can rumble.
    void rumble(float strength, float seconds, int pad = -1);
    /// The same with each motor set: `low` the heavy one, `high` the light one.
    void rumble(float low, float high, float seconds, int pad);
    /// The rumbles asked for since the last call (the program sends them).
    std::vector<RumbleRequest> takeRumbles();

    // --- Actions and remapping ---------------------------------------------------------------
    /// Names an action and its default bindings. If the player changed the
    /// action's bindings, theirs are kept (defining again is harmless).
    void defineAction(std::string_view name, std::vector<Binding> defaults);
    bool hasAction(std::string_view name) const;
    /// The actions, in the order they were defined.
    std::vector<std::string> actions() const;
    /// Any of the action's bindings is down / was pressed / was released.
    /// False for an action that does not exist.
    bool isActionDown(std::string_view name) const;
    bool wasActionPressed(std::string_view name) const;
    bool wasActionReleased(std::string_view name) const;
    /// The bindings now (the player's, or else the defaults).
    std::vector<Binding> bindings(std::string_view name) const;
    /// The binding to show for an action on `device` (e.g. in "{button:jump}"):
    /// its first binding of that kind, else its first. Nothing if none.
    std::optional<Binding> bindingFor(std::string_view name, InputDevice device) const;
    void setBindings(std::string_view name, std::vector<Binding> bindings);
    /// Back to the defaults: one action, or every action (empty name).
    void resetBindings(std::string_view name = {});

    /// Remapping: the next key, mouse button or gamepad button pressed
    /// becomes the action's binding for that kind of device (a key replaces
    /// its keyboard and mouse bindings, a gamepad button its gamepad ones).
    /// Escape or Backspace cancels (in Rolo's player, Escape also quits the
    /// game: menus should offer Backspace). The press is used up: it does not
    /// also count as pressed.
    void startRebind(std::string_view name);
    void cancelRebind() noexcept { m_rebinding.clear(); }
    bool isRebinding() const noexcept { return !m_rebinding.empty(); }
    const std::string& rebindingAction() const noexcept { return m_rebinding; }

    /// Goes up whenever the player's bindings change (to know when to save them).
    std::uint64_t bindingsRevision() const noexcept { return m_bindingsRevision; }
    /// The player's changed bindings as text, one action per line:
    /// "jump=Space,Pad A". Actions left at their defaults are not written.
    std::string saveBindings() const;
    /// Reads what saveBindings wrote; unknown names are skipped.
    void loadBindings(std::string_view text);

    /// Rolo's own actions (moveDirection, dialogue, triggers).
    static constexpr std::string_view kMoveLeft = "move left";
    static constexpr std::string_view kMoveRight = "move right";
    static constexpr std::string_view kMoveUp = "move up";
    static constexpr std::string_view kMoveDown = "move down";
    static constexpr std::string_view kInteract = "interact";

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
        GamepadStyle style = GamepadStyle::Generic;
    };
    // The pad to read: `pad` if valid, else the first connected one (null if none).
    const Pad* padFor(int pad) const noexcept;
    Vec2 stick(GamepadAxis x, GamepadAxis y, int pad) const noexcept;
    std::array<Pad, kMaxGamepads> m_pads{};

    struct Action {
        std::vector<Binding> defaults;
        std::optional<std::vector<Binding>> chosen;   // the player's, if changed
        int order = 0;
    };
    const Action* findAction(std::string_view name) const;
    bool bindingIs(const Binding& b, bool (Input::*keyQ)(Key) const noexcept,
                   bool (Input::*mouseQ)(MouseButton) const noexcept,
                   bool (Input::*padQ)(GamepadButton, int) const noexcept) const;
    void rebindTo(const Binding& binding);
    std::map<std::string, Action, std::less<>> m_actions;
    std::map<std::string, std::vector<Binding>, std::less<>> m_loaded;   // chosen bindings of actions not defined yet
    std::string m_rebinding;
    std::uint64_t m_bindingsRevision = 0;
    InputDevice m_lastDevice = InputDevice::Keyboard;
    int m_lastPad = -1;
    std::vector<RumbleRequest> m_rumbles;

public:
    Input();
};

} // namespace rolo
