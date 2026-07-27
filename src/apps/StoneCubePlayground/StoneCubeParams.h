// Stone-cube generator parameters. CPU layout mirrors the std140 uniform
// block in the raymarch shader (7 x vec4 = 112 bytes; order must match the
// glsl_uniforms table in StoneCubeScene.cpp).
#pragma once

namespace stonecube {

struct Params {
    // Shape
    float boxSize[4] = {1.0f, 1.0f, 1.0f, 0.08f}; // half extents xyz, w = bevel
    float shape1[4] = {1.6f, 1.0f, 0.22f, 3.5f};  // voronoiScale, cellJitter, grooveDepth, grooveK
    float shape2[4] = {0.02f, 4.0f, 0.35f, 0.0f}; // fbmAmp, fbmFreq, bumpStrength, seed
    // Look
    float look[4] = {0.7f, 0.9f, 0.35f, 0.0f};    // lightYaw, lightPitch, moss, toneSeed
};

} // namespace stonecube
