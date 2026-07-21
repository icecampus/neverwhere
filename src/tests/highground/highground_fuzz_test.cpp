// highground_core: fuzz detector for the "square lid" class of bugs.
//
// The top surface must (a) cover every on-node and (b) chamfer the convex
// corners of single-corner cells at their centers, matching the wall bevel —
// a top covering a single-corner cell center is an unbeveled "lid".
// This suite sweeps structured families and seeded random shapes asserting
// the invariants: nodes covered, single-corner centers open, deterministic,
// back-to-front sorted.
#include <algorithm>
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

std::string nodesStr(const std::vector<glm::ivec2>& nodes) {
    std::string s;
    for (const glm::ivec2& n : nodes) {
        s += " (" + std::to_string(n.x) + "," + std::to_string(n.y) + ")";
    }
    return s;
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

bool coveredByTop(const Mesh& mesh, const glm::vec2& groundPoint) {
    const glm::vec2 p{groundPoint.x, groundPoint.y - 96.0f};
    for (const Primitive& prim : mesh.primitives) {
        if (prim.material != Material::Top) {
            continue;
        }
        for (std::uint32_t v = prim.first; v + 2 < prim.first + prim.count; v += 3) {
            if (pointInTriangle(p, mesh.vertices[v].pos, mesh.vertices[v + 1].pos, mesh.vertices[v + 2].pos)) {
                return true;
            }
        }
    }
    return false;
}

// Returns an empty string when the shape passes every invariant, otherwise a
// description of the first violated one.
std::string shapeFailure(const std::vector<glm::ivec2>& nodes) {
    const Params params;
    const Grid grid = highground::makeGrid(nodes.data(), nodes.size());
    const Mesh mesh = highground::generate(grid, params);

    if (mesh.vertices.empty()) {
        return "empty mesh";
    }
    // Every on-node must be covered by the top surface.
    for (const glm::ivec2& n : nodes) {
        if (!coveredByTop(mesh, g_iso.nodeToField(n))) {
            return "on-node not covered: (" + std::to_string(n.x) + "," + std::to_string(n.y) + ")";
        }
    }
    // Lid detector: single-corner cells must have their convex corner at the
    // cell center chamfered (matching the wall bevel) — i.e. the center is
    // NOT covered. An unbeveled "lid" top would cover it.
    for (int cy = grid.originY - 1; cy <= grid.originY + grid.height; ++cy) {
        for (int cx = grid.originX - 1; cx <= grid.originX + grid.width; ++cx) {
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes({cx, cy});
            int on = 0;
            for (int i = 0; i < 4; ++i) {
                on += grid.at(corners[i]) ? 1 : 0;
            }
            if (on == 1 && coveredByTop(mesh, g_iso.mapToField({cx, cy}))) {
                return "single-corner cell center covered (lid): (" + std::to_string(cx) + "," + std::to_string(cy) + ")";
            }
        }
    }
    for (std::size_t i = 1; i < mesh.primitives.size(); ++i) {
        if (mesh.primitives[i].depth < mesh.primitives[i - 1].depth) {
            return "primitives not sorted back-to-front";
        }
    }
    const Mesh again = highground::generate(grid, params);
    if (again.vertices.size() != mesh.vertices.size() ||
        std::memcmp(again.vertices.data(), mesh.vertices.data(), mesh.vertices.size() * sizeof(highground::Vertex)) != 0) {
        return "not deterministic";
    }
    return "";
}

void expectShapeOk(const std::vector<glm::ivec2>& nodes) {
    const std::string failure = shapeFailure(nodes);
    EXPECT_TRUE(failure.empty()) << failure << " | nodes:" << nodesStr(nodes);
}

// Exhaustive sweep helper: every subset of `cells` (offset by `base`).
void sweepSubsets(const glm::ivec2& base, const std::vector<glm::ivec2>& cells) {
    const std::size_t n = cells.size();
    ASSERT_LE(n, 20u);
    const std::uint32_t total = 1u << n;
    std::size_t failed = 0;
    for (std::uint32_t mask = 1; mask < total; ++mask) {
        std::vector<glm::ivec2> nodes;
        for (std::size_t i = 0; i < n; ++i) {
            if (mask & (1u << i)) {
                nodes.push_back(base + cells[i]);
            }
        }
        const std::string failure = shapeFailure(nodes);
        if (!failure.empty()) {
            ++failed;
            ADD_FAILURE() << failure << " | nodes:" << nodesStr(nodes);
            if (failed >= 8) {
                return; // enough samples of the failure class
            }
        }
    }
}

} // namespace

