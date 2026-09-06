#include "../../pch.h"

// §8.3 sweep (v1.21): a tube/beam/rail mesh from a path of points and a 2D
// profile of points — the curve tool PGG lacks an edge domain for. The path is
// the ordered @index sequence of a geo<points> (mesh_line, a jittered line,
// any point set the author ordered); the profile is a closed loop of points in
// the XY plane (circle(), or hand-placed points), also ordered by @index.
//
// Frames: unit tangents by central differences (one-sided at open ends, wrapped
// when closed), the profile's X axis parallel-transported along the path from
// a reference least aligned with the first tangent — no twist accumulates on
// smooth paths and the result is deterministic. Per path point @scale (f32)
// scales the profile when present. Ring points inherit the path point's
// columns/groups (a per-point @tint on the path colours the tube); profile
// columns are ignored. Open paths get planar caps (convex profile -> one
// polygon, else ear-clipped) unless cap = false. Quads face outward for a CCW
// profile.

#include <array>
#include <cmath>

#include "builtins.h"
#include "topology_util.h"

namespace pgg {
namespace {

using namespace topo;
using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;

Value opCircle(const BoundCall& bound, RunContext& run) {
    const int64_t sides = asInt(bound.values[0]);
    const float radius = asF32(bound.values[1]);
    if (sides < 3) {
        run.report("E204", bound.span, "circle: sides must be >= 3, got " + std::to_string(sides));
        return Value(std::make_shared<const Geo>(Geo{GeoKind::Points, std::make_shared<const std::vector<glm::vec3>>()}));
    }
    std::vector<glm::vec3> pts(static_cast<size_t>(sides));
    for (int64_t i = 0; i < sides; ++i) {
        const float a = 2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(sides);
        pts[static_cast<size_t>(i)] = glm::vec3(std::cos(a) * radius, std::sin(a) * radius, 0.0f);
    }
    Geo g;
    g.kind = GeoKind::Points;
    g.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pts));
    return Value(std::make_shared<const Geo>(std::move(g)));
}

