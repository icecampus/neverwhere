#include "../../pch.h"

// §8.3 sweep (v1.21): a tube/beam/rail mesh from a path of points and a 2D
// profile of points — the curve tool PGG lacks an edge domain for. The path is
// the ordered @index sequence of a geo<points> (mesh_line, a jittered line,
// any point set the author ordered); the profile is a loop (profile_closed) or
// an open polyline (ribbons, leaves, V-blades) of points in the XY plane
// (circle(), mesh_line along X, or hand-placed points), also ordered by @index.
//
// Frames: unit tangents by central differences (one-sided at open ends, wrapped
// when closed), the profile's X axis parallel-transported along the path from
// a reference least aligned with the first tangent — no twist accumulates on
// smooth paths and the result is deterministic. Per path point @scale (f32)
// and @profile_scale (vec2, per profile axis) scale the profile, @twist (f32,
// degrees) rolls it about the tangent. Ring points inherit the path point's
// columns/groups (a per-point @tint on the path colours the tube) minus the
// consumed @scale/@profile_scale/@twist, plus @uv (u across the profile, v
// along the path); profile
// columns are ignored. Open paths with a closed profile get planar caps
// (convex profile -> one polygon, else ear-clipped) unless cap = false. Quads
// face outward for a CCW profile.

#include <algorithm>
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
    const bool profileClosed = asBool(bound.values[4]);
    const bool cap = asBool(bound.values[3]) && profileClosed;  // an open profile has no loop to cap
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
    if (nProf < (profileClosed ? 3u : 2u)) {
        run.report("E204", bound.span,
                   std::string("sweep: a ") + (profileClosed ? "closed profile needs at least 3" : "open profile needs at least 2") +
                       " points, got " + std::to_string(nProf),
                   "circle(sides = 8, radius = r) / mesh_line(count, width, dir = (1, 0, 0)) or hand-placed points in the XY plane");
        return empty();
    }
    const auto& PP = *path.positions;
    const auto& PR = *prof.positions;
    // Per path point profile scale (@scale: f32 uniform — the reserved stamp —
    // times @profile_scale: vec2 per profile axis) and twist (@twist: f32,
    // degrees about the tangent) when present.
    std::vector<glm::vec2> scale(nPath, glm::vec2(1.0f));
    std::vector<float> twist(nPath, 0.0f);
    if (const AttrSet* pa = path.pointAttrs.get()) {
        if (const AttrColumn* c = pa->find("scale")) {
            if (auto* f = std::get_if<std::shared_ptr<const std::vector<float>>>(&c->data)) {
                if ((*f)->size() == nPath)
                    for (size_t i = 0; i < nPath; ++i) scale[i] = glm::vec2((**f)[i]);
            } else {
                run.report("E204", bound.span, "sweep: @scale on the path must be f32 (reserved stamp)",
                           "per-axis scaling goes into @profile_scale (vec2)");
            }
        }
        if (const AttrColumn* c = pa->find("profile_scale")) {
            if (auto* v2 = std::get_if<std::shared_ptr<const std::vector<glm::vec2>>>(&c->data)) {
                if ((*v2)->size() == nPath)
                    for (size_t i = 0; i < nPath; ++i) scale[i] *= (**v2)[i];
            } else {
                run.report("E204", bound.span, "sweep: @profile_scale on the path must be vec2",
                           "x scales the profile X (width), y its Y (depth)");
            }
        }
        if (const AttrColumn* c = pa->find("twist")) {
            if (auto* f = std::get_if<std::shared_ptr<const std::vector<float>>>(&c->data)) {
                if ((*f)->size() == nPath) twist = **f;
            } else {
                run.report("E204", bound.span, "sweep: @twist on the path must be f32 (degrees)");
            }
        }
    }

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
    // Parallel transport of the profile frame. Reference: profile Y points as
    // "up" as the path allows (world +Y projected off the first tangent, +X
    // for vertical paths), profile X = V x T — so a path along +Z maps profile
    // X -> world X and profile Y -> world Y (a flat ribbon lies flat); the
    // frame (U, V, T) is right-handed and a CCW profile faces outward.
    std::vector<glm::vec3> U(nPath), V(nPath);
    {
        const glm::vec3 t0 = T[0];
        glm::vec3 ref = std::abs(t0.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 v = ref - t0 * glm::dot(ref, t0);  // ref projected off the tangent
        v = glm::length(v) > 1e-12f ? glm::normalize(v) : glm::vec3(0, 1, 0);
        V[0] = v;
        U[0] = glm::cross(v, t0);
        for (size_t i = 1; i < nPath; ++i) {
            const glm::vec3 a = T[i - 1], b = T[i];
            const glm::vec3 axis = glm::cross(a, b);
            const float s = glm::length(axis), c = glm::dot(a, b);
            glm::vec3 vp = V[i - 1];
            if (s > 1e-9f) {
                const glm::vec3 k = axis / s;
                // Rodrigues rotation of the previous v by the angle between tangents.
                vp = vp * c + glm::cross(k, vp) * s + k * glm::dot(k, vp) * (1.0f - c);
            }
            vp -= b * glm::dot(vp, b);
            V[i] = glm::length(vp) > 1e-12f ? glm::normalize(vp) : V[i - 1];
            U[i] = glm::cross(V[i], b);
        }
        // Twist: rotate the frame about the tangent.
        for (size_t i = 0; i < nPath; ++i) {
            if (twist[i] == 0.0f) continue;
            const float a = glm::radians(twist[i]);
            const glm::vec3 u = U[i] * std::cos(a) + V[i] * std::sin(a);
            const glm::vec3 v = V[i] * std::cos(a) - U[i] * std::sin(a);
            U[i] = u;
            V[i] = v;
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
            pos.push_back(PP[i] + U[i] * (PR[j].x * scale[i].x) + V[i] * (PR[j].y * scale[i].y));
        }
    // @uv of the sheet: u across the profile, v along the path (0..1).
    std::vector<glm::vec2> uv(nPath * nProf);
    for (size_t i = 0; i < nPath; ++i)
        for (size_t j = 0; j < nProf; ++j) {
            const float u = profileClosed ? static_cast<float>(j) / static_cast<float>(nProf)
                                          : static_cast<float>(j) / static_cast<float>(nProf - 1);
            const float v = closed ? static_cast<float>(i) / static_cast<float>(nPath)
                                   : static_cast<float>(i) / static_cast<float>(nPath - 1);
            uv[i * nProf + j] = glm::vec2(u, v);
        }
    auto ring = [&](size_t i, size_t j) { return static_cast<int32_t>(i * nProf + j); };
    std::vector<int32_t> verts, offsets{0};
    const size_t nSeg = closed ? nPath : nPath - 1;
    const size_t nProfSeg = profileClosed ? nProf : nProf - 1;
    for (size_t i = 0; i < nSeg; ++i) {
        const size_t in = (i + 1) % nPath;
        for (size_t j = 0; j < nProfSeg; ++j) {
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
    {
        std::shared_ptr<const AttrSet> inherited = buildAttrs(path.pointAttrs.get(), pointRows, nullptr);
        AttrSet attrs = inherited ? *inherited : AttrSet{};
        attrs.columns.erase("scale");  // consumed by the sweep, not a property of the sheet
        attrs.columns.erase("profile_scale");
        attrs.columns.erase("twist");
        attrs.columns["uv"] = AttrColumn{std::make_shared<const std::vector<glm::vec2>>(std::move(uv)), AttrTypeInfo::None};
        out.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    }
    out.pointGroups = buildGroups(path.pointGroups.get(), pointRows);
    out.detailAttrs = path.detailAttrs;
    out.detailGroups = path.detailGroups;
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(verts));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// --- bezier_points(p0, p1, p2, p3, count) ------------------------------------------
//
// Cubic Bezier sampled at `count` uniform parameter values (t = 0..1, written
// as @t): the path authoring primitive — a midrib, a stem, a rope — from four
// control points instead of hand-fitted polynomials in set_position.
Value opBezierPoints(const BoundCall& bound, RunContext& run) {
    const glm::vec3 p0 = asVec3(bound.values[0]), p1 = asVec3(bound.values[1]);
    const glm::vec3 p2 = asVec3(bound.values[2]), p3 = asVec3(bound.values[3]);
    const int64_t count = asInt(bound.values[4]);
    if (count < 2) {
        run.report("E204", bound.span, "bezier_points: count must be >= 2, got " + std::to_string(count));
        return Value(std::make_shared<const Geo>(Geo{GeoKind::Points, std::make_shared<const std::vector<glm::vec3>>()}));
    }
    std::vector<glm::vec3> pts(static_cast<size_t>(count));
    std::vector<float> ts(static_cast<size_t>(count));
    for (int64_t i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1), s = 1.0f - t;
        pts[static_cast<size_t>(i)] = p0 * (s * s * s) + p1 * (3.0f * s * s * t) + p2 * (3.0f * s * t * t) + p3 * (t * t * t);
        ts[static_cast<size_t>(i)] = t;
    }
    Geo g;
    g.kind = GeoKind::Points;
    g.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pts));
    AttrSet attrs;
    attrs.columns["t"] = AttrColumn{std::make_shared<const std::vector<float>>(std::move(ts)), AttrTypeInfo::None};
    g.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    return Value(std::make_shared<const Geo>(std::move(g)));
}

