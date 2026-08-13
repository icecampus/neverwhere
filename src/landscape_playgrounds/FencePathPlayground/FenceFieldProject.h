#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <topology_core/diamond_isometry.h>

#include <fence_core/fence_mesh.h>

// Playground-side world -> field projection of the fence instances (the
// fence_core lib stops at world space; every consumer keeps its own depth
// convention). Returns the field point (screen y lifted by the height) and the
// baked depth in .z: (kZFar - (groundY + y*m2p*0.5)) * kZScale — the height
// term (inside the consistent 0<F<1 window) lets raised fragments win their
// own ground row but lose to nearer rows.

// Projected, lit vertex — the exact layout of GridColorVertex (GridRenderer.h)
// so the mesh pass can share the grid's color shader/vertex format.
struct FenceFieldVertex {
    float x, y, z;
    float r, g, b, a;
};

inline glm::vec3 fenceWorldToField(const topology_core::DiamondIsometry& iso, glm::vec3 world) {
    constexpr float kZFar = 100000.0f;
    constexpr float kZScale = 1.0f / 200000.0f;
    const float halfW = iso.dims.cellSize().x * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    const float fieldX = (world.x - world.z) * halfW + halfW;
    const float groundY = (world.x + world.z) * halfH + halfH;
    const float screenY = groundY - world.y * fence_core::kFenceMetersToPoints;
    const float z =
        (kZFar - (groundY + world.y * fence_core::kFenceMetersToPoints * 0.5f)) * kZScale;
    return {fieldX, screenY, z};
}

// Instantiates the model pieces straight into projected field-space triangles
// (fence_core::buildFenceWorldTriangles + fenceWorldToField per vertex).
inline std::vector<FenceFieldVertex> buildFenceFieldTriangles(
    const topology_core::DiamondIsometry& iso,
    const fence_core::FenceModel& model,
    const fence_core::FenceMeshSet& meshes,
    int selectedFence) {
    const std::vector<fence_core::FenceWorldVertex> world =
        fence_core::buildFenceWorldTriangles(model, meshes, selectedFence);
    std::vector<FenceFieldVertex> out;
    out.reserve(world.size());
    for (const fence_core::FenceWorldVertex& v : world) {
        const glm::vec3 f = fenceWorldToField(iso, {v.pos[0], v.pos[1], v.pos[2]});
        out.push_back({f.x, f.y, f.z, v.color[0], v.color[1], v.color[2], v.color[3]});
    }
    return out;
}
