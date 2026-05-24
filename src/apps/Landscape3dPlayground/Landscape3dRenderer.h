#pragma once

#include <cstdint>
#include <filesystem>
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

#include "TerrainScene.h"

struct Landscape3dCamera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float yawDeg = 45.0f;
    float pitchDeg = 35.0f;
    float distance = 42.0f;
    float orthoScale = 22.0f;
    bool perspective = false;

    glm::vec3 position() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
};

struct Landscape3dRenderParams {
    float cubeSize = 1.0f;
    float lightYawDeg = 35.0f;
    float lightPitchDeg = 50.0f;
    bool useGrassTexture = true;
    bool showWireframe = true;
    int debugMode = 0; // 0=lit, 1=top texture, 2=earth sides, 3=height, 4=normals
};

class Landscape3dRenderer {
public:
    void init();
    void shutdown();
    bool loadGrassTexture(const std::filesystem::path& path);
    void rebuildMesh(const TerrainScene& scene, float cubeSize);
    void render(const Landscape3dCamera& camera, const Landscape3dRenderParams& params, int width, int height);

    int triangleCount() const { return m_indexCount / 3; }
    int lineCount() const { return m_lineVertexCount / 2; }

    struct TerrainVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 color;
        glm::vec2 uv;
        float faceKind; // 0=top grass, 1=earth side, 2=wire line
    };

private:
    struct VsParams {
        float mvp[16];
    };

    struct FsParams {
        float lightDir[4];
        float options[4];
    };

    void ensurePipelines();
    void destroyPipelines();
    void destroyMeshBuffers();
    void destroyGrassTexture();

    sg_pipeline m_terrainPipeline{SG_INVALID_ID};
    sg_pipeline m_linePipeline{SG_INVALID_ID};
    sg_shader m_shader{SG_INVALID_ID};
    sg_buffer m_vertexBuffer{SG_INVALID_ID};
    sg_buffer m_indexBuffer{SG_INVALID_ID};
    sg_buffer m_lineVertexBuffer{SG_INVALID_ID};
    sg_image m_grassImage{SG_INVALID_ID};
    sg_view m_grassView{SG_INVALID_ID};
    sg_sampler m_grassSampler{SG_INVALID_ID};
    sg_bindings m_terrainBindings{};
    sg_bindings m_lineBindings{};
    int m_indexCount = 0;
    int m_lineVertexCount = 0;
};

