#include "pch.h"

#include "MaskField.h"

#include <algorithm>
#include <cmath>

namespace maskfield {

MaskField::MaskField(
    const MaskFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params) {
    const bool hasNodes = nodes != nullptr && nodesX > 1 && nodesY > 1;
    m_nodesX = hasNodes ? nodesX : 0;
    m_nodesZ = hasNodes ? nodesY : 0;
    m_nodes.assign(static_cast<size_t>(m_nodesX) * m_nodesZ, 0);
    if (hasNodes) {
        std::copy(
            nodes,
            nodes + static_cast<size_t>(m_nodesX) * m_nodesZ,
            m_nodes.begin());
    }

    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ, Y spans [-padY, height + padY]. The mask itself
    // keeps F > 0 outside the region (fill = 0 there), so the XZ pad is
    // just slack for the optional blur.
    const float cell = params.cellSize;
    const float pad = params.padding;
    const float padY = 2.0f * cell;
    const float regionX = static_cast<float>(m_nodesX - 1);
    const float regionZ = static_cast<float>(m_nodesZ - 1);
    m_nx = static_cast<int>(std::ceil((regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((params.height + 2.0f * padY) / cell));
    m_origin = glm::vec3(-pad, -padY, -pad);
}

float MaskField::fillAt(int nx, int nz) const {
    const bool on = (nx >= 0 && nx < m_nodesX && nz >= 0 && nz < m_nodesZ) &&
        m_nodes[static_cast<size_t>(nz) * m_nodesX + nx] != 0;
    return on ? 1.0f : 0.0f;
}

float MaskField::eval(const glm::vec3& p) const {
    // 2D footprint: bilinear node fill over the point's cell, mask = the
    // fill >= 0.5 iso (the same contour the Texture 2D layer renders).
    const int ix = static_cast<int>(std::floor(p.x));
    const int iz = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(ix);
    const float fz = p.z - static_cast<float>(iz);
    const float f00 = fillAt(ix, iz);
    const float f10 = fillAt(ix + 1, iz);
    const float f01 = fillAt(ix, iz + 1);
    const float f11 = fillAt(ix + 1, iz + 1);
    const float fill =
        f00 * (1.0f - fx) * (1.0f - fz) + f10 * fx * (1.0f - fz) +
        f01 * (1.0f - fx) * fz + f11 * fx * fz;
    const float d = 0.5f - fill; // negative inside the mask, zero on the contour
    // Slab 0..height: max of the two terms is the exact CSG intersection —
    // the wall stands on the contour, the top is flat at y = height.
    const float hy = 0.5f * m_params.height;
    const float qy = std::fabs(p.y - hy) - hy;
    return std::max(d, qy);
}

void MaskField::sample(std::vector<float>& outValues) const {
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

    // Optional 3-tap blur over the sampled field: rounds the top lip (the
    // same anti-aliasing blur the other fields use, here as a "soften" knob).
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

cliff::ScalarFieldView MaskField::view() const {
    cliff::ScalarFieldView v;
    v.origin = m_origin;
    v.cellSize = m_params.cellSize;
    v.nx = m_nx;
    v.ny = m_ny;
    v.nz = m_nz;
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    return v;
}

} // namespace maskfield
