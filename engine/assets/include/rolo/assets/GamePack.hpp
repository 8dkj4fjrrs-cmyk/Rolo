#pragma once

// A game pack: a project's files as one file, for places where a game cannot
// be a folder. A game exported for the web is a pack beside the web player;
// the page downloads it and unpacks it into the player's memory
// (player/web/pre.js). The format is described in docs/formats/game-pack.md.
//
//   "ROLOPACK1\n"            10 bytes: what the file is, and the layout's version
//   length of the index      4 bytes, little-endian
//   the index                JSON (UTF-8): the game's name, where its saves
//                            go, and each file's path and size, in order
//   the files                their bytes, one after another, in that order
//
// Nothing is compressed: web servers compress what they send, and images
// and sounds are compressed already.

#include "rolo/core/Engine.hpp"   // Status

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rolo {

struct GamePackFormat {
    static constexpr const char* kMagic = "ROLOPACK1\n";
    static constexpr const char* kFormat = "rolo.pack";
    static constexpr int kVersion = 1;
};

struct GamePackFile {
    std::string path;          // inside the project, with '/', e.g. "scenes/main.lscene"
    std::uint64_t size = 0;    // bytes
};

struct GamePackIndex {
    std::string name;          // the game's name
    std::string saves;         // the folder name its saved games are kept under
    std::vector<GamePackFile> files;
    std::uint64_t dataOffset = 0;   // where the first file's bytes begin (set when read)
};

struct GamePackResult {
    Status status;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;   // the pack's size
};

/// Packs the files of `folder` (and its folders) into the pack `output`.
/// `keep` decides per file and folder (nothing given: all but hidden ones,
/// whose names start with '.'). Files are in alphabetical order, so the same
/// folder always gives the same pack.
GamePackResult writeGamePack(const std::filesystem::path& folder, const std::filesystem::path& output,
                             const std::string& name, const std::string& saves,
                             const std::function<bool(const std::filesystem::path&)>& keep = {});

/// Reads a pack's index. Nothing, with `error` set, if the file is not a
/// pack or is damaged.
std::optional<GamePackIndex> readGamePackIndex(const std::filesystem::path& pack, std::string* error = nullptr);

/// Unpacks a pack into `folder` (made if needed). Paths that would leave the
/// folder are refused.
Status extractGamePack(const std::filesystem::path& pack, const std::filesystem::path& folder);

/// A name safe as one folder name in any file system and in a web address:
/// letters and digits kept, everything else '-', lower case; "game" if empty.
std::string gamePackSavesName(const std::string& gameName);

} // namespace rolo
