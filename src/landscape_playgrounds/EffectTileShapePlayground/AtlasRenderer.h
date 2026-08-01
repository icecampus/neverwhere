#pragma once

#include <array>
#include <cstddef>
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
#include "SceneStitch.h"

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

// Canvas-level grass underlay: a single field-space quad under everything
// (the highground stands on it), textured with the tiling top texture
// (grass.png), UV aligned to the map grid in field space.
struct UnderlayParams {
    bool enabled = false;
    float tilesPerCell = 1.0f; // texture repeats per cell width (field space)
};

// Boulder ring scattered along the outer contour of a highground layer, so
// the wall stops meeting the ground along a clean geometric cut. Generated
// with the mesh (same cache, same debounce) and drawn in the cliff pass.
//
// Off by default: scattering identical procedural pebbles is the wrong tool
// for this — debris at the foot is meant to be placed with a dedicated prop
// brush, from a set the level designer picks to match the surroundings. Kept
// behind the toggle as a shape reference for that future brush.
struct ScreeParams {
    bool enabled = false;
    int perCell = 3;       // scatter attempts per candidate cell
    float band = 1.1f;     // how far from the footprint the ring reaches (cells)
    float sizeMin = 0.09f; // boulder radius in cells
    float sizeMax = 0.22f;
    float buried = 0.35f; // fraction of the radius sunk under the ground plane
    float seed = 3.0f;
};

// One paint layer on the shared canvas: its own node grid and either flat
// (2D atlas tiles) or scalar-field surface nets from the same nodes
// (z-buffered): cliff (omphalos grooves) or stone (StoneCubePlayground
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
    // Height the mesh is pushed below the ground plane (scalar-field units):
    // the base stops reading as a shape parked on top of the grass.
    float sink = 0.0f;
    const ScreeParams* scree = nullptr;
};

// Fragment-shader uniforms of the cliff pass (palette/light, 16-byte blocks;
// mirrors CliffFieldPlayground's FsParams with cam_pos replaced by the
// constant iso view direction).
struct CliffFsParams {
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
    float params3[4];    // rim contact darkening, height -> world scale,
                         // grass skirt height above the ground plane,
                         // skirt noise frequency
    float params4[4];    // skirt overgrowth amount, flat-top brightness,
                         // grass bounce strength, sky tint strength
    float params5[4];    // rgb: grass bounce tint, w: ground plane Y (the sink)
    float params6[4];    // rgb: sky tint, w: top texture UV rotation (radians)
    float params7[4];    // rgb: plateau top tint, w: how far up the wall the
                         // contact AO reaches (mesh units, 0 = off)
};

// Scene-level stitching knobs owned by the app (UI state). The sun and the
// tone live here because BOTH passes need them: the ground used to be drawn
// as a raw texture while the highground was lit, which is what made the
// highground read as pasted on.
struct SceneStitchSettings {
    // Azimuth 3pi/4 puts the sun exactly across the iso diagonal: the cast
    // shadow runs horizontally on screen instead of hiding behind the mesh
    // silhouette, and of the two visible wall faces one is lit and one is not.
    // A low elevation is what makes the shadow long enough to read.
    float lightAzimuth = 2.356f;  // radians
    float lightElevation = 0.62f; // radians
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float gamma = 0.85f;
    bool groundLit = true;
    // Contact AO: darkening of the ground around the highground footprint,
    // fading out over about a cell and a half. The outline is sub-cell (see
    // buildContactAoField) — with whole-cell coverage a band this wide traced
    // cell borders and read as a square blob rather than as the wall's shadow.
    bool aoEnabled = true;
    float aoStrength = 0.75f;
    float aoRadius = 1.4f; // cells
    // The same darkening climbing the wall itself (mesh units above the ground
    // plane). Without it the grass goes dark while the stone directly above it
    // stays lit, which draws the very seam the AO is there to hide.
    float aoWallFade = 0.35f;
    // Cast + self shadow through the orthographic shadow map. Off: a hard sun
    // shadow thrown across the tiles fights the painted lighting the atlas art
    // already carries, and it is the contact AO that does the stitching work.
    // The map is not even rendered while this is false.
    bool shadowsEnabled = false;
    float shadowStrength = 0.65f;
    float shadowBias = 0.0025f;

    glm::vec3 sunDirection() const;
};

// Fragment-shader uniforms shared by the ground pass and the cliff pass. The
// cliff pass declares only the leading `kStitchCoreBytes` — it has no use for
// the AO rect, and a GL driver drops unused uniforms, which sokol then reports
// as a missing block member every startup.
struct SceneStitchParams {
    float sunDir[4];     // xyz: direction towards the sun
    float shadowRow0[4]; // world (cells, world height, cells) -> shadow uv/depth
    float shadowRow1[4];
    float shadowRow2[4];
    float params0[4]; // ambient, diffuse, gamma, shadow strength
    float params1[4]; // shadow bias, shadow texel, AO strength, AO radius
    float aoRect[4];  // xy: AO field origin (cells), zw: 1 / field extent
};

inline constexpr std::size_t kStitchCoreBytes = offsetof(SceneStitchParams, aoRect);

