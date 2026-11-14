// Rolo builds Axmol without FreeType (ADR 0025): Axmol's own
// core/2d/FontFreeType.cpp is left out of the build, and this file provides
// that class's functions instead, so that the rest of Axmol's core links
// unedited. It is Rolo's own code and contains no FreeType code.
//
// No font can be made here: FontFreeType::create() gives nothing, so Axmol's
// TrueType labels (Label::createWithTTF) cannot be created. Rolo does not use
// them: it draws text itself (engine/runtime/src/TextSprites.hpp).

#include "2d/FontFreeType.h"

namespace ax
{

FT_Library FontFreeType::_FTlibrary             = nullptr;
bool FontFreeType::_FTInitialized               = false;
bool FontFreeType::_streamParsingEnabled        = false;
bool FontFreeType::_doNativeBytecodeHinting     = true;
bool FontFreeType::_globalSDFEnabled            = false;
const int FontFreeType::DistanceMapSpread       = 6;   // the value Axmol's labels expect
char32_t FontFreeType::_mssingGlyphCharacter    = 0;

void FontFreeType::setFontEngine(IFontEngine* /*engine*/) {}

FontFreeType* FontFreeType::create(std::string_view /*fontPath*/,
                                   int /*faceSize*/,
                                   GlyphCollection /*glyphs*/,
                                   std::string_view /*customGlyphs*/,
                                   bool /*distanceFieldEnabled*/,
                                   float /*outline*/)
{
    return nullptr;
}

FontFreeType* FontFreeType::createWithFaceInfo(FontFaceInfo* /*info*/, FontFreeType* /*mainFont*/)
{
    return nullptr;
}

void FontFreeType::shutdownFreeType() {}

FT_Library FontFreeType::getFTLibrary()
{
    return nullptr;
}

FontFreeType::FontFreeType(bool distanceFieldEnabled, float outline)
    : _fontFace(nullptr)
    , _fontStream(nullptr)
    , _stroker(nullptr)
    , _faceSize(0)
    , _distanceFieldEnabled(distanceFieldEnabled)
    , _outlineSize(outline)
    , _ascender(0)
    , _descender(0)
    , _lineHeight(0)
    , _usedGlyphs(GlyphCollection::ASCII)
{}

FontFreeType::~FontFreeType() {}

void FontFreeType::renderCharAt(unsigned char* /*dest*/,
                                int /*posX*/,
                                int /*posY*/,
                                unsigned char* /*bitmap*/,
                                int /*bitmapWidth*/,
                                int /*bitmapHeight*/,
                                int /*atlasWidth*/,
                                int /*atlasHeight*/)
{}

int* FontFreeType::getHorizontalKerningForTextUTF32(const std::u32string& /*text*/, int& outNumLetters) const
{
    outNumLetters = 0;
    return nullptr;
}

unsigned char* FontFreeType::getGlyphBitmap(char32_t /*charCode*/,
                                            int& outWidth,
                                            int& outHeight,
                                            Rect& outRect,
                                            int& xAdvance,
                                            FontFaceInfo** /*ppFallbackInfo*/)
{
    outWidth  = 0;
    outHeight = 0;
    outRect   = Rect::ZERO;
    xAdvance  = 0;
    return nullptr;
}

unsigned char* FontFreeType::getGlyphBitmapByIndex(unsigned int /*glyphIndex*/,
                                                   int& outWidth,
                                                   int& outHeight,
                                                   Rect& outRect,
                                                   int& xAdvance)
{
    outWidth  = 0;
    outHeight = 0;
    outRect   = Rect::ZERO;
    xAdvance  = 0;
    return nullptr;
}

int FontFreeType::getFontAscender() const
{
    return 0;
}

const char* FontFreeType::getFontFamily() const
{
    return nullptr;
}

FontAtlas* FontFreeType::newFontAtlas()
{
    return nullptr;
}

std::string_view FontFreeType::getGlyphCollection() const
{
    return {};
}

void FontFreeType::releaseFont(std::string_view /*fontName*/) {}

}  // namespace ax
