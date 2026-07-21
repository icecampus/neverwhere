#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "highground_core/highground.h"
#include "highground_core/inspect.h"

#include "topology_core/diamond_isometry.h"

namespace highground {

// Result of the wall construction: baked wall triangles (final field space,
// height applied) with one iso depth key per quad (6 vertices).
struct WallBuild {
    std::vector<Vertex> verts;   // triangles
    std::vector<float> depths;   // one per 6 verts (quad)
};

// Rock cliff walls: production wall rules (block-cliff) on the tile contour
// topology. Global contour chains walked with land on the left (normal-
// consistent at diagonal joins), 45-degree bevels at convex corners, ridged
// noise displacement with terrace steps, seam envelope (zero offset at
// top/bottom edges), baked colors.
WallBuild buildRockWalls(
    const std::vector<RockContourSegment>& segments,
    const Params& params,
    const topology_core::DiamondIsometry& iso);

// Simple flat cliff walls: a vertical quad under each contour segment — top
// edge follows the land contour lifted by height, bottom edge is the same
// segment at ground. Two brightness levels by grid axis fake iso lighting;
// bottom is darker. Appends quads (6 vertices each) + depths.
void appendFlatWalls(
    std::vector<Vertex>& verts,
    std::vector<float>& depths,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask,
    float heightPx);

} // namespace highground
