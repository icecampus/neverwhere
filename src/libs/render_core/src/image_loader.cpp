#include "render_core/image_loader.h"

#include <cstring>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace render_core {
namespace fs = std::filesystem;

ImageRGBA8 loadImageRGBA8(const fs::path& path) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = stbi_load(path.string().c_str(), &w, &h, &comp, 4);
    if (!data || w <= 0 || h <= 0) {
        throw std::runtime_error("Failed to load image: " + path.string());
    }

    ImageRGBA8 img;
    img.width = w;
    img.height = h;
    img.pixels.resize((size_t)w * (size_t)h * 4);
    std::memcpy(img.pixels.data(), data, img.pixels.size());
    stbi_image_free(data);
    return img;
}

bool writeRgbaPng(const std::filesystem::path& path, int width, int height, const std::uint8_t* pixels, int strideBytes) {
    if (!pixels || width <= 0 || height <= 0 || strideBytes <= 0) {
        return false;
    }
    return stbi_write_png(path.string().c_str(), width, height, 4, pixels, strideBytes) != 0;
}

} // namespace render_core

