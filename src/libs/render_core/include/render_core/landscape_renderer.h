#pragma once

#include <array>
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

// Presentation params of a raised (3D) slice atlas: the top is lifted by
// `height` and cliff walls are generated around the land contour
// (TileShapePlayground port).
struct RaisedParams {
    float height = 32.0f;      // pixels the top is lifted (y offset)
    bool rockWalls = true;     // true: FastNoise rock walls; false: simple flat walls
    float amplitude = 0.28f;   // rock displacement amplitude, fraction of height
    float bevel = 0.3f;        // convex-corner bevel fraction
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

    // Provide a raised atlas + its presentation params for an assetUuid.
    // topTexturePath (optional): a tiled ground texture for the raised top —
    // when given, the top is drawn as mask-shaped ground-textured triangles
    // instead of the atlas tiles.
    void ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath = {});

    void render(
        const std::vector<LandscapeTile>& tiles,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight);

    // Raised landscape: per-cell cliff walls (rock or simple flat) plus the
    // same atlas tops as the flat pass, lifted by RaisedParams::height.
    // Painter order inside the pass: all walls, then the lifted tops.
    void renderRaised(
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

    // Untextured pos+color vertex for the wall pipeline.
    struct WallVertex {
        float pos[2];
        float color[4];
    };

    struct AtlasGpu {
        TextureAtlas atlas;
        float scale = 1.0f;      // scale from atlas tile pixels to world pixels
        glm::vec2 tileSize{0.0f}; // world pixels
    };

    struct RaisedAtlasGpu {
        AtlasGpu gpu;
        RaisedParams params;
        TextureAtlas topTex; // optional tiled ground texture for the raised top
    };

    // Per-texture vertex range inside the single merged frame update
    // (sokol allows only one sg_update_buffer per buffer per frame).
    struct DrawGroup {
        const TextureAtlas* texture;
        int baseVertex;
        int vertexCount;
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    sg_bindings bind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    sg_pipeline wallPip{SG_INVALID_ID};
    sg_shader wallShd{SG_INVALID_ID};
    sg_buffer raisedVbuf{SG_INVALID_ID};
    sg_buffer wallVbuf{SG_INVALID_ID};
    sg_bindings raisedBind{};
    sg_bindings wallBind{};

    std::unordered_map<std::string, AtlasGpu> atlases;
    std::unordered_map<std::string, RaisedAtlasGpu> raisedAtlases;

    std::vector<Vertex> scratchVerts;
    std::vector<DrawGroup> scratchDraws;
    std::vector<Vertex> scratchRaisedVerts;
    std::vector<DrawGroup> scratchRaisedDraws;
    std::vector<WallVertex> scratchWallVerts;

    static AtlasGpu createAtlasGpu(const std::filesystem::path& atlasPath, int cols, int rows);

    // One atlas tile quad in screen space; yOffset shifts the quad in world
    // pixels before the camera transform (raised tops pass -height).
    void appendAtlasQuad(
        std::vector<Vertex>& out,
        const AtlasGpu& atlas,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        const glm::ivec2& cell,
        std::size_t tileIndex,
        float yOffset) const;

    // Raised top as mask-shaped triangles textured by a tiled ground texture
    // (world-space UV, so the pattern is continuous across cells). The shape
    // follows the same "axis-parallel corner" contour rule as the cliff walls:
    // full quadrant for an edge with both nodes on, half-quadrant at the on
    // corner of a transition edge. yOffset lifts the top in world pixels.
    void appendTexturedTop(
        std::vector<Vertex>& out,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        const glm::ivec2& cell,
        const std::array<bool, 4>& mask,
        float yOffset) const;

    void ensurePipeline();
    void destroyPipeline();
    void ensureWallPipeline();
    void destroyWallPipeline();
};

} // namespace render_core
