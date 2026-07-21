// highground_core: invariants for the CGAL exact-region generator.
//
// generateCgal() triangulates the boolean union of per-node unit squares, so
// by construction there are no per-cell "square lid" artifacts and holes in
// the region stay open. This suite mirrors highground_fuzz_test.cpp (same
// topology_core::DiamondIsometry mapping, cellWidth 128 / cellHeight 64) and
// asserts: deterministic byte-identical output, every on-node covered by the
// top surface, single-corner cell centers never strictly covered (the lid
// detector), back-to-front primitive order. All tests skip when the library
// was built without CGAL.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/highground.h>
#include <topology_core/diamond_isometry.h>

namespace {

using highground::Grid;
using highground::Material;
using highground::Mesh;
using highground::Params;
using highground::Primitive;

topology_core::DiamondIsometry g_iso;

// Same map->field transform as the generator's mapToFieldPx (private to the
// lib, so re-derived here): field = ((x - y) * halfW + halfW, (x + y) * halfH).
glm::vec2 mapPointToField(const glm::vec2& map) {
    const glm::vec2 cellSz = g_iso.dims.cellSize();
    return {(map.x - map.y) * cellSz.x * 0.5f + cellSz.x * 0.5f, (map.x + map.y) * cellSz.y * 0.5f};
}

// Point-in-triangle tolerances, in cross-product units (field px^2). The
// region math is exact multiples of half-cells, so boundary hits land on
// exact zeros; the epsilon only has to absorb float noise.
constexpr float kInsideEps = 1e-3f;  // inside-or-boundary coverage
constexpr float kStrictEps = 1e-3f;  // strictly-inside detection

std::string nodesStr(const std::vector<glm::ivec2>& nodes) {
    std::string s;
    for (const glm::ivec2& n : nodes) {
        s += " (" + std::to_string(n.x) + "," + std::to_string(n.y) + ")";
    }
    return s;
}

float cross2(const glm::vec2& u, const glm::vec2& v) {
    return u.x * v.y - u.y * v.x;
}

// Inside or on the boundary (either winding), with a small tolerance.
bool pointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const float d0 = cross2(b - a, p - a);
    const float d1 = cross2(c - b, p - b);
    const float d2 = cross2(a - c, p - c);
    const bool neg = (d0 < -kInsideEps) || (d1 < -kInsideEps) || (d2 < -kInsideEps);
    const bool pos = (d0 > kInsideEps) || (d1 > kInsideEps) || (d2 > kInsideEps);
    return !(neg && pos);
}

// Strictly inside (clear of every edge by at least the epsilon).
bool pointStrictlyInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const float d0 = cross2(b - a, p - a);
    const float d1 = cross2(c - b, p - b);
    const float d2 = cross2(a - c, p - c);
    const bool allPos = (d0 > kStrictEps) && (d1 > kStrictEps) && (d2 > kStrictEps);
    const bool allNeg = (d0 < -kStrictEps) && (d1 < -kStrictEps) && (d2 < -kStrictEps);
    return allPos || allNeg;
}

template <typename HitTest>
bool coveredByTopImpl(const Mesh& mesh, const glm::vec2& groundPoint, float height, HitTest hit) {
    const glm::vec2 p{groundPoint.x, groundPoint.y - height};
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        for (std::uint32_t v = prim.first; v + 2 < prim.first + prim.count; v += 3) {
            if (hit(p, mesh.vertices[v].pos, mesh.vertices[v + 1].pos, mesh.vertices[v + 2].pos)) {
                return true;
            }
        }
    }
    return false;
}

bool coveredByTop(const Mesh& mesh, const glm::vec2& groundPoint, float height) {
    return coveredByTopImpl(mesh, groundPoint, height, pointInTriangle);
}

bool strictlyCoveredByTop(const Mesh& mesh, const glm::vec2& groundPoint, float height) {
    return coveredByTopImpl(mesh, groundPoint, height, pointStrictlyInTriangle);
}

