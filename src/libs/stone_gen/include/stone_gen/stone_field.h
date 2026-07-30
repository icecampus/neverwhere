// Stone highground field: painted height nodes -> blurred plateau slab (the
// cliff::CliffField base) carved by the StoneCubePlayground voronoi stones
// (clamp(k*(F2-F1)) bulge inside voronoi cells, grooves at the cell borders)
// + fbm detail. The carve is masked to the slab surface band so the field
// stays the clean slab SDF deep inside/outside (watertight border), and —
// with flatTop — stitched to the flat plateau top: the relief fades by the
// base slab normal, but inside a rim band along the outline the stitch is
// structural — stone bulges wrap onto the top (rimBulge) and groove mouths
// scoop down into it (rimNotch), so rim stones are not sliced and the top
// edge follows them; the interior top stays exactly flat.
//
// Same contract as cliff::CliffField (nodes + params -> sampled field ->
// highground_core surface nets), no Qt/GPU.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>

namespace stone_gen {

struct StoneFieldParams {
    cliff::FieldParams base;   // nodes -> slab (its grooves/fbm stay unused)
    float voroScale = 2.0f;    // voronoi cells per world unit (stone size)
    float cellJitter = 1.0f;   // feature point jitter inside cells
    float grooveDepth = 0.08f; // bulge amplitude (world units)
    float grooveK = 2.5f;      // cell factor = clamp(k * (F2 - F1))
    float grooveMaskWidth = 0.25f; // carve band around the slab surface
    float fbmAmplitude = 0.02f;    // +- fbm displacement of the whole shape
    float fbmFrequency = 4.0f;
    float seed = 0.0f;             // voronoi lattice offset
    int blurPasses = 2;            // sampled-field blur vs voronoi terracing
                                   // (same trick as stone_mesh, 0 keeps raw)
    float flatTopLo = 0.55f;       // base-normal .y where the relief starts fading
    float flatTopHi = 0.85f;       // ... and where the top is fully flat
    float rimWidth = 0.35f;        // rim stitch band width in ~world units
                                   // from the wall into the top interior
    float rimBulge = 1.0f;         // stone bulge wrap onto the top in the band
    float rimNotch = 0.04f;        // groove-mouth scoop depth into the top
    bool flatTop = true;           // no stone relief on the plateau top: the
                                   // carve/fbm fade out by the base slab normal,
                                   // the walls keep the relief (uneven rim)
};

class StoneField {
public:
    // Same node contract as cliff::CliffField: binary nodes, row-major
    // [z * nodesX + x], region spans (nodesX-1) x (nodesY-1) map cells,
    // border nodes must be 0.
    StoneField(const StoneFieldParams& params, const std::uint8_t* nodes, int nodesX, int nodesY);

    // Full field: slab base + voronoi carve + fbm. Negative inside the solid.
    float eval(const glm::vec3& p) const;
    // Carve depth for shading: 0 on stone faces, max at groove floors.
    float grooveDepth(const glm::vec3& p) const;

    // Samples eval() on the base grid (same layout as cliff::CliffField::
    // sample), then applies the anti-terracing blur passes.
    void sample(std::vector<float>& outValues) const;

    // Generic field view for cliff::regularizeSigns/extractSurfaceNets.
    cliff::ScalarFieldView view();

    const StoneFieldParams& params() const { return m_params; }

private:
    // 0 at voronoi cell borders (grooves) .. 1 inside stones.
    float cellFactor(const glm::vec3& p) const;
    float surfaceMask(float dBase) const;
    // Base field bundle from one 7-tap evaluation: the slab value, the
    // outline pseudo-SDF d2, the slab normal .y (1 on the planar top, ~0 on
    // the walls) and the horizontal d2 gradient length.
    void sampleBase(
        const glm::vec3& p,
        float& outDBase,
        float& outD2,
        float& outNormalY,
        float& outD2GradLen) const;
    // Rim stitch weights: outTopness = 0 on walls .. 1 on the flat top
    // (angular fade), outRim = 1 at the wall plane .. 0 across rimWidth
    // world units into the top interior. Both 0 when flatTop is off.
    void rimStitch(float normalY, float d2, float d2GradLen, float& outTopness, float& outRim) const;

    StoneFieldParams m_params;
    cliff::CliffField m_base; // slab + sampling grid (evalBase only)
};

} // namespace stone_gen
