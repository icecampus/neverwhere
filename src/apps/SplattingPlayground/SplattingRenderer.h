#pragma once

#include <string>
#include <vector>

#include <topology_core/camera2d.h>
#include <topology_core/staggered_isometry.h>

// Ensure a backend is selected everywhere we include sokol_gfx.h
#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE33)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE33
    #endif
#endif

#include <sokol_gfx.h>

#include "MaterialTypes.h"

// UV mapping mode
enum class UvMode : int {
    WorldUV = 0,      // Continuous world-space UV (no tile seams)
    RandomTileUV = 1, // Per-tile randomized UV (offset/rotation)
};

struct SplattingParams {
    float blendSharpness = 0.6f;
    float noiseScale = 4.0f;
    float tileScale = 1.0f;
    float macroScale = 0.35f;
    float macroStrength = 0.12f;
    float heightInfluence = 0.35f;
    float edgeDarkness = 0.20f;
    float edgeWidth = 0.35f;
    
    // UV mode params
    UvMode uvMode = UvMode::WorldUV;
    float worldUvScale = 128.0f;       // World units per texture repeat
    float randomUvStrength = 0.25f;    // Randomization strength for RandomTileUV
};

class SplattingRenderer {
public:
    void init();
    void shutdown();

    // slot: 0..3
    bool loadMaterial(int slot, const std::string& path);
    bool loadNoiseTexture(const std::string& path);

    void render(
        const MaterialMap& map,
        const topology_core::StaggeredIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        const SplattingParams& params
    );

private:
    // Vertex: world position only (UV computed in shader from world pos)
    struct Vertex {
        float x, y;  // world position
    };

    struct VsParams {
        float viewSize[2];
        float cameraOffset[2];
        float cameraZoom;
        float _pad0[3];
    };

    struct FsParams {
        float cellSize[2];        // StaggeredIsometry cell size
        float mapSize[2];         // Material map dimensions
        float blendSharpness;
        float noiseScale;
        float tileScale;
        float macroScale;
        float macroStrength;
        float heightInfluence;
        float edgeDarkness;
        float edgeWidth;
        float worldUvScale;
        float randomUvStrength;
        int uvMode;
        float _pad0;
    };

    void ensurePipeline();
    void destroyPipeline();
    void ensureFallbackTextures();
    void buildMesh(const MaterialMap& map, const topology_core::StaggeredIsometry& iso);
    void updateMaterialIdMap(const MaterialMap& map);

    sg_pipeline pip = {};
    sg_buffer vbuf = {};
    sg_bindings bind = {};

    sg_image materials[4] = {};
    sg_image noiseTexture = {};
    sg_image materialIdMap = {};
    sg_image fallbackWhite = {};
    sg_image fallbackNoise = {};
    sg_sampler linearSampler = {};
    sg_sampler nearestSampler = {};

    std::vector<Vertex> vertices;
    std::vector<uint8_t> materialIdData;
    int materialIdMapWidth = 0;
    int materialIdMapHeight = 0;
};
