// C++ twin of the StoneCubePlayground GLSL SDF — the canonical form of the
// stone-cube generator (the raymarch shader stays the look reference).
// Principles from iq's "Voronoi - rocks" (docs/reference/shadertoy): round
// box + bulge inside voronoi cells (clamp(k*(F2-F1))), fbm detail.
//
// Pure pipeline: StoneCubeParams -> StoneSdf -> eval/map/shading queries,
// no Qt/GPU (meshed via highground_core surface nets, baked in stone_bake).
#pragma once

#include <glm/glm.hpp>

namespace stone_gen {

struct StoneCubeParams {
    float boxSize[4] = {1.0f, 1.0f, 1.0f, 0.08f}; // half extents xyz, w = bevel
    float shape1[4] = {1.6f, 1.0f, 0.22f, 2.5f};  // voronoiScale, cellJitter, grooveDepth, grooveK
    float shape2[4] = {0.02f, 4.0f, 0.35f, 0.0f}; // fbmAmp, fbmFreq, bumpStrength, seed
    float look[4] = {0.7f, 0.9f, 0.35f, 0.0f};    // lightYaw, lightPitch, moss, toneSeed
};

class StoneSdf {
public:
    explicit StoneSdf(const StoneCubeParams& params) : m_params(params) {}

    const StoneCubeParams& params() const { return m_params; }

    // Signed distance to the stone cube surface (negative inside).
    float eval(const glm::vec3& p) const;
    // Full map query: signed distance + cell factor (0 groove .. 1 stone) +
    // cell id (for the per-stone tint hash).
    void map(const glm::vec3& p, float& outDist, float& outCellF, float& outCellId) const;

    glm::vec3 normal(const glm::vec3& p) const; // central differences of eval
    // fbm-gradient bump on top of the smooth normal (detail the mesh lacks;
    // this is what the baked normal map captures). eps is the gradient
    // stencil: the baker passes ~1 texel so above-Nyquist octaves average
    // out instead of exploding into per-texel noise.
    glm::vec3 bumpNormal(const glm::vec3& p, const glm::vec3& n, float eps = 0.002f) const;
    float ambientOcclusion(const glm::vec3& p, const glm::vec3& n, int taps) const;
    // Surface albedo: per-stone tint (cell id hash) + moss + groove darkening.
    glm::vec3 albedo(const glm::vec3& p, const glm::vec3& n, float cellF, float cellId) const;

private:
    glm::vec3 hash3f(const glm::vec3& p) const;
    // (F1, F2, cellId) over the 27-neighbourhood.
    glm::vec3 voro(const glm::vec3& x) const;
    float vnoise(const glm::vec3& p) const;
    float fbm(const glm::vec3& p) const;

    StoneCubeParams m_params;
};

} // namespace stone_gen
