#include "pch.h"

#include "highground_core/highground.h"

#include "boundary.h"
#include "walls.h"

// CGAL-based generator: the land region is a boolean UNION of unit squares
// (map space) centered on the "on" nodes, so the top surface is triangulated
// over the region boundary (holes included) by a constrained Delaunay
// triangulation — no per-cell mask stitching, no "square lid" artifacts by
// construction. Walls are extruded from the same region loops. The walls
// chamfer convex corners (params.bevel, 45-degree cuts), so the top is
// triangulated over the SAME beveled contour the wall tops follow
// (beveledLoops) — the top ends flush with the walls' top edge instead of
// overhanging the chamfers. (A straight-skeleton offset was considered and
// rejected: it is a miter offset — corners stay sharp and slide along the
// bisectors instead of being cut by a 45-degree segment.)
//
// With params.smoothIterations > 0 the region loops go through Chaikin
// corner-cutting FIRST (before both the walls and the top), softening the
// whole-cell staircase of the orthogonal contour while keeping the
// wall/top junction exact by construction.
//
// The whole region math sits in map space (cell = 1 unit, all coordinates
// exact multiples of 0.5 in double), so EPICK (exact predicates, inexact
// constructions) is fully robust here and no GMP arithmetic is needed.
//
// Built only when CMake finds CGAL (HIGHGROUND_WITH_CGAL); otherwise the
// entry points compile into inert stubs so desktop-only experimentation does
// not break the Emscripten build.

#ifdef HIGHGROUND_WITH_CGAL

#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Constrained_triangulation_face_base_2.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Polygon_2.h>
#include <CGAL/Polygon_set_2.h>
#include <CGAL/Polygon_with_holes_2.h>
#include <CGAL/Triangulation_data_structure_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Triangulation_vertex_base_2.h>

#include <map>
#include <queue>
#include <utility>

namespace highground {
namespace {

using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using CgalPoint = K::Point_2;
using CgalPolygon = CGAL::Polygon_2<K>;
using CgalPwh = CGAL::Polygon_with_holes_2<K>;
using CgalPolygonSet = CGAL::Polygon_set_2<K>;

using CdtVb = CGAL::Triangulation_vertex_base_2<K>;
using CdtFb = CGAL::Constrained_triangulation_face_base_2<K>;
using CdtFbi = CGAL::Triangulation_face_base_with_info_2<int, K, CdtFb>;
using CdtTds = CGAL::Triangulation_data_structure_2<CdtVb, CdtFbi>;
using Cdt = CGAL::Constrained_Delaunay_triangulation_2<K, CdtTds, CGAL::Exact_predicates_tag>;

double signedArea(const std::vector<glm::vec2>& loop) {
    double area = 0.0;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const glm::vec2& a = loop[i];
        const glm::vec2& b = loop[(i + 1) % loop.size()];
        area += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return area * 0.5;
}

// Half-grid key for pinch-point detection (region coords are multiples of 0.5).
glm::ivec2 regionKey(const glm::vec2& p) {
    return {static_cast<int>(std::lround(p.x * 2.0f)), static_cast<int>(std::lround(p.y * 2.0f))};
}

// CGAL boolean union can emit weakly-simple boundaries (a figure-eight loop
// through a pinch vertex twice). Split those into strictly-simple loops.
void appendSplitLoops(const CgalPolygon& polygon, std::vector<std::vector<glm::vec2>>& out) {
    std::vector<glm::vec2> stack;
    std::vector<glm::ivec2> keys;
    const auto flush = [&](std::size_t from) {
        if (stack.size() - from >= 3) {
            out.emplace_back(stack.begin() + static_cast<std::ptrdiff_t>(from), stack.end());
        }
    };
    for (auto it = polygon.vertices_begin(); it != polygon.vertices_end(); ++it) {
        const glm::vec2 p{static_cast<float>(CGAL::to_double(it->x())), static_cast<float>(CGAL::to_double(it->y()))};
        const glm::ivec2 key = regionKey(p);
        const auto found = std::find(keys.begin(), keys.end(), key);
        if (found != keys.end()) {
            const std::size_t idx = static_cast<std::size_t>(found - keys.begin());
            flush(idx);
            stack.resize(idx + 1);
            keys.resize(idx + 1);
        } else {
            stack.push_back(p);
            keys.push_back(key);
        }
    }
    flush(0);
}

// Drop vertices lying exactly on the segment between their neighbors.
void dropCollinear(std::vector<glm::vec2>& loop) {
    for (bool changed = true; changed && loop.size() > 3;) {
        changed = false;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const glm::vec2& a = loop[(i + loop.size() - 1) % loop.size()];
            const glm::vec2& b = loop[i];
            const glm::vec2& c = loop[(i + 1) % loop.size()];
            const glm::vec2 seg = c - a;
            const float len = glm::length(seg);
            const float cross = (b.x - a.x) * seg.y - (b.y - a.y) * seg.x;
            const float dist = len > 1e-6f ? std::abs(cross) / len : 0.0f;
            if (dist > 0.001f) {
                continue;
            }
            if (b.x < std::min(a.x, c.x) - 0.001f || b.x > std::max(a.x, c.x) + 0.001f ||
                b.y < std::min(a.y, c.y) - 0.001f || b.y > std::max(a.y, c.y) + 0.001f) {
                continue;
            }
            loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
            changed = true;
            break;
        }
    }
}

// One Chaikin corner-cutting pass over a CLOSED loop (map space): every edge
// (a,b) emits q = 0.75a + 0.25b and r = 0.25a + 0.75b, so each iteration
// doubles the vertex count. Deterministic; convex corners shrink inwards,
// reflex ones bulge outwards slightly (never more than 1/4 of the adjacent
// edges per pass).
std::vector<glm::vec2> chaikinOnce(const std::vector<glm::vec2>& loop) {
    const std::size_t n = loop.size();
    std::vector<glm::vec2> out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2& a = loop[i];
        const glm::vec2& b = loop[(i + 1) % n];
        out.push_back(a * 0.75f + b * 0.25f);
        out.push_back(a * 0.25f + b * 0.75f);
    }
    return out;
}

