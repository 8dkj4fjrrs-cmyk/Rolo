#include "TextSprites.hpp"

#include "rolo/ui/EmbeddedFonts.hpp"

#include <fstream>
#include <iterator>
#include <span>
#include <unordered_map>
#include <vector>

namespace rolo::detail {

namespace {

// Font files read so far (kept for the program's life: games use a handful).
std::span<const unsigned char> fontBytes(const std::string& file)
{
    if (file.empty()) {
        return ui::fonts::interRegular();
    }
    static std::unordered_map<std::string, std::vector<unsigned char>> files;
    auto it = files.find(file);
    if (it == files.end()) {
        std::ifstream in(file, std::ios::binary);
        it = files.emplace(file, std::vector<unsigned char>((std::istreambuf_iterator<char>(in)),
                                                            std::istreambuf_iterator<char>()))
                 .first;
    }
    return it->second;
}

} // namespace

TextSprite makeTextSprite(const std::string& fontFile, std::string_view text, float pixelSize, const Color& color,
                          const TextSpriteOptions& options)
{
    ui::TextImageOptions drawing;
    drawing.pixelSize = pixelSize;
    drawing.wrapWidth = options.wrapWidth;
    drawing.align = options.align;
    auto drawn = ui::drawTextImage(fontBytes(fontFile), text, drawing);
    if (!drawn && !fontFile.empty()) {
        drawn = ui::drawTextImage(ui::fonts::interRegular(), text, drawing);   // an unusable font file
    }
    TextSprite result;
    if (!drawn) {
        return result;
    }
    // White letters, their coverage as alpha (colours already multiplied by
    // it, as the renderer wants); the text's colour is the sprite's tint.
    std::vector<std::uint8_t> rgba(drawn->coverage.size() * 4);
    for (std::size_t i = 0; i < drawn->coverage.size(); ++i) {
        const std::uint8_t c = drawn->coverage[i];
        rgba[i * 4] = c;
        rgba[i * 4 + 1] = c;
        rgba[i * 4 + 2] = c;
        rgba[i * 4 + 3] = c;
    }
    auto* image = new ax::Image();
    auto* texture = new ax::Texture2D();
    if (image->initWithRawData(rgba.data(), static_cast<ssize_t>(rgba.size()), drawn->width, drawn->height, 8, true)
        && texture->initWithImage(image)) {
        texture->setAntiAliasTexParameters();
#if AX_ENABLE_CACHE_TEXTURE_DATA
        // Where the graphics context can be lost (Android, when the app is in
        // the background), Axmol keeps the picture and fills the texture
        // from it again. It does that for textures it loads itself; this
        // one it has to be told about. (Images from files are read again
        // instead: TextureStore::reloadAll.)
        ax::VolatileTextureMgr::addImage(texture, image);
#endif
        auto* sprite = ax::Sprite::createWithTexture(texture);
        // The text's box is the picture without its margin.
        const auto pad = static_cast<float>(drawn->padding);
        const auto w = static_cast<float>(drawn->width);
        const auto h = static_cast<float>(drawn->height);
        result.width = w - pad * 2.0f;
        result.height = h - pad * 2.0f;
        result.lineHeight = drawn->lineHeight;
        sprite->setAnchorPoint(ax::Vec2((pad + result.width * options.anchor.x) / w,
                                        (pad + result.height * options.anchor.y) / h));
        sprite->setColor(ax::Color3B(color.r, color.g, color.b));
        sprite->setOpacity(color.a);
        result.sprite = sprite;
    }
    texture->release();   // the sprite keeps it
    image->release();
    return result;
}

float textWidth(const std::string& fontFile, std::string_view text, float pixelSize)
{
    const float width = ui::measureTextLine(fontBytes(fontFile), text, pixelSize);
    return width > 0.0f || fontFile.empty() ? width : ui::measureTextLine(ui::fonts::interRegular(), text, pixelSize);
}

} // namespace rolo::detail
