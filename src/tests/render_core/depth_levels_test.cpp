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

} // namespace
