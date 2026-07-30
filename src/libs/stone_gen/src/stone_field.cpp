#include "pch.h"

#include "stone_gen/stone_field.h"

#include <algorithm>
#include <cmath>

namespace stone_gen {

namespace {

// pcg3d integer hash + voronoi/fbm — bit-exact twins of stone_sdf.cpp (the
// canonical C++ form) and the StoneCubePlayground GLSL; duplicated here so
// StoneSdf stays untouched.
glm::uvec3 pcg3d(glm::uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

glm::vec3 hash3f(const glm::vec3& p) {
    const glm::uvec3 h = pcg3d(glm::uvec3(glm::ivec3(glm::floor(p))));
    return glm::vec3(h) * (1.0f / 4294967296.0f);
}

// (F1, F2, cellId) over the 27-neighbourhood.
glm::vec3 voro(const glm::vec3& x, float jitter) {
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

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float vnoise(const glm::vec3& p) {
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

float fbm(const glm::vec3& p) {
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

} // namespace

StoneField::StoneField(
    const StoneFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params)
    , m_base(params.base, nodes, nodesX, nodesY) {}

float StoneField::cellFactor(const glm::vec3& p) const {
    const glm::vec3 v = voro(m_params.voroScale * p + m_params.seed, m_params.cellJitter);
    return std::clamp(m_params.grooveK * (v.y - v.x), 0.0f, 1.0f);
}

float StoneField::surfaceMask(float dBase) const {
    // 1 at the slab surface, 0 beyond the carve band.
    return 1.0f - glm::smoothstep(0.0f, m_params.grooveMaskWidth, std::fabs(dBase));
}

void StoneField::sampleBase(
    const glm::vec3& p,
    float& outDBase,
    float& outD2,
    float& outNormalY,
    float& outD2GradLen) const {
    const float eps = 0.5f * m_params.base.cellSize;
    float d2xp = 0.0f;
    float d2xm = 0.0f;
    float d2zp = 0.0f;
    float d2zm = 0.0f;
    float d2yp = 0.0f;
    float d2ym = 0.0f;
    const float xp = m_base.evalBase(p + glm::vec3(eps, 0.0f, 0.0f), d2xp);
    const float xm = m_base.evalBase(p - glm::vec3(eps, 0.0f, 0.0f), d2xm);
    const float yp = m_base.evalBase(p + glm::vec3(0.0f, eps, 0.0f), d2yp);
    const float ym = m_base.evalBase(p - glm::vec3(0.0f, eps, 0.0f), d2ym);
    const float zp = m_base.evalBase(p + glm::vec3(0.0f, 0.0f, eps), d2zp);
    const float zm = m_base.evalBase(p - glm::vec3(0.0f, 0.0f, eps), d2zm);
    outDBase = m_base.evalBase(p, outD2);
    // Slab normal: straight up on the planar top, horizontal on the walls,
    // rotating smoothly across the rounded rim.
    const float gx = xp - xm;
    const float gy = yp - ym;
    const float gz = zp - zm;
    const float len = std::sqrt(gx * gx + gy * gy + gz * gz);
    outNormalY = len < 1e-6f ? 0.0f : gy / len; // degenerate: treat as wall
    outD2GradLen = std::sqrt(
        (d2xp - d2xm) * (d2xp - d2xm) + (d2zp - d2zm) * (d2zp - d2zm)) /
        (2.0f * eps);
}

void StoneField::rimStitch(
    float normalY,
    float d2,
    float d2GradLen,
    float& outTopness,
    float& outRim) const {
    if (!m_params.flatTop) {
        outTopness = 0.0f;
        outRim = 0.0f;
        return;
    }
    const float edge0 = m_params.flatTopLo;
    const float edge1 = std::max(edge0 + 1e-3f, m_params.flatTopHi);
    outTopness = glm::smoothstep(edge0, edge1, normalY);
    // d2 is a pseudo-SDF compressed into a fixed range — dividing by its
    // gradient gives the wall distance in ~world units.
    const float inward =
        (m_params.base.edgeRadius - d2) / std::max(d2GradLen, 0.05f);
    outRim = 1.0f - glm::smoothstep(0.0f, m_params.rimWidth, inward);
}

float StoneField::eval(const glm::vec3& p) const {
    float d2 = 0.0f;
    float dBase = m_base.evalBase(p, d2);
    float mask = surfaceMask(dBase);
    if (mask <= 0.0f) {
        // Outside the surface band the carve and the fbm can't reach the zero
        // crossing anyway — keep the clean slab SDF (and skip their cost).
        return dBase;
    }
    float normalY = 0.0f;
    float d2GradLen = 0.0f;
    sampleBase(p, dBase, d2, normalY, d2GradLen);
    mask = surfaceMask(dBase);
    // StoneCubePlayground carve: d -= grooveDepth * cellFactor — the surface
    // bulges out inside voronoi cells and stays on the slab at the borders
    // (grooves between stones). Rim stitch: the bulge keeps full strength
    // across the rim and wraps onto the top inside the rim band (fading
    // inward), so rim stones are not sliced; groove mouths scoop the top
    // down by rimNotch; the interior top stays exactly flat.
    float topness = 0.0f;
    float rim = 0.0f;
    rimStitch(normalY, d2, d2GradLen, topness, rim);
    const float cell = cellFactor(p);
    const float bulge = (1.0f - topness) + topness * rim * m_params.rimBulge;
    const float detail = (1.0f - topness) + topness * rim;
    float d = dBase + mask *
        (m_params.rimNotch * (1.0f - cell) * topness * rim -
         m_params.grooveDepth * cell * bulge);
    d += m_params.fbmAmplitude * (fbm(m_params.fbmFrequency * p) - 0.5f) * mask * detail;
    return d;
}

float StoneField::grooveDepth(const glm::vec3& p) const {
    float d2 = 0.0f;
    float dBase = m_base.evalBase(p, d2);
    const float mask = surfaceMask(dBase);
    if (mask <= 0.0f) {
        return 0.0f;
    }
    float normalY = 0.0f;
    float d2GradLen = 0.0f;
    sampleBase(p, dBase, d2, normalY, d2GradLen);
    float topness = 0.0f;
    float rim = 0.0f;
    rimStitch(normalY, d2, d2GradLen, topness, rim);
    // Wall groove floors plus the rim scoops read as carved area.
    return mask * (1.0f - cellFactor(p)) *
        (m_params.grooveDepth * (1.0f - topness) + m_params.rimNotch * topness * rim);
}

void StoneField::sample(std::vector<float>& outValues) const {
    const glm::vec3 origin = m_base.origin();
    const float cell = m_params.base.cellSize;
    const int px = m_base.sizeX() + 1;
    const int py = m_base.sizeY() + 1;
    const int pz = m_base.sizeZ() + 1;
    outValues.resize(static_cast<size_t>(px) * py * pz);
    for (int iy = 0; iy < py; ++iy) {
        const float y = origin.y + iy * cell;
        for (int iz = 0; iz < pz; ++iz) {
            const float z = origin.z + iz * cell;
            float* row = &outValues[(static_cast<size_t>(iy) * pz + iz) * px];
            for (int ix = 0; ix < px; ++ix) {
                row[ix] = eval(glm::vec3(origin.x + ix * cell, y, z));
            }
        }
    }

    // Gentle 3-tap blur over the sampled field: the voronoi grooves are
    // crease-like and alias into terracing on naive surface nets (same trick
    // as stone_mesh::generateMesh).
    auto sampleAt = [&outValues, px, pz](int x, int y, int z) -> float& {
        return outValues[(static_cast<size_t>(y) * pz + z) * px + x];
    };
    std::vector<float> tmp(outValues.size());
    for (int pass = 0; pass < m_params.blurPasses; ++pass) {
        for (int axis = 0; axis < 3; ++axis) {
            const int dims[3] = {px, py, pz};
            for (int y = 0; y < py; ++y) {
                for (int z = 0; z < pz; ++z) {
                    for (int x = 0; x < px; ++x) {
                        int c[3] = {x, y, z};
                        float sum = 0.0f;
                        for (int k = -1; k <= 1; ++k) {
                            int q[3] = {c[0], c[1], c[2]};
                            q[axis] = std::clamp(q[axis] + k, 0, dims[axis] - 1);
                            sum += sampleAt(q[0], q[1], q[2]);
                        }
                        tmp[(static_cast<size_t>(y) * pz + z) * px + x] = sum / 3.0f;
                    }
                }
            }
            outValues = tmp;
        }
    }
}

cliff::ScalarFieldView StoneField::view() {
    cliff::ScalarFieldView v;
    v.origin = m_base.origin();
    v.cellSize = m_params.base.cellSize;
    v.nx = m_base.sizeX();
    v.ny = m_base.sizeY();
    v.nz = m_base.sizeZ();
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    v.grooveDepth = [this](const glm::vec3& p) { return grooveDepth(p); };
    return v;
}

} // namespace stone_gen
