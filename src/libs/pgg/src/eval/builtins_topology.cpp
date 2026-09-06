#include "../../pch.h"

// §8.3 topology nodes beyond merge, value level: delete(geo, where, domain)
// and clip(geo, origin, normal, cap_group).
//
// delete removes the elements of `domain` where the mask is true and cascades
// to the incident elements of the higher domains (spec §8.3): a deleted point
// takes every face (and its corners) that touches it, a deleted corner takes
// its face, a deleted face takes its corners. Lower domains are never touched
// (deleting faces leaves orphan points — Houdini semantics; use a points mask
// when the points must go too). Columns on every surviving domain are gathered
// by the kept indices, so attributes/groups/@N stay aligned. detail is not a
// per-element domain and is rejected (E204).

#include <array>
#include <unordered_map>
#include <unordered_set>

#include "builtins.h"

namespace pgg {
namespace {

// Gathers every column of an AttrSet by `keep` (indices into the old domain).
std::shared_ptr<const AttrSet> gatherAttrs(const AttrSet* src, const std::vector<int32_t>& keep) {
    if (!src) return nullptr;
    AttrSet out;
    for (const auto& [name, col] : src->columns)
        out.columns[name] = AttrColumn{gatherColumn(col.data, keep), col.typeInfo};
    return std::make_shared<const AttrSet>(std::move(out));
}

std::shared_ptr<const GroupSet> gatherGroups(const GroupSet* src, const std::vector<int32_t>& keep) {
    if (!src) return nullptr;
    GroupSet out;
    for (const auto& [name, col] : src->columns) {
        BoolColumn g;
        g.reserve(keep.size());
        for (int32_t i : keep) g.push_back((*col)[static_cast<size_t>(i)]);
        out.columns[name] = std::make_shared<const BoolColumn>(std::move(g));
    }
    return std::make_shared<const GroupSet>(std::move(out));
}

std::vector<int32_t> keptIndices(const std::vector<uint8_t>& drop) {
    std::vector<int32_t> keep;
    keep.reserve(drop.size());
    for (size_t i = 0; i < drop.size(); ++i)
        if (!drop[i]) keep.push_back(static_cast<int32_t>(i));
    return keep;
}

// Rebuilds the mesh part of `out` from the face-drop mask: kept faces keep all
// their corners; corner indices are remapped through `pointRemap` (identity
// when null). Points are left to the caller.
void rebuildFaces(const Geo& in, Geo& out, const std::vector<uint8_t>& dropFace,
                  const std::vector<int32_t>* pointRemap) {
    const auto& offsets = *in.faceOffsets;
    const auto& verts = *in.cornerVerts;
    std::vector<int32_t> keepFace, keepCorner, newVerts, newOffsets;
    newOffsets.push_back(0);
    for (size_t f = 0; f + 1 < offsets.size(); ++f) {
        if (dropFace[f]) continue;
        keepFace.push_back(static_cast<int32_t>(f));
        for (int32_t c = offsets[f]; c < offsets[f + 1]; ++c) {
            keepCorner.push_back(c);
            const int32_t v = verts[static_cast<size_t>(c)];
            newVerts.push_back(pointRemap ? (*pointRemap)[static_cast<size_t>(v)] : v);
        }
        newOffsets.push_back(static_cast<int32_t>(newVerts.size()));
    }
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(newVerts));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(newOffsets));
    out.cornerAttrs = gatherAttrs(in.cornerAttrs.get(), keepCorner);
    out.cornerGroups = gatherGroups(in.cornerGroups.get(), keepCorner);
    out.faceAttrs = gatherAttrs(in.faceAttrs.get(), keepFace);
    out.faceGroups = gatherGroups(in.faceGroups.get(), keepFace);
}

