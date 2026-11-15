// Gamepads on Android (ADR 0028, step 4). There is no GLFW here; Android
// reports controllers to the app's Java side as events, which
// RoloGamepads.java (player/android) keeps as "what each gamepad is doing
// now". This reads that once a frame.
//
// Android has one layout for the controllers it knows (as browsers do), so
// A is the bottom face button on all of them without a table of models.

#include "rolo/runtime/Gamepads.hpp"

#include "AndroidGamepadsJava.hpp"
#include "Rumble.hpp"

#include <algorithm>
#include <chrono>

namespace rolo {

namespace {
constexpr std::string_view kCategory = "input";

// As in RoloGamepads.java: for each of four pads, "connected", 15 buttons
// and 6 axes, already in Rolo's order and directions.
constexpr int kButtons = static_cast<int>(GamepadButton::Count);
constexpr int kAxes = static_cast<int>(GamepadAxis::Count);
constexpr int kValues = 1 + kButtons + kAxes;

double now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string nameOf(JNIEnv* env, const detail::GamepadsJava& java, int pad)
{
    std::string name;
    const auto text = static_cast<jstring>(env->CallStaticObjectMethod(java.type, java.name, static_cast<jint>(pad)));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
    if (text != nullptr) {
        if (const char* chars = env->GetStringUTFChars(text, nullptr)) {
            name = chars;
            env->ReleaseStringUTFChars(text, chars);
        }
        env->DeleteLocalRef(text);
    }
    return name.empty() ? std::string("Gamepad") : name;
}
} // namespace

GamepadState gamepadFromGlfw(const unsigned char (&)[15], const float (&)[6])
{
    return {};   // GLFW's gamepads are not used on Android
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
    const detail::GamepadsJava& java = detail::gamepadsJava();
    JNIEnv* env = ax::JniHelper::getEnv();
    if (!java.found() || env == nullptr) {
        updateRumble(input);
        return;
    }
    // One call into Java for everything: it fills a list of numbers.
    float values[Input::kMaxGamepads * kValues] = {};
    const jsize count = static_cast<jsize>(std::size(values));
    if (jfloatArray list = env->NewFloatArray(count)) {
        env->CallStaticVoidMethod(java.type, java.read, list);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else {
            env->GetFloatArrayRegion(list, 0, count, values);
        }
        env->DeleteLocalRef(list);
    }

    for (int pad = 0; pad < Input::kMaxGamepads; ++pad) {
        const float* at = values + pad * kValues;
        const bool connected = at[0] > 0.5f;
        int& slot = m_joystickOf[static_cast<std::size_t>(pad)];   // here: the pad's own number, or -1
        if (!connected) {
            if (slot >= 0) {
                m_log.info(kCategory, "Gamepad " + std::to_string(pad + 1) + " was unplugged.");
                slot = -1;
                m_timer->forget(pad);
            }
            input.setGamepad(pad, GamepadState{});
            continue;
        }
        if (slot < 0) {
            slot = pad;
            m_names[static_cast<std::size_t>(pad)] = nameOf(env, java, pad);
            m_log.info(kCategory, "Gamepad " + std::to_string(pad + 1) + " plugged in: "
                                      + m_names[static_cast<std::size_t>(pad)] + ".");
        }
        GamepadState state;
        state.connected = true;
        for (int b = 0; b < kButtons; ++b) {
            state.buttons[static_cast<std::size_t>(b)] = at[1 + b] > 0.5f;
        }
        for (int a = 0; a < kAxes; ++a) {
            state.axes[static_cast<std::size_t>(a)] = std::clamp(at[1 + kButtons + a], -1.0f, 1.0f);
        }
        state.name = m_names[static_cast<std::size_t>(pad)];
        state.style = gamepadStyleFromName(state.name);   // for drawing its buttons as they look
        input.setGamepad(pad, state);
    }
    updateRumble(input);
}

} // namespace rolo
