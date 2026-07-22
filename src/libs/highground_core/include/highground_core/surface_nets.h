// Naive surface nets over the sampled cliff field: one vertex per sign-changing
// voxel (mean of edge zero crossings), one quad per sign-changing grid edge.
// Watertight by construction as long as the field is positive on the grid border.
#pragma once

#include "cliff_field.h"

#include <cstdint>
#include <vector>

namespace cliff {

struct MeshVertex {
    float px, py, pz;
    float nx, ny, nz;
    float groove; // carve depth, 0 on untouched surface
};

struct Mesh {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices; // triangles
};

struct ExtractStats {
    int signVoxels = 0;
    double vertexMs = 0.0;
    double quadMs = 0.0;
};

struct RegularizeStats {
    int saddleFaces = 0;   // checkerboard faces found (first pass)
    int flips = 0;         // corner sign flips applied (all passes)
    int passes = 0;
    int remaining = 0;     // saddles left after the last pass (must be 0)
};

// Resolves checkerboard grid faces (saddles) before extraction: naive surface
// nets are manifold iff no grid face has an alternating +−+− sign pattern.
// Each saddle is collapsed by flipping the sign of the weakest corner of the
// diagonal that disagrees with the field value at the face center; flips move
// the surface by less than a cell and iterate to a fixpoint.
void regularizeSigns(const CliffField& field, std::vector<float>& samples,
    RegularizeStats* stats = nullptr);

Mesh extractSurfaceNets(const CliffField& field, const std::vector<float>& samples,
    ExtractStats* stats = nullptr);

struct WatertightReport {
    int halfEdges = 0;
    int undirectedEdges = 0;
    int badEdges = 0; // edges not appearing exactly once per direction
    int edgesWith1Half = 0;   // cracks (should not happen in surface nets)
    int edgesWith3Half = 0;   // saddle + crack mix
    int edgesWith4Plus = 0;   // saddle faces (non-manifold)
    int degenerateTriangles = 0;
    bool ok() const { return undirectedEdges > 0 && badEdges == 0; }
};

WatertightReport checkWatertight(const Mesh& mesh);

} // namespace cliff
