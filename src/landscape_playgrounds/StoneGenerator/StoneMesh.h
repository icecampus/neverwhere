#pragma once

#include <vector>

#include <glm/glm.hpp>

// The one mesh contract of the playground: world-space triangle soup with
// flat (per-face) normals and light baked into the vertex color. Produced by
// the cut generator (StoneCut), consumed by StoneMeshRenderer.
struct StoneMesh {
    std::vector<float> pos; // xyz triplets, world units
    std::vector<float> nrm; // per-vertex flat normals
    std::vector<float> col; // rgba, light baked in
    int triCount = 0;
    glm::vec3 extentMin{0.0f};
    glm::vec3 extentMax{0.0f};
};