// Same corner-cutting pass as the generator's chaikinOnce (re-derived here
// for the reference smoothed region).
std::vector<glm::vec2> chaikinOnceRef(const std::vector<glm::vec2>& loop) {
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

// Even-odd ray cast along +X (map space), with a small boundary tolerance.
bool pointInLoopMap(const glm::vec2& p, const std::vector<glm::vec2>& loop, float eps = -1e-4f) {
    bool inside = false;
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const glm::vec2& a = loop[j];
        const glm::vec2& b = loop[i];
        if ((a.y > p.y) == (b.y > p.y)) {
            continue;
        }
        const float x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (p.x < x + eps) {
            inside = !inside;
        }
    }
    return inside;
}

// Distance from a point to the loop boundary (min over edges, map space).
float distToLoopBoundary(const glm::vec2& p, const std::vector<glm::vec2>& loop) {
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
        const glm::vec2& a = loop[j];
        const glm::vec2& b = loop[i];
        const glm::vec2 ab = b - a;
        const float len2 = glm::dot(ab, ab);
        const float t = len2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::length(p - (a + ab * t)));
    }
    return best;
}

// Inside the loop or on its boundary (contour vertices sit ON the boundary,
// where the plain ray cast is numerically unstable).
bool insideOrOnLoop(const glm::vec2& p, const std::vector<glm::vec2>& loop) {
    return pointInLoopMap(p, loop) || distToLoopBoundary(p, loop) < 1e-3f;
}

// Field-space point (un-lifted) -> map space (inverse of mapPointToField).
glm::vec2 fieldToMapPoint(const glm::vec2& f) {
    const glm::vec2 cellSz = g_iso.dims.cellSize();
    const float sx = (f.x - cellSz.x * 0.5f) / (cellSz.x * 0.5f); // mx - my
    const float sy = f.y / (cellSz.y * 0.5f);                     // mx + my
    return {(sy + sx) * 0.5f, (sy - sx) * 0.5f};
}

// Determinism: byte-identical vertex streams (the generator is deterministic,
// seed included) and logically identical primitive tables. Primitives are
// compared field-wise: the struct has padding bytes after the material tag
// that are not part of the output contract.
bool meshesIdentical(const Mesh& a, const Mesh& b) {
    if (a.vertices.size() != b.vertices.size() || a.primitives.size() != b.primitives.size()) {
        return false;
    }
    if (!a.vertices.empty() &&
        std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(highground::Vertex)) != 0) {
        return false;
    }
    for (std::size_t i = 0; i < a.primitives.size(); ++i) {
        const Primitive& pa = a.primitives[i];
        const Primitive& pb = b.primitives[i];
        if (pa.material != pb.material || pa.depth != pb.depth || pa.first != pb.first || pa.count != pb.count) {
            return false;
        }
    }
    return true;
}

