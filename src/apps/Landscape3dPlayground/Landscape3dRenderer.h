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

enum class Landscape3dTerrainMode : int {
    CubesDebug = 0,
    ValleyGeometry = 1,
    ContourGeometry = 2,
};

struct Landscape3dCamera {
    static constexpr float fixedYawDeg = 0.0f;
    static constexpr float fixedPitchDeg = 60.0f;
    static constexpr float fixedDistance = 42.0f;
    static constexpr float defaultOrthoScale = 22.0f;
    static constexpr float editorGroundAspectRatio = 1.7320508f; // 2.0 * sin(60deg)

    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float yawDeg = fixedYawDeg;
    float pitchDeg = fixedPitchDeg;
    float distance = fixedDistance;
    float orthoScale = defaultOrthoScale;
    bool perspective = false;

    glm::vec3 position() const;
    glm::vec3 right() const;
    glm::vec3 up() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
};

struct Landscape3dRenderParams {
    float cubeSize = 1.0f;
    float heightStepInCubes = 0.5f;
    float lightYawDeg = -35.0f;
    float lightPitchDeg = 48.0f;
    float ambientOcclusionStrength = 0.85f;
    bool useGrassTexture = true;
    bool showWireframe = false;
    bool showEdgeAccents = true;
    bool grassVariation = true;
    bool sideGradient = true;
    Landscape3dTerrainMode terrainMode = Landscape3dTerrainMode::ContourGeometry;
    int previewTileIndex = -1;
    int debugMode = 0; // 0=lit, 1=top texture, 2=earth sides, 3=height, 4=normals
};

struct Landscape3dTileStats {
    int unknown = 0;
    int full = 0;
    int corners = 0;
    int lacks = 0;
    int lines = 0;
    int opposites = 0;
    int contourHighCells = 0;
    int contourSmoothEdges = 0;
    int contourCliffEdges = 0;
    int contourCliffChains = 0;
};

class Landscape3dRenderer {
public:
    void init();
    void shutdown();
    bool loadGrassTexture(const std::filesystem::path& path);
    bool loadRockTexture(const std::filesystem::path& path);
    void rebuildMesh(const TerrainScene& scene, const Landscape3dRenderParams& params);
    void render(const Landscape3dCamera& camera, const Landscape3dRenderParams& params, int width, int height);

    int triangleCount() const { return m_indexCount / 3; }
    int lineCount() const { return m_lineVertexCount / 2; }
    const Landscape3dTileStats& tileStats() const { return m_tileStats; }

    struct TerrainVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 color;
        glm::vec2 uv;
        float faceKind; // 0=top grass, 1=rock side, 2=wire line
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
    void destroyRockTexture();

    sg_pipeline m_terrainPipeline{SG_INVALID_ID};
    sg_pipeline m_linePipeline{SG_INVALID_ID};
    sg_shader m_shader{SG_INVALID_ID};
    sg_buffer m_vertexBuffer{SG_INVALID_ID};
    sg_buffer m_indexBuffer{SG_INVALID_ID};
    sg_buffer m_lineVertexBuffer{SG_INVALID_ID};
    sg_image m_grassImage{SG_INVALID_ID};
    sg_view m_grassView{SG_INVALID_ID};
    sg_sampler m_grassSampler{SG_INVALID_ID};
    sg_image m_rockImage{SG_INVALID_ID};
    sg_view m_rockView{SG_INVALID_ID};
    sg_sampler m_rockSampler{SG_INVALID_ID};
    sg_bindings m_terrainBindings{};
    sg_bindings m_lineBindings{};
    Landscape3dTileStats m_tileStats{};
    int m_indexCount = 0;
    int m_lineVertexCount = 0;
};