// --- resample_points(path, count, closed) ------------------------------------------
//
// Uniform arc-length resampling of an ordered point path: `count` points from
// the first to the last (closed: around the loop, the last sample before the
// start). Point columns/groups lerp along the segment they fall on; @t
// (0..1 by arc length) is written. Makes a hand-placed or jittered path usable
// as an even sweep skeleton.
Value opResamplePoints(const BoundCall& bound, RunContext& run) {
    const GeoPtr pathPtr = asGeo(bound.values[0]);
    const Geo& path = *pathPtr;
    const int64_t count = asInt(bound.values[1]);
    const bool closed = asBool(bound.values[2]);
    auto emptyPts = [] {
        return Value(std::make_shared<const Geo>(Geo{GeoKind::Points, std::make_shared<const std::vector<glm::vec3>>()}));
    };
    if (path.kind == GeoKind::Instances) {
        run.report("E204", bound.span, "resample_points: path must be a point set", "realize() first or pass anchor points");
        return emptyPts();
    }
    const size_t n = path.pointCount();
    if (n < 2 || count < 2) {
        run.report("E204", bound.span,
                   "resample_points: needs a path of >= 2 points and count >= 2 (got " + std::to_string(n) + " / " +
                       std::to_string(count) + ")");
        return emptyPts();
    }
    const auto& P = *path.positions;
    const size_t nSeg = closed ? n : n - 1;
    std::vector<float> cum(nSeg + 1, 0.0f);
    for (size_t i = 0; i < nSeg; ++i) cum[i + 1] = cum[i] + glm::length(P[(i + 1) % n] - P[i]);
    const float total = cum[nSeg];
    std::vector<glm::vec3> pts(static_cast<size_t>(count));
    std::vector<float> ts(static_cast<size_t>(count));
    std::vector<RowSrc> rows(static_cast<size_t>(count));
    size_t seg = 0;
    for (int64_t k = 0; k < count; ++k) {
        // Open: t in [0, 1]; closed: t in [0, 1) so the last sample is not the start again.
        const float t = closed ? static_cast<float>(k) / static_cast<float>(count)
                               : static_cast<float>(k) / static_cast<float>(count - 1);
        const float d = t * total;
        while (seg + 1 < nSeg && cum[seg + 1] < d) ++seg;
        const float len = cum[seg + 1] - cum[seg];
        const float u = len > 1e-12f ? std::clamp((d - cum[seg]) / len, 0.0f, 1.0f) : 0.0f;
        const size_t a = seg, b = (seg + 1) % n;
        pts[static_cast<size_t>(k)] = glm::mix(P[a], P[b], u);
        ts[static_cast<size_t>(k)] = t;
        rows[static_cast<size_t>(k)] = RowSrc::lerp(static_cast<int32_t>(a), static_cast<int32_t>(b), u);
    }
    Geo g;
    g.kind = GeoKind::Points;
    g.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pts));
    {
        std::shared_ptr<const AttrSet> inherited = buildAttrs(path.pointAttrs.get(), rows, nullptr);
        AttrSet attrs = inherited ? *inherited : AttrSet{};
        attrs.columns["t"] = AttrColumn{std::make_shared<const std::vector<float>>(std::move(ts)), AttrTypeInfo::None};
        g.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    }
    g.pointGroups = buildGroups(path.pointGroups.get(), rows);
    g.detailAttrs = path.detailAttrs;
    g.detailGroups = path.detailGroups;
    return Value(std::make_shared<const Geo>(std::move(g)));
}

}  // namespace

Value evalSweepBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Circle: return opCircle(bound, run);
        case BuiltinId::Sweep: return opSweep(bound, run);
        case BuiltinId::BezierPoints: return opBezierPoints(bound, run);
        case BuiltinId::ResamplePoints: return opResamplePoints(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
