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

// A Tile2D world object placed on a map cell. Matches the editor's Tile2DView:
// image center = cellCenter + cellSize * pivot, image width = cellWidth * widthCells.
struct SpriteInstance {
    glm::ivec2 cell{0, 0};
    std::string assetUuid;
};

class SpriteRenderer {
public:
    // See LandscapeRenderer::init for the depthFormat contract.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Provide image for an assetUuid. widthCells — image width in map cells
    // (from asset index.json "image.width"); pivot — center offset in cell units.
    void ensureImage(const std::string& assetUuid, const std::filesystem::path& imagePath, float widthCells, const glm::vec2& pivot);

    void render(
        const std::vector<SpriteInstance>& sprites,
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

    struct SpriteGpu {
        TextureAtlas texture;      // plain image held as a 1x1 "atlas"
        glm::vec2 pivot{0.0f};     // cell units, same semantics as the editor
        float widthCells = 1.0f;   // image width in map cells
        float aspect = 1.0f;       // imageHeight / imageWidth
    };

    // Per-texture vertex range inside the single merged frame update
    // (sokol allows only one sg_update_buffer per buffer per frame).
    struct DrawGroup {
        const SpriteGpu* sprite;
        int baseVertex;
        int vertexCount;
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    sg_bindings bind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::unordered_map<std::string, SpriteGpu> sprites;

    std::vector<Vertex> scratchVerts;
    std::vector<DrawGroup> scratchDraws;

    void ensurePipeline();
    void destroyPipeline();
};

} // namespace render_core
