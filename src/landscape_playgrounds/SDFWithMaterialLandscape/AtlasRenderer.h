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

#include <highground_core/surface_nets.h>
#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "LandBrush.h"
#include "MaskField.h"

enum class AtlasKind : int {
    Grass = 0,
    Flat = 1,
    FlatGreen = 2,
};

// Overlay vertex (grid lines, hover fill/marker): field position + the ground
// plane depth of its own field row (same anchor as the mesh streams — see
// bakedDepth), so the overlay shares the z-buffer with the 3D meshes: raised
// geometry draws over it, submerged geometry under it, plus flat RGBA.
struct ColorVertex {
    float x, y, z;
    float r, g, b, a;
};

struct CliffFsParams; // defined below; PaintLayerView only points at it

// Cliff pass (scalar-field surface nets): screen position + baked depth,
// field-space normal, groove carve attribute, the world position (map
// cells, height) and the wall-proximity rim weight for per-pixel shading.
// (Mask layers carry no groove/rim — both stay 0; the format is kept to
// share the vertex layout with the other field playgrounds.)
struct CliffVertex {
    float x, y, z;
    float nx, ny, nz;
    float groove;
    float wx, wy, wz;
    float rim;
};

// One paint layer on the shared canvas: its own node grid, the flat
// (2D atlas tiles) silhouette underlay and the scalar-field surface nets
// from the same nodes (z-buffered). This fork keeps only the mask layer
// kind (maskfield::MaskField: the Texture 2D mask silhouette extruded into
// a plate with a sloped skirt, standing half-height below the grid).
struct PaintLayerView {
    const LandBrush* brush = nullptr;
    AtlasKind atlas = AtlasKind::Grass;
    float cliffHeightScale = 96.0f;                       // field px per 1.0 world height
    bool mask = false;
    const maskfield::MaskFieldParams* maskParams = nullptr; // used when mask == true
    const CliffFsParams* shadingOverride = nullptr;         // per-layer palette
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
    float params1[4];    // spec power, gamma, wrap backlight, unused
    float params2[4];    // material tiling (repeats per world unit; the
                         // Ground061 maps are 2:1, V tiles 2x faster),
                         // albedo strength (0 = procedural palette),
                         // normal-map strength, AO strength
    float params3[4];    // roughness strength, spare x3
};

// Material map slots of the cliff pass (the ambientCG Ground061 set),
// bound as texture views 0..3 with the shared REPEAT sampler.
enum MatMapIndex : int {
    kMatColor = 0,
    kMatNormal = 1,
    kMatAo = 2,
    kMatRough = 3,
    kMatCount = 4,
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
    // Loads an ambientCG material set (`<setName>_Color.jpg`,
    // `<setName>_NormalGL.jpg`, `<setName>_AmbientOcclusion.jpg`,
    // `<setName>_Roughness.jpg`) from a directory into the cliff-pass
    // material slots kMatColor..kMatRough. Each missing/failed file leaves
    // its 1x1 placeholder bound (white color, flat normal, white
    // AO/roughness), so the shader stays valid and just falls back towards
    // the palette. Returns the number of maps successfully loaded
    // (0..kMatCount).
    int loadMaterialMaps(const std::string& dir, const std::string& setName = "Ground061");

    // Loads a displacement map as a CPU grayscale ReliefMap for the mask
    // micro relief (MaskFieldParams::reliefMap; the caller keeps it alive
    // and bumps reliefVersion to trigger a debounced mesh rebuild).
    // Returns false on failure (the caller keeps reliefDepth at 0 then).
    bool loadReliefMap(const std::string& path, maskfield::ReliefMap& out);

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
    };

    // Cached scalar-field derivative of a brush: the extracted surface-nets
    // mesh plus the projected vertex stream, rebuilt only when the brush
    // version or the field params change (debounced) — the full rebuild
    // costs seconds. Holds the mask params.
    struct CliffCache {
        const LandBrush* brush = nullptr;
        std::uint64_t brushVersion = 0;
        bool mask = false;
        maskfield::MaskFieldParams maskParams{};
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
    void rebuildCliffCache(CliffCache& cache, const topology_core::DiamondIsometry& iso);

    void ensurePipelines();
    void destroyPipelines();
    void destroySlot(AtlasSlot& slot);
    bool uploadSlot(
        AtlasSlot& slot,
        const void* rgba,
        int width,
        int height,
        const char* label);
    // Same, but uploads a full CPU-built mip chain (2x2 box filter) — the
    // material maps minify hard on the ground plane, and a single-level
    // REPEAT texture aliases into noise. Sampler must use LINEAR mipmaps.
    bool uploadSlotMipmapped(
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
        float yOffset = 0.0f);
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
    // Material maps (ambientCG Ground061), texture views 0..3 in the cliff
    // pass, sampled with the shared REPEAT m_matSampler. 1x1 placeholders
    // (white color, flat normal, white AO/roughness) until loadMaterialMaps.
    sg_sampler m_matSampler{};
    AtlasSlot m_matMaps[kMatCount]{};

    std::vector<CliffCache> m_cliffCaches;

    AtlasSlot m_slots[3]{};

    int m_atlasCols = 4;
    int m_atlasRows = 6;
    bool m_ready = false;
};
