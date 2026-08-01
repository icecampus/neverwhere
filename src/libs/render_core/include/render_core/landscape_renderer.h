#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

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

// Contact-shadow field: per vertex-node darkening (0..1) spilled by nearby
// cliffs. Built from the cliff tiles (same vertex-node lattice), sampled by
// the base landscape pass at quad corners — the cliff foot's bottom blend
// continues onto the underlay, stitching the transition.
struct CliffShadowField {
    std::unordered_map<std::uint64_t, float> nodeDarken; // nodeKey -> 0..1
    bool empty() const { return nodeDarken.empty(); }
};

// Presentation params of a raised (3D) slice atlas: the top is lifted by
// `height` and cliff walls are generated around the land contour
// (SDFGeneratedLandscape port).
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
    // wallTexturePath (optional): a tiling rock texture for the triplanar
    // walls (world-space projections, no UV seams) multiplied by the baked
    // shade; when empty, walls keep the plain baked-color look.
    void ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath = {}, const std::filesystem::path& wallTexturePath = {});

    void render(
        const std::vector<LandscapeTile>& tiles,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        const CliffShadowField* cliffShadow = nullptr);

    // Raised landscape: per-cell cliff walls (rock or simple flat) plus the
    // lifted tops. With a depth attachment (depthFormat != NONE at init) the
    // pass runs through the z-buffer pipelines: vertices carry
    // highground::Vertex::groundY as z and internal overlaps resolve via the
    // depth buffer (batches merge by texture only). Without depth it falls
    // back to the CPU painter's algorithm: walls and tops depth-sorted
    // together per primitive (iso painter's algorithm by ground y; walls
    // before tops at equal depth) — a plain "all walls, then all tops" order
    // breaks on shapes where one arm of the landmass stands in front of
    // another arm's walls.
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

    // Z-buffered raised pass: pos.xy is screen space, pos.z is the raw
    // ground y (un-lifted field y); the vertex shader normalizes it into
    // clip z via the z_range uniform.
    struct DepthVertex {
        float pos[3];
        float uv[2];
        float color[4];
    };

    struct DepthWallVertex {
        float pos[3];
        float color[4];
    };

    // Triplanar walls: tcoord = undeformed map coordinates in world px + lift;
    // the fragment shader blends the (mapY,lift) and (mapX,lift) projections
    // of a tiling rock texture by the contour normal — no UVs, no seams.
    struct TriWallVertex {
        float pos[2];
        float color[4];
        float tcoord[3];
        float normal[2];
    };

    struct TriDepthWallVertex {
        float pos[3];
        float color[4];
        float tcoord[3];
        float normal[2];
    };

    struct AtlasGpu {
        TextureAtlas atlas;
        float scale = 1.0f;      // scale from atlas tile pixels to world pixels
        glm::vec2 tileSize{0.0f}; // world pixels
    };

    struct RaisedAtlasGpu {
        AtlasGpu gpu;
        RaisedParams params;
        TextureAtlas topTex;  // optional tiled ground texture for the raised top
        TextureAtlas wallTex; // optional tiling rock texture for the triplanar walls
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

    // Z-buffer variants for the raised pass (created only when init got a
    // real depth format; the editor's depth-less Qt FBO stays on painter).
    // The depth vertex streams reuse raisedVbuf/wallVbuf (byte buffers).
    sg_pipeline depthPip{SG_INVALID_ID};
    sg_shader depthTexShd{SG_INVALID_ID};
    sg_pipeline depthWallPip{SG_INVALID_ID};
    sg_shader depthWallShd{SG_INVALID_ID};

    // Triplanar wall pipelines (painter + z-buffer variant). The triplanar
    // vertex streams share triWallVbuf (painter/depth modes are mutually
    // exclusive per renderer instance).
    sg_pipeline triWallPip{SG_INVALID_ID};
    sg_shader triWallShd{SG_INVALID_ID};
    sg_pipeline triDepthWallPip{SG_INVALID_ID};
    sg_shader triDepthWallShd{SG_INVALID_ID};
    sg_buffer triWallVbuf{SG_INVALID_ID};

    std::unordered_map<std::string, AtlasGpu> atlases;
    std::unordered_map<std::string, RaisedAtlasGpu> raisedAtlases;

    std::vector<Vertex> scratchVerts;
    std::vector<DrawGroup> scratchDraws;
    std::vector<Vertex> scratchRaisedVerts;
    std::vector<WallVertex> scratchWallVerts;
    std::vector<DepthVertex> scratchDepthVerts;
    std::vector<DepthWallVertex> scratchDepthWallVerts;
    std::vector<TriWallVertex> scratchTriWallVerts;
    std::vector<TriDepthWallVertex> scratchTriDepthWallVerts;

    static AtlasGpu createAtlasGpu(const std::filesystem::path& atlasPath, int cols, int rows);

    // One atlas tile quad in FIELD space (6 verts); yOffset shifts the quad in
    // world pixels (raised tops pass -height). Screen transform happens at
    // batch emission after the depth sort. cliffShadow (base ground pass
    // only): per-corner darkening from the cliff contact-shadow field.
    void appendAtlasQuad(
        std::vector<Vertex>& out,
        const AtlasGpu& atlas,
        const topology_core::DiamondIsometry& iso,
        const glm::ivec2& cell,
        std::size_t tileIndex,
        float yOffset,
        const CliffShadowField* cliffShadow = nullptr) const;

    void ensurePipeline();
    void destroyPipeline();
    void ensureWallPipeline();
    void destroyWallPipeline();
    void ensureDepthPipelines();
    void destroyDepthPipelines();
    void ensureTriWallPipelines();
    void destroyTriWallPipelines();
};

} // namespace render_core
