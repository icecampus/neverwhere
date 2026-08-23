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

#include "StoneGen.h"

// 3D stone pass of the playground, ported from FencePathPlayground's
// FenceMeshRenderer: the generated StoneMesh (light already baked into the
// vertex color) is projected to field space with the shared baked-depth
// convention and drawn with the grid's color shader/vertex format — but with
// depth write ON, so the rock self-occludes and arbitrates against the
// depth-tested (never depth-written) grid overlay. Draw BEFORE the grid: the
// grid pipelines are LESS_EQUAL against whatever this pass wrote.
class StoneMeshRenderer {
public:
    // VS uniforms: camera only (same block as GridRenderer::VsParams).
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
    };

    void init();
    void shutdown();

    // Rebuilds the streamed vertex buffer when the content key changed
    // (camera/zoom are vertex-shader state and do not invalidate it).
    void render(
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        const StoneMesh& mesh,
        glm::vec3 worldPos,
        std::uint64_t contentKey);

private:
    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_buffer m_vbuf{};
    bool m_ready = false;

    std::uint64_t m_cacheKey = ~0ull;
    int m_vertCount = 0;
};
