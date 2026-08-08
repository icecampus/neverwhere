// Box field: the simplest possible node-driven scalar field for the
// surface-nets pipeline — a playground teaching sample. One axis-aligned box
// ("плиточка") per painted map cell:
//
//   eval(p) = min over boxes of max(|dx| - hx, |dy| - hy, |dz| - hz)
//             — the box SDF: negative inside, zero on the faces; boxes are
//             combined with min() (CSG union), so neighbouring boxes whose
//             faces touch (fill = 1) merge into one solid, while fill < 1
//             leaves real gaps — every cell keeps its own geometry.
//
// The shape needs no interpolation at all: the box SDF is exact and
// piecewise-linear, and with blurPasses = 0 the extracted mesh reproduces the
// boxes crisply. blurPasses > 0 rounds the edges (the same anti-terracing
// blur the other fields use against aliasing, here as a "soften" knob).
//
// Same contract as tech::TechField: node grid + params -> sampled field,
// no Qt/GPU. A cell gets a box when ANY of its four corner nodes is on — the
// cellNonEmpty rule TechField uses to decide emptiness.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <highground_core/surface_nets.h>

namespace boxfield {

struct BoxFieldParams {
    float cellSize = 0.06f;    // field voxel size in world units
    float padding = 0.5f;      // field margin outside the region; F > 0 on the
                               // grid border (must cover (fill - 1) / 2)
    float boxHeight = 0.35f;   // box height in world units (bottom sits at y = 0)
    float fill = 1.0f;         // fraction of the cell the box spans: 1 = the box
                               // fills its cell exactly and face-sharing
                               // neighbours merge; < 1 = gaps between cells
    int blurPasses = 0;        // sampled-field blur passes: 0 = crisp boxes,
                               // > 0 rounds the edges
};

class BoxField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // copied, the caller keeps ownership. Border nodes must be 0 (same
    // contract as cliff::CliffField / tech::TechField).
    BoxField(const BoxFieldParams& params, const std::uint8_t* nodes, int nodesX, int nodesY);

    // Union of all boxes: negative inside any box. Far from boxes returns a
    // small positive constant — no zero crossings live out there, so the exact
    // value does not matter.
    float eval(const glm::vec3& p) const;

    // Samples eval() on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix. Then applies blurPasses
    // 3-tap blur passes (same trick as tech::TechField / stone_gen::StoneField).
    void sample(std::vector<float>& outValues) const;

    // Generic field view for cliff::regularizeSigns/extractSurfaceNets
    // (no groove/rim channels — the minimal sample carries no shading attrs).
    cliff::ScalarFieldView view() const;

    const BoxFieldParams& params() const { return m_params; }
    const glm::vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }

private:
    // Cell occupancy: a cell holds a box when any of its four corner nodes
    // is on (TechField's cellNonEmpty rule). Out-of-range cells are empty.
    bool boxAt(int cx, int cz) const;

    BoxFieldParams m_params;
    std::vector<std::uint8_t> m_cells; // one flag per map cell: holds a box
    int m_cellsX = 0;
    int m_cellsZ = 0;
    int m_lookup = 1; // cell Chebyshev radius eval() searches for boxes
    glm::vec3 m_origin{0.0f};
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
};

} // namespace boxfield
