#include "rolo/ui/Context.hpp"
#include "rolo/ui/DrawList.hpp"
#include "rolo/ui/EmbeddedFonts.hpp"
#include "rolo/ui/Font.hpp"
#include "rolo/ui/TextImage.hpp"
#include "rolo/ui/Utf8.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace rolo::ui;
using Catch::Approx;

// --- UTF-8 ---------------------------------------------------------------------------

TEST_CASE("UTF-8 decodes and encodes ASCII, accents, symbols and emoji", "[ui][utf8]")
{
    const std::string text = "aé⌘😀";
    std::size_t i = 0;
    CHECK(utf8::decode(text, i) == U'a');
    CHECK(utf8::decode(text, i) == U'é');
    CHECK(utf8::decode(text, i) == U'⌘');
    CHECK(utf8::decode(text, i) == U'😀');
    CHECK(i == text.size());

    std::string rebuilt;
    for (char32_t cp : {U'a', U'é', U'⌘', U'😀'}) {
        utf8::append(rebuilt, cp);
    }
    CHECK(rebuilt == text);
    CHECK(utf8::length(text) == 4);
}

TEST_CASE("UTF-8 survives broken input without getting stuck", "[ui][utf8]")
{
    const std::string broken = "a\xC3"   // cut-off two-byte sequence
                               "\xFF"    // never valid
                               "b";
    std::size_t i = 0;
    int count = 0;
    while (i < broken.size()) {
        utf8::decode(broken, i);
        ++count;
    }
    CHECK(count == 4);
}

TEST_CASE("UTF-8 steps by whole characters and words", "[ui][utf8]")
{
    const std::string text = "héllo wörld";
    CHECK(utf8::next(text, 1) == 3);      // é is two bytes
    CHECK(utf8::previous(text, 3) == 1);
    CHECK(utf8::previous(text, 0) == 0);
    CHECK(utf8::next(text, text.size()) == text.size());

    const std::size_t end = text.size();
    const std::size_t secondWord = utf8::previousWordStart(text, end);
    CHECK(text.substr(secondWord) == "wörld");
    CHECK(utf8::nextWordEnd(text, 0) == 6); // after "héllo"
    CHECK(utf8::previousWordStart(text, secondWord) == 0);
}

// --- Fonts -----------------------------------------------------------------------------

TEST_CASE("The embedded Inter fonts load", "[ui][font]")
{
    FontAtlas atlas;
    CHECK(fonts::interRegular().size() > 100000);
    REQUIRE(atlas.addFont(fonts::interRegular()).has_value());
    REQUIRE(atlas.addFont(fonts::interSemiBold()).has_value());
    const unsigned char notAFont[] = {1, 2, 3, 4};
    CHECK_FALSE(atlas.addFont(notAFont).has_value());
}

TEST_CASE("Font metrics and measurement behave sensibly", "[ui][font]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    const FontMetrics m = atlas.metrics(font, 13.0f);
    CHECK(m.ascent > 8.0f);
    CHECK(m.descent > 1.0f);
    CHECK(m.lineHeight >= m.ascent + m.descent);

    const float wide = atlas.measure(font, 13.0f, "WWWW");
    const float narrow = atlas.measure(font, 13.0f, "iiii");
    CHECK(wide > narrow);
    CHECK(atlas.measure(font, 26.0f, "Rolo") == Approx(2.0f * atlas.measure(font, 13.0f, "Rolo")).epsilon(0.08));
    CHECK(atlas.measure(font, 13.0f, "") == 0.0f);
}

TEST_CASE("Glyphs are rasterised into the atlas once and cached", "[ui][font]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    const std::uint64_t before = atlas.version();
    const GlyphInfo a = atlas.glyph(font, 13.0f, U'A');
    CHECK(a.visible);
    CHECK(a.box.width > 0.0f);
    CHECK(a.uv1.x > a.uv0.x);
    CHECK(atlas.version() > before);

    const std::uint64_t afterFirst = atlas.version();
    atlas.glyph(font, 13.0f, U'A');
    CHECK(atlas.version() == afterFirst); // cached

    CHECK_FALSE(atlas.glyph(font, 13.0f, U' ').visible);
    CHECK(atlas.glyph(font, 13.0f, U' ').advance > 0.0f);
}

