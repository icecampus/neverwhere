#pragma once



#include "PlaygroundTypes.h"

#include "SingleQuadLabScenario.h"



#include <vector>



namespace meshgen_playground {



struct WallSeriesLabSettings {

    float wallWidth = 4.0f;

    float wallHeight = 3.0f;



    float wallCenterX = 0.0f;

    float wallCenterY = 1.4f;

    float wallCenterZ = 0.0f;

    float yawDegrees = 0.0f;

    float pitchDegrees = 0.0f;



    // Shared 2x2 center vertex pushed toward the camera before extrude enrichment.

    float centerPushForward = 0.0f;



    QuadLabOperation quadLabOperation = QuadLabOperation::Extrude;

    float extrudeDepth = 0.218f;

    float extrudeTopScale = 0.644f;

    float extrudeTopHeightSpread = 0.892f;

    float extrudeTopScaleSpread = 0.322f;

    int extrudeHeightSeed = 1337;

    bool extrudeVaryPerQuad = false;

    bool colorizeFaces = true;



    bool showWireframe = true;

};



struct WallSeriesLabModel {

    std::vector<MeshQuad> quads;

    int baseQuadCount = 0;

    int meshPanelCount = 0;

};



void sanitizeWallSeriesLabSettings(WallSeriesLabSettings& settings);

void resetWallSeriesLabCamera(QuadLabPreviewCamera& camera);

void rebuildWallSeriesLabModel();

bool runWallSeriesLabSmokeTest();



} // namespace meshgen_playground

