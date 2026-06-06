#pragma once

#include "IvtGrid.h"
#include "IvtScene.h"

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

namespace ivt_view {

struct IvtShading {
    Vec3 lightDir{-0.45f, 0.65f, -0.35f};
    Vec3 terrainTint{0.55f, 0.62f, 0.48f};
    float ambientStrength = 0.38f;
    float diffuseStrength = 0.78f;
    float specularStrength = 0.22f;
    float shininess = 18.0f;
};

struct IvtMeshGpuCamera {
    float target[3]{};
    float cameraPos[3]{};
    float orthoHalfHeight = 10.0f;
    float aspect = 1.0f;
};

class IvtMeshGpuRenderer {
public:
    void init();
    void shutdown();

    bool render(
        const IvtModel& model,
        const IvtShading& shading,
        const IvtMeshGpuCamera& camera,
        const WorldGridParams& grid,
        bool showGrid,
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

    struct LineVertex {
        float pos[3];
        float color[4];
    };

    struct VsParams {
        float mvp[16];
    };

    struct FsParams {
        float lightDir[4];
        float terrainTint[4];
        float ambientStrength;
        float diffuseStrength;
        float specularStrength;
        float shininess;
        float pad[3];
    };

    void ensurePipelines();
    void destroyPipelines();
    void ensureTarget(int width, int height);
    void destroyTarget();
    void ensureMeshBuffers(int vertexCount, int indexCount);
    void destroyMeshBuffers();
    void ensureLineBuffer(int vertexCount);
    void destroyLineBuffer();
    void uploadMesh(const IvtModel& model);
    void buildGridVertices(const WorldGridParams& grid, const Vec3& center, std::vector<LineVertex>& out) const;
    void drawGroundShadow(const float mvp[16], const IvtModel& model);
    void drawWorldGrid(const float mvp[16], const WorldGridParams& grid, const Vec3& center);

    sg_pipeline m_meshPipeline{SG_INVALID_ID};
    sg_shader m_meshShader{SG_INVALID_ID};
    sg_pipeline m_linePipeline{SG_INVALID_ID};
    sg_shader m_lineShader{SG_INVALID_ID};
    sg_buffer m_vertexBuffer{SG_INVALID_ID};
    sg_buffer m_indexBuffer{SG_INVALID_ID};
    sg_buffer m_lineVertexBuffer{SG_INVALID_ID};
    sg_bindings m_meshBindings{};

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
    int m_lineVertexCapacity = 0;
    int m_cachedVertexCount = -1;
    int m_cachedIndexCount = -1;
    bool m_initialized = false;
};

} // namespace ivt_view
