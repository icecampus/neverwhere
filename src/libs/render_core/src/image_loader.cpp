#include "render_core/image_loader.h"

#include <cstring>
#include <queue>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace render_core {
namespace fs = std::filesystem;

namespace {

// Straight-alpha textures authored with a matte (e.g. white RGB in the fully
// transparent texels — common in exported sprite PNGs) bleed that matte into
// the artwork under LINEAR filtering: a pale halo around sprites, worse at
// minification. Solidify: every fully transparent texel's RGB is filled from
// the nearest visible texel (multi-source BFS, 8-connected). Alpha is left
// untouched, so blending semantics do not change.
void bleedTransparentRGB(ImageRGBA8& img) {
    const int w = img.width;
    const int h = img.height;
    const std::uint32_t n = static_cast<std::uint32_t>(w) * static_cast<std::uint32_t>(h);

    std::vector<std::int32_t> dist(n, -1);
    std::queue<std::uint32_t> frontier;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (img.pixels[i * 4 + 3] > 0) {
            dist[i] = 0;
            frontier.push(i);
        }
    }

    static const int kNb[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    while (!frontier.empty()) {
        const std::uint32_t cur = frontier.front();
        frontier.pop();
        const int x = static_cast<int>(cur % w);
        const int y = static_cast<int>(cur / w);
        for (const auto& d : kNb) {
            const int nx = x + d[0];
            const int ny = y + d[1];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            const std::uint32_t ni = static_cast<std::uint32_t>(ny) * w + nx;
            if (dist[ni] >= 0) continue;
            dist[ni] = dist[cur] + 1;
            img.pixels[ni * 4 + 0] = img.pixels[cur * 4 + 0];
            img.pixels[ni * 4 + 1] = img.pixels[cur * 4 + 1];
            img.pixels[ni * 4 + 2] = img.pixels[cur * 4 + 2];
            frontier.push(ni);
        }
    }
}

} // namespace

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
    bleedTransparentRGB(img);
    return img;
}

bool writeRgbaPng(const std::filesystem::path& path, int width, int height, const std::uint8_t* pixels, int strideBytes) {
    if (!pixels || width <= 0 || height <= 0 || strideBytes <= 0) {
        return false;
    }
    return stbi_write_png(path.string().c_str(), width, height, 4, pixels, strideBytes) != 0;
}

} // namespace render_core

