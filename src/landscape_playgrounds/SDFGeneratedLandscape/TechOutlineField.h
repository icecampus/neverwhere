// Tech 3D Outline field: a playground-local fork of tech::TechField
// (src/libs/highground_core/include/highground_core/tech_field.h), dedicated
// to the "Tech 3D Outline" brush. The plain "Tech 3D" layer keeps using the
// shared library class (same code the editor's tech3d assets run on); this
// copy exists so the shoreline generation methods can be investigated and
// changed here without touching the library — the two layers share nothing
// but the surface-nets infrastructure (cliff::ScalarFieldView et al.).
//
// Starting state is a verbatim fork of the library field (with
// outlineDepth = 1 as the layer default):
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
// through the water plane (y = 0) into an underwater foot — a beach profile
// around the landform. Nodes are then three-valued: land +1, outline
// -outlineDepth, empty 0. The foot does NOT climb back to the water plane at
// the outer rim: cells that hold outline corners but no land corner keep the
// outline depth across their whole span (see heightAt), so the formation ends
// under water with a wall on the border of the open-water cells.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <highground_core/surface_nets.h>

namespace tech_outline {

struct TechOutlineFieldParams {
    float cellSize = 0.06f;    // field voxel size in world units
    float padding = 0.5f;      // field margin outside the region; F > 0 on the grid border
    float levelHeight = 0.35f; // world height of one level (the Python ELEVATION analog)
    float groundDepth = 0.05f; // bottom slab depth; the effective slab is at
                               // least ~2 voxels thick (see TechOutlineField::baseDepth)
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

class TechOutlineField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // copied, the caller keeps ownership. Border nodes must be 0 (same
    // contract as cliff::CliffField; with outlineDepth > 0 the caller must
    // also leave one extra empty ring for the outline to spread into).
    TechOutlineField(const TechOutlineFieldParams& params, const std::uint8_t* nodes, int nodesX, int nodesY);

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

    const TechOutlineFieldParams& params() const { return m_params; }
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

    TechOutlineFieldParams m_params;
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

} // namespace tech_outline
