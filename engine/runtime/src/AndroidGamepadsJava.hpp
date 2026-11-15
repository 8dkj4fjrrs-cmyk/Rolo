#pragma once

// The way from C++ to RoloGamepads.java (player/android), which keeps what
// Android's game controllers are doing. Used by Gamepads_android.cpp (to
// read them) and Rumble_android.cpp (to shake them).
//
// Calling Java from C++ goes through "JNI": the Java class and its methods
// are looked up by name once, and then called with the calling thread's
// "environment". Axmol's JniHelper finds both.

#include <platform/android/jni/JniHelper.h>

namespace rolo::detail {

struct GamepadsJava {
    jclass type = nullptr;        // dev.roloengine.player.RoloGamepads (kept for the program's life)
    jmethodID read = nullptr;     // static void read(float[])
    jmethodID name = nullptr;     // static String name(int)
    jmethodID rumble = nullptr;   // static boolean rumble(int, float, float)

    bool found() const noexcept { return type != nullptr && read != nullptr && name != nullptr && rumble != nullptr; }
};

/// Looked up at the first call. Not found (found() is false) only if the
/// app was built without RoloGamepads.java.
inline const GamepadsJava& gamepadsJava()
{
    static const GamepadsJava java = [] {
        GamepadsJava out;
        ax::JniMethodInfo info;
        if (!ax::JniHelper::getStaticMethodInfo(info, "dev/roloengine/player/RoloGamepads", "read", "([F)V")) {
            if (JNIEnv* env = ax::JniHelper::getEnv(); env != nullptr && env->ExceptionCheck()) {
                env->ExceptionClear();
            }
            return out;
        }
        out.type = static_cast<jclass>(info.env->NewGlobalRef(info.classID));
        info.env->DeleteLocalRef(info.classID);
        out.read = info.methodID;
        out.name = info.env->GetStaticMethodID(out.type, "name", "(I)Ljava/lang/String;");
        out.rumble = info.env->GetStaticMethodID(out.type, "rumble", "(IFF)Z");
        if (info.env->ExceptionCheck()) {
            info.env->ExceptionClear();
        }
        return out;
    }();
    return java;
}

} // namespace rolo::detail
