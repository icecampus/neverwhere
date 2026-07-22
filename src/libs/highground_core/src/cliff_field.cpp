#include "pch.h"

#include "highground_core/cliff_field.h"

namespace cliff {

namespace {

// Formula-based smoothstep; reversed edges (edge0 > edge1) are intentional
// (grooveMask fades out with distance), so no glm::smoothstep/GLES UB here.
float smoothstepf(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Omphalos Hashv4v3-style corner hash.
float hashCorner(float x, float y, float z) {
    const float s = std::sin(x * 37.0f + y * 39.0f + z * 41.0f) * 43758.54f;
    return s - std::floor(s);
}

float noisefv3(const glm::vec3& p) {
    const float ix = std::floor(p.x);
    const float iy = std::floor(p.y);
    const float iz = std::floor(p.z);
    float fx = p.x - ix;
    float fy = p.y - iy;
    float fz = p.z - iz;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    fz = fz * fz * (3.0f - 2.0f * fz);
    const float c000 = hashCorner(ix, iy, iz);
    const float c100 = hashCorner(ix + 1.0f, iy, iz);
    const float c010 = hashCorner(ix, iy + 1.0f, iz);
    const float c110 = hashCorner(ix + 1.0f, iy + 1.0f, iz);
    const float c001 = hashCorner(ix, iy, iz + 1.0f);
    const float c101 = hashCorner(ix + 1.0f, iy, iz + 1.0f);
    const float c011 = hashCorner(ix, iy + 1.0f, iz + 1.0f);
    const float c111 = hashCorner(ix + 1.0f, iy + 1.0f, iz + 1.0f);
    return lerpf(
        lerpf(lerpf(c000, c100, fx), lerpf(c010, c110, fx), fy),
        lerpf(lerpf(c001, c101, fx), lerpf(c011, c111, fx), fy),
        fz);
}

// Counter-clockwise 2D rotation, same convention as dr2's Rot2D.
void rot2d(float& a, float& b, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float na = a * c - b * s;
    const float nb = a * s + b * c;
    a = na;
    b = nb;
}

} // namespace

float gmod(float x, float m) { return x - m * std::floor(x / m); }

float CliffField::grooveWave(float y) const {
    const float period = m_params.groovePeriod;
    const float half = 0.5f * period;
    return std::fabs(gmod(y + m_params.groovePhase, period) - half) -
        (half - m_params.grooveDepthMax);
}

float CliffField::grooveMask(float d2, float y) const {
    float mask = smoothstepf(m_params.grooveMaskWidth, 0.0f, std::fabs(d2));
    if (m_params.grooveRimFade > 0.0f) {
        // Fade the carve out approaching the top rim so the edge stays solid.
        const float rim = m_params.plateauHeight - m_params.grooveRimFade;
        mask *= 1.0f - smoothstepf(rim, m_params.plateauHeight, y);
    }
    return mask;
}

float CliffField::applyGrooves(float f, const glm::vec3& p, float mask) const {
    const float fade = (1.0f - mask) * m_params.grooveFadeK;
    const float r = m_params.grooveSmooth;
    float gx = p.x;
    float gy = p.y;
    float gz = p.z;
    rot2d(gx, gy, m_params.grooveAngles[0][0]);
    f = smoothMax(f, grooveWave(gy) - fade, r);
    gx = p.x;
    gy = p.y;
    gz = p.z;
    rot2d(gx, gz, m_params.grooveAngles[1][0]);
    rot2d(gx, gy, m_params.grooveAngles[1][1]);
    f = smoothMax(f, grooveWave(gy) - fade, r);
    gx = p.x;
    gy = p.y;
    gz = p.z;
    rot2d(gx, gz, m_params.grooveAngles[2][0]);
    rot2d(gx, gy, m_params.grooveAngles[2][1]);
    f = smoothMax(f, grooveWave(gy) - fade, r);
    return f;
}

float fbm(const glm::vec3& p, int octaves) {
    float f = 0.0f;
    float a = 1.0f;
    float norm = 0.0f;
    glm::vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        f += a * noisefv3(q);
        norm += a;
        a *= 0.5f;
        q = q * 2.0f;
    }
    return f / norm;
}

float fbm3(const glm::vec3& p) {
    float f = 0.0f;
    float a = 1.0f;
    glm::vec3 q = p;
    for (int i = 0; i < 5; ++i) {
        f += a * noisefv3(q);
        a *= 0.5f;
        q = q * 2.0f;
    }
    return f * (1.0f / 1.9375f);
}

float smoothMin(float a, float b, float r) {
    const float h = std::clamp(0.5f + 0.5f * (b - a) / r, 0.0f, 1.0f);
    return lerpf(b, a, h) - r * h * (1.0f - h);
}

float smoothMax(float a, float b, float r) { return -smoothMin(-a, -b, r); }

CliffField::CliffField(const FieldParams& params, const std::uint8_t* nodes,
    int nodesX, int nodesY)
    : m_params(params) {
    // The region is defined by the injected node grid (nodesX-1 x nodesY-1
    // map cells); the field is rectangular in XZ, Y spans the plateau height.
    m_regionX = static_cast<float>(nodesX - 1);
    m_regionZ = static_cast<float>(nodesY - 1);
    const float pad = params.padding;
    const float cell = params.cellSize;
    m_nx = static_cast<int>(std::ceil((m_regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((m_regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((params.plateauHeight + 2.0f * pad) / cell));
    m_origin = glm::vec3(-pad, -pad, -pad);

    auto nodeAt = [&](int x, int z) -> float {
        return (nodes != nullptr && x >= 0 && x < nodesX && z >= 0 && z < nodesY)
            ? static_cast<float>(nodes[static_cast<size_t>(z) * nodesX + x])
            : 0.0f;
    };

    // 2D height grid at field XZ resolution, bilinear from the binary nodes.
    m_hW = m_nx + 1;
    m_hH = m_nz + 1;
    m_hGrid.resize(static_cast<size_t>(m_hW) * m_hH);
    for (int j = 0; j < m_hH; ++j) {
        const float z = m_origin.z + j * cell;
        for (int i = 0; i < m_hW; ++i) {
            const float x = m_origin.x + i * cell;
            const float gx = std::clamp(x, 0.0f, m_regionX - 1e-4f);
            const float gz = std::clamp(z, 0.0f, m_regionZ - 1e-4f);
            const int x0 = static_cast<int>(gx);
            const int z0 = static_cast<int>(gz);
            const float fx = gx - static_cast<float>(x0);
            const float fz = gz - static_cast<float>(z0);
            const float n00 = nodeAt(x0, z0);
            const float n10 = nodeAt(x0 + 1, z0);
            const float n01 = nodeAt(x0, z0 + 1);
            const float n11 = nodeAt(x0 + 1, z0 + 1);
            m_hGrid[static_cast<size_t>(j) * m_hW + i] =
                lerpf(lerpf(n00, n10, fx), lerpf(n01, n11, fx), fz);
        }
    }

    // Separable box blur passes to round the outline corners; borders clamp.
    const int radius = params.blurRadiusCells;
    std::vector<float> tmp(m_hGrid.size());
    for (int pass = 0; pass < params.blurPasses; ++pass) {
        for (int j = 0; j < m_hH; ++j) {
            for (int i = 0; i < m_hW; ++i) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int ii = std::clamp(i + k, 0, m_hW - 1);
                    sum += m_hGrid[static_cast<size_t>(j) * m_hW + ii];
                }
                tmp[static_cast<size_t>(j) * m_hW + i] = sum / static_cast<float>(2 * radius + 1);
            }
        }
        for (int j = 0; j < m_hH; ++j) {
            for (int i = 0; i < m_hW; ++i) {
                float sum = 0.0f;
                for (int k = -radius; k <= radius; ++k) {
                    const int jj = std::clamp(j + k, 0, m_hH - 1);
                    sum += tmp[static_cast<size_t>(jj) * m_hW + i];
                }
                m_hGrid[static_cast<size_t>(j) * m_hW + i] = sum / static_cast<float>(2 * radius + 1);
            }
        }
    }
}

float CliffField::heightAt(float x, float z) const {
    const float cell = m_params.cellSize;
    const float gx = std::clamp((x - m_origin.x) / cell, 0.0f, static_cast<float>(m_hW - 1) - 1e-4f);
    const float gz = std::clamp((z - m_origin.z) / cell, 0.0f, static_cast<float>(m_hH - 1) - 1e-4f);
    const int x0 = static_cast<int>(gx);
    const int z0 = static_cast<int>(gz);
    const float fx = gx - static_cast<float>(x0);
    const float fz = gz - static_cast<float>(z0);
    const float h00 = m_hGrid[static_cast<size_t>(z0) * m_hW + x0];
    const float h10 = m_hGrid[static_cast<size_t>(z0) * m_hW + x0 + 1];
    const float h01 = m_hGrid[static_cast<size_t>(z0 + 1) * m_hW + x0];
    const float h11 = m_hGrid[static_cast<size_t>(z0 + 1) * m_hW + x0 + 1];
    return lerpf(lerpf(h00, h10, fx), lerpf(h01, h11, fx), fz);
}

float CliffField::evalBase(const glm::vec3& p, float& outD2) const {
    const float h = heightAt(p.x, p.z);
    const float d2 = (0.5f - h) * m_params.d2Scale;
    outD2 = d2;
    const float halfH = 0.5f * m_params.plateauHeight;
    // Plateau slab: inside the outline and 0 <= y <= H; edgeRadius expands the
    // slab so its convex rim (wall meets top) gets rounded.
    const float slab = std::max(d2, std::fabs(p.y - halfH) - halfH) - m_params.edgeRadius;
    // Ground chunk: rounded box with its top at y = 0, fully inside the padding
    // so the field stays positive on the grid border.
    const float centerX = 0.5f * m_regionX;
    const float centerZ = 0.5f * m_regionZ;
    const float hx = centerX + m_params.groundMargin - m_params.groundRounding;
    const float hz = centerZ + m_params.groundMargin - m_params.groundRounding;
    const float hy = 0.5f * m_params.groundDepth - m_params.groundRounding;
    const float cy = -0.5f * m_params.groundDepth;
    const float qx = std::fabs(p.x - centerX) - hx;
    const float qy = std::fabs(p.y - cy) - hy;
    const float qz = std::fabs(p.z - centerZ) - hz;
    const float ax = std::max(qx, 0.0f);
    const float ay = std::max(qy, 0.0f);
    const float az = std::max(qz, 0.0f);
    const float ground = std::sqrt(ax * ax + ay * ay + az * az) +
        std::min(std::max(qx, std::max(qy, qz)), 0.0f) - m_params.groundRounding;
    return std::min(slab, ground);
}

float CliffField::evalBase(const glm::vec3& p) const {
    float d2 = 0.0f;
    return evalBase(p, d2);
}

float CliffField::eval(const glm::vec3& p) const {
    float d2 = 0.0f;
    float f = evalBase(p, d2);
    // Grooves hug the wall band: the mask fades with |d2| (and near the top
    // rim when rim fade is on), the K-term limits the carve depth.
    f = applyGrooves(f, p, grooveMask(d2, p.y));
    // Gentle fbm displacement of the whole shape for a natural rock feel.
    // Band-limited (fbmOctaves): finer octaves alias on the sampling grid and
    // produce saddle faces -> non-manifold surface nets.
    const float disp = (fbm(p * m_params.fbmFrequency, m_params.fbmOctaves) - 0.5f) *
        2.0f * m_params.fbmAmplitude;
    return f + disp;
}

float CliffField::grooveDepth(const glm::vec3& p) const {
    // Carve depth: how far the grooved field pushed the surface inward from
    // the base shape. 0 on the untouched surface, > 0 towards groove floors.
    float d2 = 0.0f;
    const float base = evalBase(p, d2);
    const float mask = grooveMask(d2, p.y);
    if (mask <= 0.0f) {
        return 0.0f;
    }
    const float f = applyGrooves(base, p, mask);
    return std::clamp(f - base, 0.0f, 0.2f);
}

void CliffField::sample(std::vector<float>& outValues) const {
    const int px = m_nx + 1;
    const int py = m_ny + 1;
    const int pz = m_nz + 1;
    outValues.resize(static_cast<size_t>(px) * py * pz);
    const float cell = m_params.cellSize;
    for (int iy = 0; iy < py; ++iy) {
        const float y = m_origin.y + iy * cell;
        for (int iz = 0; iz < pz; ++iz) {
            const float z = m_origin.z + iz * cell;
            float* row = &outValues[(static_cast<size_t>(iy) * pz + iz) * px];
            for (int ix = 0; ix < px; ++ix) {
                const float x = m_origin.x + ix * cell;
                float d2 = 0.0f;
                const float base = evalBase(glm::vec3(x, y, z), d2);
                // Fast path: this far from the surface grooves and the fbm
                // displacement cannot flip the sign, so the cheap value is safe.
                if (base > 0.25f || (base < -0.25f && std::fabs(d2) >= m_params.grooveMaskWidth)) {
                    row[ix] = base;
                } else {
                    row[ix] = eval(glm::vec3(x, y, z));
                }
            }
        }
    }
}

} // namespace cliff
