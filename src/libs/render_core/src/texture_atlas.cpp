#include "render_core/texture_atlas.h"

#include "render_core/image_loader.h"

#include <stdexcept>

namespace render_core {
namespace fs = std::filesystem;

void TextureAtlas::createFromFile(const fs::path& atlasPath, int cols_, int rows_, sg_filter filter, sg_wrap wrap) {
    destroy();

    cols = (cols_ > 0) ? cols_ : 1;
    rows = (rows_ > 0) ? rows_ : 1;

    ImageRGBA8 img = loadImageRGBA8(atlasPath);
    width = img.width;
    height = img.height;

    sg_image_desc img_desc = {};
    img_desc.width = width;
    img_desc.height = height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0].ptr = img.pixels.data();
    img_desc.data.mip_levels[0].size = img.pixels.size();
    img_desc.label = "atlas-image";
    image = sg_make_image(&img_desc);

    if (image.id == SG_INVALID_ID) {
        throw std::runtime_error("sg_make_image failed for atlas");
    }

    sg_view_desc view_desc = {};
    view_desc.texture.image = image;
    view = sg_make_view(&view_desc);

    if (view.id == SG_INVALID_ID) {
        throw std::runtime_error("sg_make_view failed for atlas");
    }

    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = filter;
    smp_desc.mag_filter = filter;
    smp_desc.wrap_u = wrap;
    smp_desc.wrap_v = wrap;
    smp_desc.label = "atlas-sampler";
    sampler = sg_make_sampler(&smp_desc);
}

void TextureAtlas::destroy() {
    if (sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(sampler);
        sampler.id = SG_INVALID_ID;
    }
    if (view.id != SG_INVALID_ID) {
        sg_destroy_view(view);
        view.id = SG_INVALID_ID;
    }
    if (image.id != SG_INVALID_ID) {
        sg_destroy_image(image);
        image.id = SG_INVALID_ID;
    }
    width = height = 0;
    cols = rows = 1;
}

TileUV TextureAtlas::tileUv(std::size_t tileIndex) const {
    TileUV uv;
    if (cols <= 0 || rows <= 0) return uv;

    const std::size_t maxTiles = (std::size_t)cols * (std::size_t)rows;
    const std::size_t idx = (maxTiles > 0) ? (tileIndex % maxTiles) : 0;
    const int tx = (int)(idx % (std::size_t)cols);
    const int ty = (int)(idx / (std::size_t)cols);

    const float invCols = 1.0f / (float)cols;
    const float invRows = 1.0f / (float)rows;

    uv.uv0 = glm::vec2(tx * invCols, ty * invRows);
    uv.uv1 = glm::vec2((tx + 1) * invCols, (ty + 1) * invRows);
    return uv;
}

} // namespace render_core

