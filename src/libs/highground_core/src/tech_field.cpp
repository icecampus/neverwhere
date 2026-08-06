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
    if (nodes != nullptr && nodesX > 0 && nodesY > 0) {
        m_nodes.assign(nodes, nodes + static_cast<size_t>(nodesX) * nodesY);
    } else {
        m_nodesX = 0;
        m_nodesY = 0;
    }
    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ, Y spans [-(groundDepth + pad), levelHeight + pad] —
    // a couple of voxels of slack is enough to keep F > 0 on the grid border.
    m_regionX = static_cast<float>(nodesX - 1);
    m_regionZ = static_cast<float>(nodesY - 1);
    const float pad = params.padding;
    const float cell = params.cellSize;
    const float padY = 2.0f * cell;
    const float yMin = -(params.groundDepth + padY);
    const float yMax = params.levelHeight + padY;
    m_nx = static_cast<int>(std::ceil((m_regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((m_regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((yMax - yMin) / cell));
    m_origin = glm::vec3(-pad, yMin, -pad);
}

float TechField::nodeAt(int x, int z) const {
    return (x >= 0 && x < m_nodesX && z >= 0 && z < m_nodesY)
        ? static_cast<float>(m_nodes[static_cast<size_t>(z) * m_nodesX + x])
        : 0.0f;
}

bool TechField::cellNonEmpty(int x, int z) const {
    return nodeAt(x, z) + nodeAt(x + 1, z) + nodeAt(x, z + 1) + nodeAt(x + 1, z + 1) > 0.0f;
}

// Center height per cell class, blending the two tilesets by `style`:
// Full = 1 (plateau), Line = 0.5 (on the ramp plane either way),
// Corner: ridge 0.5 (peak) -> valley 0 (flat floor with a ramp to the corner),
// Lack/Opposite: ridge 0.5 (fold/saddle) -> valley 1 (flat plateau / ridge crest).
float TechField::centerHeight(int nodeCount, bool opposite) const {
    const float s = std::clamp(m_params.style, 0.0f, 1.0f);
    if (nodeCount >= 4) return 1.0f;
    if (nodeCount == 3) return lerpf(0.5f, 1.0f, s);
    if (nodeCount == 1) return lerpf(0.5f, 0.0f, s);
    return opposite ? lerpf(0.5f, 1.0f, s) : 0.5f;
}

float TechField::heightAt(float x, float z) const {
    const float gx = std::clamp(x, 0.0f, m_regionX - 1e-4f);
    const float gz = std::clamp(z, 0.0f, m_regionZ - 1e-4f);
    const int ix = static_cast<int>(gx);
    const int iz = static_cast<int>(gz);
    const float u = gx - static_cast<float>(ix);
    const float v = gz - static_cast<float>(iz);
    const float h00 = nodeAt(ix, iz);
    const float h10 = nodeAt(ix + 1, iz);
    const float h01 = nodeAt(ix, iz + 1);
    const float h11 = nodeAt(ix + 1, iz + 1);
    const int count = static_cast<int>(h00 + h10 + h01 + h11);
    if (count == 0) {
        return 0.0f;
    }
    const bool opposite =
        (count == 2) && ((h00 > 0.0f && h11 > 0.0f) || (h10 > 0.0f && h01 > 0.0f));
    const float center = centerHeight(count, opposite);

    // Fan around the cell center (0.5,0.5): the sector diagonals u=v and
    // u+v=1 pick one of the four (corner, corner, center) triangles; weights
    // on shared fan edges coincide, so the surface is continuous across
    // sectors and across cells (on a cell border the center weight is 0 and
    // the interpolation reduces to the shared-node linear ramp).
    float w0 = 0.0f;
    float w1 = 0.0f;
    float wc = 0.0f;
    float h = 0.0f;
    if (u + v <= 1.0f) {
        if (u >= v) { // bottom: (0,0) - (1,0) - C
            barycentric(u, v, 0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * h00 + w1 * h10 + wc * center;
        } else { // left: (0,1) - (0,0) - C
            barycentric(u, v, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * h01 + w1 * h00 + wc * center;
        }
    } else {
        if (u >= v) { // right: (1,0) - (1,1) - C
            barycentric(u, v, 1.0f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * h10 + w1 * h11 + wc * center;
        } else { // top: (0,1) - (1,1) - C
            barycentric(u, v, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, w0, w1, wc);
            h = w0 * h01 + w1 * h11 + wc * center;
        }
    }

    const float s = std::clamp(m_params.soften, 0.0f, 1.0f);
    if (s > 0.0f) {
        // Smoothstep on the final height: keeps 0 / 0.5 / 1 fixed and the
        // field continuous, rounds the ramp shoulders and the center domes.
        h = lerpf(h, h * h * (3.0f - 2.0f * h), s);
    }
    return h * m_params.levelHeight;
}

float TechField::eval(const glm::vec3& p) const {
    const float h = heightAt(p.x, p.z);
    // Solid = { -groundDepth <= y <= max(h(x,z), eps) }: the landforms stand
    // on their own, no ground sheet under empty cells. The h = eps contour
    // gives each outer ramp a clean vertical foot (mostly below y = 0), and
    // eps keeps the field strictly positive wherever nothing is raised
    // (an all-zero grid yields no surface at all).
    const float eps = 0.03f * m_params.levelHeight;
    const float top = std::max(p.y, eps) - h;
    const float bottom = -(p.y + m_params.groundDepth);
    return std::max(top, bottom);
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
    // on either side of that border is non-empty (the tileset drew the
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
