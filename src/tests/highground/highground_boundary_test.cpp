// highground_core: boundaryLoops — the boundary-first contract (strictly
// simple closed loops in field space).
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/highground.h>
#include <topology_core/diamond_isometry.h>

namespace {

highground::Grid gridOf(std::initializer_list<glm::ivec2> nodes) {
    std::vector<glm::ivec2> v(nodes);
    return highground::makeGrid(v.data(), v.size());
}

bool noThreeCollinear(const std::vector<glm::vec2>& loop) {
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const glm::vec2& a = loop[(i + loop.size() - 1) % loop.size()];
        const glm::vec2& b = loop[i];
        const glm::vec2& c = loop[(i + 1) % loop.size()];
        const glm::vec2 ab = b - a;
        const glm::vec2 bc = c - b;
        const float cross = ab.x * bc.y - ab.y * bc.x;
        if (std::abs(cross) < 1e-3f) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(HighgroundBoundaryTest, EmptyAndSingleNode)
{
    EXPECT_TRUE(highground::boundaryLoops(highground::Grid{}, 128.0f, 64.0f).empty());

    const auto loops = highground::boundaryLoops(gridOf({{5, 5}}), 128.0f, 64.0f);
    ASSERT_EQ(loops.size(), 1u);
    // Simplified wedge loop around one node: a 4-point diamond of the cell
    // centers (edge midpoints are straight-through, dropped by the cleanup).
    EXPECT_EQ(loops[0].size(), 4u);
    EXPECT_TRUE(noThreeCollinear(loops[0]));
}

TEST(HighgroundBoundaryTest, BlockIsOneLoop)
{
    const auto loops = highground::boundaryLoops(gridOf({{5, 5}, {6, 5}, {5, 6}, {6, 6}}), 128.0f, 64.0f);
    ASSERT_EQ(loops.size(), 1u);
    EXPECT_GE(loops[0].size(), 3u);
    EXPECT_TRUE(noThreeCollinear(loops[0]));
}

TEST(HighgroundBoundaryTest, DiagonalBridgeSplitsIntoSimpleLoops)
{
    // Two 2x2 blocks joined by a diagonal node: figure-eight pinches must be
    // split into three simple loops (block, neck wedge, block).
    const auto loops = highground::boundaryLoops(
        gridOf({{2, 2}, {3, 2}, {2, 3}, {3, 3}, {4, 4}, {5, 5}, {6, 5}, {5, 6}, {6, 6}}),
        128.0f,
        64.0f);
    ASSERT_EQ(loops.size(), 3u);
    for (const auto& loop : loops) {
        EXPECT_GE(loop.size(), 3u);
        EXPECT_TRUE(noThreeCollinear(loop));
    }
}

TEST(HighgroundBoundaryTest, RingProducesTwoLoops)
{
    std::vector<glm::ivec2> nodes;
    for (int y = 2; y <= 6; ++y) {
        for (int x = 2; x <= 6; ++x) {
            if (x == 4 && y == 4) {
                continue;
            }
            nodes.push_back({x, y});
        }
    }
    const auto loops = highground::boundaryLoops(highground::makeGrid(nodes.data(), nodes.size()), 128.0f, 64.0f);
    ASSERT_EQ(loops.size(), 2u); // outer + hole
    for (const auto& loop : loops) {
        EXPECT_TRUE(noThreeCollinear(loop));
    }
}
