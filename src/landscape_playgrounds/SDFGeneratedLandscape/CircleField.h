// Circle field: the second teaching sample in the node-driven scalar-field
// series (a step up from BoxField's one box per painted cell). Every painted
// NODE grows a vertical cylinder ("a circle around the vertex"), and a cell
// whose four corner nodes are ALL on becomes a fully raised parallelepiped:
//
//   eval(p) = min over shapes of:
//     - capped cylinder per on-node:  max(sqrt(dx*dx + dz*dz) - r, |y-hy|-hy)
//     - box per full cell:            max(|dx|-0.5, |y-hy|-hy, |dz|-0.5)
//
//   min() = CSG union. Neighbouring on-nodes sit 1 cell apart, so with the
//   default radius 0.55 their circles overlap slightly and merge into one
//   blob; a full cell's box fills the middle the four corner circles leave
//   open (the cell centre sits ~0.707 from its corners, past the radius) —
//   a 2x2 node block reads as one solid parallelepiped with round bulges.
//
// Same contract as boxfield::BoxField / tech::TechField: node grid + params
// -> sampled field, no Qt/GPU. Border nodes must be 0.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <highground_core/surface_nets.h>

namespace circlefield {

struct CircleFieldParams {
    float cellSize = 0.06f;   // field voxel size in world units
    float padding = 0.75f;    // field margin outside the region; F > 0 on the
                              // grid border (auto-raised to cover nodeRadius)
    float height = 0.35f;     // shape height in world units (bottom sits at y = 0)
    float nodeRadius = 0.55f; // cylinder radius around each on-node, in cells
                              // (> 0.5 merges neighbours, > ~0.71 fills cells)
    int blurPasses = 0;       // sampled-field blur passes: 0 = crisp shapes,
                              // > 0 rounds the edges
};

class CircleField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // copied, the caller keeps ownership. Border nodes must be 0 (same
    // contract as cliff::CliffField / tech::TechField / boxfield::BoxField).
    CircleField(
        const CircleFieldParams& params,
        const std::uint8_t* nodes,
        int nodesX,
        int nodesY);

    // Union of all cylinders and full-cell boxes: negative inside any shape.
    // Far from shapes returns a small positive constant — no zero crossings
    // live out there, so the exact value does not matter.
    float eval(const glm::vec3& p) const;

    // Samples eval() on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix. Then applies blurPasses
    // 3-tap blur passes (same trick as boxfield::BoxField / tech::TechField).
    void sample(std::vector<float>& outValues) const;

    // Generic field view for cliff::regularizeSigns/extractSurfaceNets
    // (no groove/rim channels — the teaching samples carry no shading attrs).
    cliff::ScalarFieldView view() const;

    const CircleFieldParams& params() const { return m_params; }
    const glm::vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }

private:
    bool nodeAt(int nx, int nz) const;   // on-node -> grows a cylinder
    bool cellFull(int cx, int cz) const; // all four corner nodes on -> box

    CircleFieldParams m_params;
    std::vector<std::uint8_t> m_nodes; // one flag per map node: cylinder
    std::vector<std::uint8_t> m_cells; // one flag per map cell: fully raised box
    int m_nodesX = 0;
    int m_nodesZ = 0;
    int m_cellsX = 0;
    int m_cellsZ = 0;
    int m_lookup = 1; // cell Chebyshev radius eval() searches for shapes
    glm::vec3 m_origin{0.0f};
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
};

} // namespace circlefield
