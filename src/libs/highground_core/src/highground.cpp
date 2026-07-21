#include "pch.h"

#include "highground_core/highground.h"

#include "highground_core/inspect.h"
#include "walls.h"

namespace highground {
namespace {

// Per-cell raised top as mask-shaped triangles: full quadrant for an edge
// with both nodes on, half-quadrant at the on corner of a transition edge —
// the same "axis-parallel corner" contour rule as the walls, so the top
// matches the wall contour. Single-corner cells get their convex corner at
// the cell center chamfered exactly like the wall bevel.
void appendPerCellTop(
    std::vector<Vertex>& verts,
    std::vector<float>& depths,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask,
    const Params& params) {

    const auto corners = iso.cellDiamondCorners(cell); // [Left, Up, Right, Down]
    const glm::vec2 center = iso.mapToField(cell);
    const float lift = params.height;

    const auto emit = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        const auto v = [&](const glm::vec2& p) {
            Vertex out;
            out.pos = {p.x, p.y - lift};
            out.uv = {p.x * params.topUvPerWorldPx, p.y * params.topUvPerWorldPx};
            out.color = params.topTint;
            return out;
        };
        verts.push_back(v(a));
        verts.push_back(v(b));
        verts.push_back(v(c));
        depths.push_back(std::max({a.y, b.y, c.y}));
    };

    const int onCount = (mask[0] ? 1 : 0) + (mask[1] ? 1 : 0) + (mask[2] ? 1 : 0) + (mask[3] ? 1 : 0);
    if (onCount == 1) {
        // Single-corner wedge (the cell's only on-node at slot k): pentagon
        // midA -> corner -> midB -> pB -> pA with the center corner chamfered
        // exactly like the wall bevel (trim = min(bevel, len*0.45) along the
        // mid->center segments, len = 0.5 cell) — no "lid" overhang.
        const int k = mask[0] ? 0 : (mask[1] ? 1 : (mask[2] ? 2 : 3));
        const glm::vec2& corner = corners[k];
        const glm::vec2 midA = (corner + corners[(k + 3) % 4]) * 0.5f;
        const glm::vec2 midB = (corner + corners[(k + 1) % 4]) * 0.5f;
        const float f = std::min(2.0f * params.bevel, 0.45f);
        const glm::vec2 pA = center + (midA - center) * f;
        const glm::vec2 pB = center + (midB - center) * f;
        emit(midA, corner, midB);
        emit(midA, midB, pB);
        emit(midA, pB, pA);
        return;
    }

    // Diamond edges as index pairs into the [Left, Up, Right, Down] mask —
    // same table as cellContourSegments, so the top matches the wall contour.
    static constexpr int kEdges[4][2] = {
        {0, 1}, // Left-Up
        {1, 2}, // Up-Right
        {2, 3}, // Right-Down
        {3, 0}, // Down-Left
    };

    for (const auto& edge : kEdges) {
        const int a = edge[0];
        const int b = edge[1];
        if (mask[a] && mask[b]) {
            emit(center, corners[a], corners[b]); // full quadrant
        } else if (mask[a]) {
            emit(center, corners[a], (corners[a] + corners[b]) * 0.5f); // half at corner a
        } else if (mask[b]) {
            emit(center, (corners[a] + corners[b]) * 0.5f, corners[b]); // half at corner b
        }
    }
}

} // namespace

Grid makeGrid(const glm::ivec2* onNodes, std::size_t count, int margin) {
    Grid grid;
    if (count == 0 || onNodes == nullptr) {
        return grid;
    }
    glm::ivec2 lo = onNodes[0];
    glm::ivec2 hi = onNodes[0];
    for (std::size_t i = 1; i < count; ++i) {
        lo = glm::min(lo, onNodes[i]);
        hi = glm::max(hi, onNodes[i]);
    }
    grid.originX = lo.x - margin;
    grid.originY = lo.y - margin;
    grid.width = (hi.x - lo.x) + 1 + margin * 2;
    grid.height = (hi.y - lo.y) + 1 + margin * 2;
    grid.nodes.assign(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height), 0);
    for (std::size_t i = 0; i < count; ++i) {
        const int x = onNodes[i].x - grid.originX;
        const int y = onNodes[i].y - grid.originY;
        grid.nodes[static_cast<std::size_t>(y) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(x)] = 1;
    }
    return grid;
}

