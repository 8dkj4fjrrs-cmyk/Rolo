#include "rolo/runtime/Gamepads.hpp"

#include "Rumble.hpp"

#include <platform/RenderViewImpl.h>   // brings in GLFW the way Axmol expects

#include <algorithm>
#include <chrono>

namespace rolo {

namespace {
constexpr std::string_view kCategory = "input";
// GLFW numbers joysticks 0 to 15.
constexpr int kJoysticks = GLFW_JOYSTICK_LAST + 1;
} // namespace

GamepadState gamepadFromGlfw(const unsigned char (&buttons)[15], const float (&axes)[6])
{
    // GLFW's button and axis order is Rolo's (both follow the Xbox layout).
    static_assert(static_cast<int>(GamepadButton::Count) == GLFW_GAMEPAD_BUTTON_LAST + 1);
    static_assert(static_cast<int>(GamepadAxis::Count) == GLFW_GAMEPAD_AXIS_LAST + 1);
    GamepadState state;
    state.connected = true;
    for (std::size_t i = 0; i < state.buttons.size(); ++i) {
        state.buttons[i] = buttons[i] == GLFW_PRESS;
    }
    // Sticks: GLFW's y points down; Rolo's (like its world) up.
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftY)] = -axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    state.axes[static_cast<std::size_t>(GamepadAxis::RightX)] = axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
    state.axes[static_cast<std::size_t>(GamepadAxis::RightY)] = -axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
    // Triggers: GLFW goes from -1 (released) to 1; Rolo from 0 to 1.
    state.axes[static_cast<std::size_t>(GamepadAxis::LeftTrigger)] = (axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f;
    state.axes[static_cast<std::size_t>(GamepadAxis::RightTrigger)] = (axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f;
    return state;
}

namespace {
double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

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
    // Pads that were unplugged give up their number.
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        int& jid = m_joystickOf[static_cast<std::size_t>(pad)];
        if (jid >= 0 && glfwJoystickIsGamepad(jid) != GLFW_TRUE) {
            m_log.info(kCategory, "Gamepad " + std::to_string(pad + 1) + " was unplugged.");
            jid = -1;
            m_timer->forget(pad);
            input.setGamepad(pad, GamepadState{});
        }
    }
    // New pads take the first free number.
    for (int jid = 0; jid < kJoysticks; ++jid) {
        if (glfwJoystickPresent(jid) != GLFW_TRUE) {
            m_reportedUnknown[static_cast<std::size_t>(jid)] = false;
            continue;
        }
        if (glfwJoystickIsGamepad(jid) != GLFW_TRUE) {
            // A controller GLFW has no layout for (or not a gamepad: a wheel, a joystick).
            if (!m_reportedUnknown[static_cast<std::size_t>(jid)]) {
                m_reportedUnknown[static_cast<std::size_t>(jid)] = true;
                const char* name = glfwGetJoystickName(jid);
                m_log.info(kCategory, std::string("A controller was found (") + (name != nullptr ? name : "no name")
                                          + "), but Rolo does not know its button layout, so it is not used.");
            }
            continue;
        }
        if (std::find(m_joystickOf.begin(), m_joystickOf.end(), jid) != m_joystickOf.end()) {
            continue;
        }
        const auto free = std::find(m_joystickOf.begin(), m_joystickOf.end(), -1);
        if (free == m_joystickOf.end()) {
            continue;   // four pads already
        }
        *free = jid;
        const char* name = glfwGetGamepadName(jid);
        m_names[static_cast<std::size_t>(free - m_joystickOf.begin())] = name != nullptr ? name : "";
        m_log.info(kCategory, "Gamepad " + std::to_string(free - m_joystickOf.begin() + 1) + " plugged in: "
                                  + (name != nullptr ? name : "a gamepad") + ".");
    }
    // Read them.
    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        const int jid = m_joystickOf[static_cast<std::size_t>(pad)];
        GLFWgamepadstate raw{};
        if (jid >= 0 && glfwGetGamepadState(jid, &raw) == GLFW_TRUE) {
            GamepadState state = gamepadFromGlfw(raw.buttons, raw.axes);
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
