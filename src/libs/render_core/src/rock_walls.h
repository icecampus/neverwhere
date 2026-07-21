#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "topology_core/diamond_isometry.h"

namespace render_core {

// Rock cliff walls for the raised landscape (TileShapePlayground port):
// production wall rules (block-cliff from landscape_mesh) on the tile contour
// topology. Global contour chains, 45-degree bevels at convex corners, ridged
// noise displacement with terrace steps, seam envelope (zero offset at
// top/bottom edges), baked colors. Renderer-agnostic: contour segments in,
// baked-color vertices out.
struct RockWallParams {
    float cornerBevel = 0.3f; // convex corner trim, in cell units
    float amplitude = 0.28f;  // displacement scale, in cell units
    float noiseScale = 2.75f; // FastNoise feature scale, per cell unit
    int terraceSteps = 4;
    int hSub = 3;             // columns per wall piece (pieces are ~0.5 cell)
    int vSub = 4;             // rows per wall piece
    int seed = 1337;
};

// Baked-color wall vertex in field space (world px): pos.xy + rgba.
struct RockWallVertex {
    float x, y;
    float r, g, b, a;
};

// One contour segment in map space (cell = 1 unit) with its outward normal.
struct RockContourSegment {
    glm::vec2 a{}, b{};
    glm::vec2 outward{}; // unit outward normal (axial), map space
};

struct RockWallBuild {
    std::vector<RockWallVertex> verts;
    // Closed boundary polylines of the beveled wall top edge, in chain order
    // (field space, NOT lifted by heightPx). The wall top edge has zero noise
    // offset (seam envelope), so these polylines are exactly the walls' upper
    // contour — the raised top surface is triangulated from them.
    std::vector<std::vector<glm::vec2>> topChains;
    float maxAbsOffset = 0.0f;
    int quadCount = 0;
};

// Per-cell contour in map space with outward normals — same contour rule as
// raised_geometry (edge midpoint -> cell center, axis-parallel), plus normals.
// Mask order [Left, Up, Right, Down] (DiamondIsometry::cellCornerNodes slots).
std::vector<RockContourSegment> cellRockContourSegments(
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask);

// Build the global rock-wall mesh over the contour segments of a landmass.
// heightPx: raised height in field px (screen px at zoom 1).
RockWallBuild buildRockWalls(
    const std::vector<RockContourSegment>& segments,
    float heightPx,
    const RockWallParams& params,
    const topology_core::DiamondIsometry& iso);

} // namespace render_core