// Land region as strictly-simple loops in map space, every loop walked with
// land on the left (positive signed area; holes are reversed to match, so
// wall outward normals come out uniformly as "right of travel").
std::vector<std::vector<glm::vec2>> buildRegionLoops(const Grid& grid) {
    CgalPolygonSet polys;
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            if (!grid.at(x, y)) {
                continue;
            }
            const double nx = grid.originX + x;
            const double ny = grid.originY + y;
            const CgalPoint corners[4] = {
                {nx - 0.5, ny - 0.5},
                {nx + 0.5, ny - 0.5},
                {nx + 0.5, ny + 0.5},
                {nx - 0.5, ny + 0.5},
            };
            polys.join(CgalPolygon(corners, corners + 4));
        }
    }

    std::vector<CgalPwh> pwhs;
    polys.polygons_with_holes(std::back_inserter(pwhs));

    std::vector<std::vector<glm::vec2>> loops;
    for (const CgalPwh& pwh : pwhs) {
        appendSplitLoops(pwh.outer_boundary(), loops);
        for (auto hit = pwh.holes_begin(); hit != pwh.holes_end(); ++hit) {
            appendSplitLoops(*hit, loops);
        }
    }
    for (std::vector<glm::vec2>& loop : loops) {
        dropCollinear(loop);
        if (signedArea(loop) < 0.0) {
            std::reverse(loop.begin(), loop.end());
        }
    }
    loops.erase(
        std::remove_if(loops.begin(), loops.end(), [](const std::vector<glm::vec2>& loop) { return loop.size() < 3; }),
        loops.end());
    return loops;
}

// Region loops -> wall segments walked with land on the left (the contract
// buildRockWalls / buildContourChains already expect).
std::vector<RockContourSegment> regionWallSegments(const std::vector<std::vector<glm::vec2>>& loops) {
    std::vector<RockContourSegment> segments;
    for (const std::vector<glm::vec2>& loop : loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const glm::vec2 a = loop[i];
            const glm::vec2 b = loop[(i + 1) % loop.size()];
            const glm::vec2 d = b - a;
            const float len = glm::length(d);
            if (len < 1e-6f) {
                continue;
            }
            RockContourSegment seg;
            seg.a = a;
            seg.b = b;
            seg.outward = {d.y / len, -d.x / len}; // right of travel = away from land
            segments.push_back(seg);
        }
    }
    return segments;
}

