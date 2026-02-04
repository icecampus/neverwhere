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

} // namespace render_core

