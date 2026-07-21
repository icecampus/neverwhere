#include "pch.h"

#include "highground_core/inspect.h"

namespace highground {

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

std::vector<RockContourSegment> cellRockContourSegments(
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask) {

    static constexpr int kEdges[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    const auto nodes = topology_core::DiamondIsometry::cellCornerNodes(cell);
    const glm::vec2 corners[4] = {
        glm::vec2(nodes[0]), // Left
        glm::vec2(nodes[1]), // Up
        glm::vec2(nodes[2]), // Right
        glm::vec2(nodes[3]), // Down
    };
    const glm::vec2 center(static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f);

    std::vector<RockContourSegment> out;
    out.reserve(4);
    for (int e = 0; e < 4; ++e) {
        const int a = kEdges[e][0];
        const int b = kEdges[e][1];
        if (mask[a] == mask[b]) {
            continue; // both on (interior) or both off (no land) — no contour
        }
        const glm::vec2 mid = (corners[a] + corners[b]) * 0.5f;
        const glm::vec2 onCorner = mask[a] ? corners[a] : corners[b];
        const glm::vec2 quadCenter = (center + onCorner) * 0.5f;
        const glm::vec2 segDir = center - mid;
        glm::vec2 n(-segDir.y, segDir.x);
        if (glm::dot(n, mid - quadCenter) < 0.0f) {
            n = -n;
        }
        RockContourSegment seg;
        seg.a = mid;
        seg.b = center;
        seg.outward = glm::normalize(n);
        out.push_back(seg);
    }
    return out;
}

} // namespace highground