// Constrained Delaunay over the region loops; faces are classified by
// nesting level (BFS from the infinite face, parity flips across constrained
// edges), so holes and disconnected islands come out right.
std::vector<std::uint32_t> triangulateRegion(
    const std::vector<std::vector<glm::vec2>>& loops,
    std::vector<glm::vec2>& outPoints) {

    std::map<std::pair<double, double>, std::uint32_t> ids;
    std::vector<std::pair<double, double>> points;
    const auto idOf = [&](double x, double y) {
        const std::pair<double, double> key{x, y};
        const auto [it, inserted] = ids.emplace(key, static_cast<std::uint32_t>(points.size()));
        if (inserted) {
            points.push_back(key);
        }
        return it->second;
    };

    Cdt cdt;
    for (const std::vector<glm::vec2>& loop : loops) {
        std::vector<CgalPoint> pts;
        pts.reserve(loop.size());
        for (const glm::vec2& p : loop) {
            (void)idOf(p.x, p.y);
            pts.emplace_back(p.x, p.y);
        }
        cdt.insert_constraint(pts.begin(), pts.end(), true);
    }

    for (auto f = cdt.all_faces_begin(); f != cdt.all_faces_end(); ++f) {
        f->info() = -1;
    }
    std::queue<Cdt::Face_handle> queue;
    Cdt::Face_circulator fc = cdt.incident_faces(cdt.infinite_vertex());
    const Cdt::Face_circulator done(fc);
    do {
        fc->info() = 0;
        queue.push(fc);
    } while (++fc != done);
    while (!queue.empty()) {
        const Cdt::Face_handle f = queue.front();
        queue.pop();
        for (int i = 0; i < 3; ++i) {
            const Cdt::Face_handle n = f->neighbor(i);
            if (n->info() != -1) {
                continue;
            }
            n->info() = f->info() + (cdt.is_constrained(Cdt::Edge(f, i)) ? 1 : 0);
            queue.push(n);
        }
    }

    std::vector<std::uint32_t> tris;
    for (auto f = cdt.finite_faces_begin(); f != cdt.finite_faces_end(); ++f) {
        if (f->info() % 2 == 0) {
            continue; // outside the land or inside a hole
        }
        for (int i = 0; i < 3; ++i) {
            const CgalPoint& p = f->vertex(i)->point();
            tris.push_back(idOf(p.x(), p.y()));
        }
    }

    outPoints.clear();
    outPoints.reserve(points.size());
    for (const auto& [x, y] : points) {
        outPoints.emplace_back(static_cast<float>(x), static_cast<float>(y));
    }
    return tris;
}

void appendFlatWallsFromLoops(
    std::vector<Vertex>& verts,
    std::vector<float>& depths,
    const topology_core::DiamondIsometry& iso,
    const std::vector<std::vector<glm::vec2>>& loops,
    float heightPx) {

    for (const std::vector<glm::vec2>& loop : loops) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const glm::vec2 aMap = loop[i];
            const glm::vec2 bMap = loop[(i + 1) % loop.size()];
            const glm::vec2 a = mapToFieldPx(iso, aMap);
            const glm::vec2 b = mapToFieldPx(iso, bMap);
            // Same two-level axis shading as the per-cell flat walls.
            const glm::vec3 base = std::abs(bMap.x - aMap.x) >= std::abs(bMap.y - aMap.y)
                ? glm::vec3(0.62f, 0.45f, 0.22f)
                : glm::vec3(0.45f, 0.32f, 0.16f);
            const glm::vec4 top{base, 1.0f};
            const glm::vec4 bottom{base * 0.7f, 1.0f};

            // Unit outward normal in map space (loops are CCW = land on the
            // left), same formula as regionWallSegments.
            const glm::vec2 dMap = bMap - aMap;
            const float len = glm::length(dMap);
            const glm::vec2 outward = len > 1e-6f ? glm::vec2(dMap.y / len, -dMap.x / len) : glm::vec2(0.0f);

            const auto v = [&](const glm::vec2& p, float lift, const glm::vec4& c) {
                Vertex out;
                out.pos = {p.x, p.y - lift};
                out.uv = {0.0f, 0.0f};
                out.color = c;
                out.groundY = p.y;
                out.normal = outward;
                return out;
            };
            const Vertex t0 = v(a, heightPx, top);
            const Vertex t1 = v(b, heightPx, top);
            const Vertex b0 = v(a, 0.0f, bottom);
            const Vertex b1 = v(b, 0.0f, bottom);
            verts.push_back(t0);
            verts.push_back(b0);
            verts.push_back(b1);
            verts.push_back(t0);
            verts.push_back(b1);
            verts.push_back(t1);
            depths.push_back(std::max(a.y, b.y));
        }
    }
}

} // namespace

bool cgalAvailable() {
    return true;
}

