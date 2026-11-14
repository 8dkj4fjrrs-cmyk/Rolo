#pragma once

// Exporting a project as a game that runs without the editor.
//
// An exported game is Rolo's game runtime (rolo_player, renamed after
// the game) with the project's files bundled beside it, plus the license
// texts of everything inside it. The runtime finds the bundled game by
// itself (see player/runtime/main.cpp), so players just open it:
//
//   Linux, Windows                      macOS
//   My Game/                            My Game.app/
//     My Game(.exe)    the runtime        Contents/MacOS/rolo_player
//     axslc/           its shaders        Contents/Resources/axslc/
//     (*.dll)          (Windows)          Contents/Resources/game/
//     game/            the project        Contents/Resources/licenses/
//     licenses/        license texts      Contents/Info.plist (the game's name)
//     README.txt
//
// Desktop games are exported for the computer the editor runs on (no
// cross-export).
//
// A game for the web (ExportTarget::Web) is a folder to put on a web host:
//
//   My Game Web/
//     index.html          the page (player/web/index.html, with the game's name)
//     rolo_player.js      Rolo's web runtime (WebAssembly)...
//     rolo_player.wasm
//     rolo_player.data    ...and its shaders
//     game.pack           the project's files as one file (rolo/assets/GamePack.hpp)
//     licenses/           license texts
//     README.txt          how to share it
//
// See docs/exporting.md.

#include "EditorPaths.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace rolo::editor {

enum class ExportTarget {
    Desktop,   // a program for the computer the editor runs on
    Web,       // a folder for a web host, played in a browser
};

struct ExportOptions {
    ExportTarget target = ExportTarget::Desktop;
    std::filesystem::path project;   // the project folder
    std::filesystem::path output;    // the folder the game is created in
    std::string gameName;            // names the game's folder / app and program
    std::filesystem::path player;    // rolo_player(.exe), or rolo_player.app on macOS
    std::filesystem::path webPlayer; // Web: the folder with index.html, rolo_player.js, .wasm and .data
    std::filesystem::path licenses;  // Rolo's licenses/ folder
    HostOs os = currentOs();
    std::string version;             // Rolo's version, written in README.txt
};

struct ExportResult {
    bool ok = false;
    std::filesystem::path game;      // what was made: the game's folder, or its .app
    std::filesystem::path program;   // the program to run (Web: the page, index.html)
    std::string error;               // why it failed (empty when ok)
    std::uint64_t files = 0;         // files written
    std::uint64_t bytes = 0;
};

/// A file and folder name made from a game's name: letters, digits, spaces,
/// '-' and '_' kept; anything else dropped. "Game" if nothing is left.
std::string exportFileName(const std::string& gameName);

/// The files of Rolo's web runtime that every web game needs.
inline constexpr const char* kWebPlayerFiles[] = {"index.html", "rolo_player.js", "rolo_player.wasm",
                                                  "rolo_player.data"};

/// Exports the game. An earlier export of the same game in `output` is
/// replaced; any other existing folder of that name is left alone (an error).
ExportResult exportGame(const ExportOptions& options);

} // namespace rolo::editor
