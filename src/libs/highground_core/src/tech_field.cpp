#include "pch.h"

#include "highground_core/tech_field.h"

namespace tech {

namespace {

// Formula-based smoothstep; reversed edges (edge0 > edge1) are intentional
// (the crease band fades out with distance), so no glm::smoothstep/GLES UB here.
float smoothstepf(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Barycentric weights of (px,pz) in the triangle (a,b,c), all in XZ.
void barycentric(
    float px, float pz,
    float ax, float az, float bx, float bz, float cx, float cz,
    float& wa, float& wb, float& wc) {
    const float d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
    wa = ((bz - cz) * (px - cx) + (cx - bx) * (pz - cz)) / d;
    wb = ((cz - az) * (px - cx) + (ax - cx) * (pz - cz)) / d;
    wc = 1.0f - wa - wb;
}

} // namespace

TechField::TechField(
    const TechFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params), m_nodesX(nodesX), m_nodesY(nodesY) {
    const bool hasNodes = nodes != nullptr && nodesX > 0 && nodesY > 0;
    if (!hasNodes) {
        m_nodesX = 0;
        m_nodesY = 0;
    }
    const size_t count = static_cast<size_t>(m_nodesX) * m_nodesY;
    m_values.assign(count, 0.0f);

    // Node values: land +1; with outlineDepth > 0 the 8-neighborhood of the
    // land (minus the land itself) becomes an outline ring at -outlineDepth —
    // the "yellow around green" derivation, so the border ramps continue
    // below the water plane. Empty stays 0 (open water).
    const float depth = std::max(m_params.outlineDepth, 0.0f);
    if (hasNodes) {
        std::vector<std::uint8_t> ring(count, 0);
        for (int z = 0; z < m_nodesY; ++z) {
            for (int x = 0; x < m_nodesX; ++x) {
                if (nodes[static_cast<size_t>(z) * m_nodesX + x] == 0) continue;
                m_values[static_cast<size_t>(z) * m_nodesX + x] = 1.0f;
                if (depth > 0.0f) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int qx = x + dx;
                            const int qz = z + dz;
                            if (qx >= 0 && qx < m_nodesX && qz >= 0 && qz < m_nodesY) {
                                ring[static_cast<size_t>(qz) * m_nodesX + qx] = 1;
                            }
                        }
                    }
                }
            }
        }
        if (depth > 0.0f) {
            for (int z = 0; z < m_nodesY; ++z) {
                for (int x = 0; x < m_nodesX; ++x) {
                    const size_t i = static_cast<size_t>(z) * m_nodesX + x;
                    if (ring[i] != 0 && nodes[i] == 0) {
                        m_values[i] = -depth;
                    }
                }
            }
        }
    }

    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ, Y spans [-(under + base + pad), levelHeight + pad]
    // — a couple of voxels of slack keeps F > 0 on the grid border.
    m_regionX = static_cast<float>(m_nodesX - 1);
    m_regionZ = static_cast<float>(m_nodesY - 1);
    const float pad = params.padding;
    const float cell = params.cellSize;
    const float padY = 2.0f * cell;
    const float under = depth * params.levelHeight;
    const float yMin = -(under + baseDepth() + padY);
    const float yMax = params.levelHeight + padY;
    m_nx = static_cast<int>(std::ceil((m_regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((m_regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((yMax - yMin) / cell));
    m_origin = glm::vec3(-pad, yMin, -pad);
}

float TechField::nodeAt(int x, int z) const {
    return (x >= 0 && x < m_nodesX && z >= 0 && z < m_nodesY)
        ? m_values[static_cast<size_t>(z) * m_nodesX + x]
        : 0.0f;
}

bool TechField::cellNonEmpty(int x, int z) const {
    return nodeAt(x, z) > 0.0f || nodeAt(x + 1, z) > 0.0f ||
        nodeAt(x, z + 1) > 0.0f || nodeAt(x + 1, z + 1) > 0.0f;
}

// Center height per cell class, blending the two tilesets by `style`; the
// table generalizes the binary {0,1} one to signed corner values:
// Full = maxV, Line = mid, Corner: ridge mid (peak) -> valley minV (flat floor),
// Lack/Opposite: ridge mid (fold/saddle) -> valley maxV (flat plateau/crest),
// where mid = (maxV + minV) / 2 sits exactly on the ramp plane.
float TechField::centerHeight(float maxV, float minV, int nMax, bool opposite) const {
    const float s = std::clamp(m_params.style, 0.0f, 1.0f);
    const float mid = 0.5f * (maxV + minV);
    if (nMax >= 4) return maxV;
    if (nMax == 3) return lerpf(mid, maxV, s);
    if (nMax == 1) return lerpf(mid, minV, s);
    return opposite ? lerpf(mid, maxV, s) : mid;
}

float TechField::heightAt(float x, float z) const {
    return heightAt(x, z, nullptr);
}

float TechField::heightAt(float x, float z, float* outGradLen) const {
    if (outGradLen != nullptr) {
        *outGradLen = 0.0f;
    }
    const float gx = std::clamp(x, 0.0f, m_regionX - 1e-4f);
    const float gz = std::clamp(z, 0.0f, m_regionZ - 1e-4f);
    const int ix = static_cast<int>(gx);
    const int iz = static_cast<int>(gz);
    const float u = gx - static_cast<float>(ix);
    const float v = gz - static_cast<float>(iz);
    const float v00 = nodeAt(ix, iz);
    const float v10 = nodeAt(ix + 1, iz);
    const float v01 = nodeAt(ix, iz + 1);
    const float v11 = nodeAt(ix + 1, iz + 1);
    if (v00 == 0.0f && v10 == 0.0f && v01 == 0.0f && v11 == 0.0f) {
        return 0.0f;
    }
    const float maxV = std::max(std::max(v00, v10), std::max(v01, v11));
    const float minV = std::min(std::min(v00, v10), std::min(v01, v11));
    const int nMax =
        (v00 == maxV ? 1 : 0) + (v10 == maxV ? 1 : 0) +
        (v01 == maxV ? 1 : 0) + (v11 == maxV ? 1 : 0);
    const bool opposite =
        (nMax == 2) && ((v00 == maxV && v11 == maxV) || (v10 == maxV && v01 == maxV));
    const float center = centerHeight(maxV, minV, nMax, opposite);

    // Fan around the cell center (0.5,0.5): the sector diagonals u=v and
    // u+v=1 pick one of the four (corner, corner, center) triangles; weights
    // on shared fan edges coincide, so the surface is continuous across
    // sectors and across cells (on a cell border the center weight is 0 and
    // the interpolation reduces to the shared-node linear ramp). The sector
    // gradient is exact (the fan is affine per sector); the `side` clip uses
    // it to keep ramp zero-crossings inside the solid.
    float w0 = 0.0f;
    float w1 = 0.0f;
    float wc = 0.0f;
    float h = 0.0f;
    float dhu = 0.0f; // dh/du in node units per cell
    float dhv = 0.0f; // dh/dv
    if (u + v <= 1.0f) {
        if (u >= v) { // bottom: (0,0) - (1,0) - C
            barycentric(u, v, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * v00 + w1 * v10 + wc * center;
            dhu = -v00 + v10;
            dhv = -v00 - v10 + 2.0f * center;
        } else { // left: (0,1) - (0,0) - C
            barycentric(u, v, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * v01 + w1 * v00 + wc * center;
            dhu = -v00 - v01 + 2.0f * center;
            dhv = -v00 + v01;
        }
    } else {
        if (u >= v) { // right: (1,0) - (1,1) - C
            barycentric(u, v, 1.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * v10 + w1 * v11 + wc * center;
            dhu = v10 + v11 - 2.0f * center;
            dhv = -v10 + v11;
        } else { // top: (0,1) - (1,1) - C
            barycentric(u, v, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * v01 + w1 * v11 + wc * center;
            dhu = -v01 + v11;
            dhv = v01 + v11 - 2.0f * center;
        }
    }

    const float s = std::clamp(m_params.soften, 0.0f, 1.0f);
    if (s > 0.0f) {
        // Sign-aware smoothstep: keeps 0 / +-0.5 / +-1 fixed and the field
        // continuous (the underwater half mirrors the land half), rounds the
        // ramp shoulders and the center domes; the gradient follows the chain
        // rule so the `side` clip still spares softened ramp crossings.
        const float a = std::clamp(std::fabs(h), 0.0f, 1.0f);
        const float soft = a * a * (3.0f - 2.0f * a);
        const float shaped = (h >= 0.0f) ? soft : -soft;
        const float ds = 6.0f * a * (1.0f - a); // d(shaped)/dh
        h = lerpf(h, shaped, s);
        const float g = lerpf(1.0f, ds, s);
        dhu *= g;
        dhv *= g;
    }
    if (outGradLen != nullptr) {
        *outGradLen = std::sqrt(dhu * dhu + dhv * dhv);
    }
    return h * m_params.levelHeight;
}

float TechField::baseDepth() const {
    // At least ~2 voxels of solid thickness everywhere (plus one per blur
    // pass against erosion) — see the class comment above.
    return std::max(
        m_params.groundDepth,
        (2.2f + static_cast<float>(m_params.blurPasses)) * m_params.cellSize);
}

float TechField::eval(const glm::vec3& p) const {
    float gradLen = 0.0f;
    const float h = heightAt(p.x, p.z, &gradLen);
    // Solid = { min(h, 0) - baseDepth <= y <= h(x,z) }, clipped laterally to
    // the formation: the `side` term is positive only in the open water
    // (h ~ 0 AND flat), so empty cells stay outside while ramp zero-crossings
    // (the waterline between the land and the underwater foot) keep their
    // slope-based pass and no crack opens between the two.
    const float level = std::max(m_params.levelHeight, 1e-3f);
    const float epsH = 0.03f; // node-unit epsilon (3% of a level)
    const float hu = h / level;
    const float side = (epsH - std::fabs(hu) - 4.0f * epsH * gradLen) * level;
    const float top = p.y - h;
    const float bottom = std::min(h, 0.0f) - baseDepth() - p.y;
    // Region rectangle shrunk by half a voxel: the formation's toes end at
    // the zero node ring inside the region anyway, and this keeps the field
    // strictly positive on the grid border (the surface-nets contract).
    const float m = 0.5f * m_params.cellSize;
    const float qx = std::fabs(p.x - 0.5f * m_regionX) - (0.5f * m_regionX - m);
    const float qz = std::fabs(p.z - 0.5f * m_regionZ) - (0.5f * m_regionZ - m);
    const float footprint = std::max(qx, qz);
    return std::max(std::max(top, bottom), std::max(side, footprint));
}

float TechField::grooveDepth(const glm::vec3& p) const {
    if (m_params.creaseWidth <= 0.0f) {
        return 0.0f;
    }
    const float gx = std::clamp(p.x, 0.0f, m_regionX - 1e-4f);
    const float gz = std::clamp(p.z, 0.0f, m_regionZ - 1e-4f);
    const int ix = static_cast<int>(gx);
    const int iz = static_cast<int>(gz);
    const float u = gx - static_cast<float>(ix);
    const float v = gz - static_cast<float>(iz);
    // Distance to the nearest cell border; the outline is drawn when the cell
    // on either side of that border has land on it (the tileset drew the
    // contour along the edges of every raised tile).
    const float du = std::min(u, 1.0f - u);
    const float dv = std::min(v, 1.0f - v);
    const float d = std::min(du, dv);
    bool active = cellNonEmpty(ix, iz);
    if (!active) {
        if (du <= dv) {
            active = cellNonEmpty(u < 0.5f ? ix - 1 : ix + 1, iz);
        } else {
            active = cellNonEmpty(ix, v < 0.5f ? iz - 1 : iz + 1);
        }
    }
    if (!active) {
        return 0.0f;
    }
    return smoothstepf(m_params.creaseWidth, 0.0f, d);
}

void TechField::sample(std::vector<float>& outValues) const {
    const float cell = m_params.cellSize;
    const int px = m_nx + 1;
    const int py = m_ny + 1;
    const int pz = m_nz + 1;
    outValues.resize(static_cast<size_t>(px) * py * pz);
    const glm::vec3 origin = m_origin;
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

    // Gentle 3-tap blur over the sampled field: piecewise-linear ramps alias
    // into terracing on naive surface nets (same trick as stone_mesh).
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

cliff::ScalarFieldView TechField::view() const {
    cliff::ScalarFieldView v;
    v.origin = m_origin;
    v.cellSize = m_params.cellSize;
    v.nx = m_nx;
    v.ny = m_ny;
    v.nz = m_nz;
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    if (m_params.creaseWidth > 0.0f) {
        v.grooveDepth = [this](const glm::vec3& p) { return grooveDepth(p); };
    }
    return v;
}

} // namespace tech
