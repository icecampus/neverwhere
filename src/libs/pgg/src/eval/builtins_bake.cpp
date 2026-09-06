// Baking ops (spec v1.24): bake_ao — ambient occlusion written as an f32
// attribute by ray casting against the geometry itself. Deterministic
// (stratified hemisphere directions from the geometry's rng), parallel over
// the target elements, a small median-split BVH over the fan-triangulated
// faces. Rays start slightly off the surface along the sampling normal; hits
// from behind (the ray is inside the geometry it left) are ignored, which is
// what keeps welded convex corners from occluding themselves.
#include "../../pch.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "builtins.h"
#include "parallel.h"
#include "rng.h"

namespace pgg {

namespace {

struct Tri {
    glm::vec3 a, b, c;
    glm::vec3 n;  // unnormalised face normal (winding)
};

struct BvhNode {
    glm::vec3 mn, mx;
    int32_t left = -1, right = -1;  // children, or
    int32_t begin = 0, count = 0;   // leaf range into `order`
};

class TriBvh {
public:
    explicit TriBvh(std::vector<Tri> tris) : tris_(std::move(tris)) {
        order_.resize(tris_.size());
        std::iota(order_.begin(), order_.end(), 0);
        if (!tris_.empty()) build(0, static_cast<int32_t>(tris_.size()));
    }

    // Nearest front-facing hit along (o, d) within (tmin, tmax); returns t or a negative value.
    float hit(const glm::vec3& o, const glm::vec3& d, float tmin, float tmax) const {
        if (nodes_.empty()) return -1.0f;
        float best = -1.0f;
        int32_t stack[64];
        int sp = 0;
        stack[sp++] = 0;
        const glm::vec3 invD(1.0f / (d.x != 0.0f ? d.x : 1e-30f), 1.0f / (d.y != 0.0f ? d.y : 1e-30f),
                             1.0f / (d.z != 0.0f ? d.z : 1e-30f));
        while (sp > 0) {
            const BvhNode& n = nodes_[static_cast<size_t>(stack[--sp])];
            if (!slab(n, o, invD, tmin, best > 0.0f ? best : tmax)) continue;
            if (n.count > 0) {
                for (int32_t i = n.begin; i < n.begin + n.count; ++i) {
                    const Tri& t = tris_[static_cast<size_t>(order_[static_cast<size_t>(i)])];
                    if (glm::dot(t.n, d) >= 0.0f) continue;  // back face: leaving our own volume
                    const float tt = intersect(t, o, d);
                    if (tt > tmin && tt < tmax && (best < 0.0f || tt < best)) best = tt;
                }
            } else {
                stack[sp++] = n.left;
                stack[sp++] = n.right;
            }
        }
        return best;
    }

private:
    static bool slab(const BvhNode& n, const glm::vec3& o, const glm::vec3& invD, float tmin, float tmax) {
        float t0 = tmin, t1 = tmax;
        for (int k = 0; k < 3; ++k) {
            float a = (n.mn[k] - o[k]) * invD[k], b = (n.mx[k] - o[k]) * invD[k];
            if (a > b) std::swap(a, b);
            t0 = std::max(t0, a);
            t1 = std::min(t1, b);
            if (t1 < t0) return false;
        }
        return true;
    }
    static float intersect(const Tri& t, const glm::vec3& o, const glm::vec3& d) {
        // Moller-Trumbore.
        const glm::vec3 e1 = t.b - t.a, e2 = t.c - t.a;
        const glm::vec3 p = glm::cross(d, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 1e-12f) return -1.0f;
        const float inv = 1.0f / det;
        const glm::vec3 s = o - t.a;
        const float u = glm::dot(s, p) * inv;
        if (u < 0.0f || u > 1.0f) return -1.0f;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(d, q) * inv;
        if (v < 0.0f || u + v > 1.0f) return -1.0f;
        return glm::dot(e2, q) * inv;
    }
    int32_t build(int32_t begin, int32_t end) {
        BvhNode node;
        node.mn = glm::vec3(1e30f);
        node.mx = glm::vec3(-1e30f);
        for (int32_t i = begin; i < end; ++i) {
            const Tri& t = tris_[static_cast<size_t>(order_[static_cast<size_t>(i)])];
            node.mn = glm::min(node.mn, glm::min(t.a, glm::min(t.b, t.c)));
            node.mx = glm::max(node.mx, glm::max(t.a, glm::max(t.b, t.c)));
        }
        const int32_t id = static_cast<int32_t>(nodes_.size());
        nodes_.push_back(node);
        if (end - begin <= 4) {
            nodes_[static_cast<size_t>(id)].begin = begin;
            nodes_[static_cast<size_t>(id)].count = end - begin;
            return id;
        }
        const glm::vec3 ext = node.mx - node.mn;
        const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
        const int32_t mid = (begin + end) / 2;
        std::nth_element(order_.begin() + begin, order_.begin() + mid, order_.begin() + end, [&](int32_t x, int32_t y) {
            const Tri& tx = tris_[static_cast<size_t>(x)];
            const Tri& ty = tris_[static_cast<size_t>(y)];
            return (tx.a[axis] + tx.b[axis] + tx.c[axis]) < (ty.a[axis] + ty.b[axis] + ty.c[axis]);
        });
        const int32_t l = build(begin, mid);
        const int32_t r = build(mid, end);
        nodes_[static_cast<size_t>(id)].left = l;
        nodes_[static_cast<size_t>(id)].right = r;
        return id;
    }

