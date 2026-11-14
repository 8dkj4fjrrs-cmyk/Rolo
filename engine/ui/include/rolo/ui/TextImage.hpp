#pragma once

// A block of text drawn into one picture, with Rolo's own font renderer
// (stb_truetype). Game text is drawn from such pictures.
//
// The text may have several lines ("\n") and may be wrapped to a width.
// Letters are placed at fractions of a pixel across (so spacing is even),
// and every line's baseline sits on a whole pixel (so letters are sharp
// top and bottom). No hinting is used: the picture is meant to be made
// at, or above, the size it is shown.

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rolo::ui {

enum class TextImageAlign { Left, Center, Right };

struct TextImageOptions {
    float pixelSize = 16.0f;   // the font's size (its em) in pixels
    float wrapWidth = 0.0f;    // lines longer than this are wrapped at spaces; 0: never
    TextImageAlign align = TextImageAlign::Left;
};

struct TextImage {
    int width = 0;
    int height = 0;
    /// width × height bytes, row by row from the top: how much of each
    /// pixel the letters cover, 0 (none) to 255 (all).
    std::vector<std::uint8_t> coverage;
    /// Empty pixels around the text on every side (letters may reach a
    /// little outside their line). The text's own box is the picture
    /// without this margin.
    int padding = 0;
    float lineHeight = 0.0f;   // distance between baselines, in pixels (whole)
    int lines = 0;
};

/// Draws `text` (UTF-8) with the font in `fontFile` (a .ttf or .otf file's
/// bytes). Nothing if the bytes are not a usable font or the size is not
/// positive. Empty text gives a picture one line tall with nothing in it.
std::optional<TextImage> drawTextImage(std::span<const unsigned char> fontFile, std::string_view text,
                                       const TextImageOptions& options);

} // namespace rolo::ui
