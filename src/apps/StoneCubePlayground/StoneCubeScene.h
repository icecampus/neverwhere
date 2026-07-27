// Scene plumbing: raymarch pipeline rendering the stone cube into a scaled
// offscreen target + blit to the swapchain (2x-DPI framebuffers make
// full-res raymarching crawl, same as ShadertoyPlayground).
#pragma once

#include "StoneCubeParams.h"

// Ensure a backend is selected everywhere we include sokol_gfx.h (GLCORE
// everywhere in this playground).
#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif
#include <sokol_gfx.h>

namespace stonecube {

struct Camera {
    float yaw = 0.7f;
    float pitch = 0.45f;
    float dist = 4.5f;
    float target[3] = {0.0f, 0.0f, 0.0f};
};

class StoneCubeScene {
public:
    void init();
    void shutdown();

    // Renders the cube into the scaled offscreen target (own sg_pass).
    void drawScene(const Params& params, const Camera& cam, float renderScale,
        int fbWidth, int fbHeight);
    // Blits the target in the CURRENT pass (swapchain pass expected).
    void drawBlit() const;

    int targetWidth() const { return m_targetW; }
    int targetHeight() const { return m_targetH; }

    // std140 layout: 7 x vec4, order matches the GLSL uniform declarations.
    struct Uniforms {
        float boxSize[4];
        float shape1[4];
        float shape2[4];
        float look[4];
        float camera[4];
        float target[4];
        float resolution[4];
    };

private:
    void ensureTarget(int w, int h);

    sg_shader m_sceneShader{};
    sg_pipeline m_scenePip{};
    sg_shader m_blitShader{};
    sg_pipeline m_blitPip{};
    sg_sampler m_sampler{};

    sg_image m_colorImg{};
    sg_view m_colorAttach{};
    sg_view m_colorTex{};
    sg_image m_depthImg{};
    sg_view m_depthAttach{};
    int m_targetW = 0;
    int m_targetH = 0;
    sg_pixel_format m_depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;
    sg_pixel_format m_colorFormat = SG_PIXELFORMAT_RGBA8;
};

} // namespace stonecube
