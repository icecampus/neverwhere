#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace render_core {

struct ImageRGBA8 {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // RGBA8, size = width*height*4
};

ImageRGBA8 loadImageRGBA8(const std::filesystem::path& path);
bool writeRgbaPng(const std::filesystem::path& path, int width, int height, const std::uint8_t* pixels, int strideBytes);

// Full RGBA8 mip chain via a 2x2 box filter (level 0 is the source, then
// halving with clamped edges down to 1x1). For standalone tiling textures
// (material maps): without mips a minified REPEAT texture on the ground
// plane aliases into noise. NEVER for atlases — the tiles would bleed into
// each other in the low levels.
std::vector<ImageRGBA8> buildMipChain(const ImageRGBA8& src);

} // namespace render_core