// Returns an empty string when the shape passes every invariant, otherwise a
// description of the first violated one.
std::string shapeFailure(const std::vector<glm::ivec2>& nodes, const Params& params = {}) {
    const Grid grid = highground::makeGrid(nodes.data(), nodes.size());
    const Mesh mesh = highground::generateCgal(grid, params);

    if (mesh.vertices.empty()) {
        return "empty mesh";
    }
    // (b) Every on-node must be covered (inside or on the boundary) by the
    // top surface. With smoothing on, Chaikin cuts a QUARTER of each edge's
    // length at every corner, so corner nodes of long straight edges end up
    // inside the cut zone (the plateau corner is intentionally rounded off —
    // they are not coverable). Deep-interior nodes (all 8 neighbours on) sit
    // far from any cut and must stay covered.
    const bool relaxedCoverage = params.smoothIterations > 0;
    for (const glm::ivec2& n : nodes) {
        if (relaxedCoverage) {
            bool deepInterior = true;
            for (int dy = -1; dy <= 1 && deepInterior; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx || dy) && !grid.at(n + glm::ivec2{dx, dy})) {
                        deepInterior = false;
                        break;
                    }
                }
            }
            if (!deepInterior) {
                continue;
            }
        }
        if (!coveredByTop(mesh, g_iso.nodeToField(n), params.height)) {
            std::string diag = "on-node not covered: (" + std::to_string(n.x) + "," + std::to_string(n.y) + ")";
            glm::vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
            int topVerts = 0;
            for (const Primitive& prim : mesh.primitives) {
                if (prim.material != Material::Top) continue;
                for (std::uint32_t v = prim.first; v < prim.first + prim.count; ++v) {
                    lo = glm::min(lo, mesh.vertices[v].pos);
                    hi = glm::max(hi, mesh.vertices[v].pos);
                    ++topVerts;
                }
            }
            diag += " | top bbox (" + std::to_string(lo.x) + "," + std::to_string(lo.y) + ")-(" +
                std::to_string(hi.x) + "," + std::to_string(hi.y) + ") verts=" + std::to_string(topVerts) +
                " nodefield=(" + std::to_string(g_iso.nodeToField(n).x) + "," +
                std::to_string(g_iso.nodeToField(n).y - params.height) + ")";
            return diag;
        }
    }
    // (c) Lid detector: single-corner cells sit on the exact region boundary
    // at their centers, so the center must never be STRICTLY inside a top
    // triangle. A per-cell stitched "lid" top would cover it.
    for (int cy = grid.originY - 1; cy <= grid.originY + grid.height; ++cy) {
        for (int cx = grid.originX - 1; cx <= grid.originX + grid.width; ++cx) {
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes({cx, cy});
            int on = 0;
            for (int i = 0; i < 4; ++i) {
                on += grid.at(corners[i]) ? 1 : 0;
            }
            if (on == 1 && strictlyCoveredByTop(mesh, g_iso.mapToField({cx, cy}), params.height)) {
                return "single-corner cell center strictly covered (lid): (" + std::to_string(cx) + "," +
                    std::to_string(cy) + ")";
            }
        }
    }
    // (d) Back-to-front depth order (default sortPrimitives=true).
    for (std::size_t i = 1; i < mesh.primitives.size(); ++i) {
        if (mesh.primitives[i].depth < mesh.primitives[i - 1].depth) {
            return "primitives not sorted back-to-front";
        }
    }
    // (a) Determinism: same input -> identical mesh.
    const Mesh again = highground::generateCgal(grid, params);
    if (!meshesIdentical(mesh, again)) {
        return "not deterministic";
    }
    return "";
}

void expectShapeOk(const std::vector<glm::ivec2>& nodes, const Params& params = {}) {
    const std::string failure = shapeFailure(nodes, params);
    EXPECT_TRUE(failure.empty()) << failure << " | nodes:" << nodesStr(nodes);
}

} // namespace

class HighgroundCgalTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!highground::cgalAvailable()) {
            GTEST_SKIP() << "highground_core built without CGAL";
        }
    }
};

TEST_F(HighgroundCgalTest, FuzzRandomClouds)
{
    // Random clouds in a 6x6 window at several densities.
    std::vector<glm::ivec2> cells;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            cells.push_back({x, y});
        }
    }
    const double densities[] = {0.15, 0.35, 0.6, 0.85};
    std::mt19937 rng(20260721);
    std::size_t failed = 0;
    for (const double density : densities) {
        std::bernoulli_distribution pick(density);
        for (int iter = 0; iter < 120; ++iter) {
            std::vector<glm::ivec2> nodes;
            for (const glm::ivec2& c : cells) {
                if (pick(rng)) {
                    nodes.push_back(c + glm::ivec2{3, 3});
                }
            }
            if (nodes.empty()) {
                continue;
            }
            const std::string failure = shapeFailure(nodes);
            if (!failure.empty()) {
                ++failed;
                ADD_FAILURE() << failure << " | density=" << density << " nodes:" << nodesStr(nodes);
                if (failed >= 8) {
                    return;
                }
            }
        }
    }
}

