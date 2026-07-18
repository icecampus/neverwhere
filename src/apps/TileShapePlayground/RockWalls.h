#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "AtlasRenderer.h"
#include "LandBrush.h"

// Rock cliff walls for the raised layer: production wall rules (block-cliff
// from landscape_mesh) ported onto our tile contour topology. Global contour
// chains, 45-degree bevels at convex corners, ridged noise displacement with
// terrace steps, seam envelope (zero offset at top/bottom edges), baked colors.
struct RockWallParams {
    float cornerBevel = 0.3f; // convex corner trim, in cell units
    float amplitude = 0.28f;  // displacement scale, in cell units
    float noiseScale = 2.75f; // FastNoise feature scale, per cell unit
    int terraceSteps = 4;
    int hSub = 3;             // columns per wall piece (our pieces are ~0.5 cell)
    int vSub = 4;             // rows per wall piece
    int seed = 1337;
};

struct RockChainInfo {
    int segments = 0;
    int convexCorners = 0;
    int concaveCorners = 0;
    int diagonalJoins = 0;
    bool closed = false;
};

// Contour analysis only (no noise, no geometry) — exposed for smoke tests.
std::vector<RockChainInfo> analyzeRockBoundaries(const LandBrush& raised);

struct RockWallBuild {
    std::vector<ColorVertex> verts;
    float maxAbsOffset = 0.0f;
    int quadCount = 0;
};

// heightPx: raised height in field px (screen px at zoom 1).
RockWallBuild buildRockWalls(const LandBrush& raised, float heightPx, const RockWallParams& params);

// Test hook: displacement at a point (map units), seam envelope applied.
float rockWallOffset(const RockWallParams& params, float mx, float my, float heightT);
