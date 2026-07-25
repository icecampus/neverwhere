// Scalar field for the cliff/highground prototype: blurred binary height nodes
// define the plateau outline in plan; a slab (+ optional ground chunk) gives
// the 3D base; omphalos-style grooves (three rotated gmod frames) carve the
// wall band.
//
// Pure pipeline like highground.h: node grid + FieldParams -> sampled field,
// no Qt/GPU. The node grid is injected (CliffFieldPlayground's hardcoded
// pattern, TileShapePlayground's brush), so the region shape is data-driven.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace cliff {

struct FieldParams {
    float cellSize = 0.045f;       // field voxel size in world units
    float padding = 0.5f;          // field margin outside the region; F > 0 on the grid border
    float plateauHeight = 1.0f;    // H
    float d2Scale = 0.5f;          // d2 = (0.5 - h) * d2Scale, pseudo-SDF of the plateau outline
    int blurRadiusCells = 3;       // box-blur radius (2D grid cells) per pass
    int blurPasses = 3;            // blur passes for corner rounding
    float edgeRadius = 0.04f;      // slab expansion -> rounds the top rim
    float grooveMaskWidth = 0.25f; // mask = smoothstep(w, 0, |d2|): grooves hug the wall
    float grooveFadeK = 1.0f;      // G = g - (1 - mask) * K limits the carve depth
    float grooveRimFade = 0.12f;   // vertical fade: no carving in the top rim band
    float fbmAmplitude = 0.03f;    // +- fbm displacement of the whole shape
    float fbmFrequency = 5.0f;     // top octave wavelength = 2 * cell: no grid aliasing
    int fbmOctaves = 2;            // band-limited on purpose: sub-cell octaves alias
                                   // on the sampling grid and create saddle faces
    float groundDepth = 0.3f;      // ground slab thickness (y in [-groundDepth, 0])
    float groundMargin = 0.35f;    // ground slab extent beyond the region
    float groundRounding = 0.1f;   // ground slab edge rounding
    // false: no ground chunk under the plateau — the raised slab stands alone
    // (its underside closes at y = -edgeRadius, the mesh stays watertight).
    bool groundEnabled = true;
    // Groove wave: abs(gmod(y + phase, period) - period/2) - (period/2 - depthMax).
    float groovePeriod = 0.4f;     // gmod period along the groove axis
    float groovePhase = 0.1f;      // phase shift
    float grooveDepthMax = 0.1f;   // carve amplitude of the wave
    float grooveSmooth = 0.02f;    // SmoothMax radius
    // Rotation angles (radians) of the three groove frames, omphalos defaults:
    // frame1 rotates xy; frame2 xz then xy; frame3 xz then xy.
    float grooveAngles[3][2] = {
        {0.6283185f, 0.0f},       // pi/5
        {2.1991149f, 0.5654867f}, // 2.1*pi/3, 0.9*pi/5
        {-2.1467550f, 0.6911504f} // -2.05*pi/3, 1.1*pi/5
    };
};

class CliffField {
public:
    // nodes: binary (0/1) height nodes, row-major [z * nodesX + x]; the region
    // spans (nodesX-1) x (nodesY-1) map cells (cell = 1x1 in XZ). The grid is
    // consumed in the constructor (blurred into an internal lookup), the caller
    // keeps ownership. Border nodes must be 0, otherwise the blurred outline
    // extends past the region and the solid gets clipped by the field grid.
    CliffField(const FieldParams& params, const std::uint8_t* nodes, int nodesX, int nodesY);

    // Full field: base shape + grooves + fbm displacement. Negative inside the solid.
    float eval(const glm::vec3& p) const;
    // Base shape only (slab + ground chunk if enabled, no grooves/fbm). Cheap.
    float evalBase(const glm::vec3& p) const;
    // Groove carve depth: 0 on the untouched surface, > 0 towards groove floors.
    float grooveDepth(const glm::vec3& p) const;

    // Samples the full field on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix.
    void sample(std::vector<float>& outValues) const;

    const FieldParams& params() const { return m_params; }
    const glm::vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }
    float regionX() const { return m_regionX; } // region extents in map cells
    float regionZ() const { return m_regionZ; }

private:
    float heightAt(float x, float z) const; // blurred 2D node field, bilinear lookup
    float evalBase(const glm::vec3& p, float& outD2) const;
    float grooveWave(float y) const;           // parametrized omphalos wave
    float grooveMask(float d2, float y) const; // wall band mask with rim fade
    float applyGrooves(float f, const glm::vec3& p, float mask) const;

    FieldParams m_params;
    glm::vec3 m_origin{0.0f};
    float m_regionX = 0.0f;
    float m_regionZ = 0.0f;
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
    int m_hW = 0; // blurred 2D height grid dims (same XZ extent/resolution as the field)
    int m_hH = 0;
    std::vector<float> m_hGrid;
};

// Omphalos-style helpers.
float gmod(float x, float m);
float fbm3(const glm::vec3& p);  // full 5-octave fbm (fragment-shader richness)
float fbm(const glm::vec3& p, int octaves);
float smoothMin(float a, float b, float r);
float smoothMax(float a, float b, float r);

} // namespace cliff
