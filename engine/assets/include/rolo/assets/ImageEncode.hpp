#pragma once

// Writing pixels as a PNG file with Rolo's own encoder (stb_image_write,
// compiled inside rolo_assets and nowhere else). Screenshots use it, so
// that the engine needs no PNG library (ADR 0024).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rolo {

/// The bytes of a PNG file showing width × height pixels. `pixels` has
/// `channels` bytes per pixel (3: red, green, blue; 4: the same and alpha,
/// not multiplied into the colours), row by row from the top. Empty if the
/// sizes make no sense.
std::vector<std::uint8_t> encodePng(const std::uint8_t* pixels, int width, int height, int channels);

/// Writes such a file. False, with `error` set, if it could not be written.
bool writePngFile(const std::filesystem::path& path, const std::uint8_t* pixels, int width, int height, int channels,
                  std::string* error = nullptr);

} // namespace rolo
