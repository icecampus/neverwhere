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

    // Chaikin corner-cutting iterations applied to the region loops (CGAL
    // generator only) before both the walls and the top consume them, so the
    // wall/top junction stays exact by construction. 0 = off (legacy
    // orthogonal contour); each iteration doubles the loop vertices. When
    // smoothing is on, the wall bevel is suppressed (effective bevel 0):
    // the contour is already smooth, and a second chamfer on every Chaikin
    // mini-corner would eat the top at the corner nodes.
    int smoothIterations = 0;
};

enum class Material : std::uint8_t {
    Top = 0,
    Wall = 1,
};

struct Vertex {
    glm::vec2 pos;   // field space, final (lift/height already applied)
    glm::vec2 uv;    // Top: world-tiled UV; Wall: reserved (0,0)
    glm::vec4 color; // Wall: baked shade (tint multiplier); Top: topTint
    // Field y BEFORE the lift was applied (wall bottom: pos.y; wall top /
    // plateau top: pos.y + height). Monotonic with depth along the iso view
    // ray (0,+1,+1), so callers with a GPU depth buffer can use it as z.
    float groundY = 0.0f;
    // Wall: unit outward contour normal in MAP space (for triplanar blend
    // weights). Top: (0,0).
    glm::vec2 normal{0.0f, 0.0f};
};
// Tests compare meshes byte-for-byte (memcmp) — the struct must stay free of
// unnamed padding (glm defaults give alignof 4 here, so 11 floats pack tight).
static_assert(sizeof(Vertex) == sizeof(float) * 11, "Vertex grew padding: the memcmp determinism contract breaks");

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

// CGAL-based generator (desktop builds only): the land region is computed as
// a boolean union of per-node unit squares (map space), the top surface is a
// constrained Delaunay triangulation of the exact region boundary (holes
// respected — no per-cell stitching, no "square lid" artifacts), the walls
// are extruded from the same region loops. Returns an empty mesh when the
// library was built without CGAL — check cgalAvailable().
bool cgalAvailable();
Mesh generateCgal(const Grid& grid, const Params& params);

// Boundary of the land as strictly-simple closed loops in field space
// (unlifted, y-down): contour chains walked with land on the left, split at
// pinch points (figure-eight joins), collinear points dropped. The
// boundary-first contract: loops are safe to extrude into walls and to fill
// as the top surface.
std::vector<std::vector<glm::vec2>> boundaryLoops(const Grid& grid, float cellWidth, float cellHeight);

} // namespace highground