TEST_CASE("Retina rasterises at higher resolution but measures the same in points", "[ui][font]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    const float at1x = atlas.measure(font, 13.0f, "Hierarchy");
    atlas.setPixelScale(2.0f);
    const float at2x = atlas.measure(font, 13.0f, "Hierarchy");
    CHECK(at2x == Approx(at1x).epsilon(0.05));
}

TEST_CASE("Caret positions and hit testing", "[ui][font]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    const std::string text = "abé";
    const auto carets = atlas.caretPositions(font, 13.0f, text);
    REQUIRE(carets.size() == 4);
    CHECK(carets[0] == 0.0f);
    CHECK(carets[3] == Approx(atlas.measure(font, 13.0f, text)));
    CHECK(atlas.hitTest(font, 13.0f, text, -5.0f) == 0);
    CHECK(atlas.hitTest(font, 13.0f, text, 1000.0f) == text.size());
    CHECK(atlas.hitTest(font, 13.0f, text, carets[2] + 0.1f) == 2); // just after "ab"
}

TEST_CASE("A full atlas grows and reports that drawn frames are stale", "[ui][font]")
{
    FontAtlas atlas(64, 64);
    const FontId font = *atlas.addFont(fonts::interRegular());
    const int startHeight = atlas.height();
    for (char32_t c = U'A'; c <= U'z'; ++c) {
        atlas.glyph(font, 30.0f, c);
    }
    CHECK(atlas.height() > startHeight);
    CHECK(atlas.consumeResetFlag());
    CHECK_FALSE(atlas.consumeResetFlag());
}

// --- DrawList --------------------------------------------------------------------------

TEST_CASE("Rectangles become two triangles in one command", "[ui][draw]")
{
    FontAtlas atlas;
    DrawList draw(atlas);
    draw.reset({0, 0, 100, 100});
    draw.fillRect({10, 10, 20, 20}, Color::hex(0xFF0000));
    CHECK(draw.vertices().size() == 4);
    CHECK(draw.indices().size() == 6);
    REQUIRE(draw.commands().size() == 1);
    CHECK(draw.commands()[0].texture == kAtlasTexture);
    CHECK(draw.vertices()[0].color == Color::hex(0xFF0000).packed());
}

TEST_CASE("Invisible or clipped-away shapes produce nothing", "[ui][draw]")
{
    FontAtlas atlas;
    DrawList draw(atlas);
    draw.reset({0, 0, 100, 100});
    draw.fillRect({10, 10, 0, 20}, Color::hex(0xFFFFFF));            // empty
    draw.fillRect({10, 10, 20, 20}, Color::hex(0xFFFFFF, 0));        // transparent
    draw.fillRect({200, 200, 20, 20}, Color::hex(0xFFFFFF));         // off screen
    draw.pushClip({0, 0, 5, 5});
    draw.fillRect({10, 10, 20, 20}, Color::hex(0xFFFFFF));           // outside clip
    draw.popClip();
    CHECK(draw.indices().empty());
}

TEST_CASE("A new command starts when the clip rectangle or texture changes", "[ui][draw]")
{
    FontAtlas atlas;
    DrawList draw(atlas);
    draw.reset({0, 0, 100, 100});
    draw.fillRect({0, 0, 10, 10}, Color::hex(0xFFFFFF));
    draw.pushClip({0, 0, 50, 50});
    draw.fillRect({0, 0, 10, 10}, Color::hex(0xFFFFFF));
    draw.image(7, {0, 0, 10, 10});
    draw.popClip();
    REQUIRE(draw.commands().size() == 3);
    CHECK(draw.commands()[1].clip == Rect{0, 0, 50, 50});
    CHECK(draw.commands()[2].texture == 7u);
}

TEST_CASE("Rounded shapes get a soft anti-aliased edge", "[ui][draw]")
{
    FontAtlas atlas;
    DrawList draw(atlas);
    draw.reset({0, 0, 100, 100});
    draw.fillRoundedRect({10, 10, 40, 30}, 6.0f, Color::hex(0xFFFFFF));
    // Each outline point has an opaque inner and a transparent outer vertex.
    bool sawTransparent = false;
    for (const Vertex& v : draw.vertices()) {
        sawTransparent = sawTransparent || (v.color >> 24) == 0;
    }
    CHECK(sawTransparent);
    CHECK(draw.vertices().size() % 2 == 0);
}

