#include "../../pch.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "builtins.h"

namespace pgg {
namespace {

// transform(geo, translate, rotate, scale): affine p' = R*(p*s) + t with
// Euler-degree rotation; normals get the inverse-transpose direction part.
Value opTransform(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const glm::vec3 translate = asVec3(bound.values[1]);
    const glm::quat rot(glm::radians(asVec3(bound.values[2])));
    const glm::vec3 scale = asVec3(bound.values[3]);

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
Value opComputeNormals(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& mode = asString(bound.values[1]);
    const size_t count = in.pointCount();

    if (in.faceCount() == 0) {
        // No surface: deterministic up normal (documented v0 fallback).
        return Value(withNormals(in, std::vector<glm::vec3>(count, glm::vec3(0, 1, 0))));
    }

    std::vector<glm::vec3> faceN(in.faceCount());
    for (size_t f = 0; f < in.faceCount(); ++f) faceN[f] = faceNormal(in, f);

    if (mode == "flat") {
        std::vector<glm::vec3> cornerN(in.cornerCount());
        for (size_t f = 0; f < in.faceCount(); ++f) {
            glm::vec3 n = faceN[f];
            const float len = glm::length(n);
            n = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
            for (int32_t c = (*in.faceOffsets)[f]; c < (*in.faceOffsets)[f + 1]; ++c)
                cornerN[static_cast<size_t>(c)] = n;
        }
        Geo out = in;
        AttrSet attrs = out.cornerAttrs ? *out.cornerAttrs : AttrSet{};
        attrs.columns["N"] = AttrColumn{std::make_shared<const std::vector<glm::vec3>>(std::move(cornerN))};
        out.cornerAttrs = std::make_shared<const AttrSet>(std::move(attrs));
        return Value(std::make_shared<const Geo>(std::move(out)));
    }

    std::vector<glm::vec3> nrm(count, glm::vec3(0.0f));
    const bool byAngle = mode == "by_angle";
    for (size_t f = 0; f < in.faceCount(); ++f) {
        const int32_t begin = (*in.faceOffsets)[f];
        const int32_t end = (*in.faceOffsets)[f + 1];
        for (int32_t c = begin; c < end; ++c) {
            const int32_t v = (*in.cornerVerts)[c];
            float w = 1.0f;
            if (byAngle) {
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
            nrm[v] += faceN[f] * w;
        }
    }
    for (glm::vec3& n : nrm) {
        const float len = glm::length(n);
        n = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
    }
    return Value(withNormals(in, std::move(nrm)));
}

}  // namespace

Value evalTransformBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Transform: return opTransform(bound);
        case BuiltinId::SetPosition: return opSetPosition(bound, run);
        case BuiltinId::Smooth: return opSmooth(bound);
        case BuiltinId::ComputeNormals: return opComputeNormals(bound);
        default: return Value();
    }
}

}  // namespace pgg
