#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"
#include "render_core/texture_atlas.h"

#include "topology_core/camera2d.h"
#include "topology_core/diamond_isometry.h"

namespace render_core {

struct LandscapeTile {
    glm::ivec2 cell{0, 0};
    std::string assetUuid;
    std::size_t tileIndex = 0;
};

class LandscapeRenderer {
public:
    // depthFormat must match the pass the renderer draws into:
    // SG_PIXELFORMAT_DEPTH_STENCIL for the sokol_app swapchain (game client),
    // SG_PIXELFORMAT_NONE for a Qt FBO without a wrapped depth attachment (editor).
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Provide atlas for an assetUuid (lazy GPU upload can also be done by caller).
    void ensureAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows);

    void render(
        const std::vector<LandscapeTile>& tiles,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight);

private:
    struct Vertex {
        float pos[2];
        float uv[2];
        float color[4];
    };

    struct AtlasGpu {
        TextureAtlas atlas;
        float scale = 1.0f;      // scale from atlas tile pixels to world pixels
        glm::vec2 tileSize{0.0f}; // world pixels
    };

    // Per-texture vertex range inside the single merged frame update
    // (sokol allows only one sg_update_buffer per buffer per frame).
    struct DrawGroup {
        const AtlasGpu* atlas;
        int baseVertex;
        int vertexCount;
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    sg_bindings bind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::unordered_map<std::string, AtlasGpu> atlases;

    std::vector<Vertex> scratchVerts;
    std::vector<DrawGroup> scratchDraws;

    void ensurePipeline();
    void destroyPipeline();
};

} // namespace render_core