// Per-layer cliff cache status for the UI (see AtlasRenderer::cliffStatsFor).
struct CliffStats {
    bool watertight = true;
    bool pending = false;    // edits happened, waiting out the debounce
    double rebuildMs = 0.0;
    int voxelCount = 0;
    int vertexCount = 0;
    int triangleCount = 0;
    int screeCount = 0;
};

// Everything one frame needs. prepare() and render() take the same
// descriptor: prepare() runs the offscreen work (mesh cache, AO field, shadow
// map) and MUST be called before the swapchain pass is opened.
struct SceneFrame {
    const PaintLayerView* layers = nullptr;
    int layerCount = 0;
    const topology_core::DiamondIsometry* iso = nullptr;
    const topology_core::Camera2D* camera = nullptr;
    int viewW = 0;
    int viewH = 0;
    glm::ivec2 hoverNode{-1, -1};
    bool hasHover = false;
    const CliffFsParams* cliffShading = nullptr;
    const UnderlayParams* underlay = nullptr;
    const SceneStitchSettings* stitch = nullptr;
    double nowSec = 0.0;
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

    // Shadow pass VS uniforms: the light transform plus the layer's own
    // height -> world scale (it depends on the layer's height slider).
    struct ShadowVsParams {
        float row0[4];
        float row1[4];
        float row2[4];
        float heightToWorld[4]; // x used, rest padding
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

    // Offscreen work: rebuild the debounced meshes, refresh the contact AO
    // field and render the shadow map. Opens its own passes, so it cannot run
    // inside the swapchain pass.
    void prepare(const SceneFrame& frame);
    void render(const SceneFrame& frame);

    // Status of the cliff cache attached to the given brush (empty stats when
    // the layer never rendered). UI reads this for the rebuild/watertight info.
    const CliffStats& cliffStatsFor(const LandBrush* brush) const;
    bool shadowMapReady() const { return m_shadow.valid; }

private:
    struct AtlasSlot {
        sg_image image{};
        sg_view view{};
    };

    struct TexVertex {
        float x, y, z;
        float u, v;
        float wx, wz; // world position (cell units) for AO / shadow lookups
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
        ScreeParams scree{};
        float heightScale = 0.0f;
        float sink = 0.0f;
        bool contentValid = false;
        double lastEditSec = 0.0;
        bool meshDirty = false;   // field + extraction needed (heavy)
        bool streamDirty = false; // re-projection only (cheap, heightScale edits)
        bool gpuDirty = false;
        cliff::Mesh mesh;
        glm::ivec2 origin{0, 0};
        float maxHeight = 0.0f; // largest mesh py, for the shadow volume
        std::vector<CliffVertex> stream;
        sg_buffer vbuf{};
        size_t vbufSize = 0;
        CliffStats stats;
    };

    // Orthographic shadow map. This sokol build has no depth-texture sampling
    // path in the repo, so the normalized light distance rides in a float
    // COLOR attachment and the depth attachment only resolves the z-test.
    struct ShadowTarget {
        sg_image colorImage{};
        sg_view colorAttachment{};
        sg_view colorTexture{};
        sg_image depthImage{};
        sg_view depthAttachment{};
        sg_pixel_format colorFormat = SG_PIXELFORMAT_R32F;
        int size = 0;
        bool valid = false;
    };

    CliffCache& cliffCacheFor(const LandBrush* brush);
    void rebuildCliffCache(CliffCache& cache, const topology_core::DiamondIsometry& iso);
    void appendScreeRing(CliffCache& cache, const topology_core::DiamondIsometry& iso);
    void syncCliffCaches(const SceneFrame& frame);
    void refreshAoField(const SceneFrame& frame);
    void renderShadowMap(const SceneFrame& frame);
    SceneStitchParams buildStitchParams(const SceneFrame& frame) const;

    void ensurePipelines();
    void ensureShadowTarget();
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
        float yOffset = 0.0f);
    void appendDiamondOutline(
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
    sg_pipeline m_cliffPip{};
    sg_pipeline m_shadowPip{};
    sg_shader m_texShd{};
    sg_shader m_colorShd{};
    sg_shader m_cliffShd{};
    sg_shader m_shadowShd{};
    sg_buffer m_texVbuf{};
    sg_buffer m_colorVbuf{};
    sg_sampler m_sampler{};
    // Cliff/stone top texture (tiling, REPEAT sampler): placeholder 1x1 white
    // until loadTopTextureFromFile succeeds.
    sg_image m_topTexImage{};
    sg_view m_topTexView{};
    sg_sampler m_topTexSampler{};

    // Contact AO: distance to the highground footprint as an R8 texture over
    // the map bbox. A 1x1 "far" placeholder keeps the bindings valid when
    // nothing is painted.
    sg_image m_aoImage{};
    sg_view m_aoView{};
    sg_sampler m_aoSampler{};
    ContactAoField m_aoField;
    std::uint64_t m_aoKey = 0;

    ShadowTarget m_shadow;
    sg_sampler m_shadowSampler{};
    SunBasis m_sunBasis;
    float m_shadowTexel = 0.0f;

    std::vector<CliffCache> m_cliffCaches;

    AtlasSlot m_slots[2]{};

    int m_atlasCols = 4;
    int m_atlasRows = 6;
    bool m_ready = false;
};
