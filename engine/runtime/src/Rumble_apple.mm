// Rumble on macOS: the Game Controller framework's haptics (macOS 11+).
//
// The framework sees the same pads GLFW does, but numbers them its own way.
// Both list connected pads in the order they were connected, so the n-th GLFW
// gamepad is taken to be the n-th controller here. Each pad's haptic engine
// plays one continuous event, as strong as the stronger motor asked for
// (the engine has no separate heavy and light motors).

#include "Rumble.hpp"

#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>

#include <platform/RenderViewImpl.h>   // GLFW, the way Axmol includes it

#include <algorithm>
#include <map>

namespace rolo::detail {

namespace {

struct Haptic {
    CHHapticEngine* engine = nil;
    id<CHHapticPatternPlayer> player = nil;
};

class GameControllerRumble final : public Rumble {
public:
    explicit GameControllerRumble(const Logger& log)
        : m_log(log)
    {
    }
    ~GameControllerRumble() override
    {
        if (@available(macOS 11.0, *)) {
            for (auto& [jid, haptic] : m_haptics) {
                [haptic.player stopAtTime:CHHapticTimeImmediate error:nil];
                [haptic.engine stopWithCompletionHandler:nil];
            }
        }
    }

    bool set(int jid, float low, float high) override
    {
        if (@available(macOS 11.0, *)) {
            Haptic* haptic = hapticFor(jid);
            if (haptic == nullptr) {
                return false;
            }
            const float strength = std::clamp(std::max(low, high), 0.0f, 1.0f);
            if (haptic->player != nil) {
                [haptic->player stopAtTime:CHHapticTimeImmediate error:nil];
                haptic->player = nil;
            }
            if (strength <= 0.0f) {
                return true;
            }
            CHHapticEventParameter* intensity =
                [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:strength];
            CHHapticEvent* event = [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                                                 parameters:@[ intensity ]
                                                               relativeTime:0
                                                                   duration:10.0];   // Rolo stops it, sooner
            NSError* error = nil;
            CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
            if (pattern == nil) {
                return false;
            }
            haptic->player = [haptic->engine createPlayerWithPattern:pattern error:&error];
            return haptic->player != nil && [haptic->player startAtTime:CHHapticTimeImmediate error:&error];
        }
        return false;
    }

private:
    Haptic* hapticFor(int jid) API_AVAILABLE(macos(11.0))
    {
        if (const auto found = m_haptics.find(jid); found != m_haptics.end()) {
            return &found->second;
        }
        // The jid's position among GLFW's gamepads...
        int position = 0;
        for (int j = 0; j < jid; ++j) {
            position += glfwJoystickIsGamepad(j) == GLFW_TRUE ? 1 : 0;
        }
        // ...is the controller's position here.
        NSArray<GCController*>* controllers = [GCController controllers];
        if (position >= static_cast<int>(controllers.count)) {
            return nullptr;
        }
        GCController* controller = controllers[static_cast<NSUInteger>(position)];
        if (controller.haptics == nil) {
            return nullptr;   // this pad cannot rumble
        }
        CHHapticEngine* engine = [controller.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        NSError* error = nil;
        if (engine == nil || ![engine startAndReturnError:&error]) {
            m_log.debug("input", "A gamepad's rumble could not be started.");
            return nullptr;
        }
        return &m_haptics.emplace(jid, Haptic{engine, nil}).first->second;
    }

    const Logger& m_log;
    std::map<int, Haptic> m_haptics;
};

} // namespace

std::unique_ptr<Rumble> Rumble::create(const Logger& log)
{
    return std::make_unique<GameControllerRumble>(log);
}

} // namespace rolo::detail
