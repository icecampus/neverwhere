#include "../../pch.h"

// §8.4 SDF nodes (stage E4), value level.
//
// The analytic primitives and CSG ops build the immutable SdfNode DAG
// directly. sdf_displace validates the amount field against the SDF sample
// context (E307; recovery: amount counts as 0) and deep-copies it into the
// node. sdf_instance_on_points reads the stamp attributes @scale/@orient
// with the same convention as realizeInstances and culls exactly against
// per-anchor world AABBs. sdf_from_mesh voxelizes through the BVH with the
// pseudo-sign; mesh_from_sdf samples the DAG and extracts marching cubes,
// guarding the 4096-voxel axis (E306) and warning when the surface reaches
// the grid boundary (W002).

#include <glm/gtc/quaternion.hpp>

#include "builtins.h"
#include "sdf.h"

namespace pgg {
namespace {

// Empty-field recovery value: a 2^3 grid of +infinity (no surface anywhere).
SdfPtr emptyGridSdf() {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::Grid;
    n->voxel = 1.0f;
    n->origin = glm::vec3(0.0f);
    n->dims = glm::ivec3(2);
    n->values = std::make_shared<const std::vector<float>>(8, std::numeric_limits<float>::infinity());
    return n;
}

// Stamp attribute readers with defaults (same convention as realizeInstances).
float stampF32(const ColumnData* col, size_t i, float def) {
    if (!col) return def;
    switch (col->index()) {
        case 0: return (*std::get<std::shared_ptr<const std::vector<float>>>(*col))[i];
        case 1: return static_cast<float>((*std::get<std::shared_ptr<const std::vector<int64_t>>>(*col))[i]);
        case 2: return (*std::get<std::shared_ptr<const std::vector<uint8_t>>>(*col))[i] ? 1.0f : 0.0f;
        default: return def;
    }
}

glm::vec4 stampVec4(const ColumnData* col, size_t i, glm::vec4 def) {
    if (!col) return def;
    if (col->index() == 5) return (*std::get<std::shared_ptr<const std::vector<glm::vec4>>>(*col))[i];
    return def;
}

Value opSdfDisplace(const BoundCall& bound, RunContext& run) {
    SdfPtr child = asSdf(bound.values[0]);
    const FieldNode* amount = bound.fields[1];
    if (amount) {
        // Sample context (§8.4): @P only. One validation pass over the DAG.
        if (const FieldNode* bad = sdfContextViolation(amount)) {
            run.report("E307", bad->span,
                       "field is not evaluable in the SDF sample context (only @P, constants, §6.3 "
                       "functions, fbm/vnoise/random*)",
                       "remove the geometry-dependent read (@N/@index/@attribute/distance_to/ingroup)");
            amount = nullptr;  // recovery: the child passes through unchanged
        }
    }
    return Value(sdfDisplace(std::move(child), amount));
}

Value opSdfInstanceOnPoints(const BoundCall& bound, RunContext& run) {
    const Geo& pts = *asGeo(bound.values[0]);
    SdfPtr source = asSdf(bound.values[1]);
    const float k = asF32(bound.values[2]);
    const std::optional<ColumnData> scaleCol = sampleAttrColumn(pts, "scale", Domain::Points);
    const std::optional<ColumnData> orientCol = sampleAttrColumn(pts, "orient", Domain::Points);
    std::vector<SdfInstanceAnchor> anchors;
    anchors.reserve(pts.pointCount());
    for (size_t i = 0; i < pts.pointCount(); ++i) {
        const float sc = stampF32(scaleCol ? &*scaleCol : nullptr, i, 1.0f);
        if (sc <= 0.0f) continue;  // non-positive scale has no SDF meaning
        const glm::vec4 o = stampVec4(orientCol ? &*orientCol : nullptr, i, glm::vec4(0, 0, 0, 1));
        glm::quat q(o.w, o.x, o.y, o.z);
        const float qlen = glm::length(q);
        q = qlen > 0.0f ? q / qlen : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        SdfInstanceAnchor an;
        an.pos = (*pts.positions)[i];
        an.orient = q;
        an.scale = sc;
        anchors.push_back(an);
    }
    return Value(sdfInstance(std::move(source), std::move(anchors), k));
}

Value opSdfFromMesh(const BoundCall& bound, RunContext& run) {
    const Geo& geo = *asGeo(bound.values[0]);
    const float voxel = asF32(bound.values[1]);
    if (!(voxel > 0.0f)) {
        run.report("E204", bound.span, "sdf_from_mesh expects a positive voxel size");
        return Value(emptyGridSdf());
    }
    if (geo.kind != GeoKind::Mesh || geo.faceCount() == 0) {
        run.report("E204", bound.span, "sdf_from_mesh expects a geo<mesh> with faces");
        return Value(emptyGridSdf());
    }
    glm::vec3 mn, mx;
    geoBBox(geo, mn, mx);
    for (int i = 0; i < 3; ++i) {
        const int axis = static_cast<int>(std::ceil(((mx[i] - mn[i]) + 6.0f * voxel) / voxel)) + 1;
        if (axis > 4096) {
            run.report("E306", bound.span,
                       "sdf voxel grid axis would exceed 4096 voxels",
                       "increase the voxel size or shrink the mesh");
            return Value(emptyGridSdf());
        }
    }
    return Value(sdfFromMeshVoxelize(geo, voxel, run.threads));
}

Value opMeshFromSdf(const BoundCall& bound, RunContext& run) {
    SdfPtr s = asSdf(bound.values[0]);
    const float voxel = asF32(bound.values[1]);
    const float iso = asF32(bound.values[2]);
    if (!(voxel > 0.0f)) {
        run.report("E204", bound.span, "mesh_from_sdf expects a positive voxel size");
        return Value(makeMesh({}, {}, {0}));
    }
    MeshFromSdfResult res = meshFromSdfExtract(*s, voxel, iso, run.threads);
    if (res.axisOverflow) {
        run.report("E306", bound.span,
                   "sdf voxel grid axis would exceed 4096 voxels",
                   "increase the voxel size or check the field's conservative bbox");
        return Value(res.mesh);
    }
    if (res.boundaryTouch && run.diagnostics) {
        // Runtime warning (no static clipping error at E4: the analytic bbox
        // is conservative; the displace amplitude is an estimate).
        run.diagnostics->push_back(Diagnostic{"W002", bound.span,
                                              "sdf surface reaches the voxel grid boundary; extraction may be "
                                              "clipped",
                                              "check the field's bbox/displace amplitude or the voxel size",
                                              true});
    }
    return Value(res.mesh);
}

}  // namespace

Value evalSdfBuiltin(const BoundCall& bound, RunContext& run) {
    const std::vector<Value>& v = bound.values;
    switch (bound.sig->id) {
        case BuiltinId::SdfSphere:
            return Value(sdfSphere(asF32(v[0])));
        case BuiltinId::SdfBox:
            return Value(sdfBox(asVec3(v[0])));
        case BuiltinId::SdfUnion:
            return Value(sdfUnion(asSdf(v[0]), asSdf(v[1])));
        case BuiltinId::SdfUnionSmooth:
            return Value(sdfUnionSmooth(asSdf(v[0]), asSdf(v[1]), asF32(v[2])));
        case BuiltinId::SdfSubtract:
            return Value(sdfSubtract(asSdf(v[0]), asSdf(v[1])));
        case BuiltinId::SdfSubtractSmooth:
            return Value(sdfSubtractSmooth(asSdf(v[0]), asSdf(v[1]), asF32(v[2])));
        case BuiltinId::SdfIntersect:
            return Value(sdfIntersect(asSdf(v[0]), asSdf(v[1])));
        case BuiltinId::SdfGrind:
            return Value(sdfGrind(asSdf(v[0]), asSdf(v[1]), asF32(v[2])));
        case BuiltinId::SdfDisplace:
            return opSdfDisplace(bound, run);
        case BuiltinId::SdfInstanceOnPoints:
            return opSdfInstanceOnPoints(bound, run);
        case BuiltinId::SdfFromMesh:
            return opSdfFromMesh(bound, run);
        case BuiltinId::MeshFromSdf:
            return opMeshFromSdf(bound, run);
        default:
            return Value();
    }
}

}  // namespace pgg
