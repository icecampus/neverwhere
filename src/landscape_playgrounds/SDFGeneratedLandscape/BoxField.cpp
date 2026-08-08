#include "pch.h"

#include "BoxField.h"

#include <algorithm>
#include <cmath>

namespace boxfield {

BoxField::BoxField(
    const BoxFieldParams& params,
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY)
    : m_params(params) {
    const bool hasNodes = nodes != nullptr && nodesX > 1 && nodesY > 1;
    m_cellsX = hasNodes ? nodesX - 1 : 0;
    m_cellsZ = hasNodes ? nodesY - 1 : 0;
    m_cells.assign(static_cast<size_t>(m_cellsX) * m_cellsZ, 0);

    // Cell occupancy from the corner nodes (TechField's cellNonEmpty rule):
    // a painted node lights up to four cells around it.
    if (hasNodes) {
        auto nodeAt = [nodes, nodesX](int x, int z) -> bool {
            return nodes[static_cast<size_t>(z) * nodesX + x] != 0;
        };
        for (int z = 0; z < m_cellsZ; ++z) {
            for (int x = 0; x < m_cellsX; ++x) {
                const bool on = nodeAt(x, z) || nodeAt(x + 1, z) ||
                    nodeAt(x, z + 1) || nodeAt(x + 1, z + 1);
                m_cells[static_cast<size_t>(z) * m_cellsX + x] = on ? 1 : 0;
            }
        }
    }

    // How far (in cells, Chebyshev) a box can reach past its own cell:
    // half-extent fill/2, so fill <= 1 never leaves the cell and the 3x3
    // neighbourhood is enough; wider fills need a wider search.
    m_lookup = std::max(1, static_cast<int>(std::ceil(0.5f * m_params.fill + 0.5f)));

    // The region spans (nodesX-1) x (nodesY-1) map cells; the field is
    // rectangular in XZ, Y spans [-pad, boxHeight + pad] — a couple of voxels
    // of slack keeps F > 0 on the grid border.
    const float pad = params.padding;
    const float cell = params.cellSize;
    const float padY = 2.0f * cell;
    const float regionX = static_cast<float>(m_cellsX);
    const float regionZ = static_cast<float>(m_cellsZ);
    m_nx = static_cast<int>(std::ceil((regionX + 2.0f * pad) / cell));
    m_nz = static_cast<int>(std::ceil((regionZ + 2.0f * pad) / cell));
    m_ny = static_cast<int>(std::ceil((params.boxHeight + 2.0f * padY) / cell));
    m_origin = glm::vec3(-pad, -padY, -pad);
}

bool BoxField::boxAt(int cx, int cz) const {
    return (cx >= 0 && cx < m_cellsX && cz >= 0 && cz < m_cellsZ) &&
        m_cells[static_cast<size_t>(cz) * m_cellsX + cx] != 0;
}

float BoxField::eval(const glm::vec3& p) const {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iz = static_cast<int>(std::floor(p.z));
    const float half = 0.5f * m_params.fill;
    const float hy = 0.5f * m_params.boxHeight;
    // Positive "far" value: with no box inside the lookup radius the sign is
    // all that matters (no zero crossing lives out there).
    float best = 2.0f * m_params.cellSize;
    for (int dz = -m_lookup; dz <= m_lookup; ++dz) {
        for (int dx = -m_lookup; dx <= m_lookup; ++dx) {
            const int cx = ix + dx;
            const int cz = iz + dz;
            if (!boxAt(cx, cz)) {
                continue;
            }
            // Box in cell (cx, cz): center (cx+0.5, hy, cz+0.5), the bottom
            // face sits at y = 0. max of the three axis terms = box SDF
            // (negative inside); min across boxes = CSG union.
            const float qx = std::fabs(p.x - (static_cast<float>(cx) + 0.5f)) - half;
            const float qy = std::fabs(p.y - hy) - hy;
            const float qz = std::fabs(p.z - (static_cast<float>(cz) + 0.5f)) - half;
            best = std::min(best, std::max(qx, std::max(qy, qz)));
        }
    }
    return best;
}

void BoxField::sample(std::vector<float>& outValues) const {
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

    // Optional 3-tap blur over the sampled field: rounds the box edges (the
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

cliff::ScalarFieldView BoxField::view() const {
    cliff::ScalarFieldView v;
    v.origin = m_origin;
    v.cellSize = m_params.cellSize;
    v.nx = m_nx;
    v.ny = m_ny;
    v.nz = m_nz;
    v.eval = [this](const glm::vec3& p) { return eval(p); };
    return v;
}

} // namespace boxfield
