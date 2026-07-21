#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>

#include "topology_core/diamond_isometry.h"

namespace highground {

// Contour of the raised land inside one cell.
//
// Built with the "axis-parallel corner" rule (same as the flat tiles): for
// each diamond edge with exactly one endpoint node "on", the contour goes from
// the edge midpoint (0.5 along the edge) to the cell center, parallel to a
// grid axis in map space. Segments of adjacent edges meet at the center,
// forming a 90 degree corner in orthogonal projection (skewed in iso).
struct ContourSegment {
    glm::vec2 edgeMid; // field-space point on the cell edge
    glm::vec2 center;  // field-space cell center
    int axis = 0;      // 0: segment parallel to grid X; 1: parallel to grid Y (map space)
};

// Mask order matches the vertex-centric landscape contract: [Left, Up, Right,
// Down] — the slot order of DiamondIsometry::cellCornerNodes.
std::vector<ContourSegment> cellContourSegments(
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask);

// One contour segment in map space (cell = 1 unit) with its outward normal —
// same contour rule as above, plus normals for displacement.
struct RockContourSegment {
    glm::vec2 a{}, b{};
    glm::vec2 outward{}; // unit outward normal (axial), map space
};

std::vector<RockContourSegment> cellRockContourSegments(
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask);

// Signed area of a closed polygon (shoelace; positive = counter-clockwise in
// the coordinate system handedness — in y-down field space positive reads as
// visually clockwise). Used to classify boundary chains (outer vs hole).
float polygonSignedArea(const std::vector<glm::vec2>& polygon);

// Ear-clipping triangulation of a simple closed polygon (no holes, no
// self-intersections). Returns triangle vertex triples (3 per triangle) or an
// empty vector for degenerate input / when no ear could be clipped.
std::vector<glm::vec2> triangulateSimplePolygon(const std::vector<glm::vec2>& polygon);

// Standard even-odd point-in-polygon test (ray cast in +x).
bool pointInPolygon(const std::vector<glm::vec2>& polygon, const glm::vec2& point);

// Merge a hole loop into its containing outer polygon with a zero-width
// bridge channel (from the hole's rightmost vertex along +x to the nearest
// outer edge). The result is a weakly simple polygon suitable for
// triangulateSimplePolygon. Empty vector when no bridge could be placed.
std::vector<glm::vec2> mergeHoleIntoOuter(
    const std::vector<glm::vec2>& outer,
    const std::vector<glm::vec2>& hole);

} // namespace highground
