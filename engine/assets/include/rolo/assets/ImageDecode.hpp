#pragma once

// Decoding image files into pixels with Rolo's own decoder (stb_image,
// which is compiled inside rolo_assets and nowhere else).
//
// The renderer draws every image from these pixels, so that the engine
// and games need no separate JPEG or PNG library (ADR 0023, ADR 0024).

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
    /// True if the file says how see-through its pixels are (a PNG with an
    /// alpha channel or see-through palette entries). False for JPEGs and
    /// plain PNGs, whose alpha is 255 everywhere.
    bool hasAlpha = false;
};

/// Decodes a PNG file (any kind: colour, grey, palette, 8 or 16 bits,
/// interlaced) or a JPEG file (baseline or progressive). Nothing, with
/// `error` set, if the file cannot be read or is not such an image.
std::optional<DecodedImage> decodeImageFile(const std::filesystem::path& path, std::string* error = nullptr);

/// The same, from the file's bytes.
std::optional<DecodedImage> decodeImage(const std::uint8_t* bytes, std::size_t size, std::string* error = nullptr);

/// Multiplies each pixel's colours by its alpha, which is how the renderer
/// wants see-through images ("premultiplied alpha"). Each colour becomes
/// colour × (alpha + 1) / 256, rounded down. Does nothing useful twice:
/// call it once, and only for images with `hasAlpha`.
void premultiplyAlpha(DecodedImage& image);

} // namespace rolo
