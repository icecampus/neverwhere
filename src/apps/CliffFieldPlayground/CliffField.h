// Scalar field for the cliff/highground prototype: blurred binary height nodes
// define the plateau outline in plan; a slab + ground chunk gives the 3D base;
// omphalos-style grooves (three rotated gmod frames) carve the wall band.
#pragma once

#include "MiniMath.h"

#include <cstdint>
#include <vector>

namespace cliff {

struct FieldParams {
    int regionCells = 6;           // map cells per side (cell = 1x1 in XZ) -> 7x7 height nodes
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
};

class CliffField {
public:
    explicit CliffField(const FieldParams& params);

    // Full field: base shape + grooves + fbm displacement. Negative inside the solid.
    float eval(const cfm::Vec3& p) const;
    // Base shape only (slab + ground chunk, no grooves/fbm). Cheap.
    float evalBase(const cfm::Vec3& p) const;
    // Groove carve depth: 0 on the untouched surface, > 0 towards groove floors.
    float grooveDepth(const cfm::Vec3& p) const;

    // Samples the full field on the regular grid; outValues gets
    // (nx+1)*(ny+1)*(nz+1) entries; point (ix,iy,iz) sits at origin + cell * (ix,iy,iz),
    // index = (iy * (nz+1) + iz) * (nx+1) + ix.
    void sample(std::vector<float>& outValues) const;

    const FieldParams& params() const { return m_params; }
    const cfm::Vec3& origin() const { return m_origin; }
    int sizeX() const { return m_nx; } // voxel counts per axis
    int sizeY() const { return m_ny; }
    int sizeZ() const { return m_nz; }

private:
    float heightAt(float x, float z) const; // blurred 2D node field, bilinear lookup
    float evalBase(const cfm::Vec3& p, float& outD2) const;

    FieldParams m_params;
    cfm::Vec3 m_origin;
    int m_nx = 0;
    int m_ny = 0;
    int m_nz = 0;
    int m_hW = 0; // blurred 2D height grid dims (same XZ extent/resolution as the field)
    int m_hH = 0;
    std::vector<float> m_hGrid;
};

// Omphalos-style helpers.
float gmod(float x, float m);
float grooveWave(float y); // abs(gmod(y + 0.1, 0.4) - 0.2) - 0.1
float fbm3(const cfm::Vec3& p);  // full 5-octave fbm (fragment-shader richness)
float fbm(const cfm::Vec3& p, int octaves);
float smoothMin(float a, float b, float r);
float smoothMax(float a, float b, float r);

} // namespace cliff
