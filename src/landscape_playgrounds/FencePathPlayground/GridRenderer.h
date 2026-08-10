#pragma once

#include <cstdint>

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

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

// Overlay vertex (grid lines, hover fill/marker): field position + the ground
// plane depth of its own field row (same bakedDepth anchor the
// SDFGeneratedLandscape overlay uses), so future 3D passes (fence meshes,
// path slabs) can share the z-buffer with the grid, plus flat RGBA.
struct GridColorVertex {
    float x, y, z;
    float r, g, b, a;
};

// The grid/hover overlay ported from SDFGeneratedLandscape's AtlasRenderer
// (overlay pass only): diamond outlines for every map cell plus the hovered
// cell highlight (fill + outline). The cursor is cell-centric: fence pieces
// occupy exactly one cell each, so the node footprint/marker of the original
// overlay is gone. Line and triangle streams share one dynamic vertex buffer
// and one shader; the draw order is hover fill triangles first, then all the
// lines.
class GridRenderer {
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

    void render(
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        int mapW,
        int mapH,
        glm::ivec2 hoverCell,
        bool hasHover);

private:
    sg_shader m_shader{};
    sg_pipeline m_linePip{};
    sg_pipeline m_triPip{};
    sg_buffer m_vbuf{};
    bool m_ready = false;
};
