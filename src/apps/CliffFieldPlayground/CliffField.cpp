#include "pch.h"

#include "CliffField.h"

#include <algorithm>

namespace cliff {

namespace {

// 7x7 height nodes (1 = high), rows are z, columns are x.
// Main blob with a bay notch at (3,3), a NE lobe and a south peninsula with a
// widened tip, so several marching-squares cell types appear in the outline.
// All border rows/columns stay low: high border nodes would extend the blurred
// outline past the region and the solid would be clipped by the field grid.
const std::uint8_t kHeightNodes[7][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 1, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
};

// Omphalos Hashv4v3-style corner hash.
float hashCorner(float x, float y, float z) {
    const float s = std::sin(x * 37.0f + y * 39.0f + z * 41.0f) * 43758.54f;
    return s - std::floor(s);
}

float noisefv3(const cfm::Vec3& p) {
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
    return cfm::lerp(
        cfm::lerp(cfm::lerp(c000, c100, fx), cfm::lerp(c010, c110, fx), fy),
        cfm::lerp(cfm::lerp(c001, c101, fx), cfm::lerp(c011, c111, fx), fy),
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
    float mask = cfm::smoothstep(m_params.grooveMaskWidth, 0.0f, std::fabs(d2));
    if (m_params.grooveRimFade > 0.0f) {
        // Fade the carve out approaching the top rim so the edge stays solid.
        const float rim = m_params.plateauHeight - m_params.grooveRimFade;
        mask *= 1.0f - cfm::smoothstep(rim, m_params.plateauHeight, y);
    }
    return mask;
}

float CliffField::applyGrooves(float f, const cfm::Vec3& p, float mask) const {
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

float fbm(const cfm::Vec3& p, int octaves) {
    float f = 0.0f;
    float a = 1.0f;
    float norm = 0.0f;
    cfm::Vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        f += a * noisefv3(q);
        norm += a;
        a *= 0.5f;
        q = q * 2.0f;
    }
    return f / norm;
}

float fbm3(const cfm::Vec3& p) {
    float f = 0.0f;
    float a = 1.0f;
    cfm::Vec3 q = p;
    for (int i = 0; i < 5; ++i) {
        f += a * noisefv3(q);
        a *= 0.5f;
        q = q * 2.0f;
    }
    return f * (1.0f / 1.9375f);
}

float smoothMin(float a, float b, float r) {
    const float h = cfm::clamp(0.5f + 0.5f * (b - a) / r, 0.0f, 1.0f);
    return cfm::lerp(b, a, h) - r * h * (1.0f - h);
}

float smoothMax(float a, float b, float r) { return -smoothMin(-a, -b, r); }

CliffField::CliffField(const FieldParams& params) : m_params(params) {
    const float region = static_cast<float>(params.regionCells);
    const float pad = params.padding;
    const float cell = params.cellSize;
    m_nx = static_cast<int>(std::ceil((region + 2.0f * pad) / cell));
    m_nz = m_nx;
    m_ny = static_cast<int>(std::ceil((params.plateauHeight + 2.0f * pad) / cell));
    m_origin = cfm::Vec3(-pad, -pad, -pad);

    // 2D height grid at field XZ resolution, bilinear from the binary nodes.
    m_hW = m_nx + 1;
    m_hH = m_nz + 1;
    m_hGrid.resize(static_cast<size_t>(m_hW) * m_hH);
    for (int j = 0; j < m_hH; ++j) {
        const float z = m_origin.z + j * cell;
        for (int i = 0; i < m_hW; ++i) {
            const float x = m_origin.x + i * cell;
            const float gx = cfm::clamp(x, 0.0f, region - 1e-4f);
            const float gz = cfm::clamp(z, 0.0f, region - 1e-4f);
            const int x0 = static_cast<int>(gx);
            const int z0 = static_cast<int>(gz);
            const float fx = gx - static_cast<float>(x0);
            const float fz = gz - static_cast<float>(z0);
            const float n00 = static_cast<float>(kHeightNodes[z0][x0]);
            const float n10 = static_cast<float>(kHeightNodes[z0][x0 + 1]);
            const float n01 = static_cast<float>(kHeightNodes[z0 + 1][x0]);
            const float n11 = static_cast<float>(kHeightNodes[z0 + 1][x0 + 1]);
            m_hGrid[static_cast<size_t>(j) * m_hW + i] =
                cfm::lerp(cfm::lerp(n00, n10, fx), cfm::lerp(n01, n11, fx), fz);
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
    const float gx = cfm::clamp((x - m_origin.x) / cell, 0.0f, static_cast<float>(m_hW - 1) - 1e-4f);
    const float gz = cfm::clamp((z - m_origin.z) / cell, 0.0f, static_cast<float>(m_hH - 1) - 1e-4f);
    const int x0 = static_cast<int>(gx);
    const int z0 = static_cast<int>(gz);
    const float fx = gx - static_cast<float>(x0);
    const float fz = gz - static_cast<float>(z0);
    const float h00 = m_hGrid[static_cast<size_t>(z0) * m_hW + x0];
    const float h10 = m_hGrid[static_cast<size_t>(z0) * m_hW + x0 + 1];
    const float h01 = m_hGrid[static_cast<size_t>(z0 + 1) * m_hW + x0];
    const float h11 = m_hGrid[static_cast<size_t>(z0 + 1) * m_hW + x0 + 1];
    return cfm::lerp(cfm::lerp(h00, h10, fx), cfm::lerp(h01, h11, fx), fz);
}

float CliffField::evalBase(const cfm::Vec3& p, float& outD2) const {
    const float h = heightAt(p.x, p.z);
    const float d2 = (0.5f - h) * m_params.d2Scale;
    outD2 = d2;
    const float halfH = 0.5f * m_params.plateauHeight;
    // Plateau slab: inside the outline and 0 <= y <= H; edgeRadius expands the
    // slab so its convex rim (wall meets top) gets rounded.
    const float slab = std::max(d2, std::fabs(p.y - halfH) - halfH) - m_params.edgeRadius;
    // Ground chunk: rounded box with its top at y = 0, fully inside the padding
    // so the field stays positive on the grid border.
    const float region = static_cast<float>(m_params.regionCells);
    const float center = 0.5f * region;
    const float hx = center + m_params.groundMargin - m_params.groundRounding;
    const float hy = 0.5f * m_params.groundDepth - m_params.groundRounding;
    const float cy = -0.5f * m_params.groundDepth;
    const float qx = std::fabs(p.x - center) - hx;
    const float qy = std::fabs(p.y - cy) - hy;
    const float qz = std::fabs(p.z - center) - hx;
    const float ax = std::max(qx, 0.0f);
    const float ay = std::max(qy, 0.0f);
    const float az = std::max(qz, 0.0f);
    const float ground = std::sqrt(ax * ax + ay * ay + az * az) +
        std::min(std::max(qx, std::max(qy, qz)), 0.0f) - m_params.groundRounding;
    return std::min(slab, ground);
}

float CliffField::evalBase(const cfm::Vec3& p) const {
    float d2 = 0.0f;
    return evalBase(p, d2);
}

float CliffField::eval(const cfm::Vec3& p) const {
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

float CliffField::grooveDepth(const cfm::Vec3& p) const {
    // Carve depth: how far the grooved field pushed the surface inward from
    // the base shape. 0 on the untouched surface, > 0 towards groove floors.
    float d2 = 0.0f;
    const float base = evalBase(p, d2);
    const float mask = grooveMask(d2, p.y);
    if (mask <= 0.0f) {
        return 0.0f;
    }
    const float f = applyGrooves(base, p, mask);
    return cfm::clamp(f - base, 0.0f, 0.2f);
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
                const float base = evalBase(cfm::Vec3(x, y, z), d2);
                // Fast path: this far from the surface grooves and the fbm
                // displacement cannot flip the sign, so the cheap value is safe.
                if (base > 0.25f || (base < -0.25f && std::fabs(d2) >= m_params.grooveMaskWidth)) {
                    row[ix] = base;
                } else {
                    row[ix] = eval(cfm::Vec3(x, y, z));
                }
            }
        }
    }
}

} // namespace cliff