TEST_CASE("Text produces one quad per visible character", "[ui][draw]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    DrawList draw(atlas);
    draw.reset({0, 0, 300, 100});
    const float width = draw.text(font, 13.0f, {5, 5}, Color::hex(0xFFFFFF), "Hi there");
    CHECK(width == Approx(atlas.measure(font, 13.0f, "Hi there")));
    CHECK(draw.vertices().size() == 7 * 4); // 7 letters, the space draws nothing
}

TEST_CASE("Ellipsized text never exceeds its width", "[ui][draw]")
{
    FontAtlas atlas;
    const FontId font = *atlas.addFont(fonts::interRegular());
    DrawList draw(atlas);
    draw.reset({0, 0, 300, 100});
    const float drawn = draw.textEllipsized(font, 13.0f, {0, 0}, Color::hex(0xFFFFFF),
                                            "A rather long object name that will not fit", 80.0f);
    CHECK(drawn <= 80.0f);
    CHECK(drawn > 40.0f);
}

TEST_CASE("A block of text is drawn into one picture", "[ui][font][textimage]")
{
    // Rolo's own renderer for game text (a trial beside FreeType for now).
    TextImageOptions options;
    options.pixelSize = 24.0f;
    const auto one = drawTextImage(fonts::interRegular(), "Lantern", options);
    REQUIRE(one);
    CHECK(one->lines == 1);
    CHECK(one->coverage.size() == static_cast<std::size_t>(one->width) * static_cast<std::size_t>(one->height));
    CHECK(one->width > 60);
    CHECK(one->height >= 24);
    // Something is drawn, with solid pixels, and the margin stays empty.
    int solid = 0;
    for (const std::uint8_t c : one->coverage) {
        solid += c == 255 ? 1 : 0;
    }
    CHECK(solid > 50);
    for (int x = 0; x < one->width; ++x) {
        CHECK(one->coverage[static_cast<std::size_t>(x)] == 0);   // top row
    }

    // "\n" starts a new line; lines are a whole number of pixels apart.
    const auto two = drawTextImage(fonts::interRegular(), "Lantern\nKeeper", options);
    REQUIRE(two);
    CHECK(two->lines == 2);
    CHECK(two->lineHeight == std::floor(two->lineHeight));
    CHECK(two->height == one->height + static_cast<int>(two->lineHeight));

    // Wrapping: a long sentence in a narrow box takes several lines and stays inside the box.
    options.pixelSize = 12.0f;
    options.wrapWidth = 100.0f;
    const auto wrapped = drawTextImage(fonts::interRegular(), "The village lantern has gone dark again", options);
    REQUIRE(wrapped);
    CHECK(wrapped->lines >= 3);
    CHECK(wrapped->width == 100 + wrapped->padding * 2);

    // Right-aligned text ends at the box's right edge: its last column of ink is near it.
    options.wrapWidth = 200.0f;
    options.align = TextImageAlign::Right;
    const auto right = drawTextImage(fonts::interRegular(), "H", options);
    REQUIRE(right);
    int lastInk = -1;
    for (int y = 0; y < right->height; ++y) {
        for (int x = 0; x < right->width; ++x) {
            if (right->coverage[static_cast<std::size_t>(y * right->width + x)] > 0) {
                lastInk = std::max(lastInk, x);
            }
        }
    }
    CHECK(lastInk > right->width - right->padding - 4);

    // Empty text is an empty picture one line tall; bad input is nothing.
    const auto empty = drawTextImage(fonts::interRegular(), "", options);
    REQUIRE(empty);
    CHECK(empty->lines == 1);
    const unsigned char notAFont[] = {1, 2, 3, 4};
    CHECK_FALSE(drawTextImage(notAFont, "x", options));
    options.pixelSize = 0.0f;
    CHECK_FALSE(drawTextImage(fonts::interRegular(), "x", options));
}
