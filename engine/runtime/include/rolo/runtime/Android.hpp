#pragma once

// Android: what a program needs to know there that a computer does not ask
// of it (ADR 0028). Only built for Android (the functions exist nowhere else).
//
// An Android app is not started at main(). Android makes the screen itself (a
// Java "activity", Axmol's AxmolActivity, which Rolo's RoloActivity builds
// on; player/android), loads the program as a library, and calls into it:
// when the screen is ready, for every frame, when the player leaves the app.
// So the program says ahead of time what "starting" means (setStart), and is
// called when there is a screen to draw on.
//
// An app's files are also not a folder: they are packed inside the app (its
// "assets"). Rolo reads games from real folders, so a game travels as one
// packed file (a game pack) and is unpacked into the app's own folder.

#include "rolo/core/Engine.hpp"   // Status
#include "rolo/gameplay/Input.hpp"   // Key

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rolo::android {

/// Says what starts the program. Call it when the library is loaded (from
/// `axmol_android_app_init`, which Axmol calls then; see player/runtime/main.cpp).
/// `start` runs later, once the app's screen is ready, on the thread that
/// draws: make the rolo::Application there and call its run(), which
/// returns while the game goes on (RunReport::continuesInBackground).
void setStart(std::function<void()> start);

/// The app's own folder: only this app can read and write it, and Android
/// removes it with the app. For the unpacked game and the saved games.
std::filesystem::path filesFolder();

/// A text that is different after every install or update of the app (and
/// the same until the next): tells whether what was unpacked from the app
/// before is still what the app contains. Empty if it cannot be told.
std::string installStamp();

/// Copies a file packed inside the app (one of its "assets") to a real file.
Status copyPackedFile(std::string_view name, const std::filesystem::path& to);

/// The strips along the screen's edges, in pixels, where a camera notch or
/// hole is (Android's "display cutout"): left, right, bottom, top. All 0 on
/// screens without one. They change when the phone is turned round.
struct SafeInsets {
    int left = 0;
    int right = 0;
    int bottom = 0;
    int top = 0;
};
SafeInsets safeInsets();

/// A keyboard's key going down or up (Chromebooks, tablets, a keyboard
/// plugged into a phone).
struct KeyChange {
    Key key = Key::Unknown;
    bool down = false;
};
/// The keys pressed and let go since the last call, in order (RoloKeys.java
/// collects them). Keys Rolo has no name for are left out.
std::vector<KeyChange> takeKeys();
/// Rolo's key for one of Android's key codes (AKEYCODE_...), or Key::Unknown.
Key keyFromAndroid(int androidKeyCode);

/// Shows a message with an OK button over the app. For problems at
/// start-up: a player has no log to look at.
void showMessage(const std::string& title, const std::string& text);

} // namespace rolo::android
