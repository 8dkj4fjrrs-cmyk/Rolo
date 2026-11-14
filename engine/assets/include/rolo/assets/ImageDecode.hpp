#pragma once

// Decoding image files into pixels with Rolo's own decoder (stb_image,
// which is compiled inside rolo_assets and nowhere else).
//
// The renderer uses this for JPEG files, so that no separate JPEG library
// is needed in the engine or in games (ADR 0023); PNG files are decoded by
// the backend.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rolo {

struct DecodedImage {
    int width = 0;
    int height = 0;
    /// width × height pixels, each red, green, blue, alpha (one byte each),
    /// row by row from the top. Alpha is not multiplied into the colours.
    std::vector<std::uint8_t> rgba;
};

/// Decodes a PNG or JPEG file (baseline or progressive). Nothing, with
/// `error` set, if the file cannot be read or is not such an image.
std::optional<DecodedImage> decodeImageFile(const std::filesystem::path& path, std::string* error = nullptr);

/// The same, from the file's bytes.
std::optional<DecodedImage> decodeImage(const std::uint8_t* bytes, std::size_t size, std::string* error = nullptr);

/// True for the file names decodeImageFile is used for by the renderer: ".jpg", ".jpeg" (any case).
bool isJpegFileName(const std::filesystem::path& path);

} // namespace rolo
