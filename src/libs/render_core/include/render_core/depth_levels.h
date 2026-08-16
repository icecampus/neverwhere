#pragma once

// Depth-levels model — the z-order contract shared by all world passes
// (flat ground, texture2d, grid overlay, raised, cliff/stone/tech, cyclopean).
//
// A fragment's LEVEL is its world height y (world units; the water/grid plane
// is y = kWaterLevelY):
//   y > 0  — highground (cliff/stone/tech, cyclopean, raised): closer than
//            the water plane, drawn above it and above the flat 2D layers;
//   y == 0 — flat ground / texture2d / grid: the water level itself;
//   y < 0  — underwater parts (the tech-field shoreline shelf): strictly
//            behind the water plane, dissolving into the water (see the
//            cliff shading's underwaterFade).
// Stacked highground (levels +1..+2) needs no renderer changes: y is
// continuous, only the data model has to provide the geometry.
//
// Z convention: LESS_EQUAL + clear 1.0, nearer = SMALLER z. Depth along the
// iso view ray (0,+1,+1) is groundY + screenLift: a vertex lifted `liftPx`
// screen px above its ground anchor (liftPx == worldY * heightScale — the
// exact lift already applied to its screen y) is that much closer to the
// viewer. The VS normalizes the baked value via z_range = {far, 1/(far-near)}
// anchored at the visible center with generous margins; the anchor must stay
// a per-frame constant — a camera-dependent anchor desyncs the cached mesh
// streams from the per-frame overlay (SDFGeneratedLandscape gotcha).
namespace render_core {

constexpr float kWaterLevelY = 0.0f;
constexpr float kZFarOffset = 100000.0f;
constexpr float kZScale = 1.0f / 200000.0f;

// Weight of the height (lift) term in the depth metric. 1.0 = iso-consistent
// (view ray at 45 degrees); 0.0 = legacy ground-y-only ordering.
constexpr float kDepthHeightFactor = 1.0f;

// Grid overlay depth bias: keeps the grid deterministically on top of the
// flat ground (both are affine in ground-y and would otherwise z-fight).
constexpr float kGridZBias = 1e-5f;

// Sprite depth bias: Tile2D sprites render after the grid overlay and must
// keep covering it at co-planar baselines (the grid has kGridZBias); the bias
// also absorbs ULP noise against the ground plane written by the
// ALWAYS-compare flat ground pass.
constexpr float kSpriteZBias = 2e-5f; // strictly greater than kGridZBias

// Texture2d ground-cover lift, in field px of the baked ground-y. The
// texture2d pass renders AFTER the 3D passes (WorldRenderer::render) and
// depth-tests LESS_EQUAL against the z they published — it writes no depth
// of its own, so the grid drawn after it stays visible over the painted
// cover and the water-surface pass keeps reading the surface underneath.
// This lift is therefore the cover's only say against the other passes:
// it puts the ground cover ABOVE low ground-relief meshes (a mask3d beach
// top sits ~6-13 px over the plane: height*(1-sink) * raisedHeight) but
// still BELOW any raised 3D (stone/cliff/tech/raised ride a full level,
// ~96+ px).
constexpr float kTextureCoverLiftPx = 24.0f;

// Baked z-coordinate (field/world units, pre-normalization) for a vertex
// lifted `liftPx` screen px above its ground anchor.
inline constexpr float liftedGroundY(float groundY, float liftPx) {
    return groundY + liftPx * kDepthHeightFactor;
}

// Final normalized depth for a baked z (see liftedGroundY).
inline constexpr float levelGroundZ(float bakedGroundY, float zFar) {
    return (zFar - bakedGroundY) * kZScale;
}

} // namespace render_core
