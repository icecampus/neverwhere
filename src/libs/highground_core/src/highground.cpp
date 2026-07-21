#include "pch.h"

#include "highground_core/highground.h"

#include "highground_core/inspect.h"
#include "walls.h"

namespace highground {
namespace {

// Per-cell raised top as mask-shaped triangles (used for non-rock walls and
// as the fallback when contour triangulation does not apply): full quadrant
// for an edge with both nodes on, half-quadrant at the on corner of a
// transition edge — the same "axis-parallel corner" contour rule as the
// walls, so the top matches the wall contour.
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

// Contour-formed top: triangulate the wall-top boundary loops (outers with
// holes bridged into them). Returns false when the construction does not
// apply (the caller then falls back to per-cell tops).
bool appendContourTop(
    std::vector<Vertex>& verts,
    std::vector<float>& depths,
    const std::vector<std::vector<glm::vec2>>& chains,
    const Params& params) {

    if (chains.empty()) {
        return false;
    }

    // All chains of one landmass are walked with land on the left, so outer
    // chains share the winding of the largest-area chain; an opposite winding
    // means a hole.
    float refArea = 0.0f;
    for (const auto& poly : chains) {
        const float area = polygonSignedArea(poly);
        if (std::abs(area) > std::abs(refArea)) {
            refArea = area;
        }
    }
    std::vector<const std::vector<glm::vec2>*> outers;
    std::vector<const std::vector<glm::vec2>*> holes;
    for (const auto& poly : chains) {
        if (polygonSignedArea(poly) * refArea > 0.0f) {
            outers.push_back(&poly);
        } else {
            holes.push_back(&poly);
        }
    }

    std::vector<std::vector<glm::vec2>> mergedPolys;
    mergedPolys.reserve(outers.size());
    std::vector<char> paired(holes.size(), 0);
    for (const auto* outer : outers) {
        std::vector<glm::vec2> merged = *outer;
        for (std::size_t h = 0; h < holes.size(); ++h) {
            if (pointInPolygon(*outer, holes[h]->front())) {
                merged = mergeHoleIntoOuter(merged, *holes[h]);
                if (merged.empty()) {
                    return false;
                }
                paired[h] = 1;
            }
        }
        mergedPolys.push_back(std::move(merged));
    }
    // Every hole must land inside some outer chain.
    for (const char p : paired) {
        if (!p) {
            return false;
        }
    }

    const float lift = params.height;
    for (const auto& poly : mergedPolys) {
        std::vector<glm::vec2> tris = triangulateSimplePolygon(poly);
        if (tris.empty()) {
            return false;
        }
        for (std::size_t v = 0; v + 2 < tris.size(); v += 3) {
            const glm::vec2& a = tris[v];
            const glm::vec2& b = tris[v + 1];
            const glm::vec2& c = tris[v + 2];
            const auto emit = [&](const glm::vec2& p) {
                Vertex out;
                out.pos = {p.x, p.y - lift};
                out.uv = {p.x * params.topUvPerWorldPx, p.y * params.topUvPerWorldPx};
                out.color = params.topTint;
                return out;
            };
            verts.push_back(emit(a));
            verts.push_back(emit(b));
            verts.push_back(emit(c));
            depths.push_back(std::max({a.y, b.y, c.y}));
        }
    }
    return true;
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
    std::vector<std::vector<glm::vec2>> topChains;
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
        topChains = std::move(build.topChains);
    } else {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            appendFlatWalls(wallVerts, wallDepths, iso, cells[i], masks[i], params.height);
        }
    }

    // Tops: contour-formed from the wall-top loops, per-cell fallback.
    std::vector<Vertex> topVerts;
    std::vector<float> topDepths;
    if (!params.rockWalls || !appendContourTop(topVerts, topDepths, topChains, params)) {
        for (std::size_t i = 0; i < cells.size(); ++i) {
            appendPerCellTop(topVerts, topDepths, iso, cells[i], masks[i], params);
        }
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
