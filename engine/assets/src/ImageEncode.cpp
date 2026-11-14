#include "rolo/assets/ImageEncode.hpp"

// stb_image_write, compiled into this file only and with every function
// `static`, so that it cannot clash with another copy in a game's module.
// Only its PNG writer is used, and only to memory: Rolo writes the file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>

#include <cstdlib>
#include <fstream>

namespace rolo {

std::vector<std::uint8_t> encodePng(const std::uint8_t* pixels, int width, int height, int channels)
{
    if (pixels == nullptr || width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
        return {};
    }
    int size = 0;
    unsigned char* bytes = stbi_write_png_to_mem(pixels, width * channels, width, height, channels, &size);
    if (bytes == nullptr || size <= 0) {
        std::free(bytes);
        return {};
    }
    std::vector<std::uint8_t> png(bytes, bytes + size);
    std::free(bytes);   // stb_image_write allocates with malloc
    return png;
}

bool writePngFile(const std::filesystem::path& path, const std::uint8_t* pixels, int width, int height, int channels,
                  std::string* error)
{
    const auto fail = [error](const char* why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    const std::vector<std::uint8_t> png = encodePng(pixels, width, height, channels);
    if (png.empty()) {
        return fail("there is no picture to write");
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail("the file could not be opened for writing");
    }
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    out.flush();
    return out.good() ? true : fail("the file could not be written (is the disk full?)");
}

} // namespace rolo
