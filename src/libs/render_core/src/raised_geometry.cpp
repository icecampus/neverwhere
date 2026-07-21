#include "raised_geometry.h"

#include <algorithm>
#include <limits>

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

namespace render_core {

float polygonSignedArea(const std::vector<glm::vec2>& polygon) {
    float area = 0.0f;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2& a = polygon[i];
        const glm::vec2& b = polygon[(i + 1) % n];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

std::vector<glm::vec2> triangulateSimplePolygon(const std::vector<glm::vec2>& polygon) {
    constexpr float kEps = 1e-4f;

    // Working vertex list without consecutive duplicates.
    std::vector<glm::vec2> poly;
    poly.reserve(polygon.size());
    for (const glm::vec2& p : polygon) {
        if (poly.empty() || glm::length(p - poly.back()) > kEps) {
            poly.push_back(p);
        }
    }
    if (poly.size() > 1 && glm::length(poly.front() - poly.back()) <= kEps) {
        poly.pop_back();
    }
    if (poly.size() < 3) {
        return {};
    }

    // Normalize orientation: work with positive signed area (interior on the
    // left of every edge), so a convex vertex has cross(prev->cur, cur->next) > 0.
    if (polygonSignedArea(poly) < 0.0f) {
        std::reverse(poly.begin(), poly.end());
    }

    const auto cross = [](const glm::vec2& a, const glm::vec2& b) {
        return a.x * b.y - a.y * b.x;
    };
    const auto pointInTriangle = [&](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        // Strict test: points ON an edge do not block the ear. Contour chains
        // have many collinear vertices (per-cell pieces along straight runs),
        // and a blocking-on-touch rule could leave no clippable ear.
        const float d0 = cross(b - a, p - a);
        const float d1 = cross(c - b, p - b);
        const float d2 = cross(a - c, p - c);
        return d0 > kEps && d1 > kEps && d2 > kEps;
    };

    std::vector<glm::vec2> triangles;
    triangles.reserve((poly.size() - 2) * 3);

    // Ear clipping: repeatedly cut convex vertices whose triangle contains no
    // other polygon vertex. Bail out (empty result) if no ear is found — the
    // caller then falls back to a different top construction.
    for (std::size_t guard = poly.size() * poly.size() + 8; poly.size() > 3 && guard > 0; --guard) {
        bool clipped = false;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const std::size_t prev = (i + poly.size() - 1) % poly.size();
            const std::size_t next = (i + 1) % poly.size();
            const glm::vec2& a = poly[prev];
            const glm::vec2& b = poly[i];
            const glm::vec2& c = poly[next];
            if (cross(b - a, c - b) <= kEps) {
                continue; // concave or degenerate vertex
            }
            bool ear = true;
            for (std::size_t j = 0; j < poly.size(); ++j) {
                if (j == prev || j == i || j == next) {
                    continue;
                }
                if (pointInTriangle(poly[j], a, b, c)) {
                    ear = false;
                    break;
                }
            }
            if (!ear) {
                continue;
            }
            triangles.push_back(a);
            triangles.push_back(b);
            triangles.push_back(c);
            poly.erase(poly.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) {
            return {}; // no ear found — degenerate/self-intersecting input
        }
    }
    if (poly.size() == 3) {
        triangles.push_back(poly[0]);
        triangles.push_back(poly[1]);
        triangles.push_back(poly[2]);
    }
    return triangles;
}

bool pointInPolygon(const std::vector<glm::vec2>& polygon, const glm::vec2& point) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const glm::vec2& a = polygon[i];
        const glm::vec2& b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<glm::vec2> mergeHoleIntoOuter(
    const std::vector<glm::vec2>& outer,
    const std::vector<glm::vec2>& hole) {

    if (outer.size() < 3 || hole.size() < 3) {
        return {};
    }

    // Rightmost hole vertex (max x, ties by max y).
    std::size_t m = 0;
    for (std::size_t i = 1; i < hole.size(); ++i) {
        if (hole[i].x > hole[m].x || (hole[i].x == hole[m].x && hole[i].y > hole[m].y)) {
            m = i;
        }
    }
    const glm::vec2 start = hole[m];

    // Closest +x ray hit over the outer polygon's edges.
    float bestX = std::numeric_limits<float>::max();
    std::size_t bestEdge = 0;
    glm::vec2 hit{};
    for (std::size_t e = 0; e < outer.size(); ++e) {
        const glm::vec2& a = outer[e];
        const glm::vec2& b = outer[(e + 1) % outer.size()];
        const float dy = b.y - a.y;
        if (std::abs(dy) < 1e-9f) {
            continue;
        }
        const float s = (start.y - a.y) / dy;
        if (s < 0.0f || s >= 1.0f) {
            continue;
        }
        const float x = a.x + s * (b.x - a.x);
        if (x > start.x + 1e-6f && x < bestX) {
            bestX = x;
            bestEdge = e;
            hit = {x, start.y};
        }
    }
    if (bestX == std::numeric_limits<float>::max()) {
        return {};
    }

    // Merged walk: outer up to the hit edge, bridge to the hole, full hole
    // loop back to the start vertex, bridge back, rest of the outer.
    std::vector<glm::vec2> merged;
    merged.reserve(outer.size() + hole.size() + 4);
    for (std::size_t i = 0; i <= bestEdge; ++i) {
        merged.push_back(outer[i]);
    }
    merged.push_back(hit);
    merged.push_back(start);
    for (std::size_t k = 1; k < hole.size(); ++k) {
        merged.push_back(hole[(m + k) % hole.size()]);
    }
    merged.push_back(start);
    merged.push_back(hit);
    for (std::size_t i = bestEdge + 1; i < outer.size(); ++i) {
        merged.push_back(outer[i]);
    }
    return merged;
}

} // namespace render_core
