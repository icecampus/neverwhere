// Cube preview ("кубик с материалом демки"): draws a rotating cube textured
// with the Image-pass result. Universal — works for any demo without
// per-demo work, the next stop of the demo -> material pipeline.
#pragma once

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

namespace shadertoy {

class CubePreview {
public:
    void init();
    void shutdown();
    // Draws the cube in the CURRENT pass (swapchain pass expected, depth on).
    void draw(sg_view demoTexture, int fbWidth, int fbHeight, float timeSec);

private:
    sg_buffer m_vbuf{};
    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_sampler m_sampler{};
    bool m_ok = false;
};

} // namespace shadertoy
