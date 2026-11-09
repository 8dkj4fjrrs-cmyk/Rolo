// Rumble on Linux: the kernel's force feedback (evdev).
//
// GLFW reads gamepads from /dev/input/event* and names each with a GUID made
// from the device's bus, vendor, product and version numbers. The same
// device is found here by building that GUID for every event device and
// matching it (the n-th of several identical pads matching the n-th). It is
// opened for writing too, a "rumble" effect is uploaded, and playing or
// stopping it shakes the pad. Pads whose driver has no rumble are skipped.

#include "Rumble.hpp"

#include <platform/RenderViewImpl.h>   // GLFW, the way Axmol includes it

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rolo::detail {

namespace {

struct Device {
    int fd = -1;
    int effect = -1;   // the uploaded effect's id
};

// The GUID GLFW gives an evdev device (linux_joystick.c, for devices that
// report their numbers; others are not matched).
std::string guidOf(const input_id& id)
{
    char guid[33] = "";
    std::snprintf(guid, sizeof guid, "%02x%02x0000%02x%02x0000%02x%02x0000%02x%02x0000", id.bustype & 0xff,
                  id.bustype >> 8, id.vendor & 0xff, id.vendor >> 8, id.product & 0xff, id.product >> 8,
                  id.version & 0xff, id.version >> 8);
    return guid;
}

bool canRumble(int fd)
{
    unsigned long bits[(FF_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof bits), bits) < 0) {
        return false;
    }
    const std::size_t perLong = 8 * sizeof(unsigned long);
    return (bits[FF_RUMBLE / perLong] >> (FF_RUMBLE % perLong)) & 1UL;
}

class EvdevRumble final : public Rumble {
public:
    explicit EvdevRumble(const Logger& log)
        : m_log(log)
    {
    }
    ~EvdevRumble() override
    {
        for (auto& [jid, device] : m_devices) {
            close(device.fd);
        }
    }

    bool set(int jid, float low, float high) override
    {
        Device* device = open(jid);
        if (device == nullptr) {
            return false;
        }
        ff_effect effect{};
        effect.type = FF_RUMBLE;
        effect.id = static_cast<short>(device->effect);
        effect.u.rumble.strong_magnitude = static_cast<unsigned short>(std::clamp(low, 0.0f, 1.0f) * 0xffff);
        effect.u.rumble.weak_magnitude = static_cast<unsigned short>(std::clamp(high, 0.0f, 1.0f) * 0xffff);
        effect.replay.length = 10000;   // milliseconds: Rolo stops it itself, sooner
        if (ioctl(device->fd, EVIOCSFF, &effect) < 0) {
            return false;
        }
        device->effect = effect.id;
        input_event play{};
        play.type = EV_FF;
        play.code = static_cast<unsigned short>(effect.id);
        play.value = (low > 0.0f || high > 0.0f) ? 1 : 0;
        return write(device->fd, &play, sizeof play) == static_cast<ssize_t>(sizeof play);
    }

private:
    Device* open(int jid)
    {
        if (const auto found = m_devices.find(jid); found != m_devices.end()) {
            return &found->second;
        }
        if (m_tried[jid]) {
            return nullptr;   // looked before and found no way to rumble it
        }
        m_tried[jid] = true;
        const char* wanted = glfwJoystickPresent(jid) == GLFW_TRUE ? glfwGetJoystickGUID(jid) : nullptr;
        if (wanted == nullptr) {
            return nullptr;
        }
        // Which of several identical pads: count GLFW joysticks before it with the same GUID.
        int position = 0;
        for (int j = 0; j < jid; ++j) {
            const char* guid = glfwJoystickPresent(j) == GLFW_TRUE ? glfwGetJoystickGUID(j) : nullptr;
            position += guid != nullptr && std::strcmp(guid, wanted) == 0 ? 1 : 0;
        }
        std::vector<std::string> paths;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator("/dev/input", ec)) {
            if (entry.path().filename().string().rfind("event", 0) == 0) {
                paths.push_back(entry.path().string());
            }
        }
        // Numeric order (event2 before event10), as the kernel numbers them.
        std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
        for (const std::string& path : paths) {
            const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
            if (fd < 0) {
                continue;
            }
            input_id id{};
            if (ioctl(fd, EVIOCGID, &id) < 0 || guidOf(id) != wanted || position-- != 0) {
                close(fd);
                continue;
            }
            if (!canRumble(fd)) {
                close(fd);
                return nullptr;
            }
            return &m_devices.emplace(jid, Device{fd, -1}).first->second;
        }
        m_log.debug("input", "Gamepad " + std::to_string(jid) + ": no rumble (its device could not be opened for writing).");
        return nullptr;
    }

    const Logger& m_log;
    std::map<int, Device> m_devices;
    std::map<int, bool> m_tried;
};

} // namespace

std::unique_ptr<Rumble> Rumble::create(const Logger& log)
{
    return std::make_unique<EvdevRumble>(log);
}

} // namespace rolo::detail
