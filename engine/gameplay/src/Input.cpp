#include "rolo/gameplay/Input.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

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

// Lower case, without spaces, _ and -: "Left Shift" and "left_shift" are the same.
std::string normalised(std::string_view name)
{
    std::string out;
    for (const char c : name) {
        if (c != ' ' && c != '_' && c != '-') {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return out;
}

constexpr std::string_view kMouseNames[] = {"Left", "Right", "Middle"};

} // namespace

GamepadStyle gamepadStyleFromName(std::string_view name)
{
    const std::string n = normalised(name);
    const auto has = [&](std::string_view part) { return n.find(part) != std::string::npos; };
    if (has("xbox") || has("xinput")) {
        return GamepadStyle::Xbox;
    }
    if (has("playstation") || has("dualshock") || has("dualsense") || has("ps3") || has("ps4") || has("ps5")
        || has("sony")) {
        return GamepadStyle::PlayStation;
    }
    if (has("nintendo") || has("switch") || has("joycon") || has("procontroller")) {
        return GamepadStyle::Nintendo;
    }
    return GamepadStyle::Generic;
}

std::string bindingName(const Binding& b)
{
    switch (b.kind) {
    case Binding::Kind::Key: return std::string(keyName(b.key));
    case Binding::Kind::Mouse: return "Mouse " + std::string(kMouseNames[static_cast<std::size_t>(b.mouse) % 3]);
    case Binding::Kind::Pad: return "Pad " + std::string(gamepadButtonName(b.pad));
    }
    return "Unknown";
}

std::optional<Binding> parseBinding(std::string_view name)
{
    std::string n = normalised(name);
    if (n.rfind("gamepad", 0) == 0) {
        n.erase(0, 4);   // "gamepad" -> "pad"
    }
    if (n.rfind("pad", 0) == 0 && n.size() > 3) {
        for (int b = 0; b < static_cast<int>(GamepadButton::Count); ++b) {
            if (normalised(gamepadButtonName(static_cast<GamepadButton>(b))) == n.substr(3)) {
                return Binding::ofPad(static_cast<GamepadButton>(b));
            }
        }
        return std::nullopt;
    }
    if (n.rfind("mouse", 0) == 0) {
        for (int b = 0; b < 3; ++b) {
            if (normalised(kMouseNames[b]) == n.substr(5)) {
                return Binding::ofMouse(static_cast<MouseButton>(b));
            }
        }
        return std::nullopt;
    }
    if (n == "return") {
        return Binding::ofKey(Key::Enter);
    }
    if (n == "esc") {
        return Binding::ofKey(Key::Escape);
    }
    for (int k = 1; k < static_cast<int>(Key::Count); ++k) {
        if (normalised(keyName(static_cast<Key>(k))) == n) {
            return Binding::ofKey(static_cast<Key>(k));
        }
    }
    return std::nullopt;
}

Input::Input()
{
    // Rolo's own actions, as they always worked: arrows or W A S D, the
    // d-pad, and E (the Interactor's key) or A to interact.
    defineAction(kMoveLeft, {Binding::ofKey(Key::Left), Binding::ofKey(Key::A), Binding::ofPad(GamepadButton::Left)});
    defineAction(kMoveRight, {Binding::ofKey(Key::Right), Binding::ofKey(Key::D), Binding::ofPad(GamepadButton::Right)});
    defineAction(kMoveUp, {Binding::ofKey(Key::Up), Binding::ofKey(Key::W), Binding::ofPad(GamepadButton::Up)});
    defineAction(kMoveDown, {Binding::ofKey(Key::Down), Binding::ofKey(Key::S), Binding::ofPad(GamepadButton::Down)});
    defineAction(kInteract, {Binding::ofKey(Key::E), Binding::ofPad(GamepadButton::A)});
}

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
    // The move actions (arrows, W A S D and the d-pad unless the player
    // remapped them), or else the left stick, which also gives values in between.
    float x = 0.0f;
    float y = 0.0f;
    if (isActionDown(kMoveLeft)) {
        x -= 1.0f;
    }
    if (isActionDown(kMoveRight)) {
        x += 1.0f;
    }
    if (isActionDown(kMoveDown)) {
        y -= 1.0f;
    }
    if (isActionDown(kMoveUp)) {
        y += 1.0f;
    }
    if (x == 0.0f && y == 0.0f) {
        return leftStick();
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
    if (key == Key::Unknown) {
        return;
    }
    m_lastDevice = InputDevice::Keyboard;
    State& s = m_keys[index(key)];
    if (isRebinding() && !s.down) {
        // Remapping: this press chooses the binding (Escape or Backspace
        // gives up) and is used up, so it does not also jump, talk or close a menu.
        if (key != Key::Escape && key != Key::Backspace) {
            rebindTo(Binding::ofKey(key));
        }
        m_rebinding.clear();
        s.down = true;
        return;
    }
    down(s);
}

void Input::release(Key key) noexcept
{
    if (key != Key::Unknown) {
        up(m_keys[index(key)]);
    }
}

void Input::press(MouseButton button) noexcept
{
    if (button == MouseButton::Count) {
        return;
    }
    m_lastDevice = InputDevice::Keyboard;
    State& s = m_buttons[static_cast<std::size_t>(button)];
    if (isRebinding() && !s.down) {
        rebindTo(Binding::ofMouse(button));
        m_rebinding.clear();
        s.down = true;
        return;
    }
    down(s);
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
    p.style = state.style;
    for (std::size_t i = 0; i < p.buttons.size(); ++i) {
        // A gamepad unplugged with a button held lets go of it.
        const bool held = state.connected && state.buttons[i];
        if (held && !p.buttons[i].down) {
            m_lastDevice = InputDevice::Gamepad;
            m_lastPad = pad;
            if (isRebinding()) {
                rebindTo(Binding::ofPad(static_cast<GamepadButton>(i)));   // used up, as with keys
                m_rebinding.clear();
                p.buttons[i].down = true;
                continue;
            }
        }
        held ? down(p.buttons[i]) : up(p.buttons[i]);
    }
    if (state.connected) {
        const Vec2 stickNow{state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)],
                            state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)]};
        if (length(stickNow) > 0.5f) {
            m_lastDevice = InputDevice::Gamepad;
            m_lastPad = pad;
        }
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

