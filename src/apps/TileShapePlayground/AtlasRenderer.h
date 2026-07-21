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

#include <highground_core/highground.h>
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

// Z-buffered raised pass: z is the normalized depth along the iso view ray
// (baked CPU-side from highground::Vertex::groundY).
struct DepthTexVertex {
    float x, y, z;
    float u, v;
};

struct DepthColorVertex {
    float x, y, z;
    float r, g, b, a;
};

// Triplanar walls: the world-space map coordinates recovered from the field
// position (undeformed, scaled to world px) + the lift, so the fragment
// shader can blend two projections of a tiling rock texture with no UV
// seams. normal = unit outward contour normal (map space) for the weights.
struct TriWallVertex {
    float x, y;
    float r, g, b, a;
    float tx, ty, lift;
    float nx, ny;
};

struct TriDepthWallVertex {
    float x, y, z;
    float r, g, b, a;
    float tx, ty, lift;
    float nx, ny;
};

// One paint layer on the shared canvas: its own node grid, a top texture and
// either flat (2D), raised (3D, extruded with cliff walls) or prism (3D
// boundary-first demo: simplified loops extruded into rectangular walls)
// presentation. `cgal` switches a raised layer to the CGAL exact-region
// generator (ignored when the library was built without CGAL). `zbuf` draws
// a raised layer through the depth-tested pipelines (GPU depth buffer
// instead of the CPU painter sort); prism always stays painter.
struct PaintLayerView {
    const LandBrush* brush = nullptr;
    AtlasKind atlas = AtlasKind::Grass;
    bool raised = false;
    bool prism = false;
    bool cgal = false;
    bool zbuf = true;
};

class AtlasRenderer {
public:
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
        float tex_scale; // wall triplanar: world px -> uv
        float _pad[2];
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

    // Tiled ground texture for the raised top (world-UV, repeat) — one per
    // AtlasKind so a raised layer picks the variant matching its atlas kind.
    bool loadTopTextureFromFile(AtlasKind kind, const std::string& path);
    bool loadTopTextureFromRgba(AtlasKind kind, const std::uint8_t* rgba, int width, int height);

    // Tiling rock texture for the triplanar walls (world-UV, repeat).
    bool loadWallTextureFromFile(const std::string& path);

    void render(
        const PaintLayerView* layers,
        int layerCount,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        glm::ivec2 hoverNode,
        bool hasHover,
        bool hoverRaised,
        const highground::Params* raisedParams,
        bool triplanarWalls,
        float wallTexScale);

private:
    struct AtlasSlot {
        sg_image image{};
        sg_view view{};
    };

    struct TexVertex {
        float x, y;
        float u, v;
    };

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
        glm::vec4 color,
        float yOffset = 0.0f);

    sg_pipeline m_texPip{};
    sg_pipeline m_colorPip{};
    sg_pipeline m_wallPip{};
    sg_pipeline m_depthTexPip{};
    sg_pipeline m_depthWallPip{};
    sg_pipeline m_triWallPip{};
    sg_pipeline m_triDepthWallPip{};
    sg_shader m_texShd{};
    sg_shader m_colorShd{};
    sg_shader m_depthTexShd{};
    sg_shader m_depthWallShd{};
    sg_shader m_triWallShd{};
    sg_shader m_triDepthWallShd{};
    sg_buffer m_texVbuf{};
    sg_buffer m_colorVbuf{};
    sg_buffer m_depthTexVbuf{};
    sg_buffer m_depthColorVbuf{};
    sg_buffer m_triWallVbuf{};
    sg_sampler m_sampler{};
    sg_sampler m_topSampler{};

    AtlasSlot m_slots[2]{};
    AtlasSlot m_topSlots[2]{};
    AtlasSlot m_wallTexSlot{};

    int m_atlasCols = 4;
    int m_atlasRows = 6;
    bool m_ready = false;
};
