#pragma once

// Command-line options for rolo_player.
//
// Kept separate from main.cpp (and free of any backend code) so the parsing
// rules can be unit-tested without opening a window.

#include "rolo/gameplay/Input.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rolo::player {

struct PlayerOptions {
    // What to do.
    bool showHelp = false;
    bool showVersion = false;

    // What to show (both empty: Rolo's start scene).
    std::string projectPath;   // a project folder: plays its start scene
    std::string scenePath;     // a .lscene file

    // Window.
    std::string title = "Rolo Engine";
    int width = 1280;
    int height = 720;

    // Run behaviour.
    std::uint64_t maxFrames = 0;  // 0 = until closed
    std::string screenshotPath;   // empty = none
    bool showStats = true;
    bool statsChosen = false;     // --stats or --no-stats was given (exported games hide them by default)
    bool sound = true;            // --mute: play no sound

    // For automated tests of gameplay.
    std::vector<Key> heldKeys;    // --hold: keys held down for the whole run
    struct Tap {
        Key key = Key::Unknown;
        std::uint64_t from = 0;   // pressed at this frame
        std::uint64_t to = 1;     // released at this frame
        friend bool operator==(const Tap&, const Tap&) = default;
    };
    std::vector<Tap> tappedKeys;  // --tap KEY@FRAME or KEY@FROM-TO
    bool fixedStep = false;       // --fixed-step: every frame counts as 1/60 s
    std::string saveDir;          // --save-dir: where save slots go (default: the user's data folder)
};

/// A key by its name ("Right", "space", "W"; see rolo::keyName), ignoring case.
std::optional<Key> keyFromName(std::string_view name);

/// Result of parsing: either options, or an error message for the user.
struct ParseResult {
    PlayerOptions options;
    std::string error; // empty on success
    bool ok() const noexcept { return error.empty(); }
};

/// Parses arguments *after* the program name.
ParseResult parseCommandLine(std::span<const std::string_view> args);

/// The game bundled with an exported copy of the player, if there is one:
/// the folder `game` beside the program, or (macOS) in the app's Resources.
/// See editor/model/GameExport.hpp.
std::optional<std::filesystem::path> bundledGameFolder(const std::filesystem::path& executable);

/// Text printed by --help.
std::string helpText();

} // namespace rolo::player