TEST_F(HighgroundCgalTest, FuzzStripes)
{
    // Axis stripes of growing length, plus parallel-stripe pairs (the gap
    // between them must stay open).
    for (int len = 1; len <= 8; ++len) {
        std::vector<glm::ivec2> hAxis;
        std::vector<glm::ivec2> vAxis;
        for (int i = 0; i < len; ++i) {
            hAxis.push_back({10 + i, 10});
            vAxis.push_back({10, 10 + i});
        }
        expectShapeOk(hAxis);
        expectShapeOk(vAxis);
    }
    for (int gap = 1; gap <= 2; ++gap) {
        std::vector<glm::ivec2> pair_;
        for (int i = 0; i < 5; ++i) {
            pair_.push_back({10 + i, 10});
            pair_.push_back({10 + i, 10 + gap});
        }
        expectShapeOk(pair_);
    }
}

TEST_F(HighgroundCgalTest, FuzzRings)
{
    // Solid rings (square frame around an empty center) of growing size,
    // then rings with seeded random gaps.
    for (int r = 1; r <= 3; ++r) {
        std::vector<glm::ivec2> ring;
        for (int y = 0; y <= 2 * r; ++y) {
            for (int x = 0; x <= 2 * r; ++x) {
                if (x == r && y == r) {
                    continue;
                }
                ring.push_back({10 + x, 10 + y});
            }
        }
        expectShapeOk(ring);
    }
    std::mt19937 rng(4242);
    std::bernoulli_distribution pick(0.7);
    std::size_t failed = 0;
    for (int iter = 0; iter < 150; ++iter) {
        std::vector<glm::ivec2> nodes;
        for (int y = 0; y <= 4; ++y) {
            for (int x = 0; x <= 4; ++x) {
                if (x == 2 && y == 2) {
                    continue;
                }
                if (pick(rng)) {
                    nodes.push_back({10 + x, 10 + y});
                }
            }
        }
        if (nodes.size() < 3) {
            continue;
        }
        const std::string failure = shapeFailure(nodes);
        if (!failure.empty()) {
            ++failed;
            ADD_FAILURE() << failure << " | nodes:" << nodesStr(nodes);
            if (failed >= 8) {
                return;
            }
        }
    }
}

TEST_F(HighgroundCgalTest, FuzzSingleNodes)
{
    // Lone nodes scattered over the canvas (each is its own island region).
    expectShapeOk({{2, 2}});
    expectShapeOk({{9, 3}});
    expectShapeOk({{3, 9}});
    expectShapeOk({{12, 12}});
    expectShapeOk({{7, 7}});
    // Seeded spread of 1..4 far-apart singles.
    std::mt19937 rng(777);
    for (int iter = 0; iter < 60; ++iter) {
        std::vector<glm::ivec2> nodes;
        const int count = 1 + static_cast<int>(rng() % 4u);
        for (int i = 0; i < count; ++i) {
            nodes.push_back({2 + static_cast<int>(rng() % 12u), 2 + static_cast<int>(rng() % 12u)});
        }
        std::sort(nodes.begin(), nodes.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        expectShapeOk(nodes);
    }
}

TEST_F(HighgroundCgalTest, FuzzDiagonalStairs)
{
    // Diagonal neighbours touch at a single pinch vertex (figure-eight
    // region boundary) — the stress case for the loop splitting.
    for (int len = 1; len <= 8; ++len) {
        std::vector<glm::ivec2> diag;
        std::vector<glm::ivec2> stairs;
        for (int i = 0; i < len; ++i) {
            diag.push_back({10 + i, 10 + i});
            stairs.push_back({10 + 2 * i, 10 + i});
            stairs.push_back({11 + 2 * i, 11 + i});
        }
        expectShapeOk(diag);
        expectShapeOk(stairs);
    }
    // Seeded diagonal pairs at random offsets.
    std::mt19937 rng(9001);
    std::size_t failed = 0;
    for (int iter = 0; iter < 150; ++iter) {
        std::vector<glm::ivec2> nodes;
        const int pairs = 1 + static_cast<int>(rng() % 4u);
        for (int i = 0; i < pairs; ++i) {
            const glm::ivec2 base{3 + static_cast<int>(rng() % 10u), 3 + static_cast<int>(rng() % 10u)};
            nodes.push_back(base);
            nodes.push_back(base + glm::ivec2{1, 1});
        }
        std::sort(nodes.begin(), nodes.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
            return a.y != b.y ? a.y < b.y : a.x < b.x;
        });
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        const std::string failure = shapeFailure(nodes);
        if (!failure.empty()) {
            ++failed;
            ADD_FAILURE() << failure << " | nodes:" << nodesStr(nodes);
            if (failed >= 8) {
                return;
            }
        }
    }
}

