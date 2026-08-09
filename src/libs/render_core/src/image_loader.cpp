#include "render_core/image_loader.h"

#include <algorithm>
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

// Full RGBA8 mip chain via a 2x2 box filter (level 0 is the source, then
// halving with clamped edges down to 1x1). For standalone tiling textures
// (material maps): without mips a minified REPEAT texture on the ground
// plane aliases into noise. NEVER for atlases — the tiles would bleed into
// each other in the low levels.
std::vector<ImageRGBA8> buildMipChain(const ImageRGBA8& src) {
    std::vector<ImageRGBA8> levels;
    if (src.width <= 0 || src.height <= 0 || src.pixels.empty()) {
        return levels;
    }
    levels.push_back(src);
    for (;;) {
        const ImageRGBA8& prev = levels.back();
        if (prev.width <= 1 && prev.height <= 1) {
            break;
        }
        ImageRGBA8 next;
        next.width = std::max(1, prev.width / 2);
        next.height = std::max(1, prev.height / 2);
        next.pixels.resize(static_cast<size_t>(next.width) * static_cast<size_t>(next.height) * 4);
        for (int y = 0; y < next.height; ++y) {
            const int y0 = std::min(2 * y, prev.height - 1);
            const int y1 = std::min(2 * y + 1, prev.height - 1);
            for (int x = 0; x < next.width; ++x) {
                const int x0 = std::min(2 * x, prev.width - 1);
                const int x1 = std::min(2 * x + 1, prev.width - 1);
                for (int c = 0; c < 4; ++c) {
                    const int sum = static_cast<int>(prev.pixels[(static_cast<size_t>(y0) * prev.width + x0) * 4 + c]) +
                        static_cast<int>(prev.pixels[(static_cast<size_t>(y0) * prev.width + x1) * 4 + c]) +
                        static_cast<int>(prev.pixels[(static_cast<size_t>(y1) * prev.width + x0) * 4 + c]) +
                        static_cast<int>(prev.pixels[(static_cast<size_t>(y1) * prev.width + x1) * 4 + c]);
                    next.pixels[(static_cast<size_t>(y) * next.width + x) * 4 + c] =
                        static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(next));
    }
    return levels;
}

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

