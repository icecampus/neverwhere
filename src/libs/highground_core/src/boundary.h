#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "highground_core/highground.h"
#include "highground_core/inspect.h"
#include "topology_core/diamond_isometry.h"

// Private: contour chain walking shared by the wall builder and the public
// boundaryLoops() API.
namespace highground {

// Contour segment in travel order (the chain walk may swap endpoints).
struct ContourMapSeg {
    glm::ivec2 aKey{}, bKey{}; // half-grid endpoint keys (2 * map)
    glm::vec2 a{}, b{};        // map space endpoints (travel order)
    glm::vec2 outward{};       // unit outward normal (axial), map space
    bool visited = false;
};

struct ContourChain {
    std::vector<int> segs; // indices into the shared segment array, travel order
    bool closed = false;
    int diagonalJoins = 0;
};

// Build closed polylines over shared endpoints, walked with land on the left
// (outward normals on the right of travel; at diagonal joins the continuation
// is chosen by outward-normal consistency so neighbouring loops are not
// walked backwards). `segs` is filled with the travel-oriented segments the
// returned chains index into.
std::vector<ContourChain> buildContourChains(
    const std::vector<RockContourSegment>& segments,
    std::vector<ContourMapSeg>& segs);

// Field projection for fractional map coordinates — the same affine as
// DiamondIsometry::nodeToField (contour endpoints sit on half-integer coords).
glm::vec2 mapToFieldPx(const topology_core::DiamondIsometry& iso, const glm::vec2& map);

} // namespace highground
