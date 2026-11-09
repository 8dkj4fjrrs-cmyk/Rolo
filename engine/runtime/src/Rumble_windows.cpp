// Rumble on Windows: XInput (part of Windows), loaded when first needed.
//
// GLFW reads XInput controllers too and names them with a GUID starting
// "78696e707574" ("xinput" in hexadecimal), but does not say which of
// XInput's four slots each one is. Both number them in the order they were
// found, so the n-th such joystick is taken to be the n-th connected slot.

#ifndef NOMINMAX
#    define NOMINMAX   // windows.h must not turn std::min and std::max into macros
#endif
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif

#include "Rumble.hpp"

#include <platform/RenderViewImpl.h>   // GLFW, the way Axmol includes it

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace rolo::detail {

namespace {

using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

class XInputRumble final : public Rumble {
public:
    explicit XInputRumble(const Logger& log)
        : m_log(log)
    {
        for (const wchar_t* name : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
            m_library = LoadLibraryW(name);
            if (m_library != nullptr) {
                break;
            }
        }
        if (m_library != nullptr) {
            m_setState = reinterpret_cast<SetStateFn>(reinterpret_cast<void*>(GetProcAddress(m_library, "XInputSetState")));
            m_getState = reinterpret_cast<GetStateFn>(reinterpret_cast<void*>(GetProcAddress(m_library, "XInputGetState")));
        }
        if (m_setState == nullptr || m_getState == nullptr) {
            m_log.info("input", "XInput is not available; gamepads will not rumble.");
        }
    }
    ~XInputRumble() override
    {
        if (m_library != nullptr) {
            FreeLibrary(m_library);
        }
    }

    bool set(int jid, float low, float high) override
    {
        if (m_setState == nullptr) {
            return false;
        }
        const int slot = slotOf(jid);
        if (slot < 0) {
            return false;   // not an XInput controller
        }
        XINPUT_VIBRATION v{};
        v.wLeftMotorSpeed = static_cast<WORD>(std::clamp(low, 0.0f, 1.0f) * 65535.0f);    // the heavy motor
        v.wRightMotorSpeed = static_cast<WORD>(std::clamp(high, 0.0f, 1.0f) * 65535.0f);  // the light one
        return m_setState(static_cast<DWORD>(slot), &v) == ERROR_SUCCESS;
    }

private:
    int slotOf(int jid) const
    {
        const auto isXInput = [](int j) {
            const char* guid = glfwJoystickPresent(j) == GLFW_TRUE ? glfwGetJoystickGUID(j) : nullptr;
            return guid != nullptr && std::strncmp(guid, "78696e707574", 12) == 0;
        };
        if (!isXInput(jid)) {
            return -1;
        }
        int position = 0;   // among GLFW's XInput joysticks
        for (int j = 0; j < jid; ++j) {
            position += isXInput(j) ? 1 : 0;
        }
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
            XINPUT_STATE state{};
            if (m_getState(slot, &state) == ERROR_SUCCESS && position-- == 0) {
                return static_cast<int>(slot);
            }
        }
        return -1;
    }

    const Logger& m_log;
    HMODULE m_library = nullptr;
    SetStateFn m_setState = nullptr;
    GetStateFn m_getState = nullptr;
};

} // namespace

std::unique_ptr<Rumble> Rumble::create(const Logger& log)
{
    return std::make_unique<XInputRumble>(log);
}

} // namespace rolo::detail