TEST_F(HighgroundCgalTest, HoleTest)
{
    // Ring of 8 nodes around an empty center: the union of the ring squares
    // leaves a real 1x1 hole, so the center node's field point must never be
    // strictly inside a top triangle, while every ring node stays covered.
    const std::vector<glm::ivec2> ring = {
        {3, 3}, {4, 3}, {5, 3},
        {3, 4},         {5, 4},
        {3, 5}, {4, 5}, {5, 5},
    };
    const Params params;
    const Grid grid = highground::makeGrid(ring.data(), ring.size());
    const Mesh mesh = highground::generateCgal(grid, params);
    ASSERT_FALSE(mesh.vertices.empty());

    EXPECT_FALSE(strictlyCoveredByTop(mesh, g_iso.nodeToField({4, 4}), params.height))
        << "hole center (4,4) is strictly covered by the top surface";
    for (const glm::ivec2& n : ring) {
        EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField(n), params.height))
            << "ring node not covered: (" << n.x << "," << n.y << ")";
    }
}

TEST_F(HighgroundCgalTest, SingleNodeTest)
{
    // One node -> the region is exactly its unit square. With the default
    // bevel (0.3) the top region is inset via the straight skeleton, so the
    // top is the chamfered octagon (6 triangles) whose 8 vertices sit on the
    // square's edges, bevel away from the corners; the walls extrude the
    // square's 4 edges with their own chamfers. With bevel == 0 the top is
    // the full rhombus (2 triangles) as before.
    const glm::ivec2 node{5, 5};
    const Params params; // bevel = 0.3
    const Grid grid = highground::makeGrid(&node, 1);
    const Mesh mesh = highground::generateCgal(grid, params);
    ASSERT_FALSE(mesh.vertices.empty());

    int topPrims = 0;
    int wallPrims = 0;
    std::vector<glm::vec2> topVerts;
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material == Material::Top) {
            ++topPrims;
            EXPECT_EQ(prim.count % 3u, 0u);
            for (std::uint32_t k = prim.first; k < prim.first + prim.count; ++k) {
                topVerts.push_back(mesh.vertices[k].pos);
            }
        } else if (prim.material == Material::Wall && prim.count > 0) {
            ++wallPrims;
        }
    }
    EXPECT_EQ(topPrims, 6) << "beveled single-node top must be 6 triangles (CDT of the octagon)";
    EXPECT_GT(wallPrims, 0) << "single-node walls missing";

    // Distinct top vertex positions, unlifted (height added back).
    std::vector<glm::vec2> distinct;
    for (const glm::vec2& v : topVerts) {
        const glm::vec2 ground{v.x, v.y + params.height};
        const auto near = [&ground](const glm::vec2& u) {
            return std::abs(u.x - ground.x) < 1e-3f && std::abs(u.y - ground.y) < 1e-3f;
        };
        if (std::none_of(distinct.begin(), distinct.end(), near)) {
            distinct.push_back(ground);
        }
    }
    ASSERT_EQ(distinct.size(), 8u) << "beveled single-node top must have 8 distinct corners";

    // Octagon vertices (map space): (+-0.2, +-0.5) and (+-0.5, +-0.2) around
    // the node — the square's edges touched bevel = 0.3 away from the corners.
    const float b = params.bevel;
    const glm::vec2 n{static_cast<float>(node.x), static_cast<float>(node.y)};
    const glm::vec2 expectedMap[8] = {
        {n.x - 0.5f + b, n.y - 0.5f}, {n.x + 0.5f - b, n.y - 0.5f},
        {n.x - 0.5f + b, n.y + 0.5f}, {n.x + 0.5f - b, n.y + 0.5f},
        {n.x - 0.5f, n.y - 0.5f + b}, {n.x - 0.5f, n.y + 0.5f - b},
        {n.x + 0.5f, n.y - 0.5f + b}, {n.x + 0.5f, n.y + 0.5f - b},
    };
    for (const glm::vec2& e : expectedMap) {
        const glm::vec2 ef = mapPointToField(e);
        const auto matches = [&ef](const glm::vec2& u) {
            return std::abs(u.x - ef.x) < 1e-2f && std::abs(u.y - ef.y) < 1e-2f;
        };
        EXPECT_TRUE(std::any_of(distinct.begin(), distinct.end(), matches))
            << "octagon corner missing at map (" << e.x << "," << e.y << ")";
    }

    // Fast path: bevel == 0 keeps the full rhombus (2 triangles, 4 corners).
    Params noBevel;
    noBevel.bevel = 0.0f;
    const Mesh flat = highground::generateCgal(grid, noBevel);
    ASSERT_FALSE(flat.vertices.empty());
    int flatTopPrims = 0;
    std::vector<glm::vec2> flatTopVerts;
    for (const Primitive& prim : flat.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        ++flatTopPrims;
        for (std::uint32_t k = prim.first; k < prim.first + prim.count; ++k) {
            flatTopVerts.push_back(flat.vertices[k].pos);
        }
    }
    EXPECT_EQ(flatTopPrims, 2) << "bevel-0 single-node top must be exactly 2 triangles";
    std::vector<glm::vec2> flatDistinct;
    for (const glm::vec2& v : flatTopVerts) {
        const glm::vec2 ground{v.x, v.y + noBevel.height};
        const auto near = [&ground](const glm::vec2& u) {
            return std::abs(u.x - ground.x) < 1e-3f && std::abs(u.y - ground.y) < 1e-3f;
        };
        if (std::none_of(flatDistinct.begin(), flatDistinct.end(), near)) {
            flatDistinct.push_back(ground);
        }
    }
    ASSERT_EQ(flatDistinct.size(), 4u) << "bevel-0 single-node top must have 4 distinct corners";

    const glm::vec2 cellSz = g_iso.dims.cellSize();
    const glm::vec2 center = g_iso.nodeToField(node);
    const glm::vec2 expected[4] = {
        {center.x - cellSz.x * 0.5f, center.y}, // Left
        {center.x, center.y - cellSz.y * 0.5f}, // Up
        {center.x + cellSz.x * 0.5f, center.y}, // Right
        {center.x, center.y + cellSz.y * 0.5f}, // Down
    };
    for (const glm::vec2& e : expected) {
        const auto matches = [&e](const glm::vec2& u) {
            return std::abs(u.x - e.x) < 1e-3f && std::abs(u.y - e.y) < 1e-3f;
        };
        EXPECT_TRUE(std::any_of(flatDistinct.begin(), flatDistinct.end(), matches))
            << "rhombus corner missing at (" << e.x << "," << e.y << ")";
    }
}

