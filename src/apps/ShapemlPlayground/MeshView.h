// Render half of the playground: one sokol pipeline, vertex-color +
// optional-diffuse-texture draws, orbit camera. Single-source GLSL 330 with
// the GLCORE backend (same exception as StoneCube, see AGENTS.md).
#pragma once

#include "GrammarHost.h"

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

namespace shapemlhost {

struct Camera {
    float yaw = 0.7f;
    float pitch = 0.45f;
    float dist = 20.0f;
    float target[3] = {0.0f, 0.0f, 0.0f};
};

class MeshView {
public:
    void init();
    void shutdown();

    // Recreates VB/IB (meshes are static between derivations, so plain
    // immutable buffers recreated on each upload are the simplest option).
    void setModel(const DerivedModel& model);
    void clearModel();

    // Draws in the CURRENT pass (swapchain pass expected).
    void draw(const Camera& cam, int fbWidth, int fbHeight);

    bool hasModel() const { return m_indexCount > 0; }

private:
    sg_view textureFor(const std::string& path); // lazy stb_image load + cache

    struct VsUniforms {
        float mvp[16];
    };
    struct FsUniforms {
        float lightDir[4]; // xyz: direction TOWARDS the sun
    };

    sg_buffer m_vbuf{};
    sg_buffer m_ibuf{};
    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_sampler m_sampler{};
    sg_image m_whiteImg{};
    sg_view m_whiteView{};
    std::vector<DrawRange> m_draws;
    std::unordered_map<std::string, sg_view> m_texCache;
    std::unordered_map<std::string, sg_image> m_texImages;
    int m_indexCount = 0;
    int m_vertCount = 0;
};

} // namespace shapemlhost
