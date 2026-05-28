#pragma once

#include "PlaygroundTypes.h"

#include <cstdint>
#include <vector>

namespace meshgen_playground {

struct RectangleCliffSettings {
    int gridWidth = 18;
    int gridHeight = 12;
    int rectX = 3;
    int rectY = 2;
    int rectWidth = 11;
    int rectHeight = 8;
    bool enableCutout = true;
    int cutoutX = 7;
    int cutoutY = 4;
    int cutoutWidth = 4;
    int cutoutHeight = 3;
    float cliffHeight = 2.5f;
    float cornerBevel = 0.3f;
    bool rockEnabled = true;
    int rockSeed = 1337;
    float rockScale = 2.75f;
    float rockAmplitude = 0.28f;
    int wallHorizontalSubdivisions = 5;
    int wallVerticalSubdivisions = 6;
    int terraceSteps = 4;
    bool showTopFaces = true;
    bool showCliffWalls = true;
    bool showMeshWireframe = true;
    bool showCellLabels = true;
    bool showVertexLabels = true;
};

struct RectangleCliffModel {
    std::vector<std::uint8_t> solidCells;
    std::vector<BoundarySegment> boundarySegments;
    std::vector<VertexMarker> vertexMarkers;
    int solidCellCount = 0;
    int edgeVertexCount = 0;
    int outerCornerCount = 0;
    int innerCornerCount = 0;
    int diagonalJoinCount = 0;
    std::vector<MeshQuad> meshQuads;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
    int beveledSegmentCount = 0;
    int cornerCapCount = 0;
};

void sanitizeSettings(RectangleCliffSettings& settings);
bool isSolidCell(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y);
VertexKind classifyVertex(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y);
void rebuildRectangleCliffModel();

} // namespace meshgen_playground