TEST_F(HighgroundCgalTest, BevelMatchesWalls)
{
    // The top region is inset by exactly the wall bevel distance (straight
    // skeleton => the same 45-degree chamfers the walls make). So the
    // original region corner (the rhombus tip) must NOT be covered by any
    // top triangle, while the chamfer midpoint IS covered (or on the
    // boundary) — the top ends flush with the walls' top edge.
    const glm::ivec2 node{5, 5};
    const Params params; // bevel = 0.3
    const Grid grid = highground::makeGrid(&node, 1);
    const Mesh mesh = highground::generateCgal(grid, params);
    ASSERT_FALSE(mesh.vertices.empty());

    const float b = params.bevel;
    const glm::vec2 n{static_cast<float>(node.x), static_cast<float>(node.y)};
    for (const glm::vec2& s : {glm::vec2{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}) {
        // Original square corner (rhombus tip): chamfered away, not covered.
        const glm::vec2 tip{n.x + s.x * 0.5f, n.y + s.y * 0.5f};
        EXPECT_FALSE(coveredByTop(mesh, mapPointToField(tip), params.height))
            << "rhombus tip covered despite the bevel: map (" << tip.x << "," << tip.y << ")";
        // Chamfer midpoint between the two trim points: covered/boundary.
        const glm::vec2 mid{n.x + s.x * (0.5f - b * 0.5f), n.y + s.y * (0.5f - b * 0.5f)};
        EXPECT_TRUE(coveredByTop(mesh, mapPointToField(mid), params.height))
            << "chamfer midpoint not covered: map (" << mid.x << "," << mid.y << ")";
        // Trim points (the inset region vertices): covered/boundary.
        const glm::vec2 trimA{n.x + s.x * (0.5f - b), n.y + s.y * 0.5f};
        const glm::vec2 trimB{n.x + s.x * 0.5f, n.y + s.y * (0.5f - b)};
        EXPECT_TRUE(coveredByTop(mesh, mapPointToField(trimA), params.height))
            << "trim point not covered: map (" << trimA.x << "," << trimA.y << ")";
        EXPECT_TRUE(coveredByTop(mesh, mapPointToField(trimB), params.height))
            << "trim point not covered: map (" << trimB.x << "," << trimB.y << ")";
    }
    // The node itself stays covered (it sits at the region center).
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField(node), params.height));

    // Diagonal pair: the two unit squares touch at the pinch vertex, which is
    // a convex corner of BOTH squares, so the inset chamfers it away twice —
    // it must not be covered, while both nodes stay covered.
    const std::vector<glm::ivec2> diag{{5, 5}, {6, 6}};
    const Grid diagGrid = highground::makeGrid(diag.data(), diag.size());
    const Mesh diagMesh = highground::generateCgal(diagGrid, params);
    ASSERT_FALSE(diagMesh.vertices.empty());
    EXPECT_FALSE(coveredByTop(diagMesh, mapPointToField({5.5f, 5.5f}), params.height))
        << "pinch vertex covered despite the bevel";
    EXPECT_TRUE(coveredByTop(diagMesh, g_iso.nodeToField({5, 5}), params.height));
    EXPECT_TRUE(coveredByTop(diagMesh, g_iso.nodeToField({6, 6}), params.height));
}