Mesh generate(const Grid& grid, const Params& params) {
    Mesh mesh;
    if (grid.width <= 0 || grid.height <= 0) {
        return mesh;
    }

    topology_core::DiamondIsometry iso;
    iso.dims.cellWidth = params.cellWidth;
    iso.dims.aspectRatio = params.cellWidth / params.cellHeight;

    // Land cells + corner-node masks: a cell is land when any of its 4 corner
    // nodes is on. Cells touched by the node window (one ring outside it).
    std::vector<glm::ivec2> cells;
    std::vector<std::array<bool, 4>> masks;
    for (int cy = grid.originY - 1; cy <= grid.originY + grid.height; ++cy) {
        for (int cx = grid.originX - 1; cx <= grid.originX + grid.width; ++cx) {
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes({cx, cy});
            const std::array<bool, 4> mask{
                grid.at(corners[0]),
                grid.at(corners[1]),
                grid.at(corners[2]),
                grid.at(corners[3]),
            };
            if (mask[0] || mask[1] || mask[2] || mask[3]) {
                cells.push_back({cx, cy});
                masks.push_back(mask);
            }
        }
    }
    if (cells.empty()) {
        return mesh;
    }

    // Walls.
    std::vector<Vertex> wallVerts;
    std::vector<float> wallDepths;
    if (params.rockWalls) {
        std::vector<RockContourSegment> segments;
        segments.reserve(cells.size() * 2);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            std::vector<RockContourSegment> segs = cellRockContourSegments(cells[i], masks[i]);
            segments.insert(segments.end(), segs.begin(), segs.end());
        }
        WallBuild build = buildRockWalls(segments, params, iso);
        wallVerts = std::move(build.verts);
        wallDepths = std::move(build.depths);
    } else {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            appendFlatWalls(wallVerts, wallDepths, iso, cells[i], masks[i], params.height);
        }
    }

    // Tops: per-cell mask-shaped triangles, single-corner cells chamfered at
    // the center to match the wall bevel.
    std::vector<Vertex> topVerts;
    std::vector<float> topDepths;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        appendPerCellTop(topVerts, topDepths, iso, cells[i], masks[i], params);
    }

    // Assemble the mesh: one vertex array, primitives referencing it.
    const std::uint32_t wallBase = 0;
    const std::uint32_t topBase = static_cast<std::uint32_t>(wallVerts.size());
    mesh.vertices.reserve(wallVerts.size() + topVerts.size());
    mesh.vertices.insert(mesh.vertices.end(), std::make_move_iterator(wallVerts.begin()), std::make_move_iterator(wallVerts.end()));
    mesh.vertices.insert(mesh.vertices.end(), std::make_move_iterator(topVerts.begin()), std::make_move_iterator(topVerts.end()));

    mesh.primitives.reserve(wallDepths.size() + topDepths.size());
    for (std::uint32_t i = 0; i < wallDepths.size(); ++i) {
        Primitive prim;
        prim.material = Material::Wall;
        prim.depth = wallDepths[i];
        prim.first = wallBase + i * 6;
        prim.count = 6;
        mesh.primitives.push_back(prim);
    }
    for (std::uint32_t i = 0; i < topDepths.size(); ++i) {
        Primitive prim;
        prim.material = Material::Top;
        prim.depth = topDepths[i];
        prim.first = topBase + i * 3;
        prim.count = 3;
        mesh.primitives.push_back(prim);
    }

    if (params.sortPrimitives) {
        std::stable_sort(mesh.primitives.begin(), mesh.primitives.end(), [](const Primitive& a, const Primitive& b) {
            if (a.depth != b.depth) return a.depth < b.depth;
            return static_cast<int>(a.material) > static_cast<int>(b.material); // walls before tops on ties
        });
    }
    return mesh;
}

} // namespace highground
