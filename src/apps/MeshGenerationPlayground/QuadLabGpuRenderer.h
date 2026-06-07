#pragma once

#include "PlaygroundTypes.h"

#include <vector>

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

namespace meshgen_playground {

struct MeshQuadsPreviewOptions;

class QuadLabGpuRenderer {
public:
    void init();
    void shutdown();

    bool render(
        const std::vector<MeshQuad>& quads,
        const QuadLabPreviewCamera& camera,
        const MeshQuadsPreviewOptions& options,
        int width,
        int height);

    bool validOutput() const;
    sg_view outputView() const { return m_colorTextureView; }
    sg_sampler outputSampler() const { return m_outputSampler; }

private:
    void ensurePipeline();
    void destroyPipeline();
    void ensureTarget(int width, int height);
    void destroyTarget();
    void ensureVertexBuffers(std::size_t triangleVertexCount, std::size_t lineVertexCount);

    bool m_initialized = false;
    int m_width = 0;
    int m_height = 0;

    sg_image m_colorImage{};
    sg_image m_depthImage{};
    sg_view m_colorAttachmentView{};
    sg_view m_depthAttachmentView{};
    sg_view m_colorTextureView{};
    sg_sampler m_outputSampler{};

    sg_shader m_meshShader{};
    sg_shader m_lineShader{};
    sg_pipeline m_meshPipeline{};
    sg_pipeline m_linePipeline{};
    sg_buffer m_triangleBuffer{};
    sg_buffer m_lineBuffer{};
    std::size_t m_triangleCapacity = 0;
    std::size_t m_lineCapacity = 0;

    sg_bindings m_bindings{};
};

} // namespace meshgen_playground
