#pragma once

// Text as Axmol sprites, drawn by Rolo's own font renderer
// (rolo/ui/TextImage.hpp, stb_truetype): Axmol is built without FreeType
// and its labels are not used (ADR 0025).
//
// Sizes here are in the pixels of the picture that is made. Callers that
// want text sharp when zoomed make it larger than shown and scale the node
// down (SceneRenderer does, by 2).

#include "rolo/scene/Components.hpp"
#include "rolo/ui/TextImage.hpp"

#include <axmol.h>

#include <string>
#include <string_view>

namespace rolo::detail {

struct TextSprite {
    ax::Sprite* sprite = nullptr;   // not yet in the scene; null if nothing could be drawn
    float width = 0.0f;             // the text's box (the widest line, or the wrap width)
    float height = 0.0f;
    float lineHeight = 0.0f;
};

struct TextSpriteOptions {
    float wrapWidth = 0.0f;                                   // 0: no wrapping
    ui::TextImageAlign align = ui::TextImageAlign::Left;
    /// Which point of the text's box the sprite's position means:
    /// (0, 0) bottom-left, (0, 1) top-left, (0.5, 0.5) the middle.
    ax::Vec2 anchor{0.0f, 1.0f};
};

/// `fontFile`: a .ttf or .otf file; empty, or a file that cannot be used,
/// means Rolo's built-in font. The letters are white, tinted with `color`.
TextSprite makeTextSprite(const std::string& fontFile, std::string_view text, float pixelSize, const Color& color,
                          const TextSpriteOptions& options = {});

/// The width of one line of text at that size.
float textWidth(const std::string& fontFile, std::string_view text, float pixelSize);

} // namespace rolo::detail
