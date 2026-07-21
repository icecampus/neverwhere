#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

// Highground (raised 3D landscape) geometry generation from a vertex-node
// grid. Pure conveyor: input data -> generation -> output mesh. No Qt, no
// GPU, no textures: primitives are tagged with a material id and the caller
// decides how to draw them (two materials for now: top surface and walls).
// Caching/incremental rebuilds are intentionally left to the caller.
namespace highground {

// Dense window over the vertex-node grid (a node is the Up-corner of the
// cell with the same map coordinates — the vertex-centric landscape
// contract of topology_core::DiamondIsometry).
struct Grid {
    int originX = 0;
    int originY = 0; // world node coordinates of the (0,0) local node
    int width = 0;
    int height = 0;  // window size in nodes
    std::vector<std::uint8_t> nodes; // width*height, row-major

    bool at(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return false;
        }
        return nodes[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] != 0;
    }
    bool at(const glm::ivec2& node) const {
        return at(node.x - originX, node.y - originY);
    }
};

// Build a dense grid window from a sparse list of "on" nodes (margin in
// nodes around the bounding box, so contours of border cells resolve).
Grid makeGrid(const glm::ivec2* onNodes, std::size_t count, int margin = 1);

struct Params {
    float cellWidth = 128.0f;  // grid step, field px (map X axis)
    float cellHeight = 64.0f;  // grid step, field px (map Y axis)
    float height = 96.0f;      // raised height, field px

    bool rockWalls = true;     // false: simple flat walls instead of FastNoise rock
    float amplitude = 0.28f;   // rock displacement amplitude, in cell units
    float bevel = 0.3f;        // convex-corner bevel, in cell units
    float noiseScale = 2.75f;  // FastNoise feature scale, per cell unit
    int terraceSteps = 4;
    int hSub = 3;              // columns per wall piece
    int vSub = 4;              // rows per wall piece
    int seed = 1337;

    float topUvPerWorldPx = 1.0f / 256.0f; // world px -> UV of the top texture tiling
    glm::vec4 topTint{1.0f, 1.0f, 1.0f, 1.0f};

    bool sortPrimitives = true; // false: emit unsorted (e.g. for a GPU depth buffer)
};

enum class Material : std::uint8_t {
    Top = 0,
    Wall = 1,
};

struct Vertex {
    glm::vec2 pos;   // field space, final (lift/height already applied)
    glm::vec2 uv;    // Top: world-tiled UV; Wall: reserved (0,0)
    glm::vec4 color; // Wall: baked shade (tint multiplier); Top: topTint
};

struct Primitive {
    Material material = Material::Top;
    float depth = 0.0f;       // iso painter's key: ground y of the primitive's lowest point
    std::uint32_t first = 0;  // into Mesh::vertices
    std::uint32_t count = 0;  // multiple of 3 (wall quads = 2 triangles = 6)
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Primitive> primitives; // back-to-front when sortPrimitives
};

// Deterministic: same input -> byte-identical output (seed is in Params).
Mesh generate(const Grid& grid, const Params& params);

} // namespace highground
