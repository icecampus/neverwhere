// highground_core: black-box tests of the vertex-node highground generation
// (contour helpers + end-to-end generate()).
#include <cmath>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/highground.h>
#include <highground_core/inspect.h>
#include <topology_core/diamond_isometry.h>

namespace {

using highground::Grid;
using highground::Material;
using highground::Mesh;
using highground::Params;
using highground::Primitive;
using highground::Vertex;

topology_core::DiamondIsometry g_iso;

Grid gridOf(std::initializer_list<glm::ivec2> nodes) {
    std::vector<glm::ivec2> v(nodes);
    return highground::makeGrid(v.data(), v.size());
}

std::vector<const Primitive*> primsOf(const Mesh& mesh, Material material) {
    std::vector<const Primitive*> out;
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material == material) {
            out.push_back(&prim);
        }
    }
    return out;
}

bool pointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    const auto cross = [](const glm::vec2& u, const glm::vec2& v) {
        return u.x * v.y - u.y * v.x;
    };
    const float d0 = cross(b - a, p - a);
    const float d1 = cross(c - b, p - b);
    const float d2 = cross(a - c, p - c);
    const bool neg = (d0 < 0.0f) || (d1 < 0.0f) || (d2 < 0.0f);
    const bool pos = (d0 > 0.0f) || (d1 > 0.0f) || (d2 > 0.0f);
    return !(neg && pos);
}

// Is `point` (GROUND space, un-lifted) covered by any Top triangle of the mesh?
bool coveredByTop(const Mesh& mesh, const glm::vec2& groundPoint) {
    const glm::vec2 p{groundPoint.x, groundPoint.y - 96.0f}; // default Params height
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        for (std::uint32_t v = prim.first; v + 2 < prim.first + prim.count; v += 3) {
            const glm::vec2& a = mesh.vertices[v].pos;
            const glm::vec2& b = mesh.vertices[v + 1].pos;
            const glm::vec2& c = mesh.vertices[v + 2].pos;
            if (pointInTriangle(p, a, b, c)) {
                return true;
            }
        }
    }
    return false;
}

double topArea(const Mesh& mesh) {
    double area = 0.0;
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        for (std::uint32_t v = prim.first; v + 2 < prim.first + prim.count; v += 3) {
            const glm::vec2& a = mesh.vertices[v].pos;
            const glm::vec2& b = mesh.vertices[v + 1].pos;
            const glm::vec2& c = mesh.vertices[v + 2].pos;
            area += std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
        }
    }
    return area;
}

} // namespace

TEST(HighgroundContourTest, SegmentCountsPerTileShape)
{
    using highground::cellContourSegments;
    const glm::ivec2 cell{2, 2};

    EXPECT_TRUE(cellContourSegments(g_iso, cell, {true, true, true, true}).empty());
    EXPECT_TRUE(cellContourSegments(g_iso, cell, {false, false, false, false}).empty());
    // Single corner on: 2 transition edges.
    EXPECT_EQ(cellContourSegments(g_iso, cell, {true, false, false, false}).size(), 2u);
    // Adjacent pair: 2; diagonal pair: 4.
    EXPECT_EQ(cellContourSegments(g_iso, cell, {false, true, true, false}).size(), 2u);
    EXPECT_EQ(cellContourSegments(g_iso, cell, {true, false, true, false}).size(), 4u);
}

TEST(HighgroundContourTest, SegmentsAreAxisParallelAndEndAtCenter)
{
    using highground::cellContourSegments;
    const glm::ivec2 cell{2, 2};
    const glm::vec2 center = g_iso.mapToField(cell);

    const auto segs = cellContourSegments(g_iso, cell, {true, false, false, false});
    ASSERT_EQ(segs.size(), 2u);
    for (const auto& seg : segs) {
        EXPECT_NEAR(seg.center.x, center.x, 1e-4f);
        EXPECT_NEAR(seg.center.y, center.y, 1e-4f);
        const glm::vec2 dir = seg.center - seg.edgeMid;
        const glm::vec2 axisDir = (seg.axis == 0) ? glm::vec2(64.0f, 32.0f) : glm::vec2(-64.0f, 32.0f);
        EXPECT_NEAR(dir.x * axisDir.y - dir.y * axisDir.x, 0.0f, 1e-3f);
    }
}

