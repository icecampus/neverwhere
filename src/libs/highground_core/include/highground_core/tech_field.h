// Tech-landscape field: a C++ rethink of the Python TechnicalGrass atlas
// generators (utils/asset_generator/technical/generate_atlas_{ridge,valley}.py).
// Those rasterize a piecewise-linear heightfield on 5 support points per map
// cell (4 corner nodes + center) into a 2D tileset; here the same shape becomes
// a real scalar field for the surface-nets pipeline:
//
//   h(x,z) = fan interpolation over the cell (center + adjacent corner pairs),
//            corner heights = node values, center height by cell class:
//            Full maxV, Line (maxV+minV)/2, Corner lerp(mid, minV, style),
//            Lack/Opposite lerp(mid, maxV, style) — with {0,1} nodes this
//            reproduces both Python tilesets bit-for-bit
//   eval(p) = max(p.y - h, min(h, 0) - groundDepth - p.y, side)
//             — bumps on a flat base, no ground sheet under empty cells; the
//             `side` term clips the open water (h ~ 0 with no local slope)
//             while ramp zero-crossings stay inside (no waterline crack)
//
// Shoreline mode (outlineDepth > 0, the "yellow around green" principle): the
// 8-neighborhood of the painted land nodes minus the land itself becomes an
// outline ring of nodes at height -outlineDepth, so the border ramps continue
// through the water plane (y = 0) into an underwater foot, then back up to
// the open water — a beach profile around the landform. Nodes are then
// three-valued: land +1, outline -outlineDepth, empty 0.
//
// Ridge and Valley differ only in the center heights (the Valley split
// triangulation of Lack tiles is geometrically identical to a fan with
// center = maxV: the ramp planes coincide), so one `style` parameter blends
// both tilesets continuously — a hybrid no 2D atlas could draw. The Python
// "walls" under the rhombus edges were an artifact of the screen projection
// (the top shifted up by ELEVATION px); in true 3D the surface is continuous
// across shared nodes and no separate wall geometry exists. The dark tile
// outline of the tileset becomes the shading-only groove channel (proximity
// to raised-cell borders), nothing is carved.
//
// Same contract as cliff::CliffField: node grid + params -> sampled field,
// no Qt/GPU. Cell classification mirrors landscape_core's nodeMaskToTileType
// (src/libs/landscape_core/src/landscape_logic.cpp) — generalized to signed
// node values here to keep highground_core free of that dependency.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "surface_nets.h"

namespace tech {

struct TechFieldParams {
    float cellSize = 0.06f;    // field voxel size in world units
    float padding = 0.5f;      // field margin outside the region; F > 0 on the grid border
    float levelHeight = 0.35f; // world height of one level (the Python ELEVATION analog)
    float groundDepth = 0.05f; // bottom slab depth; the effective slab is at
                               // least ~2 voxels thick (see TechField::baseDepth)
    float style = 0.0f;        // 0 = Ridge (folds/peaks/saddles) .. 1 = Valley (flat planes)
    float soften = 0.0f;       // 0 = linear ramps (faithful), 1 = smoothstep-shouldered ramps
    float creaseWidth = 0.0f;  // groove shading band around raised-cell borders (0 = off)
    int blurPasses = 1;        // sampled-field anti-terracing blur (same trick
                               // as stone; the base slab is thickened to match)
    // Shoreline outline ("yellow around green"): when > 0, the 8-neighborhood
    // of the painted land nodes (minus the land) forms an outline ring at
    // height -outlineDepth * levelHeight — the border ramps continue below
    // the water plane into an underwater foot. 0 = off (land only).
    float outlineDepth = 0.0f; // outline ring depth in node units (1 = one level down)
};

class TechField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // copied, the caller keeps ownership. Border nodes must be 0 (same
    // contract as cliff::CliffField; with outlineDepth > 0 the caller must
    // also leave one extra empty ring for the outline to spread into).
    TechField(const TechFieldParams& params, const std::uint8_t* nodes, int nodesX, int nodesY);

    // Full field: heightfield top + bottom slab, clipped laterally where the
    // open water begins. Negative inside the solid.
    float eval(const glm::vec3& p) const;
    // Outline channel for shading: > 0 near the borders of non-empty cells
    // (1 at the border line), 0 elsewhere — the tileset's dark contour.
    float grooveDepth(const glm::vec3& p) const;

    // Surface height in world units (below zero inside the outline ring),
    // public for tests/debug.
    float heightAt(float x, float z) const;

    // Samples eval() on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix. Then applies blurPasses
    // 3-tap blur passes (anti-terracing, as in stone_gen::StoneField).
    void sample(std::vector<float>& outValues) const;

    // Generic field view for cliff::regularizeSigns/extractSurfaceNets.
    cliff::ScalarFieldView view() const;

    const TechFieldParams& params() const { return m_params; }
    const glm::vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }

private:
    // Node value: +1 land, -outlineDepth outline, 0 empty (0 outside the grid).
    float nodeAt(int x, int z) const;
    bool cellNonEmpty(int x, int z) const;     // any corner with value > 0 (land)
    // Base slab depth under the solid: at least ~2 voxels thick everywhere,
    // plus one voxel per blur pass against erosion — a sub-voxel solid makes
    // the top and bottom surfaces share voxel vertices (membrane with junk
    // normals, and the blur erodes it away entirely).
    float baseDepth() const;
    // Style-blended center height for the cell class (maxV/minV are the
    // extreme corner values; nMax counts the maxV corners; opposite marks the
    // diagonal 2-maxV case).
    float centerHeight(float maxV, float minV, int nMax, bool opposite) const;
    // Fan heightfield + the sector gradient length in node units per cell
    // (piecewise constant; the `side` clip needs it to spare ramp
    // zero-crossings).
    float heightAt(float x, float z, float* outGradLen) const;

    TechFieldParams m_params;
    std::vector<float> m_values; // per-node signed heights (row-major)
    int m_nodesX = 0;
    int m_nodesY = 0;
    glm::vec3 m_origin{0.0f};
    float m_regionX = 0.0f; // region extents in map cells
    float m_regionZ = 0.0f;
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
};

} // namespace tech

