#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_gfx.h>

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>
#include <stone_gen/stone_field.h>
#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "LandBrush.h"

enum class AtlasKind : int {
    Grass = 0,
    Flat = 1,
};

struct ColorVertex {
    float x, y;
    float r, g, b, a;
};

struct CliffFsParams; // defined below; PaintLayerView only points at it

// Cliff pass (scalar-field surface nets): screen position + baked depth,
// field-space normal, groove carve attribute, the world position (map
// cells, height) and the wall-proximity rim weight for per-pixel shading.
struct CliffVertex {
    float x, y, z;
    float nx, ny, nz;
    float groove;
    float wx, wy, wz;
    float rim;
};

// One paint layer on the shared canvas: its own node grid and either flat
// (2D atlas tiles) or scalar-field surface nets from the same nodes
// (z-buffered): cliff (omphalos grooves) or stone (StoneCube
// voronoi stones).
struct PaintLayerView {
    const LandBrush* brush = nullptr;
    AtlasKind atlas = AtlasKind::Grass;
    bool cliff = false;
    const cliff::FieldParams* cliffParams = nullptr; // used when cliff == true
    float cliffHeightScale = 96.0f;                  // field px per 1.0 world height
    bool stone = false;
    const stone_gen::StoneFieldParams* stoneParams = nullptr; // used when stone == true
    const CliffFsParams* shadingOverride = nullptr;           // per-layer palette (stone)
    // Flat tiles only: draw the layer with a tiling texture instead of the
    // atlas color. The texture is sampled continuously in world (field)
    // coordinates — no per-tile cuts, so any tiling texture stays seamless —
    // while the atlas tile keeps working as the alpha mask only.
    int tilingTex = -1;          // tiling array layer, -1 = off
    float tilingRepeats = 1.0f;  // texture repeats per cell width
    // Multi-texture layer: each cell is a diamond fan (center + the 4 corner
    // nodes) whose vertices carry one-hot weights over the cell's candidate
    // textures (LandBrush::cellTextureBlend), so neighboring textures blend
    // smoothly across the shared nodes. Overrides tilingTex; tilingRepeats
    // and the blend settings stay shared by the whole layer.
    bool multiTexture = false;
    float blendSharpness = 1.0f;  // weight exponent: > 1 narrows the blend band
    float blendNoise = 0.0f;      // organic edge wobble strength (0 = straight)
    float blendNoiseScale = 4.0f; // noise repeats per cell width
    float edgeFade = 0.2f;        // soft edge into empty space: feather width
                                  // around the fill = 0.5 iso (0 = hard cut)
};

// Fragment-shader uniforms of the cliff pass (palette/light, 16-byte blocks;
// mirrors CliffFieldPlayground's FsParams with cam_pos replaced by the
// constant iso view direction).
struct CliffFsParams {
    float lightDir[4];   // xyz: direction towards the sun
    float viewDir[4];    // xyz: constant iso view direction (scene -> viewer)
    float darkColor[4];  // rgb: groove floors
    float goldColor[4];  // rgb: outer shell
    float grassA[4];     // rgb
    float grassB[4];     // rgb
    float params0[4];    // vein threshold, ambient, diffuse, spec strength
    float params1[4];    // spec power, gamma, wrap backlight, boulder plane Y
                         // (field units; > 0: rim-stitch shading on)
    float params2[4];    // grass->stone fade depth below the top plane,
                         // rim gradient strength (0..1),
                         // top texture strength (0..1), top texture tiling
                         // (tiles per world unit)
};

// Per-layer cliff cache status for the UI (see AtlasRenderer::cliffStatsFor).
struct CliffStats {
    bool watertight = true;
    bool pending = false;    // edits happened, waiting out the debounce
    double rebuildMs = 0.0;
    int voxelCount = 0;
    int vertexCount = 0;
    int triangleCount = 0;
};

class AtlasRenderer {
public:
    // VS uniforms: camera only (20 bytes; the GL backend validates that the
    // declared uniforms sum exactly to the block size).
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
    };

    // FS uniforms of the flat-tile pass (32 bytes): tex_params x = color mode
    // (0 = atlas color, 1 = world-space tiling array), y = tiling scale
    // (repeats per field unit), z/w = unused; blend_params x = weight
    // sharpness, y = edge noise strength, z = noise scale (per field unit),
    // w = empty-edge feather width around the fill = 0.5 iso (0 = hard cut).
    struct TexFsParams {
        float values[4];
        float blend[4];
    };

    void init();
    void shutdown();

    bool loadAtlasFromFile(AtlasKind kind, const std::string& path, int cols = 4, int rows = 6);
    bool loadAtlasFromRgba(
        AtlasKind kind,
        const std::uint8_t* rgba,
        int width,
        int height,
        int cols = 4,
        int rows = 6);
    // Tiling texture for the cliff/stone flat tops (world-space UV on the
    // top plane; strength/tiling go through CliffFsParams.params2.zw).
    // Until a file is loaded a 1x1 white placeholder is bound.
    bool loadTopTextureFromFile(const std::string& path);

    // Tiling texture array for flat layers (PaintLayerView::tilingTex /
    // multiTexture): every file becomes one array layer (its index = the
    // layer the LandBrush tags point at), sampled in world coordinates under
    // the atlas-tile alpha mask. Files are resampled to a shared size (array
    // slices must match). outLayers (optional) reports per-file the array
    // layer or -1 when the file failed to load. Returns the layer count.
    int buildTilingTextureArray(const std::vector<std::string>& paths, std::vector<int>* outLayers = nullptr);

    void render(
        const PaintLayerView* layers,
        int layerCount,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        glm::ivec2 hoverNode,
        bool hasHover,
        const CliffFsParams* cliffShading,
        double nowSec);

    // Status of the cliff cache attached to the given brush (empty stats when
    // the layer never rendered). UI reads this for the rebuild/watertight info.
    const CliffStats& cliffStatsFor(const LandBrush* brush) const;