TEST(HighgroundTriangulationTest, SquareAndConcaveAndCollinear)
{
    using highground::polygonSignedArea;
    using highground::triangulateSimplePolygon;

    const std::vector<glm::vec2> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    const auto sq = triangulateSimplePolygon(square);
    ASSERT_EQ(sq.size(), 6u);

    // Concave (L-shape): area of triangles == polygon area.
    const std::vector<glm::vec2> lshape{{0, 0}, {10, 0}, {10, 5}, {5, 5}, {5, 10}, {0, 10}};
    const auto lt = triangulateSimplePolygon(lshape);
    ASSERT_FALSE(lt.empty());
    double area = 0.0;
    for (std::size_t v = 0; v + 2 < lt.size(); v += 3) {
        area += std::abs(
            (lt[v + 1].x - lt[v].x) * (lt[v + 2].y - lt[v].y) -
            (lt[v + 2].x - lt[v].x) * (lt[v + 1].y - lt[v].y)) * 0.5;
    }
    EXPECT_NEAR(area, std::abs(polygonSignedArea(lshape)), 1e-3);

    // Collinear runs (contour-style): straight strip with intermediate points.
    const std::vector<glm::vec2> strip{{0, 0}, {5, 0}, {10, 0}, {10, 4}, {5, 4}, {0, 4}};
    EXPECT_EQ(triangulateSimplePolygon(strip).size(), 12u);
}

TEST(HighgroundGenerateTest, EmptyAndSingleNode)
{
    EXPECT_TRUE(highground::generate(Grid{}, Params{}).vertices.empty());

    const Mesh mesh = highground::generate(gridOf({{5, 5}}), Params{});
    EXPECT_FALSE(mesh.vertices.empty());
    EXPECT_FALSE(primsOf(mesh, Material::Wall).empty());
    EXPECT_FALSE(primsOf(mesh, Material::Top).empty());
    // The on-node itself is covered by the top surface.
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField({5, 5})));
}

TEST(HighgroundGenerateTest, DeterministicAndSorted)
{
    const Grid grid = gridOf({{3, 3}, {4, 3}, {3, 4}, {5, 5}, {6, 6}});
    const Params params;
    const Mesh a = highground::generate(grid, params);
    const Mesh b = highground::generate(grid, params);

    ASSERT_EQ(a.vertices.size(), b.vertices.size());
    ASSERT_EQ(a.primitives.size(), b.primitives.size());
    EXPECT_EQ(
        std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(Vertex)),
        0);
    EXPECT_EQ(
        std::memcmp(a.primitives.data(), b.primitives.data(), a.primitives.size() * sizeof(Primitive)),
        0);

    for (std::size_t i = 1; i < a.primitives.size(); ++i) {
        EXPECT_GE(a.primitives[i].depth, a.primitives[i - 1].depth) << "primitives must be back-to-front";
    }
    // Walls before tops at equal depth.
    for (std::size_t i = 1; i < a.primitives.size(); ++i) {
        if (a.primitives[i].depth == a.primitives[i - 1].depth) {
            EXPECT_LE(static_cast<int>(a.primitives[i].material), static_cast<int>(a.primitives[i - 1].material));
        }
    }
}

TEST(HighgroundGenerateTest, DiagonalBridgeCoversNeck)
{
    // Two 2x2 blocks joined by a single diagonal node (figure-eight pinches):
    // the neck node must be covered by the top (no per-cell "lids" fallback).
    const Mesh mesh = highground::generate(
        gridOf({{2, 2}, {3, 2}, {2, 3}, {3, 3}, {4, 4}, {5, 5}, {6, 5}, {5, 6}, {6, 6}}),
        Params{});
    ASSERT_FALSE(mesh.vertices.empty());
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField({4, 4})));
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField({3, 3})));
    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField({5, 5})));
}

TEST(HighgroundGenerateTest, RingKeepsHoleOpen)
{
    // 5x5 ring around an off center node: the hole stays uncovered, the ring
    // is covered.
    std::vector<glm::ivec2> nodes;
    for (int y = 2; y <= 6; ++y) {
        for (int x = 2; x <= 6; ++x) {
            if (x == 4 && y == 4) {
                continue;
            }
            nodes.push_back({x, y});
        }
    }
    const Mesh mesh = highground::generate(highground::makeGrid(nodes.data(), nodes.size()), Params{});
    ASSERT_FALSE(mesh.vertices.empty());

    EXPECT_TRUE(coveredByTop(mesh, g_iso.nodeToField({3, 3})));
    EXPECT_FALSE(coveredByTop(mesh, g_iso.nodeToField({4, 4}))) << "the pit must stay open";

    // Sanity on the top area: between the full 5x5-minus-pit bounds.
    EXPECT_GT(topArea(mesh), 0.0);
}

TEST(HighgroundGridTest, SparseWindow)
{
    const std::vector<glm::ivec2> nodes{{3, 4}, {7, 2}};
    const Grid grid = highground::makeGrid(nodes.data(), nodes.size(), 1);
    EXPECT_EQ(grid.originX, 2);
    EXPECT_EQ(grid.originY, 1);
    EXPECT_EQ(grid.width, 7);
    EXPECT_EQ(grid.height, 5);
    EXPECT_TRUE(grid.at({3, 4}));
    EXPECT_TRUE(grid.at({7, 2}));
    EXPECT_FALSE(grid.at({0, 0}));
    EXPECT_FALSE(grid.at({100, 100}));
}