Mesh generateCgal(const Grid& grid, const Params& params) {
    Mesh mesh;
    if (grid.width <= 0 || grid.height <= 0) {
        return mesh;
    }

    std::vector<std::vector<glm::vec2>> loops = buildRegionLoops(grid);
    if (loops.empty()) {
        return mesh;
    }

    // Chaikin smoothing of the region loops BEFORE both consumers (walls and
    // top): they share the very same contour, so the wall/top junction stays
    // exact by construction. Holes go through the same code path.
    for (std::vector<glm::vec2>& loop : loops) {
        for (int it = 0; it < params.smoothIterations; ++it) {
            if (loop.size() >= 3) {
                loop = chaikinOnce(loop);
            }
        }
    }

    // Smoothing replaces the bevel: the wall builder's 45-degree chamfer
    // would cut every Chaikin mini-corner a second time (a full
    // min(bevel, len*0.45) trim on each long enough smoothed edge), which
    // eats the contour at the corner nodes and breaks both the top coverage
    // and the wall/top junction. With smoothing on, both consumers see an
    // effective bevel of 0 (the contour is already smooth).
    const float effectiveBevel = params.smoothIterations > 0 ? 0.0f : params.bevel;

    topology_core::DiamondIsometry iso;
    iso.dims.cellWidth = params.cellWidth;
    iso.dims.aspectRatio = params.cellWidth / params.cellHeight;

    // Walls from the exact region loops; they are NOT modified for the top —
    // the chamfer lives in their bevel pieces.
    const std::vector<RockContourSegment> wallSegments = regionWallSegments(loops);
    std::vector<Vertex> wallVerts;
    std::vector<float> wallDepths;
    if (params.rockWalls) {
        highground::Params wallParams = params;
        wallParams.bevel = effectiveBevel;
        WallBuild build = buildRockWalls(wallSegments, wallParams, iso);
        wallVerts = std::move(build.verts);
        wallDepths = std::move(build.depths);
    } else {
        appendFlatWallsFromLoops(wallVerts, wallDepths, iso, loops, params.height);
    }

    // Top: constrained Delaunay (holes respected) over the loops chamfered
    // exactly like the wall tops — the same bevel pieces the wall builder
    // emits — so the top ends flush with the walls' top edge instead of
    // overhanging the 45-degree chamfers. Flat walls do not chamfer, so the
    // raw region loops are used.
    const bool chamferTop = params.rockWalls && effectiveBevel > 0.0f;
    const std::vector<std::vector<glm::vec2>> topLoops =
        chamferTop ? beveledLoops(wallSegments, effectiveBevel) : loops;

    std::vector<glm::vec2> topPoints;
    const std::vector<std::uint32_t> tris = triangulateRegion(topLoops, topPoints);
    std::vector<glm::vec2> fieldPoints(topPoints.size());
    for (std::size_t i = 0; i < topPoints.size(); ++i) {
        fieldPoints[i] = mapToFieldPx(iso, topPoints[i]);
    }

    std::vector<Vertex> topVerts;
    std::vector<float> topDepths;
    topVerts.reserve(tris.size());
    topDepths.reserve(tris.size() / 3);
    for (std::size_t t = 0; t + 2 < tris.size(); t += 3) {
        float groundY = -std::numeric_limits<float>::max();
        for (int k = 0; k < 3; ++k) {
            const glm::vec2& f = fieldPoints[tris[t + k]];
            Vertex v;
            v.pos = {f.x, f.y - params.height};
            v.uv = {f.x * params.topUvPerWorldPx, f.y * params.topUvPerWorldPx};
            v.color = params.topTint;
            v.groundY = f.y;
            topVerts.push_back(v);
            groundY = std::max(groundY, f.y);
        }
        topDepths.push_back(groundY);
    }

    // Assemble: one vertex array, primitives referencing it (same contract as
    // generate(): walls first, then tops, depth-sorted back-to-front).
    const std::uint32_t topBase = static_cast<std::uint32_t>(wallVerts.size());
    mesh.vertices.reserve(wallVerts.size() + topVerts.size());
    mesh.vertices.insert(mesh.vertices.end(), std::make_move_iterator(wallVerts.begin()), std::make_move_iterator(wallVerts.end()));
    mesh.vertices.insert(mesh.vertices.end(), std::make_move_iterator(topVerts.begin()), std::make_move_iterator(topVerts.end()));

    mesh.primitives.reserve(wallDepths.size() + topDepths.size());
    for (std::uint32_t i = 0; i < wallDepths.size(); ++i) {
        Primitive prim;
        prim.material = Material::Wall;
        prim.depth = wallDepths[i];
        prim.first = i * 6;
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

#else // !HIGHGROUND_WITH_CGAL

namespace highground {

bool cgalAvailable() {
    return false;
}

Mesh generateCgal(const Grid&, const Params&) {
    return {};
}

} // namespace highground

#endif
