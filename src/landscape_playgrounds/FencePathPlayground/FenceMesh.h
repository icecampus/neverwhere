#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <topology_core/diamond_isometry.h>

#include "FenceModel.h"

// Baked fence piece meshes (ShapeML fence.shp OBJ exports) and the pure-CPU
// half of the 3D fence visualization: OBJ/MTL loading and instancing of the
// pieces of a FenceModel into projected field-space vertices. No sokol here —
// the GPU shell is FenceMeshRenderer. World convention: 1 cell = 1 meter,
// y-up, cell (cx,cy) spans [cx,cx+1) x [cy,cy+1); the pieces keep the
// fence.shp pivots (post axis at origin, base y=0; a section spans x
// 0..section_length from the first post axis).

// Projected, lit vertex — the exact layout of GridColorVertex (GridRenderer.h)
// so the mesh pass can share the grid's color shader/vertex format.
struct FenceFieldVertex {
    float x, y, z;
    float r, g, b, a;
};

struct FenceMesh {
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 nrm;
        glm::vec3 rgb; // material Kd
    };
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    glm::vec3 aabbMin{0.0f};
    glm::vec3 aabbMax{0.0f};
};

// The baked piece set of resources/models/fence/.
struct FenceMeshSet {
    FenceMesh post;     // fence_post.obj (piece=0)
    FenceMesh corner;   // fence_corner.obj (piece=1, taller, spike finial)
    FenceMesh section2; // fence_section2.obj (1-cell section, post pitch 2 m)
    FenceMesh section3; // fence_section3.obj (2-cell section, post pitch 3 m)
    bool ok = false;
};

// Minimal OBJ/MTL reader for the known ShapeML exporter output: v/vn/vt,
// usemtl groups, triangular f v/t/n or f v//n; MTL newmtl + Kd (everything
// else is skipped; the bakes carry no textures). Vertices are deduplicated on
// (position, normal, material). false on hard failure (no file, no faces).
bool loadFenceMeshObj(const std::string& objPath, FenceMesh* out, std::string* error);

// Loads the four-piece set from <dir>/fence_{post,corner,section2,section3}.obj.
bool loadFenceMeshSet(const std::string& dir, FenceMeshSet* out, std::string* error);

// Repo root via .git walk-up from cwd (Qt-free, same idea as ShapemlPlayground).
std::string findRepoRoot();

// A post with exactly two incident sections running along perpendicular axes
// gets the taller corner/gate mesh.
bool isCornerPost(const FenceModel& model, int postPieceId);

// Instantiates every piece of the model into projected field-space triangles
// (camera/zoom are applied later in the vertex shader, so the result is cached
// per FenceModel::version()). Sections are oriented along postA->postB; posts
// sit on cell centers with a deterministic per-id quarter-turn (cheap scar
// variety); the selected fence gets an amber tint. Lighting is baked into the
// vertex color: hemisphere ambient + lambert sun. z follows the playground
// depth convention: (kZFar - (groundFieldY + y*m2p*0.5)) * kZScale — the
// height term keeps raised fragments on their own ground row while losing to
// nearer rows.
std::vector<FenceFieldVertex> buildFenceFieldTriangles(
    const topology_core::DiamondIsometry& iso,
    const FenceModel& model,
    const FenceMeshSet& meshes,
    int selectedFence);
