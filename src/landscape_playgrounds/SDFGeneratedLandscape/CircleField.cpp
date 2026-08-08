#include "pch.h"

#include "CircleField.h"

#include <algorithm>
#include <cmath>

namespace circlefield {

CircleField::CircleField(
    const CircleFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params) {
    const bool hasNodes = nodes != nullptr && nodesX > 1 && nodesY > 1;
    m_nodesX = hasNodes ? nodesX : 0;
    m_nodesZ = hasNodes ? nodesY : 0;
    m_cellsX = hasNodes ? nodesX - 1 : 0;
    m_cellsZ = hasNodes ? nodesY - 1 : 0;
    m_nodes.assign(static_cast<size_t>(m_nodesX) * m_nodesZ, 0);
    m_cells.assign(static_cast<size_t>(m_cellsX) * m_cellsZ, 0);

    if (hasNodes) {
        std::copy(
            nodes,
            nodes + static_cast<size_t>(m_nodesX) * m_nodesZ,
            m_nodes.begin());
        auto nodeOn = [this](int x, int z) {
            return m_nodes[static_cast<size_t>(z) * m_nodesX + x] != 0;
        };
        // A cell is fully raised when ALL its four corner nodes are on —
        // the opposite end of BoxField's any-corner occupancy rule.
        for (int z = 0; z < m_cellsZ; ++z) {
            for (int x = 0; x < m_cellsX; ++x) {
                const bool full = nodeOn(x, z) && nodeOn(x + 1, z) &&
                    nodeOn(x, z + 1) && nodeOn(x + 1, z + 1);
                m_cells[static_cast<size_t>(z) * m_cellsX + x] = full ? 1 : 0;
            }
        }
    }

    // How far (in cells, Chebyshev) a shape can influence a point: cylinders
    // reach nodeRadius past their node; full-cell boxes stay inside their
    // cell (radius >= 1 also covers the face-on-the-border interpolation,
    // same as BoxField).
    m_lookup = std::max(1, static_cast<int>(std::ceil(m_params.nodeRadius)));

    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ, Y spans [-padY, height + padY] — a couple of voxels
    // of slack keeps F > 0 on the grid border. Cylinders on the innermost
    // nodes (border nodes are 0 by contract) reach nodeRadius - 1 past the
    // region edge, so the pad auto-covers that and a radius slider cannot
    // push F <= 0 onto the border.
    const float cell = params.cellSize;
    const float pad = std::max(params.padding, params.nodeRadius - 1.0f + 2.0f * cell);
    const float padY = 2.0f * cell;
    const float regionX = static_cast<float>(m_cellsX);
    const float regionZ = static_cast<float>(m_cellsZ);
    m_nx = static_cast<int>(std::ceil((regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((params.height + 2.0f * padY) / cell));
    m_origin = glm::vec3(-pad, -padY, -pad);
}

bool CircleField::nodeAt(int nx, int nz) const {
    return (nx >= 0 && nx < m_nodesX && nz >= 0 && nz < m_nodesZ) &&
        m_nodes[static_cast<size_t>(nz) * m_nodesX + nx] != 0;
}

bool CircleField::cellFull(int cx, int cz) const {
    return (cx >= 0 && cx < m_cellsX && cz >= 0 && cz < m_cellsZ) &&
        m_cells[static_cast<size_t>(cz) * m_cellsX + cx] != 0;
}

float CircleField::eval(const glm::vec3& p) const {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iz = static_cast<int>(std::floor(p.z));
    const float hy = 0.5f * m_params.height;
    const float qy = std::fabs(p.y - hy) - hy; // shared cap term: bottom sits at y = 0
    // Positive "far" value: with no shape inside the lookup radius the sign
    // is all that matters (no zero crossing lives out there).
    float best = 2.0f * m_params.cellSize;
    for (int dz = -m_lookup; dz <= m_lookup; ++dz) {
        for (int dx = -m_lookup; dx <= m_lookup; ++dx) {
            const int nx = ix + dx;
            const int nz = iz + dz;
            if (nodeAt(nx, nz)) {
                // Capped cylinder around the node: the max of the radial and
                // the axial term is the exact CSG intersection of an infinite
                // cylinder with the 0..height slab (zero set = the surface).
                const float ddx = p.x - static_cast<float>(nx);
                const float ddz = p.z - static_cast<float>(nz);
                const float radial = std::sqrt(ddx * ddx + ddz * ddz) - m_params.nodeRadius;
                best = std::min(best, std::max(radial, qy));
            }
            if (cellFull(nx, nz)) { // the same offsets double as cell indices
                // Fully painted cell -> parallelepiped over
                // [nx, nx+1] x [0, height] x [nz, nz+1]: BoxField's box SDF
                // with fill = 1, so adjacent full cells merge seamlessly.
                const float qx = std::fabs(p.x - (static_cast<float>(nx) + 0.5f)) - 0.5f;
                const float qz = std::fabs(p.z - (static_cast<float>(nz) + 0.5f)) - 0.5f;
                best = std::min(best, std::max(qx, std::max(qy, qz)));
            }
        }
    }
    return best;
}

void CircleField::sample(std::vector<float>& outValues) const {
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

    // Optional 3-tap blur over the sampled field: rounds the shape edges (the
    // same anti-terracing blur the other fields use against aliasing).
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

cliff::ScalarFieldView CircleField::view() const {
    cliff::ScalarFieldView v;
    v.origin = m_origin;
    v.cellSize = m_params.cellSize;
    v.nx = m_nx;
    v.ny = m_ny;
    v.nz = m_nz;
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    return v;
}

} // namespace circlefield
