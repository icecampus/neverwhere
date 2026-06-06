#pragma once

#include "RockFractureScene.h"

#include <cstdint>

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

namespace render_playground {

struct RockFractureShading;

struct RockMeshGpuCamera {
    float target[3]{};
    float cameraPos[3]{};
    float orthoHalfHeight = 10.0f;
    float aspect = 1.0f;
};

class RockMeshGpuRenderer {
public:
    void init();
    void shutdown();

    bool render(
        const RockFractureModel& model,
        const RockFractureShading& shading,
        const RockMeshGpuCamera& camera,
        int width,
        int height);

    bool validOutput() const;
    sg_view outputView() const { return m_colorTextureView; }
    sg_sampler outputSampler() const { return m_outputSampler; }

    void invalidateMeshCache();

private:
    struct GpuVertex {
        float pos[3];
        float normal[3];
    };

    struct VsParams {
        float mvp[16];
    };

    struct FsParams {
        float lightDir[4];
        float rockTint[4];
        float ambientStrength;
        float diffuseStrength;
        float specularStrength;
        float shininess;
        float pad[3];
    };

    void ensurePipeline();
    void destroyPipeline();
    void ensureTarget(int width, int height);
    void destroyTarget();
    void ensureMeshBuffers(int vertexCount, int indexCount);
    void destroyMeshBuffers();
    void uploadMesh(const RockFractureModel& model);

    sg_pipeline m_pipeline{SG_INVALID_ID};
    sg_shader m_shader{SG_INVALID_ID};
    sg_buffer m_vertexBuffer{SG_INVALID_ID};
    sg_buffer m_indexBuffer{SG_INVALID_ID};
    sg_bindings m_bindings{};

    sg_image m_colorImage{SG_INVALID_ID};
    sg_image m_depthImage{SG_INVALID_ID};
    sg_view m_colorAttachmentView{SG_INVALID_ID};
    sg_view m_depthAttachmentView{SG_INVALID_ID};
    sg_view m_colorTextureView{SG_INVALID_ID};
    sg_sampler m_outputSampler{SG_INVALID_ID};

    int m_width = 0;
    int m_height = 0;
    int m_vertexCapacity = 0;
    int m_indexCapacity = 0;
    int m_cachedVertexCount = -1;
    int m_cachedIndexCount = -1;
    bool m_initialized = false;
};

} // namespace render_playground
