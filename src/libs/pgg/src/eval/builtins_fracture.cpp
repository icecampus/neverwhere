#include "../../pch.h"

// §8.3 islands and §8.11 fracture nodes (stage E7), value level.
//
// islands tags the connected components (computeIslands) as an int @island_id
// column on the faces. fracture is the SDF-Voronoi model v0 (spec §8.11):
// deduped cut sites -> one voxelization of the input -> per site the analytic
// half-plane cell intersected with the mesh SDF -> marching cubes per cell ->
// rigid merge in site order with a dense @island_id per produced piece. The
// model has no stochastic decisions (bisector ties are value-symmetric); the
// spec-required rng parameter is accepted and reserved for future models.

#include "builtins.h"
#include "fracture.h"
#include "sdf.h"

namespace pgg {
namespace {

// Returns a copy of the mesh with the given int64 @island_id column on faces.
GeoPtr writeFaceIslandIds(const Geo& mesh, std::shared_ptr<const std::vector<int64_t>> ids) {
    AttrSet attrs;
    if (const AttrSet* src = mesh.attrs(Domain::Faces)) attrs = *src;
    attrs.columns["island_id"] = AttrColumn{ColumnData(std::move(ids))};
    return withAttrs(mesh, Domain::Faces, std::make_shared<const AttrSet>(std::move(attrs)));
}

Value opIslands(const BoundCall& bound, RunContext& run) {
    const Geo& geo = *asGeo(bound.values[0]);
    if (geo.kind != GeoKind::Mesh) {
        run.report("E204", bound.span, "islands expects a geo<mesh>");
        return bound.values[0];
    }
    size_t count = 0;
    const std::vector<int32_t> ids = computeIslands(geo, count);
    return Value(writeFaceIslandIds(geo, std::make_shared<const std::vector<int64_t>>(ids.begin(), ids.end())));
}

Value opFracture(const BoundCall& bound, RunContext& run) {
    const Geo& geo = *asGeo(bound.values[0]);
    const Geo& planes = *asGeo(bound.values[1]);
    // bound.values[2] (rng): reserved, unused by the v0 model (§8.11).
    if (geo.kind != GeoKind::Mesh || geo.faceCount() == 0) {
        run.report("E608", bound.span, "fracture expects a geo<mesh> with faces");
        return Value(makeMesh({}, {}, {0}));
    }
    // Dedupe cut sites (exact match, keep-first).
    std::vector<glm::vec3> sites;
    if (planes.positions) {
        for (const glm::vec3& p : *planes.positions)
            if (std::find(sites.begin(), sites.end(), p) == sites.end()) sites.push_back(p);
    }
    if (sites.empty()) {
        run.report("E608", bound.span, "fracture needs at least one cut site (planes has 0 points)");
        return Value(makeMesh({}, {}, {0}));
    }
    if (sites.size() == 1) {
        // One cell, no cut: the input mesh tagged @island_id = 0.
        return Value(writeFaceIslandIds(
            geo, std::make_shared<const std::vector<int64_t>>(geo.faceCount(), int64_t(0))));
    }

    glm::vec3 mn, mx;
    geoBBox(geo, mn, mx);
    const float maxExtent = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
    const float voxel = maxExtent / 128.0f;  // v0 auto resolution (§8.11)
    if (!(voxel > 0.0f)) {
        run.report("E608", bound.span, "fracture cannot derive a voxel size from a degenerate mesh bbox");
        return Value(makeMesh({}, {}, {0}));
    }
    for (int i = 0; i < 3; ++i) {
        const int axis = static_cast<int>(std::ceil(((mx[i] - mn[i]) + 6.0f * voxel) / voxel)) + 1;
        if (axis > 4096) {
            run.report("E306", bound.span,
                       "sdf voxel grid axis would exceed 4096 voxels",
                       "shrink the mesh");
            return Value(makeMesh({}, {}, {0}));
        }
    }
    const SdfPtr meshSdf = sdfFromMeshVoxelize(geo, voxel, run.threads);

    // One marching-cubes extraction per cell; empty pieces (site outside the
    // mesh) are skipped. Produced pieces keep the ascending site order.
    std::vector<GeoPtr> pieces;
    for (size_t i = 0; i < sites.size(); ++i) {
        const SdfPtr pieceSdf = sdfIntersect(meshSdf, sdfVoronoiCell(sites, static_cast<int32_t>(i)));
        MeshFromSdfResult res = meshFromSdfExtract(*pieceSdf, voxel, 0.0f, run.threads);
        if (res.axisOverflow) {
            run.report("E608", bound.span, "fracture piece extraction exceeded the voxel grid guard");
            return Value(makeMesh({}, {}, {0}));
        }
        if (!res.mesh || res.mesh->faceCount() == 0) continue;
        pieces.push_back(res.mesh);
    }
    std::vector<GeoPtr> tagged;
    tagged.reserve(pieces.size());
    for (size_t i = 0; i < pieces.size(); ++i)
        tagged.push_back(writeFaceIslandIds(
            *pieces[i], std::make_shared<const std::vector<int64_t>>(pieces[i]->faceCount(),
                                                                     static_cast<int64_t>(i))));
    return Value(mergeMeshPieces(tagged));
}

}  // namespace

Value evalFractureBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Islands:
            return opIslands(bound, run);
        case BuiltinId::Fracture:
            return opFracture(bound, run);
        default:
            return Value();
    }
}

}  // namespace pgg
