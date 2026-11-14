// Android's files and messages (rolo/runtime/Android.hpp), through Axmol's
// Android code. The start of the program is in Application.cpp, beside the
// Application it starts.

#include "rolo/runtime/Android.hpp"

#include <axmol.h>
#include <platform/android/FileUtils-android.h>
#include <platform/android/jni/Java_dev_axmol_lib_AxmolEngine.h>

#include <android/asset_manager.h>

#include <chrono>
#include <fstream>
#include <system_error>
#include <vector>

namespace rolo::android {

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

void showMessage(const std::string& title, const std::string& text)
{
    ax::showAlert(text, title);
}

} // namespace rolo::android