GamepadStyle Input::gamepadStyle() const noexcept
{
    if (m_lastPad >= 0 && m_pads[static_cast<std::size_t>(m_lastPad)].connected) {
        return m_pads[static_cast<std::size_t>(m_lastPad)].style;
    }
    const Pad* p = padFor(-1);
    return p != nullptr ? p->style : GamepadStyle::Generic;
}

// --- Rumble -----------------------------------------------------------------------------------------------

void Input::rumble(float strength, float seconds, int pad)
{
    rumble(strength, strength, seconds, pad);
}

void Input::rumble(float low, float high, float seconds, int pad)
{
    const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    const float time = std::isfinite(seconds) ? std::clamp(seconds, 0.0f, 10.0f) : 0.0f;
    m_rumbles.push_back({pad >= 0 && pad < kMaxGamepads ? pad : -1, unit(low), unit(high), time});
}

std::vector<RumbleRequest> Input::takeRumbles()
{
    std::vector<RumbleRequest> out;
    out.swap(m_rumbles);
    return out;
}

// --- Actions --------------------------------------------------------------------------------------------

const Input::Action* Input::findAction(std::string_view name) const
{
    const auto found = m_actions.find(name);
    return found != m_actions.end() ? &found->second : nullptr;
}

void Input::defineAction(std::string_view name, std::vector<Binding> defaults)
{
    if (name.empty()) {
        return;
    }
    auto found = m_actions.find(name);
    if (found == m_actions.end()) {
        found = m_actions.emplace(std::string(name), Action{}).first;
        found->second.order = static_cast<int>(m_actions.size());
        // Bindings the player chose before the game defined the action.
        if (const auto loaded = m_loaded.find(name); loaded != m_loaded.end()) {
            found->second.chosen = loaded->second;
        }
    }
    found->second.defaults = std::move(defaults);
}

bool Input::hasAction(std::string_view name) const
{
    return findAction(name) != nullptr;
}

std::vector<std::string> Input::actions() const
{
    std::vector<std::pair<int, std::string>> ordered;
    for (const auto& [name, action] : m_actions) {
        ordered.emplace_back(action.order, name);
    }
    std::sort(ordered.begin(), ordered.end());
    std::vector<std::string> out;
    for (auto& [order, name] : ordered) {
        out.push_back(std::move(name));
    }
    return out;
}

std::vector<Binding> Input::bindings(std::string_view name) const
{
    const Action* a = findAction(name);
    if (a == nullptr) {
        return {};
    }
    return a->chosen ? *a->chosen : a->defaults;
}

