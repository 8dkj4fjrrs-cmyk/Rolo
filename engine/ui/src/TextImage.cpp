#include "rolo/ui/TextImage.hpp"

#include "rolo/ui/Utf8.hpp"

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace rolo::ui {

namespace {

struct Placed {
    int glyph = 0;
    float x = 0.0f;   // pen position from the line's start, in pixels
};

struct Line {
    std::vector<Placed> glyphs;
    float width = 0.0f;
};

struct Word {
    std::vector<int> glyphs;
    bool space = false;   // a run of spaces: stays at the end of its line, where it takes no room
};

class Shaper {
public:
    Shaper(const stbtt_fontinfo& info, float scale)
        : m_info(info)
        , m_scale(scale)
    {
    }

    float advance(int glyph) const
    {
        int adv = 0;
        int bearing = 0;
        stbtt_GetGlyphHMetrics(&m_info, glyph, &adv, &bearing);
        return static_cast<float>(adv) * m_scale;
    }

    float kern(int left, int right) const
    {
        return static_cast<float>(stbtt_GetGlyphKernAdvance(&m_info, left, right)) * m_scale;
    }

    /// Adds `glyphs` to the end of `line`.
    void append(Line& line, const std::vector<int>& glyphs) const
    {
        for (const int glyph : glyphs) {
            float x = line.width;
            if (!line.glyphs.empty()) {
                x += kern(line.glyphs.back().glyph, glyph);
            }
            line.glyphs.push_back({glyph, x});
            line.width = x + advance(glyph);
        }
    }

    float widthWith(const Line& line, const std::vector<int>& glyphs) const
    {
        Line copy = line;
        append(copy, glyphs);
        return copy.width;
    }

private:
    const stbtt_fontinfo& m_info;
    float m_scale;
};

/// One paragraph (no line breaks in it) as words and runs of spaces.
std::vector<Word> wordsOf(const stbtt_fontinfo& info, std::string_view paragraph)
{
    std::vector<Word> words;
    std::size_t i = 0;
    while (i < paragraph.size()) {
        char32_t cp = utf8::decode(paragraph, i);
        if (cp == U'\r') {
            continue;
        }
        const bool space = cp == U' ' || cp == U'\t';
        if (cp == U'\t') {
            cp = U' ';
        }
        if (words.empty() || words.back().space != space) {
            words.push_back({{}, space});
        }
        words.back().glyphs.push_back(stbtt_FindGlyphIndex(&info, static_cast<int>(cp)));
    }
    return words;
}

} // namespace

std::optional<TextImage> drawTextImage(std::span<const unsigned char> fontFile, std::string_view text,
                                       const TextImageOptions& options)
{
    if (fontFile.empty() || !(options.pixelSize > 0.0f) || options.pixelSize > 2000.0f) {
        return std::nullopt;
    }
    stbtt_fontinfo info{};
    const int offset = stbtt_GetFontOffsetForIndex(fontFile.data(), 0);
    if (offset < 0 || stbtt_InitFont(&info, fontFile.data(), offset) == 0) {
        return std::nullopt;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, options.pixelSize);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    // Whole pixels, so that every baseline is on a pixel row.
    const int ascentPx = static_cast<int>(std::ceil(static_cast<float>(ascent) * scale));
    const int descentPx = static_cast<int>(std::ceil(static_cast<float>(-descent) * scale));
    const int lineHeight =
        std::max(1, static_cast<int>(std::lround(static_cast<float>(ascent - descent + lineGap) * scale)));

    // Lines: one per "\n", more where a paragraph is wrapped.
    const Shaper shaper(info, scale);
    std::vector<Line> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        Line line;
        for (const Word& word : wordsOf(info, text.substr(start, end - start))) {
            const bool wraps = options.wrapWidth > 0.0f && !word.space && !line.glyphs.empty()
                               && shaper.widthWith(line, word.glyphs) > options.wrapWidth;
            if (wraps) {
                lines.push_back(std::move(line));
                line = Line{};
            }
            shaper.append(line, word.glyphs);
        }
        lines.push_back(std::move(line));
        start = end + 1;
    }
    // Spaces at the end of a line take no room when lines are placed (so
    // centred and right-aligned lines sit where the words are).
    const int spaceGlyph = stbtt_FindGlyphIndex(&info, ' ');
    for (Line& line : lines) {
        std::size_t count = line.glyphs.size();
        while (count > 0 && line.glyphs[count - 1].glyph == spaceGlyph) {
            --count;
        }
        if (count < line.glyphs.size()) {
            line.width = count == 0 ? 0.0f : line.glyphs[count - 1].x + shaper.advance(line.glyphs[count - 1].glyph);
            line.glyphs.resize(count);
        }
    }

    float widest = 0.0f;
    for (const Line& line : lines) {
        widest = std::max(widest, line.width);
    }
    const float boxWidth = options.wrapWidth > 0.0f ? std::max(options.wrapWidth, widest) : widest;

    TextImage image;
    image.padding = 2 + static_cast<int>(options.pixelSize / 8.0f);
    image.lines = static_cast<int>(lines.size());
    image.lineHeight = static_cast<float>(lineHeight);
    image.width = static_cast<int>(std::ceil(boxWidth)) + image.padding * 2;
    image.height = (image.lines - 1) * lineHeight + ascentPx + descentPx + image.padding * 2;
    if (image.width > 16384 || image.height > 16384) {
        return std::nullopt;   // larger than a texture can be
    }
    image.coverage.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);

    std::vector<unsigned char> bitmap;
    for (std::size_t row = 0; row < lines.size(); ++row) {
        const Line& line = lines[row];
        const float free = boxWidth - line.width;
        const float lineX = options.align == TextImageAlign::Left ? 0.0f
                            : options.align == TextImageAlign::Right ? free : free * 0.5f;
        const int baseline = image.padding + ascentPx + static_cast<int>(row) * lineHeight;
        for (const Placed& placed : line.glyphs) {
            if (stbtt_IsGlyphEmpty(&info, placed.glyph) != 0) {
                continue;
            }
            const float x = static_cast<float>(image.padding) + lineX + placed.x;
            const float whole = std::floor(x);
            const float part = x - whole;
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;
            stbtt_GetGlyphBitmapBoxSubpixel(&info, placed.glyph, scale, scale, part, 0.0f, &x0, &y0, &x1, &y1);
            const int w = x1 - x0;
            const int h = y1 - y0;
            if (w <= 0 || h <= 0) {
                continue;
            }
            bitmap.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
            stbtt_MakeGlyphBitmapSubpixel(&info, bitmap.data(), w, h, w, scale, scale, part, 0.0f, placed.glyph);
            const int left = static_cast<int>(whole) + x0;
            const int top = baseline + y0;
            for (int y = 0; y < h; ++y) {
                const int py = top + y;
                if (py < 0 || py >= image.height) {
                    continue;
                }
                for (int xx = 0; xx < w; ++xx) {
                    const int px = left + xx;
                    if (px < 0 || px >= image.width) {
                        continue;
                    }
                    // Letters that touch add up (kerned pairs overlap a little).
                    auto& out = image.coverage[static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width)
                                               + static_cast<std::size_t>(px)];
                    const int sum = out + bitmap[static_cast<std::size_t>(y) * static_cast<std::size_t>(w)
                                                 + static_cast<std::size_t>(xx)];
                    out = static_cast<std::uint8_t>(std::min(sum, 255));
                }
            }
        }
    }
    return image;
}

} // namespace rolo::ui
