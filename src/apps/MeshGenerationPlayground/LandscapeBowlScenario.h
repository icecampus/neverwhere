#pragma once

#include "PlaygroundTypes.h"

#include <cstdint>
#include <vector>

namespace meshgen_playground {

struct LandscapeBowlSettings {
    int gridWidth = 32;
    int gridHeight = 24;
    int seed = 2027;
    float clearingRadius = 5.5f;
    float clearingSoftness = 2.2f;
    float highGroundRadius = 9.5f;
    float highGroundWidth = 3.5f;
    float highGroundHeight = 3.2f;
    int heightLevels = 4;
    float arcNoiseScale = 4.0f;
    float arcNoiseAmplitude = 1.6f;
    int hillCount = 5;
    float hillHeight = 1.2f;
    float hillRadius = 2.6f;
    bool showTopFaces = true;
    bool showCliffWalls = true;
    bool showMeshWireframe = true;
    bool showHeightValues = false;
};

struct LandscapeBowlModel {
    std::vector<float> heights;
    std::vector<std::uint8_t> heightLevels;
    std::vector<LandscapeZone> zones;
    std::vector<MeshQuad> meshQuads;
    std::vector<int> levelCellCounts;
    int clearingCellCount = 0;
    int highGroundCellCount = 0;
    int hillCellCount = 0;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    int surfaceTileCount = 0;
    int wallTileCount = 0;
    int uniqueTileMeshCount = 0;
    int reusedTileMeshCount = 0;
    int seamCheckedEdges = 0;
    int seamMismatchCount = 0;
    float seamMaxGap = 0.0f;
    int beveledSegmentCount = 0;
    int cornerCapCount = 0;
    int maxAdjacentLevelDelta = 0;
};

int landscapeIndex(int x, int y, int width);
void sanitizeSettings(LandscapeBowlSettings& settings);
int landscapeLevelAtCell(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y);
void rebuildLandscapeBowlModel();

} // namespace meshgen_playground
