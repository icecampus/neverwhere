#pragma once

#include "PlaygroundTypes.h"
#include "rock_fracture/Blocks.h"

#include <cstdint>
#include <string>
#include <vector>

namespace meshgen_playground {

enum class RockFractureKind : int {
    Equidimensional = 0,
    Rhombohedral    = 1,
    Polyhedral      = 2,
    Tabular         = 3,
};

struct RockFractureSettings {
    RockFractureKind kind = RockFractureKind::Equidimensional;
    int seed = 1234;
    float tileSize = 20.0f;
    float poissonRadius = 0.5f;
    int poissonTries = 10000;
    float fractureInflate = 3.0f;
    int mcResolution = 100;
    double blockSmoothingRadius = 0.25;
    double bvhTransitionRadius = 0.5;
    bool useOpenMP = true;
    bool useTextureWarp = true;
    bool showWireframe = true;
    bool showSamples2d = true;
    bool showFractures2d = true;
    bool regenerateRequested = false;
};

struct RockFractureModel {
    std::vector<Vec3> samples;
    std::vector<Vec3> fractureCenters;
    std::vector<Vec3> clusterCenters;
    std::vector<Vec3> meshVertices;
    std::vector<Vec3> meshNormals;
    std::vector<std::uint32_t> meshIndices;
    int sampleCount = 0;
    int fractureCount = 0;
    int clusterCount = 0;
    int triangleCount = 0;
    int vertexCount = 0;
    double buildSeconds = 0.0;
    double fieldMin = 0.0;
    double fieldMax = 0.0;
    bool usedOpenMP = false;
    bool usedTextureWarp = false;
    bool usedFallbackTexture = false;
    bool generationFailed = false;
    std::string failureMessage;
};

int rockFractureKindCount();
const char* rockFractureKindName(RockFractureKind kind);
void sanitizeSettings(RockFractureSettings& settings);
void rebuildRockFractureModel();
bool rockFractureModelValid();

} // namespace meshgen_playground
