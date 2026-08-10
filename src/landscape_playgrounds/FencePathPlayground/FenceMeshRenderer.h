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

#include "FenceMesh.h"
#include "FenceModel.h"

// 3D fence pass of the FencePathPlayground: instances the baked ShapeML piece
// meshes (FenceMeshSet) per FenceModel piece into projected field-space
// triangles (buildFenceFieldTriangles, cached per content version + selection)
// and draws them with the same color shader/vertex format as GridRenderer —
// but with depth write ON, so the boulders self-occlude and rows arbitrate
// correctly (the grid itself never writes depth). Draw after the grid, before
// the ghost overlay.
class FenceMeshRenderer {
public:
    // VS uniforms: camera only (same block as GridRenderer::VsParams).
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
    };

    void init();
    void shutdown();

    // Loads resources/models/fence piece meshes; false = caller should fall
    // back to the schematic renderer.
    bool loadMeshes(const std::string& dir);
    bool meshesOk() const { return m_meshes.ok; }

    void render(
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        const FenceModel& model,
        int selectedFence);

private:
    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_buffer m_vbuf{};
    bool m_ready = false;

    FenceMeshSet m_meshes;
    // Instance cache: rebuilt only when the content key changes (camera and
    // zoom are vertex-shader state and do not invalidate it).
    std::uint64_t m_cacheVersion = ~0ull;
    int m_cacheSelected = -2;
    int m_vertCount = 0;
};
