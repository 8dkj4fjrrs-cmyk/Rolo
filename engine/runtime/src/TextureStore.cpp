#include "TextureStore.hpp"

#include "rolo/assets/ImageDecode.hpp"

namespace rolo::detail {

namespace {
constexpr std::string_view kCategory = "runtime";

void applyFilter(ax::Texture2D& texture, bool pixelArt)
{
    if (pixelArt) {
        texture.setAliasTexParameters();     // nearest pixel: crisp pixel art
    } else {
        texture.setAntiAliasTexParameters(); // linear: smooth scaling
    }
}
} // namespace

// Reads an image file into `texture` (new, or one whose picture was lost).
// Rolo decodes the file itself (rolo/assets/ImageDecode.hpp): Axmol is built
// without JPEG and PNG libraries (ADR 0023, ADR 0024). See-through images
// are handed over with their colours already multiplied by alpha, which is
// what Axmol's own PNG loading did.
bool TextureStore::fill(ax::Texture2D& texture, const std::filesystem::path& path, bool pixelArt)
{
    std::string why;
    auto decoded = decodeImageFile(path, &why);
    if (!decoded) {
        m_log.warning(kCategory, path.string() + ": " + why + ".");
        return false;
    }
    if (decoded->hasAlpha) {
        premultiplyAlpha(*decoded);
    }
    auto* image = new ax::Image();
    const bool filled = image->initWithRawData(decoded->rgba.data(), static_cast<ssize_t>(decoded->rgba.size()),
                                               decoded->width, decoded->height, 8, decoded->hasAlpha)
                        && texture.initWithImage(image);
    image->release();
    if (filled) {
        applyFilter(texture, pixelArt);
    }
    return filled;
}

void TextureStore::reloadAll()
{
    // The same texture objects are filled again, so the sprites that use
    // them need not be told.
    for (auto& [path, entry] : m_entries) {
        if (entry.texture != nullptr && !fill(*entry.texture, path, entry.pixelArt)) {
            m_log.warning(kCategory, "Could not load the image " + path + " again.");
        }
    }
}

TextureStore::TextureStore(const Logger& log)
    : m_log(log)
{
}

TextureStore::~TextureStore()
{
    clear();
}

void TextureStore::clear()
{
    for (auto& [path, entry] : m_entries) {
        AX_SAFE_RELEASE_NULL(entry.texture);
    }
    m_entries.clear();
    ++m_generation;
}

ax::Texture2D* TextureStore::get(const std::filesystem::path& path, bool pixelArt)
{
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    const std::string key = path.string();
    auto it = m_entries.find(key);
    if (it != m_entries.end() && !ec && it->second.stamp == stamp) {
        Entry& entry = it->second;
        if (entry.texture != nullptr && entry.pixelArt != pixelArt) {
            applyFilter(*entry.texture, pixelArt);
            entry.pixelArt = pixelArt;
            ++m_generation;
        }
        return entry.texture;
    }

    // New, changed or vanished file: (re)load it.
    Entry entry;
    entry.stamp = stamp;
    entry.pixelArt = pixelArt;
    if (!ec) {
        auto* texture = new ax::Texture2D();
        if (fill(*texture, path, pixelArt)) {
            entry.texture = texture; // keeps the reference from `new`
        } else {
            texture->release();
        }
    }
    if (entry.texture == nullptr) {
        m_log.warning(kCategory, "Could not load the image " + key + ".");
    }
    if (it != m_entries.end()) {
        AX_SAFE_RELEASE_NULL(it->second.texture);
        it->second = entry;
    } else {
        m_entries.emplace(key, entry);
    }
    ++m_generation;
    return entry.texture;
}

} // namespace rolo::detail
