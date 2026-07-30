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

float StoneField::reliefMask(const glm::vec3& p) const {
    if (!m_params.flatTop) {
        return 1.0f;
    }
    // Base slab normal (central differences): on the planar top it points
    // straight up, on the walls it is horizontal, across the rounded rim it
    // rotates smoothly — so the relief fades over the rim and the flat top
    // inherits the uneven outline of the carved wall below it.
    const float eps = 0.5f * m_params.base.cellSize;
    const float gx = m_base.evalBase(p + glm::vec3(eps, 0.0f, 0.0f)) -
        m_base.evalBase(p - glm::vec3(eps, 0.0f, 0.0f));
    const float gy = m_base.evalBase(p + glm::vec3(0.0f, eps, 0.0f)) -
        m_base.evalBase(p - glm::vec3(0.0f, eps, 0.0f));
    const float gz = m_base.evalBase(p + glm::vec3(0.0f, 0.0f, eps)) -
        m_base.evalBase(p - glm::vec3(0.0f, 0.0f, eps));
    const float len = std::sqrt(gx * gx + gy * gy + gz * gz);
    if (len < 1e-6f) {
        return 1.0f; // degenerate spot (medial axis), relief does no harm there
    }
    const float edge0 = m_params.flatTopLo;
    const float edge1 = std::max(edge0 + 1e-3f, m_params.flatTopHi);
    return 1.0f - glm::smoothstep(edge0, edge1, gy / len);
}

float StoneField::eval(const glm::vec3& p) const {
    const float dBase = m_base.evalBase(p);
    const float mask = surfaceMask(dBase);
    if (mask <= 0.0f) {
        // Outside the surface band the carve and the fbm can't reach the zero
        // crossing anyway — keep the clean slab SDF (and skip their cost).
        return dBase;
    }
    // StoneCubePlayground carve: d -= grooveDepth * cellFactor — the surface
    // bulges out inside voronoi cells and stays on the slab at the borders
    // (grooves between stones). Both the carve and the fbm fade out on the
    // flat plateau top (reliefMask).
    const float relief = reliefMask(p);
    float d = dBase - m_params.grooveDepth * cellFactor(p) * mask * relief;
    d += m_params.fbmAmplitude * (fbm(m_params.fbmFrequency * p) - 0.5f) * mask * relief;
    return d;
}

float StoneField::grooveDepth(const glm::vec3& p) const {
    const float mask = surfaceMask(m_base.evalBase(p));
    if (mask <= 0.0f) {
        return 0.0f;
    }
    return m_params.grooveDepth * (1.0f - cellFactor(p)) * mask * reliefMask(p);
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
