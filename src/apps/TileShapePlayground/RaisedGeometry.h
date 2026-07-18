#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "DiamondIso.h"

// Contour of the raised land inside one cell.
//
// Built with the "axis-parallel corner" rule (same as the flat yellow tiles):
// for each diamond edge with exactly one endpoint node "on", the contour goes
// from the edge midpoint (0.5 along the edge) to the cell center, parallel to
// a grid axis in map space. Segments of adjacent edges meet at the center,
// forming a 90 degree corner in orthogonal projection (skewed in iso).
struct ContourSegment {
    glm::vec2 edgeMid; // field-space point on the cell edge
    glm::vec2 center;  // field-space cell center
    int axis = 0;      // 0: segment parallel to grid X; 1: parallel to grid Y (map space)
};

// Mask order matches LandBrush::nodeMaskAt: [Left, Up, Right, Down].
std::vector<ContourSegment> cellContourSegments(
    const DiamondIso& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask);
