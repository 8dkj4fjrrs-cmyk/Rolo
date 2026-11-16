// Android's files and messages (rolo/runtime/Android.hpp), through Axmol's
// Android code. The start of the program is in Application.cpp, beside the
// Application it starts.

#include "rolo/runtime/Android.hpp"

#include <axmol.h>
#include <platform/android/FileUtils-android.h>
#include <platform/android/jni/Java_dev_axmol_lib_AxmolEngine.h>
#include <platform/android/jni/JniHelper.h>

#include <android/asset_manager.h>
#include <android/keycodes.h>

#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>

namespace rolo::android {

namespace {
// A static method of the app's Java side, looked up by name once and kept:
// the lookup is slow, and takeKeys() is called every frame.
struct JavaMethod {
    jclass type = nullptr;
    jmethodID method = nullptr;
};

JavaMethod findJavaMethod(const char* className, const char* name, const char* signature)
{
    JavaMethod out;
    ax::JniMethodInfo info;
    if (!ax::JniHelper::getStaticMethodInfo(info, className, name, signature)) {
        if (JNIEnv* env = ax::JniHelper::getEnv(); env != nullptr && env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return out;
    }
    out.type = static_cast<jclass>(info.env->NewGlobalRef(info.classID));   // kept for the program's life
    out.method = info.methodID;
    info.env->DeleteLocalRef(info.classID);
    return out;
}
} // namespace

std::filesystem::path filesFolder()
{
    // Android's Context.getFilesDir(): /data/user/0/<the app's package>/files
    return std::filesystem::path(ax::FileUtils::getInstance()->getWritablePath());
}

std::string installStamp()
{
    // The app's own file (its .apk). Android gives it a new place on every
    // install and update, so its path, size and date together name one
    // installation.
    const char* apk = getApkPath();
    if (apk == nullptr || *apk == '\0') {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path path(apk);
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return {};
    }
    const auto changed = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return {};
    }
    // (Whole seconds as a plain number: the clock's own count is a 128-bit
    // number on Android, which cannot be written as text directly.)
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(changed.time_since_epoch()).count();
    return std::string(apk) + "|" + std::to_string(static_cast<unsigned long long>(size)) + "|"
           + std::to_string(static_cast<long long>(seconds));
}

Status copyPackedFile(std::string_view name, const std::filesystem::path& to)
{
    AAssetManager* manager = ax::FileUtilsAndroid::getAssetManager();
    if (manager == nullptr) {
        return Status::failure("The app's files cannot be read yet (Android has not handed them over).");
    }
    const std::string assetName(name);
    // Streaming: read from start to end, a piece at a time, so a large game
    // is never in memory whole.
    AAsset* asset = AAssetManager_open(manager, assetName.c_str(), AASSET_MODE_STREAMING);
    if (asset == nullptr) {
        return Status::failure("The app does not contain the file '" + assetName + "'.");
    }
    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    std::vector<char> piece(256 * 1024);
    bool ok = out.good();
    while (ok) {
        const int got = AAsset_read(asset, piece.data(), piece.size());
        if (got == 0) {
            break;   // the end
        }
        if (got < 0) {
            ok = false;
            break;
        }
        out.write(piece.data(), got);
        ok = out.good();
    }
    AAsset_close(asset);
    out.close();
    if (!ok || out.fail()) {
        std::filesystem::remove(to, ec);
        return Status::failure("Could not copy '" + assetName + "' out of the app to " + to.string()
                               + ". Is the device's storage full?");
    }
    return Status::success();
}

SafeInsets safeInsets()
{
    // RoloActivity keeps them (Android tells its Java side); one call fetches all four.
    SafeInsets out;
    static const JavaMethod java = findJavaMethod("dev/roloengine/player/RoloActivity", "safeInsets", "()[I");
    JNIEnv* env = ax::JniHelper::getEnv();
    if (java.method == nullptr || env == nullptr) {
        return out;
    }
    const auto list = static_cast<jintArray>(env->CallStaticObjectMethod(java.type, java.method));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (list != nullptr && env->GetArrayLength(list) >= 4) {
        jint values[4] = {};
        env->GetIntArrayRegion(list, 0, 4, values);
        out = {values[0], values[1], values[2], values[3]};
    }
    if (list != nullptr) {
        env->DeleteLocalRef(list);
    }
    return out;
}

Key keyFromAndroid(int code)
{
    // Runs of keys that follow one another in both lists.
    if (code >= AKEYCODE_A && code <= AKEYCODE_Z) {
        return static_cast<Key>(static_cast<int>(Key::A) + (code - AKEYCODE_A));
    }
    if (code >= AKEYCODE_0 && code <= AKEYCODE_9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (code - AKEYCODE_0));
    }
    if (code >= AKEYCODE_NUMPAD_0 && code <= AKEYCODE_NUMPAD_9) {
        return static_cast<Key>(static_cast<int>(Key::Num0) + (code - AKEYCODE_NUMPAD_0));
    }
    if (code >= AKEYCODE_F1 && code <= AKEYCODE_F12) {
        return static_cast<Key>(static_cast<int>(Key::F1) + (code - AKEYCODE_F1));
    }
    switch (code) {
        case AKEYCODE_SPACE: return Key::Space;
        case AKEYCODE_ENTER:
        case AKEYCODE_NUMPAD_ENTER:
        case AKEYCODE_DPAD_CENTER: return Key::Enter;
        case AKEYCODE_ESCAPE: return Key::Escape;
        case AKEYCODE_TAB: return Key::Tab;
        case AKEYCODE_DEL: return Key::Backspace;   // (Android's name for the key that deletes backwards)
        case AKEYCODE_DPAD_LEFT: return Key::Left;
        case AKEYCODE_DPAD_RIGHT: return Key::Right;
        case AKEYCODE_DPAD_UP: return Key::Up;
        case AKEYCODE_DPAD_DOWN: return Key::Down;
        case AKEYCODE_SHIFT_LEFT: return Key::LeftShift;
        case AKEYCODE_SHIFT_RIGHT: return Key::RightShift;
        case AKEYCODE_CTRL_LEFT: return Key::LeftControl;
        case AKEYCODE_CTRL_RIGHT: return Key::RightControl;
        case AKEYCODE_ALT_LEFT: return Key::LeftAlt;
        case AKEYCODE_ALT_RIGHT: return Key::RightAlt;
        default: return Key::Unknown;
    }
}

std::vector<KeyChange> takeKeys()
{
    std::vector<KeyChange> out;
    static const JavaMethod java = findJavaMethod("dev/roloengine/player/RoloKeys", "take", "()[I");
    JNIEnv* env = ax::JniHelper::getEnv();
    if (java.method == nullptr || env == nullptr) {
        return out;
    }
    const auto list = static_cast<jintArray>(env->CallStaticObjectMethod(java.type, java.method));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    } else if (list != nullptr) {
        const jsize count = env->GetArrayLength(list);
        std::vector<jint> values(static_cast<std::size_t>(count));
        env->GetIntArrayRegion(list, 0, count, values.data());
        for (std::size_t i = 0; i + 1 < values.size(); i += 2) {
            if (const Key key = keyFromAndroid(values[i]); key != Key::Unknown) {
                out.push_back({key, values[i + 1] != 0});
            }
        }
    }
    if (list != nullptr) {
        env->DeleteLocalRef(list);
    }
    return out;
}

void showMessage(const std::string& title, const std::string& text)
{
    ax::showAlert(text, title);
}

} // namespace rolo::android
