// Gamepads in a web browser (the WebAssembly build). The browser's GLFW has
// no gamepad layouts, so this reads the browser's own Gamepad API instead
// (through Emscripten's html5.h), which has one "standard" layout: the same
// buttons in the same places on every pad the browser knows.
//
// Browsers only show a gamepad to a page after a button on it was pressed
// while the page was in front. Rumble is not done here yet (Rumble_none).

#include "rolo/runtime/Gamepads.hpp"

#include "Rumble.hpp"

#include <emscripten/html5.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace rolo {

namespace {
constexpr std::string_view kCategory = "input";

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The pad in slot `index`, if the browser has one there with the standard layout.
bool standardPad(int index, EmscriptenGamepadEvent& out, bool* connected = nullptr)
{
    const bool ok = emscripten_get_gamepad_status(index, &out) == EMSCRIPTEN_RESULT_SUCCESS && out.connected;
    if (connected != nullptr) {
        *connected = ok;
    }
    return ok && std::strcmp(out.mapping, "standard") == 0;
}

// The standard layout's buttons, in Rolo's order (GamepadButton).
// https://w3c.github.io/gamepad/#remapping
constexpr int kStandardButton[static_cast<int>(GamepadButton::Count)] = {
    0,  1,  2,  3,    // A B X Y
    4,  5,            // bumpers
    8,  9,  16,       // Back, Start, Guide
    10, 11,           // pressing the sticks in
    12, 15, 13, 14,   // d-pad: up, right, down, left
};

GamepadState stateOf(const EmscriptenGamepadEvent& pad)
{
    GamepadState state;
    state.connected = true;
    for (std::size_t i = 0; i < state.buttons.size(); ++i) {
        const int b = kStandardButton[i];
        state.buttons[i] = b < pad.numButtons && pad.digitalButton[b];
    }
    const auto axis = [&pad](int i) { return i < pad.numAxes ? static_cast<float>(pad.axis[i]) : 0.0f; };
    const auto trigger = [&pad](int i) { return i < pad.numButtons ? static_cast<float>(pad.analogButton[i]) : 0.0f; };
    // Sticks: the browser's y points down; Rolo's (like its world) up.
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = axis(0);
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = -axis(1);
    state.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = axis(2);
    state.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = -axis(3);
    // Triggers are buttons 6 and 7, pressed from 0 to 1.
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = trigger(6);
    state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = trigger(7);
    return state;
}
} // namespace

GamepadState gamepadFromGlfw(const unsigned char (&)[15], const float (&)[6])
{
    return {};   // GLFW's gamepads are not used on the web
}

GamepadReader::GamepadReader(const Logger& log)
    : m_log(log)
    , m_rumble(detail::Rumble::create(log))
    , m_timer(std::make_unique<detail::RumbleTimer>())
{
}

GamepadReader::~GamepadReader()
{
    stopRumble();
}

void GamepadReader::stopRumble()
{
    m_timer->stop(m_joystickOf, *m_rumble);
}

void GamepadReader::updateRumble(Input& input)
{
    m_timer->update(input.takeRumbles(), now(), m_joystickOf, *m_rumble);
}

void GamepadReader::read(Input& input)
{
    // The browser's list of pads is a snapshot, taken here once a frame.
    const bool sampled = emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS;
    const int count = sampled ? std::min(emscripten_get_num_gamepads(), static_cast<int>(m_reportedUnknown.size())) : 0;

    // Pads that were unplugged give up their number.
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        int& slot = m_joystickOf[static_cast<std::size_t>(pad)];
        EmscriptenGamepadEvent e{};
        if (slot >= 0 && (slot >= count || !standardPad(slot, e))) {
            m_log.info(kCategory, "Gamepad " + std::to_string(pad + 1) + " was unplugged.");
            slot = -1;
            m_timer->forget(pad);
            input.setGamepad(pad, GamepadState{});
        }
    }
    // New pads take the first free number.
    for (int slot = 0; slot < count; ++slot) {
        EmscriptenGamepadEvent e{};
        bool connected = false;
        if (!standardPad(slot, e, &connected)) {
            if (connected && !m_reportedUnknown[static_cast<std::size_t>(slot)]) {
                m_reportedUnknown[static_cast<std::size_t>(slot)] = true;
                m_log.info(kCategory, std::string("A controller was found (") + e.id
                                          + "), but the browser does not know its button layout, so it is not used.");
            }
            if (!connected) {
                m_reportedUnknown[static_cast<std::size_t>(slot)] = false;
            }
            continue;
        }
        if (std::find(m_joystickOf.begin(), m_joystickOf.end(), slot) != m_joystickOf.end()) {
            continue;
        }
        const auto free = std::find(m_joystickOf.begin(), m_joystickOf.end(), -1);
        if (free == m_joystickOf.end()) {
            continue;   // four pads already
        }
        *free = slot;
        m_names[static_cast<std::size_t>(free - m_joystickOf.begin())] = e.id;
        m_log.info(kCategory, "Gamepad " + std::to_string(free - m_joystickOf.begin() + 1) + " plugged in: " + e.id
                                  + ".");
    }
    // Read them.
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        const int slot = m_joystickOf[static_cast<std::size_t>(pad)];
        EmscriptenGamepadEvent e{};
        if (slot >= 0 && standardPad(slot, e)) {
            GamepadState state = stateOf(e);
            state.name = m_names[static_cast<std::size_t>(pad)];
            state.style = gamepadStyleFromName(state.name);   // for drawing its buttons as they look
            input.setGamepad(pad, state);
        } else {
            input.setGamepad(pad, GamepadState{});
        }
    }
    updateRumble(input);
}

} // namespace rolo
