// render_core texture-2D layer: per-node texture recovery by tile voting and
// the per-cell fan blend data (texture_blend.h).
#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <render_core/landscape_renderer.h>
#include <render_core/texture_blend.h>

namespace {

using render_core::LandscapeTile;
using render_core::TextureBlendCell;

LandscapeTile makeTile(int x, int y, std::size_t tileIndex, const char* uuid) {
    LandscapeTile t;
    t.cell = {x, y};
    t.assetUuid = uuid;
    t.tileIndex = tileIndex;
    return t;
}

const TextureBlendCell* findCell(const std::vector<TextureBlendCell>& cells, int x, int y) {
    for (const auto& c : cells) {
        if (c.cell.x == x && c.cell.y == y) {
            return &c;
        }
    }
    return nullptr;
}

TEST(TextureBlend, SingleFullCellOneCandidate) {
    const auto cells = render_core::buildTextureBlendCells({makeTile(0, 0, 0, "A")}); // 0 = Full
    ASSERT_EQ(cells.size(), 1u);
    const TextureBlendCell& c = cells.front();
    EXPECT_EQ(c.candidateCount, 1);
    EXPECT_EQ(c.candidateUuids[0], "A");
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(c.cornerWeights[i], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)) << "corner " << i;
        EXPECT_FLOAT_EQ(c.cornerFill[i], 1.0f) << "corner " << i;
    }
}

TEST(TextureBlend, AdjacentCellsBlendAcrossSharedNodes) {
    // Two full cells side by side share the nodes (1,0) and (1,1). Both vote
    // 1:1 there — the tie goes to the later tile ("B"), so the blend band
    // lives inside the "A" cell while the "B" cell stays pure.
    const auto cells = render_core::buildTextureBlendCells({
        makeTile(0, 0, 0, "A"),
        makeTile(1, 0, 0, "B"),
    });
    ASSERT_EQ(cells.size(), 2u);

    const TextureBlendCell* a = findCell(cells, 0, 0);
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->candidateCount, 2);
    EXPECT_EQ(a->candidateUuids[0], "A");
    EXPECT_EQ(a->candidateUuids[1], "B");
    // Corner order is [Left, Up, Right, Down]: Left/Up stay A, the shared
    // Right/Down nodes vote B.
    EXPECT_EQ(a->cornerWeights[0], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(a->cornerWeights[1], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(a->cornerWeights[2], glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    EXPECT_EQ(a->cornerWeights[3], glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));

    const TextureBlendCell* b = findCell(cells, 1, 0);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->candidateCount, 1);
    EXPECT_EQ(b->candidateUuids[0], "B");
}

TEST(TextureBlend, PartialCellFillAndOffCornerSlot) {
    // tileIndex 8 = UpCorner: only the Up node is on.
    const auto cells = render_core::buildTextureBlendCells({makeTile(2, 2, 8, "A")});
    ASSERT_EQ(cells.size(), 1u);
    const TextureBlendCell& c = cells.front();
    EXPECT_EQ(c.candidateCount, 1);
    // Fill follows the node mask [Left, Up, Right, Down].
    EXPECT_FLOAT_EQ(c.cornerFill[0], 0.0f);
    EXPECT_FLOAT_EQ(c.cornerFill[1], 1.0f);
    EXPECT_FLOAT_EQ(c.cornerFill[2], 0.0f);
    EXPECT_FLOAT_EQ(c.cornerFill[3], 0.0f);
    // Off corners take the first candidate's slot (their region fades out).
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(c.cornerWeights[i], glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)) << "corner " << i;
    }
}

TEST(TextureBlend, EmptyCellDropped) {
    // tileIndex 22+ is outside the atlas convention (Unknown) — no on-nodes.
    const auto cells = render_core::buildTextureBlendCells({makeTile(0, 0, 22, "A")});
    EXPECT_TRUE(cells.empty());
}

TEST(TextureBlend, DeterministicForSameInput) {
    const std::vector<LandscapeTile> tiles = {
        makeTile(0, 0, 0, "A"),
        makeTile(1, 0, 0, "B"),
        makeTile(0, 1, 0, "B"),
        makeTile(5, 5, 8, "C"),
    };
    const auto first = render_core::buildTextureBlendCells(tiles);
    const auto second = render_core::buildTextureBlendCells(tiles);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        EXPECT_EQ(first[i].cell, second[i].cell);
        EXPECT_EQ(first[i].candidateCount, second[i].candidateCount);
        for (int k = 0; k < 4; ++k) {
            EXPECT_EQ(first[i].candidateUuids[k], second[i].candidateUuids[k]);
            EXPECT_EQ(first[i].cornerWeights[k], second[i].cornerWeights[k]);
            EXPECT_FLOAT_EQ(first[i].cornerFill[k], second[i].cornerFill[k]);
        }
    }
}

} // namespace
