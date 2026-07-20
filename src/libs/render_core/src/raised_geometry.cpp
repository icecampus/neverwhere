#include "raised_geometry.h"

namespace render_core {

std::vector<ContourSegment> cellContourSegments(
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask) {

    // Diamond edges as index pairs into the [Left, Up, Right, Down] mask.
    static constexpr int kEdges[4][2] = {
        {0, 1}, // Left-Up
        {1, 2}, // Up-Right
        {2, 3}, // Right-Down
        {3, 0}, // Down-Left
    };
    // Segment from edge midpoint to center is parallel to grid X for edges
    // Left-Up / Right-Down (edges themselves run along grid Y), and to grid Y
    // for edges Up-Right / Down-Left.
    static constexpr int kEdgeAxis[4] = {0, 1, 0, 1};

    std::vector<ContourSegment> out;
    out.reserve(4);

    const auto nodes = topology_core::DiamondIsometry::cellCornerNodes(cell);
    const glm::vec2 center = iso.mapToField(cell);

    for (int e = 0; e < 4; ++e) {
        const int a = kEdges[e][0];
        const int b = kEdges[e][1];
        if (mask[a] == mask[b]) {
            continue; // both on (interior) or both off (no land) — no contour
        }
        ContourSegment seg;
        seg.edgeMid = (iso.nodeToField(nodes[a]) + iso.nodeToField(nodes[b])) * 0.5f;
        seg.center = center;
        seg.axis = kEdgeAxis[e];
        out.push_back(seg);
    }
    return out;
}

} // namespace render_core
