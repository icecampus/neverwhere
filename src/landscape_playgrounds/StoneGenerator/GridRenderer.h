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

// Overlay vertex (grid lines, hover fill, node markers): field position +
// the ground plane depth of its own field row (same bakedDepth anchor the
// SDFGeneratedLandscape overlay uses), so a future 3D stone pass can share
// the z-buffer with the grid, plus flat RGBA.
struct GridColorVertex {
    float x, y, z;
    float r, g, b, a;
};

// The grid/hover overlay ported from B-repGeneratedLandscape (itself from
// FencePathPlayground / SDFGeneratedLandscape): diamond outlines for every
// map cell, a hover footprint on the four cells sharing the hovered vertex
// node, and filled markers for painted seed nodes. Line and triangle streams
// share one dynamic vertex buffer and one shader; the draw order is fills
// first, then all the lines.
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
        glm::ivec2 hoverNode,
        bool hasHover,
        const std::uint8_t* nodes,
        int nodeW,
        int nodeH);

private:
    sg_shader m_shader{};
    sg_pipeline m_linePip{};
    sg_pipeline m_triPip{};
    sg_buffer m_vbuf{};
    bool m_ready = false;
};
