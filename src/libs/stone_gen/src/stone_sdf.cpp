#include "pch.h"

#include "stone_gen/stone_sdf.h"

#include <algorithm>
#include <cmath>

namespace stone_gen {

namespace {

float fractf(float x) { return x - std::floor(x); }

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float sdRoundBox(const glm::vec3& p, const glm::vec3& b, float r) {
    const glm::vec3 q = glm::abs(p) - b;
    return glm::length(glm::max(q, glm::vec3(0.0f))) +
        std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - r;
}

} // namespace

glm::vec3 StoneSdf::hash3f(const glm::vec3& p) const {
    const glm::vec3 q(
        glm::dot(p, glm::vec3(127.1f, 311.7f, 74.7f)),
        glm::dot(p, glm::vec3(269.5f, 183.3f, 246.1f)),
        glm::dot(p, glm::vec3(113.5f, 271.9f, 124.6f)));
    return glm::vec3(
        fractf(std::sin(q.x) * 43758.5453123f),
        fractf(std::sin(q.y) * 43758.5453123f),
        fractf(std::sin(q.z) * 43758.5453123f));
}

glm::vec3 StoneSdf::voro(const glm::vec3& x) const {
    const float jitter = m_params.shape1[1];
    const glm::vec3 p = glm::floor(x);
    const glm::vec3 f = glm::fract(x);
    float id = 0.0f;
    glm::vec2 res(100.0f);
    for (int k = -1; k <= 1; ++k) {
        for (int j = -1; j <= 1; ++j) {
            for (int i = -1; i <= 1; ++i) {
                const glm::vec3 b(static_cast<float>(i), static_cast<float>(j),
                    static_cast<float>(k));
                const glm::vec3 r = b - f +
                    glm::mix(glm::vec3(0.5f), hash3f(p + b), jitter);
                const float d = glm::dot(r, r);
                if (d < res.x) {
                    id = glm::dot(p + b, glm::vec3(1.0f, 57.0f, 113.0f));
                    res = glm::vec2(d, res.x);
                } else if (d < res.y) {
                    res.y = d;
                }
            }
        }
    }
    return glm::vec3(std::sqrt(res.x), std::sqrt(res.y), std::fabs(id));
}

float StoneSdf::vnoise(const glm::vec3& p) const {
    const glm::vec3 i = glm::floor(p);
    glm::vec3 f = glm::fract(p);
    f = f * f * (3.0f - 2.0f * f);
    const float n000 = hash3f(i).x;
    const float n100 = hash3f(i + glm::vec3(1, 0, 0)).x;
    const float n010 = hash3f(i + glm::vec3(0, 1, 0)).x;
    const float n110 = hash3f(i + glm::vec3(1, 1, 0)).x;
    const float n001 = hash3f(i + glm::vec3(0, 0, 1)).x;
    const float n101 = hash3f(i + glm::vec3(1, 0, 1)).x;
    const float n011 = hash3f(i + glm::vec3(0, 1, 1)).x;
    const float n111 = hash3f(i + glm::vec3(1, 1, 1)).x;
    return lerpf(
        lerpf(lerpf(n000, n100, f.x), lerpf(n010, n110, f.x), f.y),
        lerpf(lerpf(n001, n101, f.x), lerpf(n011, n111, f.x), f.y),
        f.z);
}

float StoneSdf::fbm(const glm::vec3& p) const {
    float f = 0.0f;
    float a = 0.5f;
    glm::vec3 q = p;
    for (int i = 0; i < 4; ++i) {
        f += a * vnoise(q);
        q = q * 2.0f + 11.7f;
        a *= 0.5f;
    }
    return f;
}

void StoneSdf::map(const glm::vec3& p, float& outDist, float& outCellF,
    float& outCellId) const {
    const float voroScale = m_params.shape1[0];
    const float grooveDepth = m_params.shape1[2];
    const float grooveK = m_params.shape1[3];
    const float fbmAmp = m_params.shape2[0];
    const float fbmFreq = m_params.shape2[1];
    const float seed = m_params.shape2[3];

    const glm::vec3 boxSize(m_params.boxSize[0], m_params.boxSize[1], m_params.boxSize[2]);
    const float dBox = sdRoundBox(p, boxSize, m_params.boxSize[3]);
    const glm::vec3 v = voro(voroScale * p + seed);
    const float f = std::clamp(grooveK * (v.y - v.x), 0.0f, 1.0f);
    float d = dBox - grooveDepth * f;
    d += fbmAmp * (fbm(fbmFreq * p) - 0.5f);
    outDist = d;
    outCellF = f;
    outCellId = v.z;
}

float StoneSdf::eval(const glm::vec3& p) const {
    float d = 0.0f;
    float f = 0.0f;
    float id = 0.0f;
    map(p, d, f, id);
    return d;
}

glm::vec3 StoneSdf::normal(const glm::vec3& p) const {
    const glm::vec2 e(0.0015f, -0.0015f);
    const glm::vec3 n =
        glm::vec3(e.x, e.y, e.y) * eval(p + glm::vec3(e.x, e.y, e.y)) +
        glm::vec3(e.y, e.y, e.x) * eval(p + glm::vec3(e.y, e.y, e.x)) +
        glm::vec3(e.y, e.x, e.y) * eval(p + glm::vec3(e.y, e.x, e.y)) +
        glm::vec3(e.x, e.x, e.x) * eval(p + glm::vec3(e.x, e.x, e.x));
    const float len2 = glm::dot(n, n);
    return len2 > 1e-12f ? n * (1.0f / std::sqrt(len2)) : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 StoneSdf::bumpNormal(const glm::vec3& p, const glm::vec3& n, float eps) const {
    const float bumpStr = m_params.shape2[2];
    if (bumpStr <= 0.0f) {
        return n;
    }
    const float fbmFreq = m_params.shape2[1];
    const glm::vec3 q = fbmFreq * 4.0f * p;
    const glm::vec3 dq = fbmFreq * 4.0f * glm::vec3(eps, 0.0f, 0.0f);
    const float ref = fbm(q);
    const glm::vec3 grad(
        fbm(q + glm::vec3(dq.x, 0.0f, 0.0f)) - ref,
        fbm(q + glm::vec3(0.0f, dq.x, 0.0f)) - ref,
        fbm(q + glm::vec3(0.0f, 0.0f, dq.x)) - ref);
    glm::vec3 g = grad / eps;
    g -= n * glm::dot(n, g);
    // Cap the perturbation: the raw fbm gradient reaches tens of units and
    // bumpStr * g would randomize the normal instead of perturbing it
    // (baked normal map came out as rainbow noise). g/(1+|g|) keeps the
    // detail direction, bumpStr stays the strength ceiling.
    const float gl = std::sqrt(glm::dot(g, g));
    if (gl > 1e-6f) {
        g = g * (1.0f / (1.0f + gl)); // g / (1 + |g|)
    }
    const glm::vec3 bumped = n + bumpStr * g;
    const float len2 = glm::dot(bumped, bumped);
    return len2 > 1e-12f ? bumped * (1.0f / std::sqrt(len2)) : n;
}

float StoneSdf::ambientOcclusion(const glm::vec3& p, const glm::vec3& n, int taps) const {
    float occ = 0.0f;
    float sca = 1.0f;
    for (int i = 1; i <= taps; ++i) {
        const float h = 0.01f + 0.05f * static_cast<float>(i);
        occ += (h - eval(p + n * h)) * sca;
        sca *= 0.75f;
    }
    return std::clamp(1.0f - 2.0f * occ, 0.0f, 1.0f);
}

glm::vec3 StoneSdf::albedo(const glm::vec3& p, const glm::vec3& n, float cellF,
    float cellId) const {
    const float mossAmt = m_params.look[2];
    const float toneSeed = m_params.look[3];

    const float idHash = fractf(std::sin(cellId * 17.31f + toneSeed * 91.7f) * 43758.5453f);
    glm::vec3 rockCol = glm::mix(glm::vec3(0.40f, 0.40f, 0.43f),
        glm::vec3(0.60f, 0.57f, 0.50f), idHash);
    rockCol *= 0.85f + 0.30f * idHash;
    const float mossMask = glm::smoothstep(0.35f, 0.75f, n.y) *
        glm::smoothstep(0.4f, 0.6f, fbm(2.0f * p + toneSeed));
    rockCol = glm::mix(rockCol, glm::vec3(0.24f, 0.36f, 0.15f), mossAmt * mossMask);
    rockCol *= 0.45f + 0.55f * cellF;
    return rockCol;
}

} // namespace stone_gen