    std::vector<Tri> tris_;
    std::vector<int32_t> order_;
    std::vector<BvhNode> nodes_;
};

std::vector<Tri> fanTriangles(const Geo& g) {
    std::vector<Tri> out;
    const auto& P = *g.positions;
    const auto& CV = *g.cornerVerts;
    const auto& FO = *g.faceOffsets;
    for (size_t f = 0; f < g.faceCount(); ++f) {
        const int32_t begin = FO[f], end = FO[f + 1];
        if (end - begin < 3) continue;
        const glm::vec3 n = faceNormal(g, f);
        for (int32_t c = begin + 1; c + 1 < end; ++c)
            out.push_back(Tri{P[static_cast<size_t>(CV[static_cast<size_t>(begin)])], P[static_cast<size_t>(CV[static_cast<size_t>(c)])],
                              P[static_cast<size_t>(CV[static_cast<size_t>(c + 1)])], n});
    }
    return out;
}

// Cosine-weighted hemisphere direction around n from two uniform numbers.
glm::vec3 hemisphereDir(const glm::vec3& n, float u1, float u2) {
    const float r = std::sqrt(u1);
    const float phi = 6.2831853f * u2;
    const glm::vec3 local(r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0f, 1.0f - u1)));
    const glm::vec3 t = glm::normalize(std::abs(n.x) < 0.9f ? glm::cross(n, glm::vec3(1, 0, 0)) : glm::cross(n, glm::vec3(0, 1, 0)));
    const glm::vec3 b = glm::cross(n, t);
    return glm::normalize(t * local.x + b * local.y + n * local.z);
}

// --- bake_ao(geo, rays, distance, rng, domain, name) ----------------------------
Value opBakeAo(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const int rays = static_cast<int>(std::max<int64_t>(1, asInt(bound.values[1])));
    float distance = asF32(bound.values[2]);
    const Rng rng = asRng(bound.values[3]);
    std::string domainName = asString(bound.values[4]);
    const std::string name = asString(bound.values[5]);
    if (in.kind != GeoKind::Mesh || !in.faceOffsets || !in.cornerVerts || in.faceCount() == 0) {
        run.report("E204", bound.span, "bake_ao needs a geo<mesh> with faces",
                   in.kind == GeoKind::Instances ? "realize() first" : "points cast no shadows on each other");
        return Value(inPtr);
    }
    const bool cornerN = in.cornerAttrs && in.cornerAttrs->find("N") &&
                         std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(in.cornerAttrs->find("N")->data);
    if (domainName == "auto") domainName = cornerN ? "corners" : "points";
    const Domain domain = domainName == "corners" ? Domain::Corners : Domain::Points;
    if (domainName != "corners" && domainName != "points") {
        run.report("E204", bound.span, "bake_ao: domain must be points, corners or auto");
        return Value(inPtr);
    }
    glm::vec3 mn, mx;
    geoBBox(in, mn, mx);
    const float diag = glm::length(mx - mn);
    if (!(distance > 0.0f)) distance = diag * 0.25f;
    if (!(distance > 0.0f) || !std::isfinite(distance)) return Value(inPtr);
    const float eps = std::max(1e-6f, diag * 1e-4f);

    // Sampling normals on the target domain.
    std::shared_ptr<const std::vector<glm::vec3>> normals;
    if (domain == Domain::Corners && cornerN) {
        normals = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(in.cornerAttrs->find("N")->data);
    } else if (domain == Domain::Points && in.normals && in.normals->size() == in.pointCount()) {
        normals = in.normals;
    } else {
        normals = derivedNormals(in, domain);
    }
    const size_t count = in.elementCount(domain);
    if (!normals || normals->size() != count) return Value(inPtr);

    const TriBvh bvh(fanTriangles(in));
    const auto& P = *in.positions;
    const auto& CV = *in.cornerVerts;
    std::vector<float> ao(count, 1.0f);
    const int strata = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(rays))));
    parallelFor(count, run.threads, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
            glm::vec3 n = (*normals)[i];
            const float ln = glm::length(n);
            n = ln > 1e-12f ? n / ln : glm::vec3(0, 1, 0);
            const glm::vec3 p = domain == Domain::Corners ? P[static_cast<size_t>(CV[i])] : P[i];
            const glm::vec3 o = p + n * eps;
            float occluded = 0.0f;
            for (int k = 0; k < rays; ++k) {
                // Stratified jittered (u1, u2) per (element, ray) — deterministic.
                const float j1 = rngF32(rng, static_cast<uint64_t>(i), static_cast<uint32_t>(k * 2));
                const float j2 = rngF32(rng, static_cast<uint64_t>(i), static_cast<uint32_t>(k * 2 + 1));
                const float u1 = (static_cast<float>(k % strata) + j1) / static_cast<float>(strata);
                const float u2 = (static_cast<float>(k / strata) + j2) / static_cast<float>(strata);
                const glm::vec3 d = hemisphereDir(n, std::min(u1, 0.99999f), std::min(u2, 0.99999f));
                const float t = bvh.hit(o, d, eps, distance);
                if (t > 0.0f) occluded += 1.0f - (t / distance) * (t / distance);  // near hits darken more
            }
            ao[i] = std::clamp(1.0f - occluded / static_cast<float>(rays), 0.0f, 1.0f);
        }
    });
    AttrSet attrs = in.attrs(domain) ? *in.attrs(domain) : AttrSet{};
    attrs.columns[name] = AttrColumn{std::make_shared<const std::vector<float>>(std::move(ao)), AttrTypeInfo::None};
    return Value(withAttrs(in, domain, std::make_shared<const AttrSet>(std::move(attrs))));
}

}  // namespace

Value evalBakeBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::BakeAo: return opBakeAo(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
