#pragma once

#include "PlaygroundTypes.h"

#include <vector>

namespace meshgen_playground {

struct WallSeriesLabSettings {
    float wallWidth = 4.0f;
    float wallHeight = 3.0f;
    int wallHorizontalSubdivisions = 8;
    int wallVerticalSubdivisions = 6;

    float wallCenterX = 0.0f;
    float wallCenterY = 1.4f;
    float wallCenterZ = 0.0f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;

    bool rockEnabled = true;
    int rockSeed = 1337;
    float rockScale = 2.75f;
    float rockAmplitude = 0.28f;
    int terraceSteps = 4;
    bool fadeDisplacementAtBottom = true;

    bool showWireframe = true;
};

struct WallSeriesLabModel {
    std::vector<MeshQuad> quads;
    int quadCount = 0;
    int subdivisionsX = 0;
    int subdivisionsY = 0;
};

void sanitizeWallSeriesLabSettings(WallSeriesLabSettings& settings);
void resetWallSeriesLabCamera(QuadLabPreviewCamera& camera);
void rebuildWallSeriesLabModel();
bool runWallSeriesLabSmokeTest();

} // namespace meshgen_playground
