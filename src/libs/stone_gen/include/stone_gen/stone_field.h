// Stone highground field: painted height nodes -> blurred plateau slab (the
// cliff::CliffField base) carved by the StoneCubePlayground voronoi stones
// (clamp(k*(F2-F1)) bulge inside voronoi cells, grooves at the cell borders)
// + fbm detail. The carve is masked to the slab surface band so the field
// stays the clean slab SDF deep inside/outside (watertight border), and —
// with flatTop — faded out where the base slab normal points up: the plateau
// top stays exactly flat while the walls (and the rim they bulge into) keep
// the stone relief.
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
    // Relief fade by the base slab normal: 1 on the walls, 0 on the flat top.
    float reliefMask(const glm::vec3& p) const;

    StoneFieldParams m_params;
    cliff::CliffField m_base; // slab + sampling grid (evalBase only)
};

} // namespace stone_gen
