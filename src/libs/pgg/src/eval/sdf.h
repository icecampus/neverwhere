#pragma once

// First-class SDF values (spec §8.4, stage E4): an immutable analytic DAG,
// not voxels. Voxel grids appear only at the two boundaries — sdf_from_mesh
// (materializes a Grid node through the BVH voxelizer) and mesh_from_sdf
// (samples the DAG once into a lattice and extracts with marching cubes).
//
// SdfNode is an immutable DAG node shared by pointer (SdfPtr, see value.h).
// eval() is a pure recursive function of the position — thread-safe, called
// from the parallel grid samplings. conservativeBBox() must (a) contain the
// iso-0 surface and (b) be cull-safe: outside the box the field dominates
// the point-to-box distance (the instance node's exact culling rests on it;
// documented per case in sdf.cpp).
//
// sdf_displace owns a deep copy of its amount field DAG (the RunContext
// arena dies after the run, and sdf values live in the cross-run cache).
// The sample context is @P only: validation walks the copy once and rejects
// (@N/@index/@attribute/distance_to/ingroup) with E307; evalFieldAtPoint is
// the scalar at-point evaluator mirroring the buffered evaluator formulas
// bit-for-bit.

#include "field.h"
#include "value.h"

#include <glm/gtc/quaternion.hpp>

namespace pgg {

enum class SdfKind {
    Sphere,
    Box,
    Union,
    UnionSmooth,
    Subtract,
    SubtractSmooth,
    Intersect,
    Displace,
    Instance,
    Grid,
    VoronoiCell,
    Grind,
};

// One transformed instance anchor (sdf_instance_on_points). Transforms mirror
// realizeInstances: T(@P) * R(@orient) * S(@scale), quaternion normalized.
struct SdfInstanceAnchor {
    glm::vec3 pos{0.0f};
    glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    glm::vec3 aabbMin{0.0f};  // world AABB of the transformed source bbox
    glm::vec3 aabbMax{0.0f};
};

struct SdfNode {
    SdfKind kind = SdfKind::Sphere;
    std::shared_ptr<const SdfNode> a;  // left child / displace child / instance source
    std::shared_ptr<const SdfNode> b;  // right child (CSG)
    float r = 0.0f;                    // Sphere
    glm::vec3 size{0.0f};              // Box
    float k = 0.0f;                    // smooth radius (*Smooth), Instance blend
    // Displace: owned deep copy of the amount field DAG + the amplitude
    // estimate used by the conservative bbox (max |amount| over a 4^3 probe
    // of the child bbox, spec §19 v1.2).
    std::vector<std::unique_ptr<FieldNode>> fieldArena;
    const FieldNode* amount = nullptr;  // root into fieldArena; nullptr = zero displacement
    float ampEstimate = 0.0f;
    // Instance
    std::vector<SdfInstanceAnchor> anchors;
    // VoronoiCell (fracture, §8.11): precomputed half-planes of one Voronoi
    // cell — eval = max over planes of dot(p - cellPoints[k], cellNormals[k]).
    // Unbounded field: it has no finite conservative bbox and is only valid
    // as the clipped operand of an Intersect (whose bbox comes from `a`).
    std::vector<glm::vec3> cellNormals;
    std::vector<glm::vec3> cellPoints;
    // Grid (sdf_from_mesh): trilinear lattice of signed distances
    glm::vec3 origin{0.0f};
    glm::ivec3 dims{0};
    float voxel = 0.0f;
    std::shared_ptr<const std::vector<float>> values;