TEST_F(HighgroundCgalTest, SmoothBasic)
{
    // Single node with 2 Chaikin iterations: the orthogonal square becomes a
    // 16-vertex smoothed loop. The node stays covered, the original square
    // corner is cut away, every top vertex stays inside the reference
    // smoothed region (no self-intersections), and the output is
    // deterministic.
    const glm::ivec2 node{5, 5};
    Params params;
    params.smoothIterations = 2;
    const Grid grid = highground::makeGrid(&node, 1);
    const Mesh mesh = highground::generateCgal(grid, params);
    ASSERT_FALSE(mesh.vertices.empty());

    // Reference smoothed region: the node's unit square after 2 Chaikin
    // passes (map space, closed loop).
    std::vector<glm::vec2> region = {
        {4.5f, 4.5f}, {5.5f, 4.5f}, {5.5f, 5.5f}, {4.5f, 5.5f},
    };
    for (int it = 0; it < params.smoothIterations; ++it) {
        region = chaikinOnceRef(region);
    }
    ASSERT_EQ(region.size(), 16u);

    // Node covered; original square corner cut away.
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField(node), params.height));
    EXPECT_FALSE(coveredByTop(mesh, mapPointToField({5.5f, 5.5f}), params.height))
        << "original square corner still covered after smoothing";

    // Every top vertex (un-lifted, map space) stays inside the reference
    // smoothed region; centroids of all top triangles as well.
    int topPrims = 0;
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        ++topPrims;
        for (std::uint32_t v = prim.first; v + 2 < prim.first + prim.count; v += 3) {
            glm::vec2 centroid{0.0f, 0.0f};
            for (std::uint32_t k = 0; k < 3; ++k) {
                const glm::vec2& pos = mesh.vertices[v + k].pos;
                const glm::vec2 ground{pos.x, pos.y + params.height};
                const glm::vec2 map = fieldToMapPoint(ground);
                EXPECT_TRUE(insideOrOnLoop(map, region))
                    << "top vertex outside the smoothed region: map (" << map.x << "," << map.y << ")";
                centroid += ground;
            }
            const glm::vec2 c = fieldToMapPoint(centroid / 3.0f);
            EXPECT_TRUE(insideOrOnLoop(c, region))
                << "top centroid outside the smoothed region: map (" << c.x << "," << c.y << ")";
        }
    }
    // The smoothed contour is more detailed than the unsmoothed one (the
    // bevel-only octagon triangulates into 6 top triangles).
    int topTris = 0;
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material == Material::Top) {
            topTris += static_cast<int>(prim.count / 3);
        }
    }
    EXPECT_GT(topTris, 6) << "smoothed single-node top should have more than 6 triangles";
    (void)topPrims;

    // Determinism.
    const Mesh again = highground::generateCgal(grid, params);
    EXPECT_TRUE(meshesIdentical(mesh, again)) << "smoothed output is not deterministic";
}

