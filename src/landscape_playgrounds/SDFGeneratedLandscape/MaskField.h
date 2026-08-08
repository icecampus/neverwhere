// Mask field: the third teaching sample in the node-driven scalar-field
// series. Takes the 2D silhouette the "Texture 2D" layer draws — per-node
// fill (1 at an on-node, 0 at an off-node) interpolated across each cell,
// mask = fill >= 0.5 — and extrudes that footprint into a thin plate with
// a sloped skirt around it:
//
//   eval(p) = max(p.y - top(s), -(p.y + hh), s - S)
//     s = sd(x,z), S = spreadDistance, hh = height/2
//
//   top(s) = +hh                        for s <= 0  (the core silhouette)
//          = height * (1 - s/S) - hh    for 0 <= s <= S  (the skirt: a linear
//          = -hh                        for s >= S    ramp down past the grid)
//
//   The plate stands half its height below the node grid plane (y = 0): the
//   slab spans y = -hh..+hh and the skirt foot reaches y = -hh exactly at
//   s = S.
//
//   sd(x,z) is the signed distance to the fill = 0.5 iso contour (negative
//   inside), rasterized at construction time at kDistTexelsPerCell texels
//   per cell: seeded with the fractional zero crossings of fill - 0.5 on
//   texel edges (the fill is linear along an axis-aligned segment, so the
//   crossings are exact), then propagated with a two-pass chamfer (1, sqrt2)
//   — the same trick as the contact-AO field in scene_stitch. eval() just
//   bilinearly samples that raster, so it stays O(1).
//
//   The core (the silhouette spread 0 would draw) keeps the full height
//   (Height, world). spreadDistance > 0 grows no full-height geometry —
//   instead the plate's edge rolls off as a height ramp driven by the
//   distance from the core contour: +hh at the wall, -hh exactly at s = S,
//   with the iso lines (and so the skirt foot) rounded around convex
//   corners (the defining property of a true distance field). At
//   spreadDistance = 0 the skirt vanishes and the formula reduces exactly
//   to a slab max(s, |y| - hh): a vertical wall on the same contour the
//   Texture 2D mask renders (edges cross at their midpoints, half-painted
//   cells become wedges, a lone node becomes a small blob), minus the
//   shader-only fbm wobble and alpha fade.
//
// Unlike BoxField/CircleField there is no neighbourhood loop at all: the
// distance raster is built once in the constructor and every eval() is a
// single clamped bilinear lookup. Far outside the mask the field grows with
// distance, so the sampled volume border stays positive as long as the
// caller's zero border ring is wider than spreadDistance (the renderer
// derives the ring from it automatically).
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
    float cellSize = 0.06f;      // field voxel size in world units
    float padding = 0.5f;        // field margin outside the region (slack for blur;
                                 // the field stays positive on the border anyway)
    float height = 0.2f;         // core plate height in world units; the plate is
                                 // centered on the node grid plane (y = 0): it spans
                                 // y = -height/2..+height/2
    float spreadDistance = 0.0f; // skirt width in cells: the plate edge rolls off
                                 // as a linear height ramp driven by the distance
                                 // from the core contour, reaching the bottom plane
                                 // y = -height/2 this many cells outside it. Must
                                 // stay below the caller's zero border ring.
    int blurPasses = 0;          // sampled-field blur passes: 0 = crisp walls,
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

    // Solid under the core plate and its skirt: negative below the top
    // surface above y = -height/2 within s <= spreadDistance, positive
    // outside. Outside the distance raster the lookup clamps to the raster
    // edge, whose distance already exceeds the spread (given the zero-ring
    // contract), so the field stays positive out there.
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
    // Texels per cell of the distance raster (same resolution as the
    // contact-AO field in scene_stitch).
    static constexpr int kDistTexelsPerCell = 8;

    // Raw node value at (nx, nz); out-of-range reads as 0 (open space).
    float nodeFill(int nx, int nz) const;
    // Bilinear node fill at the point (x, z) in node (cell) coordinates.
    float fillAt(float x, float z) const;
    // Builds the raster signed distance to the fill = 0.5 contour.
    void buildDistanceField();
    // Clamped bilinear sample of the distance raster at (x, z) in node
    // coordinates; negative inside the mask.
    float distanceAt(float x, float z) const;

    MaskFieldParams m_params;
    std::vector<std::uint8_t> m_nodes; // binary fill per map node
    int m_nodesX = 0;
    int m_nodesZ = 0;
    glm::vec3 m_origin{0.0f};
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
    // Signed distance to the fill = 0.5 contour in node (cell) units,
    // kDistTexelsPerCell texels per cell; texel (i, j) sits at node
    // coordinate (i, j) / kDistTexelsPerCell. Empty when there is no grid.
    std::vector<float> m_dist;
    int m_distW = 0;
    int m_distH = 0;
};

} // namespace maskfield
