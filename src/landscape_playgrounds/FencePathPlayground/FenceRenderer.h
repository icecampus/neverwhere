#pragma once

#include <cstdint>
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

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "FenceModel.h"

// Schematic fence renderer for the FencePathPlayground: posts as filled cell
// diamonds, sections as thick bands between their endpoint posts, plus the
// translucent ghost preview (stroke plan or move preview — both arrive as a
// StrokePiece list; green = applicable, red = rejected). Same color pass and
// bakedDepth ground-plane convention as GridRenderer; all geometry is
// triangles, one dynamic buffer, one upload + one draw per frame.
class FenceRenderer {
public:
    void init();
    void shutdown();

    void render(
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        const FenceModel& model,
        int selectedFence,
        const std::vector<FenceModel::StrokePiece>* ghost,
        bool ghostValid);

private:
    sg_shader m_shader{};
    sg_pipeline m_triPip{};
    sg_buffer m_vbuf{};
    bool m_ready = false;
};