    float eval(const glm::vec3& p) const;
    void conservativeBBox(glm::vec3& outMin, glm::vec3& outMax) const;
};

// --- factories (small builders shared by the builtin dispatch and tests) ----

SdfPtr sdfSphere(float r);
SdfPtr sdfBox(glm::vec3 size);
SdfPtr sdfUnion(SdfPtr a, SdfPtr b);
SdfPtr sdfUnionSmooth(SdfPtr a, SdfPtr b, float k);
SdfPtr sdfSubtract(SdfPtr a, SdfPtr b);
SdfPtr sdfSubtractSmooth(SdfPtr a, SdfPtr b, float k);
SdfPtr sdfIntersect(SdfPtr a, SdfPtr b);
// Grind (§8.4, spec §19 v1.10): cuts a along the MIDDLE surface of the a∩b
// penetration — eval = max(a, a - b + gap). The symmetric pair (grind(a,b,g),
// grind(b,a,g)) leaves a uniform g-wide slit centred on the shared contact
// surface (gap = 0: both solids share the exact middle surface, "lapped"
// masonry). Contacts closer than gap are separated to gap; fields further
// than gap leave a bit-exactly unchanged.
SdfPtr sdfGrind(SdfPtr a, SdfPtr b, float gap);
// Deep-copies the amount field and estimates its amplitude (4^3 probe).
SdfPtr sdfDisplace(SdfPtr child, const FieldNode* amount);
// Anchors are read from the points' stamp attributes by the builtin.
SdfPtr sdfInstance(SdfPtr source, std::vector<SdfInstanceAnchor> anchors, float k);
// One Voronoi cell of the deduped site set (fracture §8.11): the half-plane
// intersection dot(p - (pi+pj)/2, normalize(pj-pi)) for j != site, planes in
// ascending site order (deterministic; degenerate pairs skipped).
SdfPtr sdfVoronoiCell(const std::vector<glm::vec3>& sites, int32_t site);

// Unique-node count of the DAG (shared children counted once; PggTool summary).
size_t sdfNodeCount(const SdfNode& root);

// --- displace field machinery -------------------------------------------------

// At-point scalar evaluation of a field DAG in the SDF sample context
// (@P = p). Mirrors the buffered evaluator's formulas bit-for-bit (§6.3
// functions go through evalExprFuncBuf on 1-element buffers — the same code
// that constant-folds value calls; fbm/vnoise/random* call the same scalar
// noise/rng functions as evalFieldGenBuf).
Value evalFieldAtPoint(const FieldNode* node, const glm::vec3& p);

// One pass over the DAG: nullptr when evaluable in the SDF sample context,
// otherwise the offending node (@N/@index/@attribute/distance_to/ingroup —
// the caller reports E307 with its span and recovers with amount = 0).
const FieldNode* sdfContextViolation(const FieldNode* root);

// --- mesh <-> sdf bridges -----------------------------------------------------

// Deterministic AABB BVH over the mesh triangles (mid-split on the longest
// centroid axis, tie-break by triangle index). Closest-point queries return
// the nearest triangle by (distance, face, tri) — the pseudo-sign oracle of
// sdf_from_mesh. (distance_to will reuse this structure in a later stage.)
class MeshBvh {
public:
    struct Tri {
        glm::vec3 a, b, c;
        glm::vec3 normal;  // unit geometric normal
        int32_t face = 0;
        int32_t tri = 0;
    };

    void build(const Geo& geo);
    // false when the mesh has no triangles. outPoint/outNormal: closest point
    // and its triangle normal (pseudo-sign oracle).
    bool closest(const glm::vec3& p, float& outDist, glm::vec3& outPoint, glm::vec3& outNormal) const;

private:
    struct Node {
        glm::vec3 mn{0.0f}, mx{0.0f};
        int32_t left = -1, right = -1;  // < 0 => leaf [begin, begin+count)
        int32_t begin = 0, count = 0;
    };
    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
    int32_t buildRec(int32_t begin, int32_t end);
};

// Voxelizes the mesh into a Grid node: origin = mesh bbox - 3 voxels, sign by
// the pseudo-normal of the closest triangle. Slices sample in parallel
// (disjoint writes, bit-identical at any lane count).
SdfPtr sdfFromMeshVoxelize(const Geo& geo, float voxel, unsigned threads);

struct MeshFromSdfResult {
    GeoPtr mesh;
    bool axisOverflow = false;    // a grid axis would exceed the 4096-voxel guard (E306)
    bool boundaryTouch = false;   // a boundary-slab voxel is <= iso (W002)
};

// Samples the DAG over conservativeBBox() + 1 voxel margin and extracts the
// iso surface with marching cubes (fixed z,y,x cell order, grid-edge vertex
// dedup, linear interpolation with the |vb-va| guard). Output carries @P
// only (attribute barrier, §8.4).
MeshFromSdfResult meshFromSdfExtract(const SdfNode& root, float voxel, float iso, unsigned threads);

}  // namespace pgg
