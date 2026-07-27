// Stone cube mesh: StoneSdf sampled on a grid -> highground_core surface nets
// (regularizeSigns + extractSurfaceNets via ScalarFieldView) -> vertices with
// box-projected UVs and the cell factor for shading.
#pragma once

#include "stone_gen/stone_sdf.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace stone_gen {

struct StoneMeshVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float cellFactor = 0.0f; // 0 at groove bottoms, 1 inside stones
};

struct StoneMesh {
    std::vector<StoneMeshVertex> vertices;
    std::vector<std::uint32_t> indices; // triangles
    int watertightBadEdges = 0;         // 0 = watertight
    int remainingSaddles = 0;
    double meshMs = 0.0;
};

struct MeshParams {
    float cellSize = 0.04f; // field voxel size (world units)
    float padding = 0.3f;   // margin outside the cube bbox
    int blurPasses = 1;     // 3-tap box blur passes over the sampled field:
                            // smooths naive-surface-nets terracing on the
                            // steep voronoi grooves (0 keeps the raw field)
};

// Runs the full pipeline: sample -> regularize -> surface nets -> UV/cell
// factor assignment + watertight check.
StoneMesh generateMesh(const StoneSdf& sdf, const MeshParams& meshParams = MeshParams{});

} // namespace stone_gen
