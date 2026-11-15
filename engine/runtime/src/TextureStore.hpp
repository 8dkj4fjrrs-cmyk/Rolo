#pragma once

// TextureStore: images loaded from files as GPU textures, shared by the scene
// renderer (sprites, tiles) and the UI layer (thumbnails).
//
// A texture is loaded the first time it is asked for, and again when its file
// changes on disk (checked on each request), so an image edited in another
// program updates in the editor. Textures stay loaded until clear().
//
// Internal to rolo_runtime.

#include "rolo/core/Log.hpp"

#include <axmol.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace rolo::detail {

class TextureStore {
public:
    explicit TextureStore(const Logger& log);
    ~TextureStore();

    TextureStore(const TextureStore&) = delete;
    TextureStore& operator=(const TextureStore&) = delete;

    /// The texture for an image file, or null if it cannot be loaded (the
    /// problem is logged once). `pixelArt` keeps hard pixel edges when scaled.
    ax::Texture2D* get(const std::filesystem::path& path, bool pixelArt);

    /// Releases every texture.
    void clear();

    /// Fills every texture from its file again. For when the graphics
    /// context was lost (Android may take it from an app in the
    /// background): the textures are still there, but empty.
    void reloadAll();

    /// Goes up whenever a texture is loaded again or its filter changes, so
    /// users holding texture pointers know to fetch them again.
    std::uint64_t generation() const noexcept { return m_generation; }

private:
    struct Entry {
        ax::Texture2D* texture = nullptr;   // retained; null if loading failed
        std::filesystem::file_time_type stamp;
        bool pixelArt = false;
    };

    bool fill(ax::Texture2D& texture, const std::filesystem::path& path, bool pixelArt);

    const Logger& m_log;
    std::unordered_map<std::string, Entry> m_entries;
    std::uint64_t m_generation = 1;
};

} // namespace rolo::detail
