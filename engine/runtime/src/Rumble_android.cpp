// Rumble on Android: the controller's own motors, through Android's
// InputDevice (RoloGamepads.java does the asking). The phone itself is not
// shaken: that would need a permission, and Rolo's apps ask for none.
//
// Written without a controller to try it on (the emulator's stand-in
// gamepads have no motors); see docs/known-limitations.md.

#include "AndroidGamepadsJava.hpp"
#include "Rumble.hpp"

namespace rolo::detail {

namespace {
class AndroidRumble final : public Rumble {
public:
    // `jid` is the pad's number (0 to 3) on Android.
    bool set(int jid, float low, float high) override
    {
        const GamepadsJava& java = gamepadsJava();
        JNIEnv* env = ax::JniHelper::getEnv();
        if (!java.found() || env == nullptr) {
            return false;
        }
        const jboolean done = env->CallStaticBooleanMethod(java.type, java.rumble, static_cast<jint>(jid),
                                                           static_cast<jfloat>(low), static_cast<jfloat>(high));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return done == JNI_TRUE;
    }
};
} // namespace

std::unique_ptr<Rumble> Rumble::create(const Logger&)
{
    return std::make_unique<AndroidRumble>();
}

} // namespace rolo::detail
