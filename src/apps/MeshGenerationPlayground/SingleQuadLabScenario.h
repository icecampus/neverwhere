#pragma once

#include "PlaygroundTypes.h"

#include <vector>

namespace meshgen_playground {

enum class QuadLabOperation : int {
    Flat = 0,
    Extrude = 1,
};

struct SingleQuadLabSettings {
    float quadWidth = 1.0f;
    float quadHeight = 1.0f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float centerX = 0.0f;
    float centerY = 0.5f;
    float centerZ = 0.0f;

    QuadLabOperation operation = QuadLabOperation::Extrude;
    float extrudeDepth = 0.25f;
    float extrudeTopScale = 0.9f;
    float extrudeTopHeightSpread = 0.0f;
    int extrudeHeightSeed = 1337;

    bool showWireframe = true;
    bool colorizeFaces = true;
};

struct SingleQuadLabModel {
    MeshQuad baseQuad;
    std::vector<MeshQuad> quads;
    int panelCount = 0;
};

void sanitizeSingleQuadLabSettings(SingleQuadLabSettings& settings);
void rebuildSingleQuadLabModel();
bool runSingleQuadLabSmokeTest();

} // namespace meshgen_playground