std::optional<Binding> Input::bindingFor(std::string_view name, InputDevice device) const
{
    const std::vector<Binding> all = bindings(name);
    for (const Binding& b : all) {
        if (b.isGamepad() == (device == InputDevice::Gamepad)) {
            return b;
        }
    }
    return all.empty() ? std::nullopt : std::optional<Binding>(all.front());
}

bool Input::bindingIs(const Binding& b, bool (Input::*keyQ)(Key) const noexcept,
                      bool (Input::*mouseQ)(MouseButton) const noexcept,
                      bool (Input::*padQ)(GamepadButton, int) const noexcept) const
{
    switch (b.kind) {
    case Binding::Kind::Key: return (this->*keyQ)(b.key);
    case Binding::Kind::Mouse: return (this->*mouseQ)(b.mouse);
    case Binding::Kind::Pad: return (this->*padQ)(b.pad, -1);
    }
    return false;
}

bool Input::isActionDown(std::string_view name) const
{
    for (const Binding& b : bindings(name)) {
        if (bindingIs(b, &Input::isDown, &Input::isDown, &Input::isDown)) {
            return true;
        }
    }
    return false;
}

bool Input::wasActionPressed(std::string_view name) const
{
    for (const Binding& b : bindings(name)) {
        if (bindingIs(b, &Input::wasPressed, &Input::wasPressed, &Input::wasPressed)) {
            return true;
        }
    }
    return false;
}

bool Input::wasActionReleased(std::string_view name) const
{
    for (const Binding& b : bindings(name)) {
        if (bindingIs(b, &Input::wasReleased, &Input::wasReleased, &Input::wasReleased)) {
            return true;
        }
    }
    return false;
}

void Input::setBindings(std::string_view name, std::vector<Binding> list)
{
    auto found = m_actions.find(name);
    if (found == m_actions.end()) {
        return;
    }
    found->second.chosen = std::move(list);
    ++m_bindingsRevision;
}

void Input::resetBindings(std::string_view name)
{
    for (auto& [actionName, action] : m_actions) {
        if (name.empty() || actionName == name) {
            action.chosen.reset();
        }
    }
    if (name.empty()) {
        m_loaded.clear();
    } else if (const auto loaded = m_loaded.find(name); loaded != m_loaded.end()) {
        m_loaded.erase(loaded);
    }
    ++m_bindingsRevision;
}

void Input::startRebind(std::string_view name)
{
    if (hasAction(name)) {
        m_rebinding = std::string(name);
    }
}

void Input::rebindTo(const Binding& binding)
{
    // The new binding replaces the ones for the same kind of device; the
    // other kind stays (remapping the keyboard keeps the gamepad's button).
    std::vector<Binding> list;
    for (const Binding& b : bindings(m_rebinding)) {
        if (b.isGamepad() != binding.isGamepad()) {
            list.push_back(b);
        }
    }
    list.insert(binding.isGamepad() ? list.end() : list.begin(), binding);
    setBindings(m_rebinding, std::move(list));
}

std::string Input::saveBindings() const
{
    std::string out;
    for (const std::string& name : actions()) {
        const Action& a = *findAction(name);
        if (!a.chosen || *a.chosen == a.defaults) {
            continue;
        }
        out += name + "=";
        for (std::size_t i = 0; i < a.chosen->size(); ++i) {
            out += (i == 0 ? "" : ",") + bindingName((*a.chosen)[i]);
        }
        out += "\n";
    }
    // Chosen bindings of actions this game run has not defined (yet) are kept too.
    for (const auto& [name, list] : m_loaded) {
        if (hasAction(name)) {
            continue;
        }
        out += name + "=";
        for (std::size_t i = 0; i < list.size(); ++i) {
            out += (i == 0 ? "" : ",") + bindingName(list[i]);
        }
        out += "\n";
    }
    return out;
}

void Input::loadBindings(std::string_view text)
{
    std::istringstream lines{std::string(text)};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            continue;
        }
        const std::string name = line.substr(0, equals);
        std::vector<Binding> list;
        std::istringstream items(line.substr(equals + 1));
        std::string item;
        while (std::getline(items, item, ',')) {
            if (const auto b = parseBinding(item)) {
                list.push_back(*b);
            }
        }
        m_loaded[name] = list;
        if (auto found = m_actions.find(name); found != m_actions.end()) {
            found->second.chosen = list;
        }
    }
    ++m_bindingsRevision;
}

} // namespace rolo