private:
    struct AtlasSlot {
        sg_image image{};
        sg_view view{};
    };

    struct TexVertex {
        float x, y;
        float u, v;
        // Tiling array blend: candidate layer indices (constant per cell —
        // interpolation must not mix them) and their weights (vary per
        // vertex). Pure cells are simply (1,0,0,0) over one layer.
        float layers[4];
        float weights[4];
        // Fill weight (1 at on-nodes, 0 at off-nodes, interpolated): drives
        // the soft coverage fade into empty space. Constant 1 for quads.
        float fill;
    };

    // Cached scalar-field derivative of a brush: the extracted surface-nets
    // mesh plus the projected vertex stream, rebuilt only when the brush
    // version or the field params change (debounced) — the full rebuild
    // costs seconds. Holds either cliff or stone params, per the layer kind.
    struct CliffCache {
        const LandBrush* brush = nullptr;
        std::uint64_t brushVersion = 0;
        bool stone = false;
        cliff::FieldParams params{};
        stone_gen::StoneFieldParams stoneParams{};
        float heightScale = 0.0f;
        bool contentValid = false;
        double lastEditSec = 0.0;
        bool meshDirty = false;   // field + extraction needed (heavy)
        bool streamDirty = false; // re-projection only (cheap, heightScale edits)
        bool gpuDirty = false;
        cliff::Mesh mesh;
        glm::ivec2 origin{0, 0};
        std::vector<CliffVertex> stream;
        sg_buffer vbuf{};
        size_t vbufSize = 0;
        CliffStats stats;
    };

    CliffCache& cliffCacheFor(const LandBrush* brush);
    void rebuildCliffCache(
        CliffCache& cache,
        const topology_core::DiamondIsometry& iso,
        float zFar,
        float zScale);

    void ensurePipelines();
    void destroyPipelines();
    void destroySlot(AtlasSlot& slot);
    bool uploadSlot(
        AtlasSlot& slot,
        const void* rgba,
        int width,
        int height,
        const char* label);
    glm::vec4 atlasUvRect(int tileIndex) const;
    void appendTileQuad(
        std::vector<TexVertex>& out,
        const topology_core::DiamondIsometry& iso,
        glm::ivec2 cell,
        int tileIndex,
        float yOffset = 0.0f,
        float baseLayer = 0.0f);
    // Diamond fan of a multi-texture cell: center + the 4 corner node
    // positions (corner order Left/Up/Right/Down), 4 triangles. The center
    // weight is the corner average; the UVs reproduce the square-frame
    // mapping of appendTileQuad, so the atlas mask works unchanged.
    void appendTileFan(
        std::vector<TexVertex>& out,
        const topology_core::DiamondIsometry& iso,
        glm::ivec2 cell,
        int tileIndex,
        const float layers[4],
        const glm::vec4 cornerWeights[4],
        const float cornerFill[4]);
    void appendDiamondOutline(
        std::vector<ColorVertex>& out,
        const topology_core::DiamondIsometry& iso,
        glm::ivec2 cell,
        glm::vec4 color);
    // Solid diamond of a cell (2 triangles) for the hover footprint tint.
    void appendDiamondFill(
        std::vector<ColorVertex>& out,
        const topology_core::DiamondIsometry& iso,
        glm::ivec2 cell,
        glm::vec4 color);
    void appendNodeMarker(
        std::vector<ColorVertex>& out,
        const topology_core::DiamondIsometry& iso,
        glm::ivec2 node,
        glm::vec4 color);

    sg_pipeline m_texPip{};
    sg_pipeline m_colorPip{};
    sg_pipeline m_colorTriPip{};
    sg_pipeline m_cliffPip{};
    sg_shader m_texShd{};
    sg_shader m_colorShd{};
    sg_shader m_cliffShd{};
    sg_buffer m_texVbuf{};
    sg_buffer m_colorVbuf{};
    sg_sampler m_sampler{};
    // Cliff/stone top texture (tiling, REPEAT sampler): placeholder 1x1 white
    // until loadTopTextureFromFile succeeds.
    sg_image m_topTexImage{};
    sg_view m_topTexView{};
    sg_sampler m_topTexSampler{};

    std::vector<CliffCache> m_cliffCaches;

    AtlasSlot m_slots[2]{};
    // Tiling texture array for flat layers (world-space UV under the atlas
    // mask); sampled with the REPEAT m_topTexSampler. Placeholder 1x1x1
    // white until buildTilingTextureArray succeeds.
    AtlasSlot m_tilingArray{};
    int m_tilingArrayLayers = 0;

    int m_atlasCols = 4;
    int m_atlasRows = 6;
    bool m_ready = false;
};
