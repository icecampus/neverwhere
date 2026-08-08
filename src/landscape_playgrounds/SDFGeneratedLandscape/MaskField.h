// Mask field: the third teaching sample in the node-driven scalar-field
// series. Takes the 2D silhouette the "Texture 2D" layer draws — per-node
// fill (1 at an on-node, 0 at an off-node) interpolated across each cell,
// mask = fill >= 0.5 — and extrudes that footprint into a thin slab:
//
//   eval(p) = max(0.5 - fill(x,z), |y - hy| - hy)
//
//   fill(x,z) is bilinear over the four corner nodes of the point's cell, so
//   the 0.5 iso line is the same contour the Texture 2D mask renders (edges
//   cross at their midpoints, half-painted cells become wedges, a lone node
//   becomes a small blob) — minus the shader-only fbm wobble and alpha fade,
//   which have no geometric counterpart: the contour becomes a crisp
//   vertical wall, the top a flat plate. max() is the exact CSG intersection
//   of the footprint prism with the 0..height slab.
//
// Unlike BoxField/CircleField there is no neighbourhood loop at all: the
// mask is a single continuous function (C0 across shared edges), every point
// belongs to exactly one cell, and outside the node grid fill = 0 keeps the
// field positive — eval() is O(1).
//
// Same contract as boxfield::BoxField / circlefield::CircleField: node grid
// + params -> sampled field, no Qt/GPU. Border nodes must be 0.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <highground_core/surface_nets.h>

namespace maskfield {

struct MaskFieldParams {
    float cellSize = 0.06f; // field voxel size in world units
    float padding = 0.5f;   // field margin outside the region (slack for blur;
                            // the field stays positive on the border anyway)
    float height = 0.2f;    // slab height in world units (bottom sits at y = 0)
    int blurPasses = 0;     // sampled-field blur passes: 0 = crisp walls,
                            // > 0 rounds the top lip
};

class MaskField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // copied, the caller keeps ownership. Border nodes must be 0 (same
    // contract as cliff::CliffField / boxfield::BoxField).
    MaskField(
        const MaskFieldParams& params,
        const std::uint8_t* nodes,
        int nodesX,
        int nodesY);

    // Footprint prism intersected with the 0..height slab: negative inside
    // the mask below the top, positive outside. Outside the node grid the
    // fill is 0, so the field is a constant +0.5 out there.
    float eval(const glm::vec3& p) const;

    // Samples eval() on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix. Then applies blurPasses
    // 3-tap blur passes (same trick as the other teaching fields).
    void sample(std::vector<float>& outValues) const;

    // Generic field view for cliff::regularizeSigns/extractSurfaceNets
    // (no groove/rim channels — the teaching samples carry no shading attrs).
    cliff::ScalarFieldView view() const;

    const MaskFieldParams& params() const { return m_params; }
    const glm::vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }

private:
    // Node fill at (nx, nz); out-of-range reads as 0 (open space).
    float fillAt(int nx, int nz) const;

    MaskFieldParams m_params;
    std::vector<std::uint8_t> m_nodes; // binary fill per map node
    int m_nodesX = 0;
    int m_nodesZ = 0;
    glm::vec3 m_origin{0.0f};
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
};

} // namespace maskfield