Value opDelete(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const Domain domain = domainFromName(asString(bound.values[2]));
    if (domain == Domain::Detail) {
        run.report("E204", bound.span, "delete: detail is not a per-element domain",
                   "mask points, corners or faces instead");
        return Value(inPtr);
    }
    const bool hasFaces = in.kind == GeoKind::Mesh && in.faceOffsets && in.cornerVerts;
    if (domain != Domain::Points && !hasFaces) {
        run.report("E204", bound.span,
                   std::string("delete on ") + domainName(domain) + " needs a geo<mesh>; " +
                       geoKindName(in.kind) + " has only points",
                   "use domain = points");
        return Value(inPtr);
    }

    ConstBufferPtr where = convertBuffer(evalField(bound.fields[1], in, domain, run), ScalarType::Bool);
    const std::vector<uint8_t>& drop = std::get<BoolBuf>(*where);
    if (drop.size() != in.elementCount(domain)) return Value(inPtr);  // field error already reported
    if (std::find(drop.begin(), drop.end(), uint8_t(1)) == drop.end()) return Value(inPtr);

    Geo out = in;
    if (domain == Domain::Points) {
        const std::vector<int32_t> keep = keptIndices(drop);
        std::vector<int32_t> remap(in.pointCount(), -1);
        for (size_t i = 0; i < keep.size(); ++i) remap[static_cast<size_t>(keep[i])] = static_cast<int32_t>(i);
        using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;
        out.positions = std::get<Vec3Col>(gatherColumn(ColumnData(in.positions), keep));
        if (in.normals) out.normals = std::get<Vec3Col>(gatherColumn(ColumnData(in.normals), keep));
        out.pointAttrs = gatherAttrs(in.pointAttrs.get(), keep);
        out.pointGroups = gatherGroups(in.pointGroups.get(), keep);
        if (hasFaces) {
            // A face dies with any of its points.
            const auto& offsets = *in.faceOffsets;
            const auto& verts = *in.cornerVerts;
            std::vector<uint8_t> dropFace(in.faceCount(), 0);
            for (size_t f = 0; f + 1 < offsets.size(); ++f)
                for (int32_t c = offsets[f]; c < offsets[f + 1] && !dropFace[f]; ++c)
                    if (drop[static_cast<size_t>(verts[static_cast<size_t>(c)])]) dropFace[f] = 1;
            rebuildFaces(in, out, dropFace, &remap);
        }
    } else if (domain == Domain::Faces) {
        rebuildFaces(in, out, drop, nullptr);
    } else {  // corners: a face dies with any of its corners
        const auto& offsets = *in.faceOffsets;
        std::vector<uint8_t> dropFace(in.faceCount(), 0);
        for (size_t f = 0; f + 1 < offsets.size(); ++f)
            for (int32_t c = offsets[f]; c < offsets[f + 1] && !dropFace[f]; ++c)
                if (drop[static_cast<size_t>(c)]) dropFace[f] = 1;
        rebuildFaces(in, out, dropFace, nullptr);
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// --- clip(geo, origin, normal, cap_group) -------------------------------------
//
// Half-space clip (spec §8.3, Houdini Clip / Blender Bisect): keeps the side
// the normal points into (dot(P - origin, normal) >= 0), removes the rest and
// seals every closed cut loop with a planar cap. Faces are clipped polygon by
// polygon (Sutherland-Hodgman); a new point on a crossed edge is shared by both
// faces of that edge (dedup by edge key), so the mesh stays welded. Point and
// corner columns interpolate along the edge (numeric: lerp; int/bool/string:
// nearer end; @N renormalized), face columns and groups are copied. Cap faces
// inherit the face columns and groups of the first cut face of their loop
// (the cut end of a brick is still brick) and additionally join `cap_group`
// when it is given; cap corners get zero columns except corner N = -normal.
// Open loops (open input surfaces) get no cap. geo<points>: keeps the points
// in the half-space. geo<instances>: E204 (realize first).

// Where a value of an output row comes from: copy of row `a` (a == b), lerp
// between rows a and b with weight t, or a fresh row (a < 0) filled with a
// default.
struct RowSrc {
    int32_t a = -1, b = -1;
    float t = 0.0f;
    static RowSrc copy(int32_t i) { return {i, i, 0.0f}; }
    static RowSrc lerp(int32_t a, int32_t b, float t) { return {a, b, t}; }
    static RowSrc fresh() { return {-1, -1, 0.0f}; }
};

template <class T>
T lerpRow(const T& a, const T& b, float t) {
    if constexpr (std::is_same_v<T, float>) return a + (b - a) * t;
    else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>)
        return a + (b - a) * t;
    else return t < 0.5f ? a : b;  // int64 / bool (u8) / string: nearer end
}

// Builds a column from row sources; `fresh` (a typed default for RowSrc::fresh)
// may be null -> zero value.
ColumnData buildColumn(const ColumnData& src, const std::vector<RowSrc>& rows, const glm::vec3* freshVec3) {
    return std::visit(
        [&](const auto& ptr) -> ColumnData {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            VecT out;
            out.reserve(rows.size());
            const VecT& in = *ptr;
            for (const RowSrc& r : rows) {
                if (r.a < 0) {
                    if constexpr (std::is_same_v<ElemT, glm::vec3>) out.push_back(freshVec3 ? *freshVec3 : glm::vec3(0.0f));
                    else out.push_back(ElemT{});
                } else if (r.a == r.b) {
                    out.push_back(in[static_cast<size_t>(r.a)]);
                } else {
                    out.push_back(lerpRow(in[static_cast<size_t>(r.a)], in[static_cast<size_t>(r.b)], r.t));
                }
            }
            return std::make_shared<const VecT>(std::move(out));
        },
        src);
}

std::shared_ptr<const AttrSet> buildAttrs(const AttrSet* src, const std::vector<RowSrc>& rows,
                                          const glm::vec3* capNormal) {
    if (!src) return nullptr;
    AttrSet out;
    for (const auto& [name, col] : src->columns) {
        const bool isNormal = name == "N" && col.typeInfo == AttrTypeInfo::Normal;
        out.columns[name] = AttrColumn{buildColumn(col.data, rows, isNormal ? capNormal : nullptr), col.typeInfo};
    }
    return std::make_shared<const AttrSet>(std::move(out));
}

std::shared_ptr<const GroupSet> buildGroups(const GroupSet* src, const std::vector<RowSrc>& rows) {
    if (!src) return nullptr;
    GroupSet out;
    for (const auto& [name, col] : src->columns) {
        BoolColumn g;
        g.reserve(rows.size());
        for (const RowSrc& r : rows) {
            if (r.a < 0) g.push_back(0);
            else if (r.a == r.b) g.push_back((*col)[static_cast<size_t>(r.a)]);
            else {
                const float v = (*col)[static_cast<size_t>(r.a)] * (1.0f - r.t) + (*col)[static_cast<size_t>(r.b)] * r.t;
                g.push_back(v > 0.5f ? 1 : 0);
            }
        }
        out.columns[name] = std::make_shared<const BoolColumn>(std::move(g));
    }
    return std::make_shared<const GroupSet>(std::move(out));
}

// Ear clipping of a planar polygon given in 2D; returns triangle index triples
// into `pts`. Falls back to a fan when no ear is found (degenerate input).
void earClip(const std::vector<glm::vec2>& pts, std::vector<std::array<int, 3>>& tris) {
    std::vector<int> idx(pts.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
    auto cross2 = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    // Orientation of the polygon (shoelace).
    float area2 = 0.0f;
    for (size_t i = 0; i < pts.size(); ++i) {
        const glm::vec2& p = pts[i];
        const glm::vec2& q = pts[(i + 1) % pts.size()];
        area2 += p.x * q.y - q.x * p.y;
    }
    const float sign = area2 >= 0.0f ? 1.0f : -1.0f;
    auto inside = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& p) {
        return cross2(a, b, p) * sign >= 0.0f && cross2(b, c, p) * sign >= 0.0f && cross2(c, a, p) * sign >= 0.0f;
    };
    size_t guard = 0;
    while (idx.size() > 3 && guard++ < pts.size() * pts.size()) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            const int ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            if (cross2(pts[static_cast<size_t>(ia)], pts[static_cast<size_t>(ib)], pts[static_cast<size_t>(ic)]) * sign <= 0.0f)
                continue;  // reflex
            bool empty = true;
            for (int j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                if (inside(pts[static_cast<size_t>(ia)], pts[static_cast<size_t>(ib)], pts[static_cast<size_t>(ic)],
                           pts[static_cast<size_t>(j)])) {
                    empty = false;
                    break;
                }
            }
            if (!empty) continue;
            tris.push_back({ia, ib, ic});
            idx.erase(idx.begin() + static_cast<ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) {
        tris.push_back({idx[0], idx[1], idx[2]});
    } else {
        for (size_t i = 1; i + 1 < idx.size(); ++i) tris.push_back({idx[0], idx[i], idx[i + 1]});
    }
}

Value opClip(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const glm::vec3 origin = asVec3(bound.values[1]);
    glm::vec3 n = asVec3(bound.values[2]);
    const std::string capGroup = asString(bound.values[3]);
    const float nlen = glm::length(n);
    if (!(nlen > 1e-12f) || !std::isfinite(nlen)) {
        run.report("E612", bound.span, "clip: normal must be a non-zero finite vector",
                   "pass the plane normal, e.g. normal = (1, 0, 0)");
        return Value(inPtr);
    }
    n /= nlen;
    if (in.kind == GeoKind::Instances) {
        run.report("E204", bound.span, "clip on geo<instances> is not defined", "realize() first, then clip");
        return Value(inPtr);
    }
    if (!in.positions || in.positions->empty()) return Value(inPtr);

    const std::vector<glm::vec3>& P = *in.positions;
    glm::vec3 mn, mx;
    geoBBox(in, mn, mx);
    const float eps = 1e-6f * std::max(1.0f, glm::length(mx - mn));
    // side: +1 inside, -1 outside, 0 on the plane (treated as inside, no split).
    std::vector<float> d(P.size());
    std::vector<int8_t> side(P.size());
    bool anyOut = false, anyIn = false;
    for (size_t i = 0; i < P.size(); ++i) {
        d[i] = glm::dot(P[i] - origin, n);
        side[i] = d[i] > eps ? 1 : (d[i] < -eps ? -1 : 0);
        anyOut |= side[i] < 0;
        anyIn |= side[i] >= 0;
    }
    if (!anyOut) return Value(inPtr);  // everything kept: identity

    // Points: kept originals first, then edge intersections.
    std::vector<RowSrc> pointRows;
    std::vector<int32_t> remap(P.size(), -1);
    for (size_t i = 0; i < P.size(); ++i) {
        if (side[i] < 0) continue;
        remap[i] = static_cast<int32_t>(pointRows.size());
        pointRows.push_back(RowSrc::copy(static_cast<int32_t>(i)));
    }
    const bool hasFaces = in.kind == GeoKind::Mesh && in.faceOffsets && in.cornerVerts && in.faceCount() > 0;
    std::unordered_map<uint64_t, int32_t> edgePoint;  // (lo, hi) -> new point row
    auto pointOnEdge = [&](int32_t a, int32_t b) -> int32_t {
        const int32_t lo = std::min(a, b), hi = std::max(a, b);
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) | static_cast<uint32_t>(hi);
        auto it = edgePoint.find(key);
        if (it != edgePoint.end()) return it->second;
        const float da = d[static_cast<size_t>(lo)], db = d[static_cast<size_t>(hi)];
        const float t = std::clamp(da / (da - db), 0.0f, 1.0f);
        const int32_t row = static_cast<int32_t>(pointRows.size());
        pointRows.push_back(RowSrc::lerp(lo, hi, t));
        edgePoint.emplace(key, row);
        return row;
    };

    std::vector<RowSrc> cornerRows, faceRows;
    std::vector<int32_t> cornerVerts, faceOffsets{0};
    // Directed cap edges (from -> to, in cap winding) with the face that cut them.
    struct CapEdge { int32_t from, to, face; };
    std::vector<CapEdge> capEdges;
    if (hasFaces) {
        const auto& FO = *in.faceOffsets;
        const auto& CV = *in.cornerVerts;
        struct Emitted { int32_t point; };
        std::vector<Emitted> seq;
        for (size_t f = 0; f < in.faceCount(); ++f) {
            const int32_t begin = FO[f], end = FO[f + 1];
            const int32_t k = end - begin;
            if (k < 3) continue;
            bool allIn = true, allOut = true;
            for (int32_t c = begin; c < end; ++c) {
                const int8_t s = side[static_cast<size_t>(CV[static_cast<size_t>(c)])];
                allIn &= s >= 0;
                allOut &= s < 0;
            }
            if (allOut) continue;
            if (allIn) {
                for (int32_t c = begin; c < end; ++c) {
                    cornerRows.push_back(RowSrc::copy(c));
                    cornerVerts.push_back(remap[static_cast<size_t>(CV[static_cast<size_t>(c)])]);
                }
                faceRows.push_back(RowSrc::copy(static_cast<int32_t>(f)));
                faceOffsets.push_back(static_cast<int32_t>(cornerVerts.size()));
                continue;
            }
            // Sutherland-Hodgman against the single plane.
            seq.clear();
            std::vector<RowSrc> seqRows;
            for (int32_t j = 0; j < k; ++j) {
                const int32_t c = begin + j, cn = begin + (j + 1) % k;
                const int32_t p = CV[static_cast<size_t>(c)], pn = CV[static_cast<size_t>(cn)];
                const int8_t s = side[static_cast<size_t>(p)], sn = side[static_cast<size_t>(pn)];
                if (s >= 0) {
                    seq.push_back({remap[static_cast<size_t>(p)]});
                    seqRows.push_back(RowSrc::copy(c));
                }
                if (s * sn < 0) {
                    const int32_t row = pointOnEdge(p, pn);
                    const float t = std::clamp(d[static_cast<size_t>(p)] / (d[static_cast<size_t>(p)] - d[static_cast<size_t>(pn)]), 0.0f, 1.0f);
                    seq.push_back({row});
                    seqRows.push_back(RowSrc::lerp(c, cn, t));
                }
            }
            if (seq.size() < 3) continue;
            for (size_t i = 0; i < seq.size(); ++i) {
                cornerRows.push_back(seqRows[i]);
                cornerVerts.push_back(seq[i].point);
            }
            faceRows.push_back(RowSrc::copy(static_cast<int32_t>(f)));
            faceOffsets.push_back(static_cast<int32_t>(cornerVerts.size()));
        }
    }

    // Cut loops = boundary edges of the clipped mesh whose both ends lie in the
    // plane (a directed edge a->b with no b->a partner). This covers edges that
    // Sutherland-Hodgman created and original edges that lay in the plane and
    // lost their other face; boundary edges off the plane (open input) never
    // close a loop and get no cap.
    if (hasFaces) {
        std::vector<uint8_t> onPlane(pointRows.size(), 0);
        for (size_t i = 0; i < pointRows.size(); ++i)
            onPlane[i] = pointRows[i].a != pointRows[i].b || side[static_cast<size_t>(pointRows[i].a)] == 0;
        auto key = [](int32_t a, int32_t b) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
        };
        std::unordered_set<uint64_t> directed;
        directed.reserve(cornerVerts.size());
        for (size_t f = 0; f + 1 < faceOffsets.size(); ++f)
            for (int32_t c = faceOffsets[f]; c < faceOffsets[f + 1]; ++c) {
                const int32_t cn = c + 1 < faceOffsets[f + 1] ? c + 1 : faceOffsets[f];
                directed.insert(key(cornerVerts[static_cast<size_t>(c)], cornerVerts[static_cast<size_t>(cn)]));
            }
        for (size_t f = 0; f + 1 < faceOffsets.size(); ++f)
            for (int32_t c = faceOffsets[f]; c < faceOffsets[f + 1]; ++c) {
                const int32_t cn = c + 1 < faceOffsets[f + 1] ? c + 1 : faceOffsets[f];
                const int32_t a = cornerVerts[static_cast<size_t>(c)], b = cornerVerts[static_cast<size_t>(cn)];
                if (!onPlane[static_cast<size_t>(a)] || !onPlane[static_cast<size_t>(b)]) continue;
                if (directed.count(key(b, a))) continue;
                capEdges.push_back({b, a, faceRows[f].a});
            }
    }

    // Caps: chain directed cut edges into closed loops.
    std::vector<uint8_t> isCap(faceRows.size(), 0);
    const glm::vec3 capNormal = -n;
    if (!capEdges.empty()) {
        std::unordered_multimap<int32_t, size_t> byFrom;
        for (size_t i = 0; i < capEdges.size(); ++i) byFrom.emplace(capEdges[i].from, i);
        std::vector<uint8_t> used(capEdges.size(), 0);
        // Plane basis for 2D tests.
        const glm::vec3 helper = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 u = glm::normalize(glm::cross(n, helper));
        const glm::vec3 v = glm::cross(n, u);
        // Positions of new points are needed for the 2D projection.
        auto pointPos = [&](int32_t row) -> glm::vec3 {
            const RowSrc& r = pointRows[static_cast<size_t>(row)];
            if (r.a == r.b) return P[static_cast<size_t>(r.a)];
            return P[static_cast<size_t>(r.a)] + (P[static_cast<size_t>(r.b)] - P[static_cast<size_t>(r.a)]) * r.t;
        };
        for (size_t start = 0; start < capEdges.size(); ++start) {
            if (used[start]) continue;
            std::vector<int32_t> loop;
            size_t cur = start;
            bool closed = false;
            while (!used[cur]) {
                used[cur] = 1;
                loop.push_back(capEdges[cur].from);
                const int32_t to = capEdges[cur].to;
                if (to == capEdges[start].from) { closed = true; break; }
                auto range = byFrom.equal_range(to);
                size_t next = SIZE_MAX;
                for (auto it = range.first; it != range.second; ++it)
                    if (!used[it->second]) { next = it->second; break; }
                if (next == SIZE_MAX) break;
                cur = next;
            }
            if (!closed || loop.size() < 3) continue;
            // Winding: the cap must face -n (outward from the kept half).
            glm::vec3 nl(0.0f);
            for (size_t i = 0; i < loop.size(); ++i) {
                const glm::vec3 a = pointPos(loop[i]), b = pointPos(loop[(i + 1) % loop.size()]);
                nl += glm::cross(a, b);
            }
            if (glm::dot(nl, capNormal) < 0.0f) std::reverse(loop.begin(), loop.end());
            // Convex -> one polygon; otherwise ear-clipped triangles.
            std::vector<glm::vec2> pts2;
            pts2.reserve(loop.size());
            for (int32_t row : loop) {
                const glm::vec3 p = pointPos(row) - origin;
                pts2.push_back(glm::vec2(glm::dot(p, u), glm::dot(p, v)));
            }
            bool convex = true;
            {
                float sgn = 0.0f;
                for (size_t i = 0; i < pts2.size() && convex; ++i) {
                    const glm::vec2& a = pts2[i];
                    const glm::vec2& b = pts2[(i + 1) % pts2.size()];
                    const glm::vec2& c = pts2[(i + 2) % pts2.size()];
                    const float cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
                    if (std::abs(cr) < 1e-12f) continue;
                    if (sgn == 0.0f) sgn = cr;
                    else if (cr * sgn < 0.0f) convex = false;
                }
            }
            const int32_t srcFace = capEdges[start].face;
            auto emitFace = [&](const std::vector<int32_t>& rows) {
                for (int32_t row : rows) {
                    cornerRows.push_back(RowSrc::fresh());
                    cornerVerts.push_back(row);
                }
                faceRows.push_back(RowSrc::copy(srcFace));
                faceOffsets.push_back(static_cast<int32_t>(cornerVerts.size()));
                isCap.push_back(1);
            };
            if (convex) {
                emitFace(loop);
            } else {
                std::vector<std::array<int, 3>> tris;
                earClip(pts2, tris);
                for (const auto& t : tris)
                    emitFace({loop[static_cast<size_t>(t[0])], loop[static_cast<size_t>(t[1])], loop[static_cast<size_t>(t[2])]});
            }
        }
    }

    Geo out = in;
    using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;
    out.positions = std::get<Vec3Col>(buildColumn(ColumnData(in.positions), pointRows, nullptr));
    if (in.normals && in.normals->size() == P.size()) {
        std::vector<glm::vec3> nn = *std::get<Vec3Col>(buildColumn(ColumnData(in.normals), pointRows, nullptr));
        for (glm::vec3& x : nn) {
            const float len = glm::length(x);
            if (len > 1e-12f) x /= len;
        }
        out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nn));
    }
    out.pointAttrs = buildAttrs(in.pointAttrs.get(), pointRows, nullptr);
    out.pointGroups = buildGroups(in.pointGroups.get(), pointRows);
    if (hasFaces) {
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(cornerVerts));
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(faceOffsets));
        out.cornerAttrs = buildAttrs(in.cornerAttrs.get(), cornerRows, &capNormal);
        out.cornerGroups = buildGroups(in.cornerGroups.get(), cornerRows);
        out.faceAttrs = buildAttrs(in.faceAttrs.get(), faceRows, nullptr);
        std::shared_ptr<const GroupSet> fg = buildGroups(in.faceGroups.get(), faceRows);
        if (!capGroup.empty()) {
            GroupSet groups = fg ? *fg : GroupSet{};
            BoolColumn col(faceRows.size(), 0);
            if (auto existing = groups.find(capGroup); existing && existing->size() == col.size()) col = *existing;
            for (size_t i = 0; i < isCap.size(); ++i)
                if (isCap[i]) col[i] = 1;
            groups.columns[capGroup] = std::make_shared<const BoolColumn>(std::move(col));
            fg = std::make_shared<const GroupSet>(std::move(groups));
        }
        out.faceGroups = fg;
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

}  // namespace

Value evalTopologyBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Delete:
            return opDelete(bound, run);
        case BuiltinId::Clip:
            return opClip(bound, run);
        default:
            return Value();
    }
}

}  // namespace pgg