TEST_F(HighgroundCgalTest, FuzzSmoothed)
{
    // The same shapes as the unsmoothed fuzzers, through 2 Chaikin
    // iterations: all four invariants must hold, including on the tricky
    // ones — 1-cell-wide stripes (narrow necks) and diagonal pinch pairs.
    Params smooth;
    smooth.smoothIterations = 2;

    // Axis stripes of growing length (narrow 1-cell-wide arms).
    for (int len = 1; len <= 8; ++len) {
        std::vector<glm::ivec2> hAxis;
        std::vector<glm::ivec2> vAxis;
        for (int i = 0; i < len; ++i) {
            hAxis.push_back({10 + i, 10});
            vAxis.push_back({10, 10 + i});
        }
        expectShapeOk(hAxis, smooth);
        expectShapeOk(vAxis, smooth);
    }
    // Parallel stripe pairs (the gap must stay open).
    for (int gap = 1; gap <= 2; ++gap) {
        std::vector<glm::ivec2> pair_;
        for (int i = 0; i < 5; ++i) {
            pair_.push_back({10 + i, 10});
            pair_.push_back({10 + i, 10 + gap});
        }
        expectShapeOk(pair_, smooth);
    }
    // Solid rings (a real hole) of growing size.
    for (int r = 1; r <= 3; ++r) {
        std::vector<glm::ivec2> ring;
        for (int y = 0; y <= 2 * r; ++y) {
            for (int x = 0; x <= 2 * r; ++x) {
                if (x == r && y == r) {
                    continue;
                }
                ring.push_back({10 + x, 10 + y});
            }
        }
        expectShapeOk(ring, smooth);
    }
    // Diagonal chains and stairs (pinch vertices split into separate loops
    // BEFORE smoothing, so the tips get cut independently).
    for (int len = 1; len <= 8; ++len) {
        std::vector<glm::ivec2> diag;
        std::vector<glm::ivec2> stairs;
        for (int i = 0; i < len; ++i) {
            diag.push_back({10 + i, 10 + i});
            stairs.push_back({10 + 2 * i, 10 + i});
            stairs.push_back({11 + 2 * i, 11 + i});
        }
        expectShapeOk(diag, smooth);
        expectShapeOk(stairs, smooth);
    }
    // Seeded random clouds at two densities.
    std::vector<glm::ivec2> cells;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            cells.push_back({x, y});
        }
    }
    std::mt19937 rng(20260722);
    std::size_t failed = 0;
    for (const double density : {0.35, 0.6}) {
        std::bernoulli_distribution pick(density);
        for (int iter = 0; iter < 60; ++iter) {
            std::vector<glm::ivec2> nodes;
            for (const glm::ivec2& c : cells) {
                if (pick(rng)) {
                    nodes.push_back(c + glm::ivec2{3, 3});
                }
            }
            if (nodes.empty()) {
                continue;
            }
            const std::string failure = shapeFailure(nodes, smooth);
            if (!failure.empty()) {
                ++failed;
                ADD_FAILURE() << failure << " | smoothed density=" << density << " nodes:" << nodesStr(nodes);
                if (failed >= 8) {
                    return;
                }
            }
        }
    }
}
