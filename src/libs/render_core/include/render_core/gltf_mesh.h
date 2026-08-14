#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace render_core {

// CPU-side triangle mesh from a GLB (glTF 2 binary). Positions are Y-up
// model space as authored; call fitGltfMeshToFootprint() to sit the mesh on
// y=0 and scale XZ into a cell-unit footprint before uploading to the GPU.
struct GltfVertex {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
};

struct GltfMesh {
    std::vector<GltfVertex> vertices;
    std::vector<std::uint32_t> indices;
};

bool loadGltfMeshFromGlbBytes(const std::uint8_t* data, std::size_t size, GltfMesh& out, std::string* error);
bool loadGltfMesh(const std::filesystem::path& path, GltfMesh& out, std::string* error);

// Uniform scale so the XZ AABB fits inside footprintW x footprintH (cell
// units), XZ-centered at the origin, Y min at 0 (sits on the ground plane).
// yawDegrees rotates around +Y after the fit (asset-facing correction).
void fitGltfMeshToFootprint(GltfMesh& mesh, float footprintW, float footprintH, float yawDegrees = 0.0f);

} // namespace render_core