TEST(HighgroundFuzzTest, AllPairsWithin3x3)
{
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 2; ++dy) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            expectShapeOk({{5, 5}, {5 + dx, 5 + dy}});
        }
    }
}

TEST(HighgroundFuzzTest, AllSubsetsOf3x3)
{
    std::vector<glm::ivec2> cells;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            cells.push_back({x, y});
        }
    }
    sweepSubsets({4, 4}, cells);
}

TEST(HighgroundFuzzTest, StripesBothAxesAndDiagonal)
{
    for (int len = 1; len <= 6; ++len) {
        std::vector<glm::ivec2> vAxis;
        std::vector<glm::ivec2> hAxis;
        std::vector<glm::ivec2> diag;
        for (int i = 0; i < len; ++i) {
            vAxis.push_back({10, 10 + i});
            hAxis.push_back({10 + i, 10});
            diag.push_back({10 + i, 10 + i});
        }
        expectShapeOk(vAxis);
        expectShapeOk(hAxis);
        expectShapeOk(diag);
    }
}

TEST(HighgroundFuzzTest, StructuredFamilies)
{
    expectShapeOk({{4, 4}, {5, 4}, {4, 5}, {5, 5}});                                   // 2x2 block
    expectShapeOk({{4, 4}, {5, 4}, {6, 4}, {4, 5}, {5, 5}, {6, 5}, {4, 6}, {5, 6}, {6, 6}}); // 3x3 block
    expectShapeOk({{4, 4}, {5, 4}, {6, 4}, {4, 5}, {6, 5}, {4, 6}, {5, 6}, {6, 6}});   // ring
    expectShapeOk({{4, 4}, {5, 4}, {4, 5}, {5, 5}, {6, 6}, {7, 7}, {8, 7}, {7, 8}, {8, 8}}); // bridge
    expectShapeOk({{4, 4}, {5, 5}, {6, 4}, {5, 6}, {6, 6}});                           // checker-ish
    expectShapeOk({{5, 4}, {4, 5}, {5, 5}, {6, 5}, {5, 6}});                           // plus
    expectShapeOk({{4, 4}, {4, 5}, {4, 6}, {5, 6}, {6, 6}, {6, 5}, {6, 4}, {5, 4}});   // U
}

TEST(HighgroundFuzzTest, SeededRandom4x4)
{
    std::vector<glm::ivec2> cells;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            cells.push_back({x, y});
        }
    }
    std::mt19937 rng(20260721);
    std::size_t failed = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        std::vector<glm::ivec2> nodes;
        for (const glm::ivec2& c : cells) {
            if (rng() & 1u) {
                nodes.push_back(c + glm::ivec2{3, 3});
            }
        }
        if (nodes.empty()) {
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

TEST(HighgroundFuzzTest, SeededRandom8x8Sizes)
{
    std::vector<glm::ivec2> cells;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            cells.push_back({x, y});
        }
    }
    const int sizes[] = {5, 10, 15, 25, 40, 60};
    std::mt19937 rng(1337);
    std::size_t failed = 0;
    for (const int size : sizes) {
        for (int iter = 0; iter < 150; ++iter) {
            std::shuffle(cells.begin(), cells.end(), rng);
            std::vector<glm::ivec2> nodes(cells.begin(), cells.begin() + std::min<int>(size, (int)cells.size()));
            const std::string failure = shapeFailure(nodes);
            if (!failure.empty()) {
                ++failed;
                ADD_FAILURE() << failure << " | size=" << size << " nodes:" << nodesStr(nodes);
                if (failed >= 8) {
                    return;
                }
            }
        }
    }
}

TEST(HighgroundFuzzTest, LargeBlocks)
{
    std::vector<glm::ivec2> block10;
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            block10.push_back({x, y});
        }
    }
    expectShapeOk(block10);

    std::vector<glm::ivec2> ring10 = block10;
    ring10.erase(std::remove(ring10.begin(), ring10.end(), glm::ivec2{5, 5}), ring10.end());
    expectShapeOk(ring10);
}
