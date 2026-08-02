// render_core scene stitching CPU half: the contact-AO distance field
// (sub-cell coverage, chamfer transform) and the shared stitch uniforms.
// Ported from HighgroundWithEffects' PlaygroundSmokeTest AO checks.
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <render_core/scene_stitch.h>

namespace {

using render_core::AoFootprint;
using render_core::ContactAoField;

// Node grid over (nodesX - 1) x (nodesY - 1) cells; nodeOn is the same
// callback shape the editor's layer node grids will plug into.
struct NodeGrid {
    int nodesX = 0;
    int nodesY = 0;
    std::vector<std::uint8_t> nodes;

    explicit NodeGrid(int cellsX = 24, int cellsY = 24)
        : nodesX(cellsX + 1),
          nodesY(cellsY + 1),
          nodes(static_cast<std::size_t>(nodesX) * nodesY, 0) {}

    void set(int x, int y) { nodes[static_cast<std::size_t>(y) * nodesX + x] = 1; }

    AoFootprint footprint() const {
        AoFootprint fp;
        fp.nodesX = nodesX;
        fp.nodesY = nodesY;
        fp.nodeOn = [this](int x, int y) {
            return nodes[static_cast<std::size_t>(y) * nodesX + x] != 0;
        };
        return fp;
    }
};

// Solid 4x4 block of cells [10..13] x [10..13] on a 24x24 map, 4 texels per
// cell, 4 cells of margin — the playground smoke-test geometry.
ContactAoField buildSquareAo() {
    NodeGrid grid;
    for (int y = 10; y <= 13; ++y) {
        for (int x = 10; x <= 13; ++x) {
            grid.set(x, y);
        }
    }
    const AoFootprint fp = grid.footprint();
    ContactAoField field;
    render_core::buildContactAoField(&fp, 1, 4, 4, field);
    return field;
}

TEST(SceneStitchContactAo, FieldCoversMapPlusMargin) {
    const ContactAoField ao = buildSquareAo();
    ASSERT_FALSE(ao.empty());
    EXPECT_EQ(ao.width, (24 + 8) * 4);
    EXPECT_EQ(ao.height, (24 + 8) * 4);
    EXPECT_FLOAT_EQ(ao.cellsPerTexel, 0.25f);
    EXPECT_FLOAT_EQ(ao.originX, -4.0f);
    EXPECT_FLOAT_EQ(ao.originZ, -4.0f);
    // Inside the footprint the distance is zero.
    EXPECT_LE(ao.distanceAt(11.5f, 11.5f), 0.01f);
}

TEST(SceneStitchContactAo, HalfLitCellHasSubCellOutline) {
    const ContactAoField ao = buildSquareAo();
    // Cell (13, 11) has only its Left and Up nodes on, so the far half of
    // that same cell is already outside the footprint. Whole-cell coverage
    // would report zero across all of it and the AO ring would trace cell
    // borders instead of the wall.
    EXPECT_LE(ao.distanceAt(13.25f, 11.5f), 0.01f);
    const float emptyHalf = ao.distanceAt(13.75f, 11.5f);
    EXPECT_GE(emptyHalf, 0.1f);
    EXPECT_LE(emptyHalf, 0.8f);
}

TEST(SceneStitchContactAo, DistanceGrowsMonotonicallyAndSaturates) {
    const ContactAoField ao = buildSquareAo();
    float prev = -1.0f;
    for (float d = 0.0f; d <= 6.0f; d += 0.25f) {
        const float cur = ao.distanceAt(14.0f + d, 11.5f);
        EXPECT_GE(cur, prev - 1e-4f) << "not monotonic at +" << d;
        prev = cur;
    }
    EXPECT_GE(prev, render_core::kAoMaxDistanceCells - 0.01f);
    // At/ beyond the max distance the raw texel is exactly 255.
    EXPECT_FLOAT_EQ(ao.distanceAt(20.0f, 11.5f), render_core::kAoMaxDistanceCells);
}

TEST(SceneStitchContactAo, EmptyFootprintYieldsEmptyField) {
    NodeGrid grid;
    const AoFootprint fp = grid.footprint();
    ContactAoField field;
    // All nodes off -> empty field (the renderer falls back to "no AO").
    render_core::buildContactAoField(&fp, 1, 4, 4, field);
    EXPECT_TRUE(field.empty());
    // Degenerate inputs are empty too.
    render_core::buildContactAoField(nullptr, 0, 4, 4, field);
    EXPECT_TRUE(field.empty());
    render_core::buildContactAoField(&fp, 1, 0, 4, field);
    EXPECT_TRUE(field.empty());
    // A footprint without a node callback is skipped.
    const AoFootprint nullFp{25, 25, nullptr};
    render_core::buildContactAoField(&nullFp, 1, 4, 4, field);
    EXPECT_TRUE(field.empty());
}

TEST(SceneStitchContactAo, MultipleFootprintsFormAUnion) {
    NodeGrid a;
    a.set(4, 4);
    NodeGrid b;
    b.set(20, 20);
    const AoFootprint fps[2] = {a.footprint(), b.footprint()};
    ContactAoField field;
    render_core::buildContactAoField(fps, 2, 4, 4, field);
    ASSERT_FALSE(field.empty());
    // Both single-node bumps read as solid next to their node...
    EXPECT_LE(field.distanceAt(4.375f, 4.125f), 0.01f);
    EXPECT_LE(field.distanceAt(20.375f, 20.125f), 0.01f);
    // ...while the midpoint between them is far.
    EXPECT_GT(field.distanceAt(12.5f, 12.5f), 0.5f);
}

TEST(SceneStitchContactAo, OutOfBoundsReadsAsFar) {
    const ContactAoField ao = buildSquareAo();
    EXPECT_FLOAT_EQ(ao.distanceAt(-100.0f, 0.0f), render_core::kAoMaxDistanceCells);
    const ContactAoField empty;
    EXPECT_FLOAT_EQ(empty.distanceAt(0.0f, 0.0f), render_core::kAoMaxDistanceCells);
}

TEST(SceneStitchDiamondFill, WedgeSemantics) {
    const std::array<bool, 4> allOn{true, true, true, true};
    const std::array<bool, 4> allOff{false, false, false, false};
    EXPECT_FLOAT_EQ(render_core::diamondNodeFill(allOn, {0.3f, -0.2f}), 1.0f);
    EXPECT_FLOAT_EQ(render_core::diamondNodeFill(allOff, {0.3f, -0.2f}), 0.0f);
    // Single node on (Left): full coverage at its corner, none at the
    // opposite one, and the centre falls back to the node mean.
    const std::array<bool, 4> leftOnly{true, false, false, false};
    EXPECT_FLOAT_EQ(render_core::diamondNodeFill(leftOnly, {-1.0f, 0.0f}), 1.0f);
    EXPECT_FLOAT_EQ(render_core::diamondNodeFill(leftOnly, {1.0f, 0.0f}), 0.0f);
    EXPECT_FLOAT_EQ(render_core::diamondNodeFill(leftOnly, {0.0f, 0.0f}), 0.25f);
    // uv (0.5, 0.5) is the cell centre in diamond coordinates.
    const glm::vec2 centre = render_core::cellSquareToDiamond({0.5f, 0.5f});
    EXPECT_NEAR(centre.x, 0.0f, 1e-6f);
    EXPECT_NEAR(centre.y, 0.0f, 1e-6f);
}

TEST(SceneStitchSettings, SunDirectionIsNormalized) {
    const render_core::SceneStitchSettings s;
    const glm::vec3 sun = s.sunDirection();
    EXPECT_NEAR(glm::length(sun), 1.0f, 1e-6f);
    EXPECT_GT(sun.y, 0.0f); // elevation above the horizon
}

TEST(SceneStitchParams, BuildFromSettings) {
    const render_core::SceneStitchSettings s;
    const ContactAoField empty;
    const render_core::SceneStitchParams p = render_core::buildStitchParams(s, empty);
    const glm::vec3 sun = s.sunDirection();
    EXPECT_FLOAT_EQ(p.sunDir[0], sun.x);
    EXPECT_FLOAT_EQ(p.sunDir[1], sun.y);
    EXPECT_FLOAT_EQ(p.sunDir[2], sun.z);
    // Empty AO field: 1x1 "far" placeholder rect.
    EXPECT_FLOAT_EQ(p.aoRect[0], 0.0f);
    EXPECT_FLOAT_EQ(p.aoRect[1], 0.0f);
    EXPECT_FLOAT_EQ(p.aoRect[2], 1.0f);
    EXPECT_FLOAT_EQ(p.aoRect[3], 1.0f);
    EXPECT_FLOAT_EQ(p.params0[0], s.ambient);
    EXPECT_FLOAT_EQ(p.params0[1], s.diffuse);
    EXPECT_FLOAT_EQ(p.params0[2], s.gamma);
    EXPECT_FLOAT_EQ(p.params1[2], s.aoStrength);
    EXPECT_FLOAT_EQ(p.params1[3], s.aoRadius);
}

TEST(SceneStitchParams, TogglesGateTheUniforms) {
    render_core::SceneStitchSettings s;
    s.groundLit = false;
    s.aoEnabled = false;
    const ContactAoField empty;
    const render_core::SceneStitchParams p = render_core::buildStitchParams(s, empty);
    // Unlit ground: raw texture look (full ambient, no diffuse, gamma 1).
    EXPECT_FLOAT_EQ(p.params0[0], 1.0f);
    EXPECT_FLOAT_EQ(p.params0[1], 0.0f);
    EXPECT_FLOAT_EQ(p.params0[2], 1.0f);
    EXPECT_FLOAT_EQ(p.params1[2], 0.0f);
}

TEST(SceneStitchParams, AoRectFromField) {
    const ContactAoField ao = buildSquareAo();
    const render_core::SceneStitchSettings s;
    const render_core::SceneStitchParams p = render_core::buildStitchParams(s, ao);
    EXPECT_FLOAT_EQ(p.aoRect[0], ao.originX);
    EXPECT_FLOAT_EQ(p.aoRect[1], ao.originZ);
    EXPECT_FLOAT_EQ(p.aoRect[2], 1.0f / ao.extentX());
    EXPECT_FLOAT_EQ(p.aoRect[3], 1.0f / ao.extentZ());
}

TEST(SceneStitchParams, CoreBytesExcludeAoRect) {
    EXPECT_EQ(render_core::kStitchCoreBytes, offsetof(render_core::SceneStitchParams, aoRect));
    EXPECT_EQ(render_core::kStitchCoreBytes, 3u * 16u);
    EXPECT_EQ(sizeof(render_core::SceneStitchParams), 4u * 16u);
}

TEST(SceneStitchIsoHeight, MatchesProjectionFormula) {
    const float expected = 96.0f / std::sqrt(2.0f * (64.0f * 64.0f - 32.0f * 32.0f));
    EXPECT_FLOAT_EQ(render_core::isoHeightToWorld(64.0f, 32.0f, 96.0f), expected);
    // Degenerate cell proportions fall back to 1 instead of dividing by zero.
    EXPECT_FLOAT_EQ(render_core::isoHeightToWorld(32.0f, 32.0f, 96.0f), 1.0f);
}

} // namespace
