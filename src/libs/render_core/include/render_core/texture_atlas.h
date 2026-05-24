#pragma once

#include <filesystem>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

namespace render_core {

struct TileUV {
    glm::vec2 uv0{0.0f, 0.0f};
    glm::vec2 uv1{1.0f, 1.0f};
};

class TextureAtlas {
public:
    bool valid() const { return image.id != SG_INVALID_ID && view.id != SG_INVALID_ID; }

    void createFromFile(const std::filesystem::path& atlasPath, int cols, int rows);
    void destroy();

    TileUV tileUv(std::size_t tileIndex) const;

    sg_image sgImage() const { return image; }
    sg_view sgView() const { return view; }
    sg_sampler sgSampler() const { return sampler; }

    int atlasWidth() const { return width; }
    int atlasHeight() const { return height; }
    int tileWidth() const { return (cols > 0) ? (width / cols) : 0; }
    int tileHeight() const { return (rows > 0) ? (height / rows) : 0; }

    int tileCols() const { return cols; }
    int tileRows() const { return rows; }

private:
    sg_image image{SG_INVALID_ID};
    sg_view view{SG_INVALID_ID};
    sg_sampler sampler{SG_INVALID_ID};
    int width = 0;
    int height = 0;
    int cols = 1;
    int rows = 1;
};

} // namespace render_core

