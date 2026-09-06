#include "../../pch.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "builtins.h"
#include "parallel.h"

namespace pgg {
namespace {

// transform(geo, translate, rotate, scale): affine p' = R*(p*s) + t with
// Euler-degree rotation; normals get the inverse-transpose direction part.
Value opTransform(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const glm::vec3 translate = asVec3(bound.values[1]);
    const glm::quat rot(glm::radians(asVec3(bound.values[2])));
    const glm::vec3 scale = asVec3(bound.values[3]);
    if (in.kind == GeoKind::Instances) {
        // A stamp holds one scalar @scale: a non-uniform scale has no
        // representation on geo<instances> and would silently only move the
        // anchors (E611). Realize first, or scale the source.
        const bool uniform = glm::abs(scale.x - scale.y) < 1e-6f && glm::abs(scale.x - scale.z) < 1e-6f;
        if (!uniform) {
            run.report("E611", bound.span,
                       "transform: non-uniform scale on geo<instances> cannot be stamped into @scale",
                       "use a uniform scale, scale the source geometry, or realize() first");
            return Value(asGeo(bound.values[0]));
        }
    }

    std::vector<glm::vec3> pos(in.pointCount());
    for (size_t i = 0; i < pos.size(); ++i)
        pos[i] = rot * ((*in.positions)[i] * scale) + translate;

    Geo out = in;
    out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    if (in.normals) {
        const bool safeScale = glm::abs(scale.x) > 1e-12f && glm::abs(scale.y) > 1e-12f &&
                               glm::abs(scale.z) > 1e-12f;
        std::vector<glm::vec3> nrm(in.pointCount());
        for (size_t i = 0; i < nrm.size(); ++i) {
            // Inverse-transpose: R * S^-1 for the direction part.
            const glm::vec3 n = safeScale ? (*in.normals)[i] / scale : (*in.normals)[i];
            const glm::vec3 rn = rot * n;
            const float len = glm::length(rn);
            nrm[i] = len > 0.0f ? rn / len : glm::vec3(0, 1, 0);
        }
        out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nrm));
    }
    // Tagged attributes ride along by typeinfo (v1.14): vector — rotate+scale,
    // normal — inverse-transpose, point — full affine, quaternion — compose.
    // Untagged columns are plain data and stay untouched.
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* as = in.attrs(d);
        if (!as) continue;
        std::optional<AttrSet> copy;
        for (const auto& [name, col] : as->columns) {
            if (col.typeInfo == AttrTypeInfo::None) continue;
            ColumnData moved = transformColumn(col, rot, scale, translate);
            if (moved.index() != col.data.index()) continue;  // tag on a non-vector column: ignored
            if (!copy) copy = *as;
            copy->columns[name] = AttrColumn{std::move(moved), col.typeInfo};
        }
        if (copy) {
            auto ptr = std::make_shared<const AttrSet>(std::move(*copy));
            switch (d) {
                case Domain::Points: out.pointAttrs = std::move(ptr); break;
                case Domain::Corners: out.cornerAttrs = std::move(ptr); break;
                case Domain::Faces: out.faceAttrs = std::move(ptr); break;
                case Domain::Detail: out.detailAttrs = std::move(ptr); break;
            }
        }
    }
    // geo<instances>: the rotation must reach the realized copies, so the
    // @orient/@scale stamps are materialized (identity when absent) and
    // composed — @orient is Quaternion-tagged and already rotated above when
    // present; the (uniform, see E611) scale multiplies @scale.
    if (in.kind == GeoKind::Instances) {
        AttrSet attrs;
        if (out.pointAttrs) attrs = *out.pointAttrs;
        bool changed = false;
        if (const AttrColumn* col = attrs.find("orient"); !col) {
            attrs.columns["orient"] = AttrColumn{std::make_shared<const std::vector<glm::vec4>>(
                                                     in.pointCount(), glm::vec4(rot.x, rot.y, rot.z, rot.w)),
                                                 AttrTypeInfo::Quaternion};
            changed = true;
        } else if (col->typeInfo != AttrTypeInfo::Quaternion && col->data.index() == 5) {
            // Untagged vec4 named orient (legacy data): treat as a quaternion.
            const auto& src = *std::get<std::shared_ptr<const std::vector<glm::vec4>>>(col->data);
            std::vector<glm::vec4> o(src.size());
            for (size_t i = 0; i < o.size(); ++i) {
                const glm::quat q = rot * glm::quat(src[i].w, src[i].x, src[i].y, src[i].z);
                o[i] = glm::vec4(q.x, q.y, q.z, q.w);
            }
            attrs.columns["orient"] = AttrColumn{std::make_shared<const std::vector<glm::vec4>>(std::move(o)),
                                                 AttrTypeInfo::Quaternion};
            changed = true;
        }
        const float stampScale = scale.x;  // uniform — guaranteed by the E611 guard above
        if (const AttrColumn* col = attrs.find("scale"); col && col->data.index() == 0) {
            if (glm::abs(stampScale - 1.0f) > 1e-6f) {
                const auto& src = *std::get<std::shared_ptr<const std::vector<float>>>(col->data);
                std::vector<float> sc(src.size());
                for (size_t i = 0; i < sc.size(); ++i) sc[i] = src[i] * stampScale;
                attrs.columns["scale"] = AttrColumn{std::make_shared<const std::vector<float>>(std::move(sc))};
                changed = true;
            }
        } else if (!col) {
            attrs.columns["scale"] = AttrColumn{
                std::make_shared<const std::vector<float>>(in.pointCount(), stampScale)};
            changed = true;
        }
        if (changed) out.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// set_position(geo, offset, pos, where): the main displace node — per-point
// replace or offset of @P under a mask, all fields on the points domain.
Value opSetPosition(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const size_t count = in.pointCount();

    ConstBufferPtr offset;
    if (bound.fields[1]) {
        offset = convertBuffer(evalField(bound.fields[1], in, Domain::Points, run), ScalarType::Vec3);
    } else {
        offset = makeConstBuffer(Value(glm::vec3(0.0f)), count);
    }
    ConstBufferPtr where;
    if (bound.fields[3]) {
        where = convertBuffer(evalField(bound.fields[3], in, Domain::Points, run), ScalarType::Bool);
    } else {
        where = makeConstBuffer(Value(true), count);
    }
    ConstBufferPtr pos;
    if (bound.fields[2]) {
        pos = convertBuffer(evalField(bound.fields[2], in, Domain::Points, run), ScalarType::Vec3);
    }

    const auto& off = std::get<Vec3Buf>(*offset);
    const auto& mask = std::get<BoolBuf>(*where);
    std::vector<glm::vec3> out(count);
    for (size_t i = 0; i < count; ++i) {
        if (!mask[i]) {
            out[i] = (*in.positions)[i];
        } else if (pos) {
            out[i] = std::get<Vec3Buf>(*pos)[i];
        } else {
            out[i] = (*in.positions)[i] + off[i];
        }
    }
    return Value(withPositions(in, std::move(out)));
}

// smooth(geo, iterations, factor): Laplacian relaxation over face-adjacency
// (Jacobi: all updates read the same snapshot). Topology is unchanged.
Value opSmooth(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const int iterations = static_cast<int>(asInt(bound.values[1]));
    const float factor = asF32(bound.values[2]);

    const size_t count = in.pointCount();
    std::vector<std::vector<int32_t>> neighbors(count);
    if (in.cornerVerts && in.faceOffsets) {
        for (size_t f = 0; f < in.faceCount(); ++f) {
            const int32_t begin = (*in.faceOffsets)[f];
            const int32_t end = (*in.faceOffsets)[f + 1];
            for (int32_t c = begin; c < end; ++c) {
                const int32_t v = (*in.cornerVerts)[c];
                const int32_t prev = (*in.cornerVerts)[c > begin ? c - 1 : end - 1];
                const int32_t next = (*in.cornerVerts)[c + 1 < end ? c + 1 : begin];
                neighbors[v].push_back(prev);
                neighbors[v].push_back(next);
            }
        }
    }
    std::vector<glm::vec3> pos(*in.positions);
    std::vector<glm::vec3> next(count);
    for (int it = 0; it < iterations; ++it) {
        for (size_t v = 0; v < count; ++v) {
            std::vector<int32_t>& nb = neighbors[v];
            if (nb.empty()) {
                next[v] = pos[v];
                continue;
            }
            std::sort(nb.begin(), nb.end());
            nb.erase(std::unique(nb.begin(), nb.end()), nb.end());
            glm::vec3 avg(0.0f);
            for (int32_t u : nb) avg += pos[u];
            avg /= static_cast<float>(nb.size());
            next[v] = pos[v] + (avg - pos[v]) * factor;
        }
        pos = next;
    }
    return Value(withPositions(in, std::move(pos)));
}

// compute_normals(geo, mode): smooth = area-weighted vertex normals;
// by_angle = corner-angle-weighted; flat = per-corner "N" attribute
// (faceted look; points @N is left untouched in flat mode).
//
// Parallel structure (N7): face normals per face (disjoint slots), then a
// per-point gather over sequentially built incident-corner lists — the gather
// keeps the original ascending-corner accumulation order per point (bit-
// identical to the old face loop) while writing disjoint point slots.
Value opComputeNormals(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& mode = asString(bound.values[1]);
    const size_t count = in.pointCount();
    const unsigned threads = run.threads;

    if (in.faceCount() == 0) {
        // No surface: deterministic up normal (documented v0 fallback).
        return Value(withNormals(in, std::vector<glm::vec3>(count, glm::vec3(0, 1, 0))));
    }

    std::vector<glm::vec3> faceN(in.faceCount());
    parallelFor(in.faceCount(), threads, [&](size_t s, size_t e) {
        for (size_t f = s; f < e; ++f) faceN[f] = faceNormal(in, f);
    });

    if (mode == "auto") {
        // Auto-smooth (v1.24): per-corner normals, smooth across edges whose
        // dihedral angle is below `angle`, hard above it — one call for a
        // mesh that mixes curved sheets (leaf, barrel, rounded bevel) with
        // sharp architecture. A corner averages the normals of the faces
        // meeting at its vertex whose normal lies within `angle` of its own
        // face's normal (area-weighted through faceNormal's magnitude).
        const float cosLimit = std::cos(glm::radians(std::clamp(asF32(bound.values[2]), 0.0f, 180.0f)));
        const size_t nc = in.cornerCount();
        std::vector<int32_t> faceOf(nc);
        std::vector<int32_t> incidentBegin(count + 1, 0);
        for (int32_t v : *in.cornerVerts) incidentBegin[static_cast<size_t>(v) + 1] += 1;
        for (size_t v = 0; v < count; ++v) incidentBegin[v + 1] += incidentBegin[v];
        std::vector<int32_t> incidentCorners(nc);
        {
            std::vector<int32_t> cursor(incidentBegin.begin(), incidentBegin.end() - 1);
            for (size_t f = 0; f < in.faceCount(); ++f)
                for (int32_t c = (*in.faceOffsets)[f]; c < (*in.faceOffsets)[f + 1]; ++c) {
                    faceOf[static_cast<size_t>(c)] = static_cast<int32_t>(f);
                    const int32_t v = (*in.cornerVerts)[static_cast<size_t>(c)];
                    incidentCorners[static_cast<size_t>(cursor[static_cast<size_t>(v)]++)] = c;
                }
        }
        std::vector<glm::vec3> unitN(in.faceCount());
        for (size_t f = 0; f < in.faceCount(); ++f) {
            const float len = glm::length(faceN[f]);
            unitN[f] = len > 0.0f ? faceN[f] / len : glm::vec3(0, 1, 0);
        }
        std::vector<glm::vec3> cornerN(nc);
        parallelFor(nc, threads, [&](size_t s, size_t e) {
            for (size_t c = s; c < e; ++c) {
                const int32_t f = faceOf[c];
                const int32_t v = (*in.cornerVerts)[c];
                glm::vec3 sum(0.0f);
                for (int32_t k = incidentBegin[static_cast<size_t>(v)]; k < incidentBegin[static_cast<size_t>(v) + 1]; ++k) {
                    const int32_t g = faceOf[static_cast<size_t>(incidentCorners[static_cast<size_t>(k)])];
                    if (g == f || glm::dot(unitN[static_cast<size_t>(f)], unitN[static_cast<size_t>(g)]) >= cosLimit)
                        sum += faceN[static_cast<size_t>(g)];
                }
                const float len = glm::length(sum);
                cornerN[c] = len > 0.0f ? sum / len : unitN[static_cast<size_t>(f)];
            }
        });
        Geo out = in;
        AttrSet attrs = out.cornerAttrs ? *out.cornerAttrs : AttrSet{};
        attrs.columns["N"] = AttrColumn{std::make_shared<const std::vector<glm::vec3>>(std::move(cornerN)),
                                        AttrTypeInfo::Normal};
        out.cornerAttrs = std::make_shared<const AttrSet>(std::move(attrs));
        return Value(std::make_shared<const Geo>(std::move(out)));
    }

    if (mode == "flat") {
        std::vector<glm::vec3> cornerN(in.cornerCount());
        parallelFor(in.faceCount(), threads, [&](size_t s, size_t e) {
            for (size_t f = s; f < e; ++f) {
                glm::vec3 n = faceN[f];
                const float len = glm::length(n);
                n = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
                for (int32_t c = (*in.faceOffsets)[f]; c < (*in.faceOffsets)[f + 1]; ++c)
                    cornerN[static_cast<size_t>(c)] = n;
            }
        });
        Geo out = in;
        AttrSet attrs = out.cornerAttrs ? *out.cornerAttrs : AttrSet{};
        attrs.columns["N"] = AttrColumn{std::make_shared<const std::vector<glm::vec3>>(std::move(cornerN)),
                                        AttrTypeInfo::Normal};
        out.cornerAttrs = std::make_shared<const AttrSet>(std::move(attrs));
        return Value(std::make_shared<const Geo>(std::move(out)));
    }

    // Incident-corner lists per point + the face of every corner (sequential
    // build, ascending corner order per point = the old accumulation order).
    const size_t cornerCount = in.cornerCount();
    std::vector<int32_t> faceOf(cornerCount);
    std::vector<int32_t> incidentBegin(count + 1, 0);
    for (int32_t v : *in.cornerVerts) incidentBegin[static_cast<size_t>(v) + 1] += 1;
    for (size_t v = 0; v < count; ++v) incidentBegin[v + 1] += incidentBegin[v];
    std::vector<int32_t> incidentCorners(cornerCount);
    {
        std::vector<int32_t> cursor(incidentBegin.begin(), incidentBegin.end() - 1);
        for (size_t f = 0; f < in.faceCount(); ++f) {
            const int32_t begin = (*in.faceOffsets)[f];
            const int32_t end = (*in.faceOffsets)[f + 1];
            for (int32_t c = begin; c < end; ++c) {
                faceOf[static_cast<size_t>(c)] = static_cast<int32_t>(f);
                const int32_t v = (*in.cornerVerts)[static_cast<size_t>(c)];
                incidentCorners[static_cast<size_t>(cursor[static_cast<size_t>(v)]++)] = c;
            }
        }
    }

    std::vector<glm::vec3> nrm(count);
    const bool byAngle = mode == "by_angle";
    parallelFor(count, threads, [&](size_t s, size_t e) {
        for (size_t v = s; v < e; ++v) {
            glm::vec3 sum(0.0f);
            for (int32_t k = incidentBegin[v]; k < incidentBegin[v + 1]; ++k) {
                const int32_t c = incidentCorners[static_cast<size_t>(k)];
                const int32_t f = faceOf[static_cast<size_t>(c)];
                float w = 1.0f;
                if (byAngle) {
                    const int32_t begin = (*in.faceOffsets)[static_cast<size_t>(f)];
                    const int32_t end = (*in.faceOffsets)[static_cast<size_t>(f) + 1];
                    const glm::vec3& p = (*in.positions)[v];
                    const glm::vec3 a = (*in.positions)[(*in.cornerVerts)[c > begin ? c - 1 : end - 1]] - p;
                    const glm::vec3 b = (*in.positions)[(*in.cornerVerts)[c + 1 < end ? c + 1 : begin]] - p;
                    const float la = glm::length(a);
                    const float lb = glm::length(b);
                    if (la > 0.0f && lb > 0.0f) {
                        const glm::vec3 ua = a / la;
                        const glm::vec3 ub = b / lb;
                        w = std::atan2(glm::length(glm::cross(ua, ub)), glm::dot(ua, ub));
                    } else {
                        w = 0.0f;
                    }
                }
                sum += faceN[static_cast<size_t>(f)] * w;
            }
            const float len = glm::length(sum);
            nrm[v] = len > 0.0f ? sum / len : glm::vec3(0, 1, 0);
        }
    });
    return Value(withNormals(in, std::move(nrm)));
}

}  // namespace

Value evalTransformBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Transform: return opTransform(bound, run);
        case BuiltinId::SetPosition: return opSetPosition(bound, run);
        case BuiltinId::Smooth: return opSmooth(bound);
        case BuiltinId::ComputeNormals: return opComputeNormals(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
