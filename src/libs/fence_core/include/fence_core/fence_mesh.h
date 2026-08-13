#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "fence_core/fence_model.h"

// Baked fence piece meshes (ShapeML fence.shp OBJ exports) and the pure-CPU
// half of the 3D fence visualization: OBJ/MTL loading and instancing of the
// pieces of a FenceModel into world-space triangles with lighting baked into
// the vertex color. No sokol here — the GPU shells are the playground
// FenceMeshRenderer and render_core::FenceRenderer (each does its own
// world->field projection and depth convention). World convention: 1 cell =
// 1 meter, y-up, and cell (cx,cy) spans [cx-0.5,cx+0.5) x [cy-0.5,cy+0.5) so
// that the cell CENTER sits at the integer world point (cx,cy) — matching
// DiamondIsometry::mapToField, which returns the diamond center for integer
// cell coords (nodes/corners live at half-integer coords). The pieces keep the
// fence.shp pivots (post axis at origin, base y=0; a section spans x
// 0..section_length from the first post axis).
// Graduated from the FencePathPlayground into this lib for the editor port.
namespace fence_core {

// World-space, lit vertex (layout matches render_core's baked-color passes:
// CyclopeanVertex/FenceVertex {pos[3], color[4]}).
struct FenceWorldVertex {
    float pos[3];
    float color[4];
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

// The baked four-piece set (resources/models/fence/ or a fence3d asset bundle).
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

// Meters -> screen points of vertical lift (DiamondIsometry-agnostic). Default
// of the fence3d asset's metersToPoints parameter.
constexpr float kFenceMetersToPoints = 96.0f;

// How an instanced piece gets its vertex color.
enum class FenceInstanceShading {
    Lit,      // fixed sun + hemisphere ambient
    Selected, // Lit, mixed 50% toward the amber selection tint
    Flat,     // flatColor as-is (ghost previews)
};

// Instancing primitive behind buildFenceWorldTriangles, exposed for the
// consumers' own orchestration (render_core::FenceRenderer instances per
// annotated piece without a FenceModel; ghost previews use Flat): appends
// `mesh` rotated yawDeg degrees around Y at `offset` (world meters).
void appendFenceInstance(
    const FenceMesh& mesh,
    glm::vec3 offset,
    float yawDeg,
    FenceInstanceShading shading,
    glm::vec4 flatColor,
    std::vector<FenceWorldVertex>& out);

// Instantiates every piece of the model into world-space triangles (the
// consumer projects to field space; camera/zoom are vertex-shader state).
// Sections are oriented along postA->postB; posts sit on cell centers with a
// deterministic per-id quarter-turn (cheap scar variety); the selected fence
// gets an amber tint. Lighting is baked into the vertex color: hemisphere
// ambient + lambert sun.
std::vector<FenceWorldVertex> buildFenceWorldTriangles(
    const FenceModel& model,
    const FenceMeshSet& meshes,
    int selectedFence);

} // namespace fence_core