Value opSweep(const BoundCall& bound, RunContext& run) {
    const GeoPtr pathPtr = asGeo(bound.values[0]);
    const GeoPtr profPtr = asGeo(bound.values[1]);
    const bool closed = asBool(bound.values[2]);
    const bool cap = asBool(bound.values[3]);
    const Geo& path = *pathPtr;
    const Geo& prof = *profPtr;
    auto empty = [] {
        Geo g;
        g.kind = GeoKind::Mesh;
        g.positions = std::make_shared<const std::vector<glm::vec3>>();
        g.cornerVerts = std::make_shared<const std::vector<int32_t>>();
        g.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::vector<int32_t>{0});
        return Value(std::make_shared<const Geo>(std::move(g)));
    };
    if (path.kind == GeoKind::Instances || prof.kind == GeoKind::Instances) {
        run.report("E204", bound.span, "sweep: path and profile must be point sets (geo<points> or the points of a mesh)",
                   "realize() instances first, or pass anchor points");
        return empty();
    }
    const size_t nPath = path.pointCount(), nProf = prof.pointCount();
    if (nPath < 2) {
        run.report("E204", bound.span, "sweep: the path needs at least 2 points, got " + std::to_string(nPath));
        return empty();
    }
    if (nProf < 3) {
        run.report("E204", bound.span, "sweep: the profile needs at least 3 points, got " + std::to_string(nProf),
                   "circle(sides = 8, radius = r) or 3+ points in the XY plane");
        return empty();
    }
    const auto& PP = *path.positions;
    const auto& PR = *prof.positions;
    // Per path point scale (@scale, points) when present.
    std::vector<float> scale(nPath, 1.0f);
    if (const AttrSet* pa = path.pointAttrs.get())
        if (const AttrColumn* c = pa->find("scale"))
            if (auto* f = std::get_if<std::shared_ptr<const std::vector<float>>>(&c->data))
                if ((*f)->size() == nPath) scale = **f;

    // Tangents.
    std::vector<glm::vec3> T(nPath);
    auto seg = [&](size_t a, size_t b) {
        const glm::vec3 d = PP[b] - PP[a];
        const float l = glm::length(d);
        return l > 1e-12f ? d / l : glm::vec3(0.0f);
    };
    for (size_t i = 0; i < nPath; ++i) {
        glm::vec3 t(0.0f);
        if (closed) {
            t = seg((i + nPath - 1) % nPath, i) + seg(i, (i + 1) % nPath);
        } else if (i == 0) {
            t = seg(0, 1);
        } else if (i + 1 == nPath) {
            t = seg(nPath - 2, nPath - 1);
        } else {
            t = seg(i - 1, i) + seg(i, i + 1);
        }
        const float l = glm::length(t);
        T[i] = l > 1e-12f ? t / l : (i > 0 ? T[i - 1] : glm::vec3(0, 0, 1));
    }
    // Parallel transport of the profile X axis.
    std::vector<glm::vec3> U(nPath), V(nPath);
    {
        const glm::vec3 t0 = T[0];
        glm::vec3 ref = std::abs(t0.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 u = ref - t0 * glm::dot(ref, t0);  // ref projected off the tangent
        u = glm::length(u) > 1e-12f ? glm::normalize(u) : glm::vec3(1, 0, 0);
        // Rotate so that the frame is (u, v, T) right-handed: v = T x u.
        U[0] = u;
        V[0] = glm::cross(t0, u);
        for (size_t i = 1; i < nPath; ++i) {
            const glm::vec3 a = T[i - 1], b = T[i];
            const glm::vec3 axis = glm::cross(a, b);
            const float s = glm::length(axis), c = glm::dot(a, b);
            glm::vec3 up = U[i - 1];
            if (s > 1e-9f) {
                const glm::vec3 k = axis / s;
                // Rodrigues rotation of the previous u by the angle between tangents.
                up = up * c + glm::cross(k, up) * s + k * glm::dot(k, up) * (1.0f - c);
            }
            up -= b * glm::dot(up, b);
            U[i] = glm::length(up) > 1e-12f ? glm::normalize(up) : U[i - 1];
            V[i] = glm::cross(b, U[i]);
        }
    }

    // Ring points: ring i, profile j -> row i * nProf + j; each copies the path point's columns.
    std::vector<RowSrc> pointRows;
    std::vector<glm::vec3> pos;
    pointRows.reserve(nPath * nProf);
    pos.reserve(nPath * nProf);
    for (size_t i = 0; i < nPath; ++i)
        for (size_t j = 0; j < nProf; ++j) {
            pointRows.push_back(RowSrc::copy(static_cast<int32_t>(i)));
            pos.push_back(PP[i] + (U[i] * PR[j].x + V[i] * PR[j].y) * scale[i]);
        }
    auto ring = [&](size_t i, size_t j) { return static_cast<int32_t>(i * nProf + j); };
    std::vector<int32_t> verts, offsets{0};
    const size_t nSeg = closed ? nPath : nPath - 1;
    for (size_t i = 0; i < nSeg; ++i) {
        const size_t in = (i + 1) % nPath;
        for (size_t j = 0; j < nProf; ++j) {
            const size_t jn = (j + 1) % nProf;
            verts.push_back(ring(i, j));
            verts.push_back(ring(i, jn));
            verts.push_back(ring(in, jn));
            verts.push_back(ring(in, j));
            offsets.push_back(static_cast<int32_t>(verts.size()));
        }
    }
    if (!closed && cap) {
        // Convexity of the profile (2D).
        bool convex = true;
        {
            float sgn = 0.0f;
            for (size_t j = 0; j < nProf && convex; ++j) {
                const glm::vec3& a = PR[j];
                const glm::vec3& b = PR[(j + 1) % nProf];
                const glm::vec3& c = PR[(j + 2) % nProf];
                const float cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                if (std::abs(cr) < 1e-12f) continue;
                if (sgn == 0.0f) sgn = cr;
                else if (cr * sgn < 0.0f) convex = false;
            }
        }
        auto emitCap = [&](size_t i, bool reverse) {
            std::vector<int32_t> loop(nProf);
            for (size_t j = 0; j < nProf; ++j) loop[j] = ring(i, j);
            if (reverse) std::reverse(loop.begin(), loop.end());
            if (convex) {
                for (int32_t v : loop) verts.push_back(v);
                offsets.push_back(static_cast<int32_t>(verts.size()));
                    return;
            }
            std::vector<glm::vec2> pts2(nProf);
            for (size_t j = 0; j < nProf; ++j) {
                const glm::vec3& p = PR[reverse ? nProf - 1 - j : j];
                pts2[j] = glm::vec2(p.x, p.y);
            }
            std::vector<std::array<int, 3>> tris;
            earClip(pts2, tris);
            for (const auto& t : tris) {
                verts.push_back(loop[static_cast<size_t>(t[0])]);
                verts.push_back(loop[static_cast<size_t>(t[1])]);
                verts.push_back(loop[static_cast<size_t>(t[2])]);
                offsets.push_back(static_cast<int32_t>(verts.size()));
                }
        };
        emitCap(0, true);          // start cap faces -T
        emitCap(nPath - 1, false); // end cap faces +T
    }

    Geo out;
    out.kind = GeoKind::Mesh;
    out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    out.pointAttrs = buildAttrs(path.pointAttrs.get(), pointRows, nullptr);
    out.pointGroups = buildGroups(path.pointGroups.get(), pointRows);
    out.detailAttrs = path.detailAttrs;
    out.detailGroups = path.detailGroups;
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(verts));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    return Value(std::make_shared<const Geo>(std::move(out)));
}

}  // namespace

Value evalSweepBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Circle: return opCircle(bound, run);
        case BuiltinId::Sweep: return opSweep(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
