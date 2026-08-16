#include <gtest/gtest.h>

#include "render_core/depth_levels.h"

// Depth-levels model (render_core/depth_levels.h): the water/grid plane is
// world y = 0; fragments above it must be CLOSER (smaller z under
// LESS_EQUAL), fragments below it FARTHER (larger z). The z-buffer only
// arbitrates between fragments sharing a screen row, so the tests build
// screen-row-consistent cases: a vertex at world height worldY with ground
// anchor groundY appears at row = groundY - worldY * heightScale.
namespace {

constexpr float kHeightScale = 96.0f; // field px per 1.0 world height
constexpr float kLevelHeight = 0.35f; // one tech-field level (world units)

float zFarFor(float centerGroundY) {
    return centerGroundY + render_core::kZFarOffset;
}

// Baked (pre-normalization) depth of a fragment that appears at `screenRow`
// standing at world height `worldY`.
float bakedZAtRow(float screenRow, float worldY) {
    const float groundY = screenRow + worldY * kHeightScale;
    return render_core::liftedGroundY(groundY, worldY * kHeightScale);
}

float zAtRow(float screenRow, float worldY, float zFar) {
    return render_core::levelGroundZ(bakedZAtRow(screenRow, worldY), zFar);
}

TEST(DepthLevels, ZeroLiftKeepsLegacyGroundDepth) {
    // Flat content (y = 0) must keep the legacy ground-y-only depth.
    const float groundY = 1234.5f;
    const float zFar = zFarFor(groundY);
    EXPECT_FLOAT_EQ(render_core::levelGroundZ(render_core::liftedGroundY(groundY, 0.0f), zFar),
        (zFar - groundY) * render_core::kZScale);
}

TEST(DepthLevels, RaisedBeatsPlaneAndUnderwaterLoses) {
    const float zFar = zFarFor(1000.0f);
    const float row = 1000.0f;
    const float zPlane = zAtRow(row, 0.0f, zFar);
    const float zPlateau = zAtRow(row, kLevelHeight, zFar);  // +1 level
    const float zShelf = zAtRow(row, -kLevelHeight, zFar);   // -1 level (shoreline)
    EXPECT_LT(zPlateau, zPlane);
    EXPECT_GT(zShelf, zPlane);
}

TEST(DepthLevels, TallerFragmentWinsOnSharedRow) {
    // Stacked-highground readiness: at a shared screen row the higher
    // fragment is the iso-closer one.
    const float zFar = zFarFor(2000.0f);
    const float row = 2000.0f;
    EXPECT_LT(zAtRow(row, 2.0f * kLevelHeight, zFar), zAtRow(row, kLevelHeight, zFar));
}

TEST(DepthLevels, UnderwaterShelfLosesToPlaneOfItsLandingRow) {
    // The shelf slides DOWN-screen (worldY < 0 lifts the vertex by a negative
    // amount): it lands on a NEARER row than its ground anchor — and must
    // still lose to the water plane there.
    const float groundY = 500.0f;
    const float worldY = -kLevelHeight;
    const float zFar = zFarFor(groundY);
    const float landingRow = groundY - worldY * kHeightScale;
    const float zShelf = render_core::levelGroundZ(render_core::liftedGroundY(groundY, worldY * kHeightScale), zFar);
    const float zPlane = render_core::levelGroundZ(render_core::liftedGroundY(landingRow, 0.0f), zFar);
    EXPECT_LT(zPlane, zShelf);
}

TEST(DepthLevels, GridBiasKeepsGridOnPlane) {
    // The grid is the water-level plane marker: after the bias it is strictly
    // closer than the flat ground at the same ground-y (no z-fighting ties).
    const float groundY = 777.0f;
    const float zFar = zFarFor(groundY);
    const float zGround = render_core::levelGroundZ(render_core::liftedGroundY(groundY, 0.0f), zFar);
    const float zGrid = zGround - render_core::kGridZBias;
    EXPECT_LT(zGrid, zGround);
}

TEST(DepthLevels, TextureCoverAboveBeachBelowRaised) {
    // The texture2d ground cover bakes kTextureCoverLiftPx into its z. Its
    // pass runs after the 3D passes and depth-tests LESS_EQUAL against the
    // published depth (writing none), so the lift is its only arbitration:
    // it must beat a low mask3d beach top and lose to any raised level
    // (stone/cliff/tech/raised).
    const float zFar = zFarFor(1000.0f);
    const float row = 1000.0f;
    const float zTexture = render_core::levelGroundZ(row + render_core::kTextureCoverLiftPx, zFar);
    const float zBeach = zAtRow(row, 0.1f, zFar);          // mask3d beach top, ~+0.1 level
    const float zStone = zAtRow(row, kLevelHeight, zFar);  // raised 3D, +1 level
    EXPECT_LT(zTexture, zBeach);
    EXPECT_LT(zStone, zTexture);
}

TEST(DepthLevels, SpriteBaselineSitsBetweenGridAndGround) {
    // Sprite bias sandwich: a sprite's ground baseline must keep covering the
    // co-planar grid lines (kSpriteZBias > kGridZBias) while sitting on the
    // ground plane itself.
    EXPECT_GT(render_core::kSpriteZBias, render_core::kGridZBias);
    const float groundY = 777.0f;
    const float zFar = zFarFor(groundY);
    const float zGround = render_core::levelGroundZ(render_core::liftedGroundY(groundY, 0.0f), zFar);
    const float zGrid = zGround - render_core::kGridZBias;
    const float zSprite = zGround - render_core::kSpriteZBias;
    EXPECT_LT(zSprite, zGrid);
    EXPECT_LT(zGrid, zGround);
}

TEST(DepthLevels, SpriteWallMatchesReal3dWallDepth) {
    // A sprite is a vertical plane on its baseline row B: its fragment at
    // screen row R carries the same baked depth as a real 3D vertex of the
    // matching height standing at B — that is what makes sprites interleave
    // with the depth-writing 3D passes per-pixel.
    const float baseRow = 1000.0f;
    const float worldY = kLevelHeight; // fragment height (world units)
    const float zFar = zFarFor(baseRow);
    const float zSpriteFrag = render_core::levelGroundZ(
        render_core::liftedGroundY(baseRow, worldY * kHeightScale), zFar) - render_core::kSpriteZBias;
    const float z3dFrag = zAtRow(baseRow - worldY * kHeightScale, worldY, zFar);
    EXPECT_FLOAT_EQ(zSpriteFrag, z3dFrag - render_core::kSpriteZBias);
}

TEST(DepthLevels, SpriteBeatsFarther3dAndLosesToNearer3d) {
    // The core 2D<->3D contract on a shared screen row R: a sprite standing
    // at baseline row B loses to 3D geometry in front of it (larger ground-y)
    // and wins against 3D geometry behind it — painter order alone could
    // never express this.
    const float baseRow = 1000.0f;                    // sprite baseline (screen row of its feet)
    const float worldY = kLevelHeight;                // sprite fragment height
    const float row = baseRow - worldY * kHeightScale; // screen row of the fragment
    const float zFar = zFarFor(baseRow);
    const float zSprite = zAtRow(row, worldY, zFar) - render_core::kSpriteZBias;

    // 3D fragment on the same row, standing 30 field px IN FRONT of the sprite
    // (taller lift to reach the same row from a nearer ground row).
    const float z3dNear = zAtRow(row, worldY + 30.0f / kHeightScale, zFar);
    // 3D fragment on the same row, standing 30 field px BEHIND the sprite.
    const float z3dFar = zAtRow(row, worldY - 30.0f / kHeightScale, zFar);

    EXPECT_LT(z3dNear, zSprite);
    EXPECT_LT(zSprite, z3dFar);
}

TEST(DepthLevels, SpriteTopIsCloserThanItsFeet) {
    // Like a real wall: the higher a fragment on the sprite plane, the closer
    // it is (its screen row belongs to ground farther back).
    const float baseRow = 1200.0f;
    const float zFar = zFarFor(baseRow);
    const float zFeet = render_core::levelGroundZ(render_core::liftedGroundY(baseRow, 0.0f), zFar)
        - render_core::kSpriteZBias;
    const float zTop = render_core::levelGroundZ(
        render_core::liftedGroundY(baseRow, kLevelHeight * kHeightScale), zFar) - render_core::kSpriteZBias;
    EXPECT_LT(zTop, zFeet);
}

} // namespace
