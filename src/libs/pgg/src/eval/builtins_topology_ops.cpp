#include "../../pch.h"

// §8.3 topology nodes, second wave (v1.21): extrude, inset, separate,
// triangulate, subdivide, merge_by_distance, mirror.
//
// All of them are value-level mesh rebuilds sharing the row-source machinery
// of topology_util.h: every output element names the input rows it copies /
// blends, so attributes, groups and @N of all four domains stay aligned
// without per-node special cases. Faces keep their columns when split or
// copied (an extruded brick side is still brick), points blend linearly.

#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "builtins.h"
#include "topology_util.h"

namespace pgg {
namespace {

using namespace topo;
using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;

// Corner row shorthands: copy of a corner, midpoint of two corners.
RowBlend C(int32_t c) { return RowBlend::copy(c); }
RowBlend M(int32_t a, int32_t b) { return RowBlend{{{a, 0.5f}, {b, 0.5f}}}; }

glm::vec3 unitNormal(const Geo& g, size_t f) {
    const glm::vec3 n = faceNormal(g, f);  // Newell, |n| = 2 * area
    const float len = glm::length(n);
    return len > 1e-20f ? n / len : glm::vec3(0.0f);
}

bool meshWithFaces(const Geo& g) { return g.kind == GeoKind::Mesh && g.faceOffsets && g.cornerVerts && g.faceCount() > 0; }

uint64_t edgeKey(int32_t a, int32_t b) {
    const int32_t lo = std::min(a, b), hi = std::max(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) | static_cast<uint32_t>(hi);
}

// Adds `group` (when named) on the faces flagged in `mark` to a face GroupSet.
std::shared_ptr<const GroupSet> withFaceGroup(std::shared_ptr<const GroupSet> fg, const std::string& group,
                                              const std::vector<uint8_t>& mark) {
    if (group.empty()) return fg;
    GroupSet groups = fg ? *fg : GroupSet{};
    BoolColumn col(mark.size(), 0);
    if (auto existing = groups.find(group); existing && existing->size() == col.size()) col = *existing;
    for (size_t i = 0; i < mark.size(); ++i)
        if (mark[i]) col[i] = 1;
    groups.columns[group] = std::make_shared<const BoolColumn>(std::move(col));
    return std::make_shared<const GroupSet>(std::move(groups));
}

// Output mesh assembled from row sources (points blend; corners/faces copy).
struct MeshBuild {
    std::vector<RowBlend> pointRows;
    std::vector<glm::vec3> positions;  // explicit positions (moved/blended by the op)
    std::vector<RowBlend> cornerRows;
    std::vector<RowSrc> faceRows;
    std::vector<int32_t> cornerVerts;
    std::vector<int32_t> faceOffsets{0};

    int32_t addPoint(RowBlend src, glm::vec3 pos) {
        pointRows.push_back(std::move(src));
        positions.push_back(pos);
        return static_cast<int32_t>(pointRows.size() - 1);
    }
    void addFace(RowSrc face, const std::vector<int32_t>& verts, const std::vector<RowBlend>& corners) {
        for (size_t i = 0; i < verts.size(); ++i) {
            cornerVerts.push_back(verts[i]);
            cornerRows.push_back(corners[i]);
        }
        faceRows.push_back(face);
        faceOffsets.push_back(static_cast<int32_t>(cornerVerts.size()));
    }
    Geo finish(const Geo& in, const glm::vec3* freshCornerNormal, const std::vector<glm::vec3>* pointNormals) const {
        Geo out = in;
        out.kind = GeoKind::Mesh;
        out.positions = std::make_shared<const std::vector<glm::vec3>>(positions);
        if (pointNormals && pointNormals->size() == positions.size()) {
            out.normals = std::make_shared<const std::vector<glm::vec3>>(*pointNormals);
        } else if (in.normals && in.normals->size() == in.pointCount()) {
            std::vector<glm::vec3> nn = *std::get<Vec3Col>(buildColumnBlend(ColumnData(in.normals), pointRows, nullptr));
            for (glm::vec3& x : nn) {
                const float len = glm::length(x);
                if (len > 1e-12f) x /= len;
            }
            out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nn));
        }
        out.pointAttrs = buildAttrsBlend(in.pointAttrs.get(), pointRows, nullptr);
        out.pointGroups = buildGroupsBlend(in.pointGroups.get(), pointRows);
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(cornerVerts);
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(faceOffsets);
        out.cornerAttrs = buildAttrsBlend(in.cornerAttrs.get(), cornerRows, freshCornerNormal);
        out.cornerGroups = buildGroupsBlend(in.cornerGroups.get(), cornerRows);
        out.faceAttrs = buildAttrs(in.faceAttrs.get(), faceRows, nullptr);
        out.faceGroups = buildGroups(in.faceGroups.get(), faceRows);
        return out;
    }
};

// Drops points no face references among `candidates` (indices into the
// build's point rows) — the orphan cleanup of extrude/inset: the old points of
// a face that moved away stay only while another face still uses them.
Geo dropUnreferencedPoints(Geo g, const std::vector<uint8_t>& candidate) {
    std::vector<uint8_t> used(g.pointCount(), 0);
    for (int32_t v : *g.cornerVerts) used[static_cast<size_t>(v)] = 1;
    std::vector<uint8_t> drop(g.pointCount(), 0);
    bool any = false;
    for (size_t i = 0; i < drop.size(); ++i) {
        drop[i] = candidate[i] && !used[i];
        any |= drop[i] != 0;
    }
    if (!any) return g;
    return *deleteByMask(g, Domain::Points, drop);
}

// --- extrude(geo, distance, where, mode, side_group, top_group) ---------------
//
// Region mode (default, Blender "extrude region"): the selected faces move as
// one sheet along the per-point average of their normals; side quads close the
// boundary of the selection (edges with an unselected or no neighbour face);
// unselected faces keep the old points. Individual mode: every selected face
// extrudes along its own normal with its own side walls. `distance` is a face
// field (region: per-point mean of the incident selected faces).
Value opExtrude(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    if (!meshWithFaces(in)) {
        if (in.kind != GeoKind::Mesh)
            run.report("E204", bound.span, "extrude needs a geo<mesh> with faces",
                       in.kind == GeoKind::Instances ? "realize() first" : "points have nothing to extrude");
        return Value(inPtr);
    }
    const std::string mode = asString(bound.values[3]);
    const std::string sideGroup = asString(bound.values[4]);
    const std::string topGroup = asString(bound.values[5]);
    const size_t nf = in.faceCount();
    ConstBufferPtr distB = convertBuffer(evalField(bound.fields[1], in, Domain::Faces, run), ScalarType::F32);
    const auto& dist = std::get<F32Buf>(*distB);
    std::vector<uint8_t> sel(nf, 1);
    if (bound.fields[2]) {
        ConstBufferPtr w = convertBuffer(evalField(bound.fields[2], in, Domain::Faces, run), ScalarType::Bool);
        sel = std::get<BoolBuf>(*w);
    }
    if (dist.size() != nf || sel.size() != nf) return Value(inPtr);
    if (std::find(sel.begin(), sel.end(), uint8_t(1)) == sel.end()) return Value(inPtr);

    const auto& FO = *in.faceOffsets;
    const auto& CV = *in.cornerVerts;
    const auto& P = *in.positions;
    const size_t np = P.size();
    std::vector<glm::vec3> fn(nf);
    for (size_t f = 0; f < nf; ++f) fn[f] = unitNormal(in, f);

    MeshBuild b;
    std::vector<glm::vec3> normalsOut;
    const bool haveN = in.normals && in.normals->size() == np;
    for (size_t i = 0; i < np; ++i) b.addPoint(RowBlend::copy(static_cast<int32_t>(i)), P[i]);
    if (haveN) normalsOut = *in.normals;
    std::vector<uint8_t> candidate(np, 0);  // old points that may become orphans
    std::vector<uint8_t> isSide, isTop;
    auto emit = [&](RowSrc face, const std::vector<int32_t>& verts, const std::vector<RowBlend>& corners, bool side,
                    bool top) {
        b.addFace(face, verts, corners);
        isSide.push_back(side ? 1 : 0);
        isTop.push_back(top ? 1 : 0);
    };
    // Unselected faces pass through untouched.
    for (size_t f = 0; f < nf; ++f) {
        if (sel[f]) continue;
        std::vector<int32_t> verts;
        std::vector<RowBlend> corners;
        for (int32_t c = FO[f]; c < FO[f + 1]; ++c) {
            verts.push_back(CV[static_cast<size_t>(c)]);
            corners.push_back(C(c));
        }
        emit(RowSrc::copy(static_cast<int32_t>(f)), verts, corners, false, false);
    }

    if (mode == "individual") {
        for (size_t f = 0; f < nf; ++f) {
            if (!sel[f]) continue;
            const int32_t begin = FO[f], end = FO[f + 1], k = end - begin;
            if (k < 3) continue;
            const glm::vec3 n = fn[f];
            std::vector<int32_t> top(static_cast<size_t>(k));
            std::vector<RowBlend> topCorners(static_cast<size_t>(k));
            for (int32_t j = 0; j < k; ++j) {
                const int32_t v = CV[static_cast<size_t>(begin + j)];
                candidate[static_cast<size_t>(v)] = 1;
                top[static_cast<size_t>(j)] = b.addPoint(RowBlend::copy(v), P[static_cast<size_t>(v)] + n * dist[f]);
                if (haveN) normalsOut.push_back(n);
                topCorners[static_cast<size_t>(j)] = C(begin + j);
            }
            emit(RowSrc::copy(static_cast<int32_t>(f)), top, topCorners, false, true);
            for (int32_t j = 0; j < k; ++j) {
                const int32_t jn = (j + 1) % k;
                const int32_t a = CV[static_cast<size_t>(begin + j)], bb = CV[static_cast<size_t>(begin + jn)];
                emit(RowSrc::copy(static_cast<int32_t>(f)),
                     {a, bb, top[static_cast<size_t>(jn)], top[static_cast<size_t>(j)]},
                     {C(begin + j), C(begin + jn), C(begin + jn), C(begin + j)},
                     true, false);
            }
        }
    } else {
        // Region: per-point mean normal and distance over the selected faces.
        std::vector<glm::vec3> pn(np, glm::vec3(0.0f));
        std::vector<float> pd(np, 0.0f);
        std::vector<int32_t> pc(np, 0);
        for (size_t f = 0; f < nf; ++f) {
            if (!sel[f]) continue;
            for (int32_t c = FO[f]; c < FO[f + 1]; ++c) {
                const size_t v = static_cast<size_t>(CV[static_cast<size_t>(c)]);
                pn[v] += fn[f];
                pd[v] += dist[f];
                pc[v] += 1;
            }
        }
        std::vector<int32_t> newIdx(np, -1);
        for (size_t v = 0; v < np; ++v) {
            if (pc[v] == 0) continue;
            candidate[v] = 1;
            const float len = glm::length(pn[v]);
            const glm::vec3 dir = len > 1e-12f ? pn[v] / len : glm::vec3(0.0f);
            const float d = pd[v] / static_cast<float>(pc[v]);
            newIdx[v] = b.addPoint(RowBlend::copy(static_cast<int32_t>(v)), P[v] + dir * d);
            if (haveN) normalsOut.push_back(dir);
        }
        // Directed edges of selected faces: an edge whose reverse is also a
        // selected-face edge is interior; otherwise it is a boundary edge.
        std::unordered_set<uint64_t> selDirected;
        auto dkey = [](int32_t a, int32_t bb) {
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(bb);
        };
        for (size_t f = 0; f < nf; ++f) {
            if (!sel[f]) continue;
            for (int32_t c = FO[f]; c < FO[f + 1]; ++c) {
                const int32_t cn = c + 1 < FO[f + 1] ? c + 1 : FO[f];
                selDirected.insert(dkey(CV[static_cast<size_t>(c)], CV[static_cast<size_t>(cn)]));
            }
        }
        for (size_t f = 0; f < nf; ++f) {
            if (!sel[f]) continue;
            const int32_t begin = FO[f], end = FO[f + 1], k = end - begin;
            if (k < 3) continue;
            std::vector<int32_t> top(static_cast<size_t>(k));
            std::vector<RowBlend> topCorners(static_cast<size_t>(k));
            for (int32_t j = 0; j < k; ++j) {
                top[static_cast<size_t>(j)] = newIdx[static_cast<size_t>(CV[static_cast<size_t>(begin + j)])];
                topCorners[static_cast<size_t>(j)] = C(begin + j);
            }
            emit(RowSrc::copy(static_cast<int32_t>(f)), top, topCorners, false, true);
            for (int32_t j = 0; j < k; ++j) {
                const int32_t jn = (j + 1) % k;
                const int32_t a = CV[static_cast<size_t>(begin + j)], bb = CV[static_cast<size_t>(begin + jn)];
                if (selDirected.count(dkey(bb, a))) continue;  // interior edge of the region
                emit(RowSrc::copy(static_cast<int32_t>(f)),
                     {a, bb, top[static_cast<size_t>(jn)], top[static_cast<size_t>(j)]},
                     {C(begin + j), C(begin + jn), C(begin + jn), C(begin + j)},
                     true, false);
            }
        }
    }
    Geo out = b.finish(in, nullptr, haveN ? &normalsOut : nullptr);
    out.faceGroups = withFaceGroup(out.faceGroups, sideGroup, isSide);
    out.faceGroups = withFaceGroup(out.faceGroups, topGroup, isTop);
    candidate.resize(out.pointCount(), 0);
    out = dropUnreferencedPoints(std::move(out), candidate);
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// --- inset(geo, amount, depth, where, inner_group, rim_group) -----------------
//
// Per-face inset (Blender "inset individual"): every selected face gets an
// inner polygon offset inward by `amount` (constant border width via mitred
// edge offsets, clamped so the polygon cannot flip) and pushed along the face
// normal by `depth`; the border becomes a ring of quads. Panels, door leaves,
// window panes and timber framing are one inset away from a flat box face.
Value opInset(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    if (!meshWithFaces(in)) {
        if (in.kind != GeoKind::Mesh)
            run.report("E204", bound.span, "inset needs a geo<mesh> with faces",
                       in.kind == GeoKind::Instances ? "realize() first" : "points have no faces to inset");
        return Value(inPtr);
    }
    const std::string innerGroup = asString(bound.values[4]);
    const std::string rimGroup = asString(bound.values[5]);
    const size_t nf = in.faceCount();
    const auto& amount = std::get<F32Buf>(*convertBuffer(evalField(bound.fields[1], in, Domain::Faces, run), ScalarType::F32));
    const auto& depth = std::get<F32Buf>(*convertBuffer(evalField(bound.fields[2], in, Domain::Faces, run), ScalarType::F32));
    std::vector<uint8_t> sel(nf, 1);
    if (bound.fields[3]) sel = std::get<BoolBuf>(*convertBuffer(evalField(bound.fields[3], in, Domain::Faces, run), ScalarType::Bool));
    if (amount.size() != nf || depth.size() != nf || sel.size() != nf) return Value(inPtr);
    if (std::find(sel.begin(), sel.end(), uint8_t(1)) == sel.end()) return Value(inPtr);

    const auto& FO = *in.faceOffsets;
    const auto& CV = *in.cornerVerts;
    const auto& P = *in.positions;
    const size_t np = P.size();
    const bool haveN = in.normals && in.normals->size() == np;
    MeshBuild b;
    std::vector<glm::vec3> normalsOut;
    for (size_t i = 0; i < np; ++i) b.addPoint(RowBlend::copy(static_cast<int32_t>(i)), P[i]);
    if (haveN) normalsOut = *in.normals;
    std::vector<uint8_t> isInner, isRim;
    auto emit = [&](RowSrc face, const std::vector<int32_t>& verts, const std::vector<RowBlend>& corners, bool inner,
                    bool rim) {
        b.addFace(face, verts, corners);
        isInner.push_back(inner ? 1 : 0);
        isRim.push_back(rim ? 1 : 0);
    };
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = FO[f], end = FO[f + 1], k = end - begin;
        std::vector<int32_t> verts(static_cast<size_t>(k));
        std::vector<RowBlend> corners(static_cast<size_t>(k));
        for (int32_t j = 0; j < k; ++j) {
            verts[static_cast<size_t>(j)] = CV[static_cast<size_t>(begin + j)];
            corners[static_cast<size_t>(j)] = C(begin + j);
        }
        if (!sel[f] || k < 3) {
            emit(RowSrc::copy(static_cast<int32_t>(f)), verts, corners, false, false);
            continue;
        }
        const glm::vec3 n = unitNormal(in, f);
        // Inward edge normals (in the face plane) and the mitred offset per
        // vertex; the offset is clamped to 0.45 of the shortest centroid-to-
        // edge distance so the inner polygon keeps its orientation.
        glm::vec3 centroid(0.0f);
        for (int32_t v : verts) centroid += P[static_cast<size_t>(v)];
        centroid /= static_cast<float>(k);
        std::vector<glm::vec3> inward(static_cast<size_t>(k));
        float minEdgeDist = 1e30f;
        for (int32_t j = 0; j < k; ++j) {
            const glm::vec3& a = P[static_cast<size_t>(verts[static_cast<size_t>(j)])];
            const glm::vec3& c = P[static_cast<size_t>(verts[static_cast<size_t>((j + 1) % k)])];
            glm::vec3 e = c - a;
            const float el = glm::length(e);
            glm::vec3 nin = el > 1e-12f ? glm::cross(n, e / el) : glm::vec3(0.0f);  // points into the face for CCW
            inward[static_cast<size_t>(j)] = nin;
            if (el > 1e-12f) minEdgeDist = std::min(minEdgeDist, std::abs(glm::dot(centroid - a, nin)));
        }
        const float amt = std::min(amount[f], minEdgeDist * 0.45f);
        std::vector<int32_t> inner(static_cast<size_t>(k));
        for (int32_t j = 0; j < k; ++j) {
            const glm::vec3& nPrev = inward[static_cast<size_t>((j + k - 1) % k)];
            const glm::vec3& nNext = inward[static_cast<size_t>(j)];
            const float denom = 1.0f + glm::dot(nPrev, nNext);
            const glm::vec3 offset = denom > 1e-4f ? (nPrev + nNext) / denom : nNext;
            const int32_t v = verts[static_cast<size_t>(j)];
            inner[static_cast<size_t>(j)] = b.addPoint(RowBlend::copy(v), P[static_cast<size_t>(v)] + offset * amt + n * depth[f]);
            if (haveN) normalsOut.push_back(n);
        }
        emit(RowSrc::copy(static_cast<int32_t>(f)), inner, corners, true, false);
        for (int32_t j = 0; j < k; ++j) {
            const int32_t jn = (j + 1) % k;
            emit(RowSrc::copy(static_cast<int32_t>(f)),
                 {verts[static_cast<size_t>(j)], verts[static_cast<size_t>(jn)], inner[static_cast<size_t>(jn)], inner[static_cast<size_t>(j)]},
                 {C(begin + j), C(begin + jn), C(begin + jn), C(begin + j)},
                 false, true);
        }
    }
    Geo out = b.finish(in, nullptr, haveN ? &normalsOut : nullptr);
    out.faceGroups = withFaceGroup(out.faceGroups, innerGroup, isInner);
    out.faceGroups = withFaceGroup(out.faceGroups, rimGroup, isRim);
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// --- separate(geo, where, domain) -> (yes, no) ---------------------------------
Value opSeparate(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const Domain domain = domainFromName(asString(bound.values[2]));
    auto pair = [](GeoPtr yes, GeoPtr no) {
        auto elems = std::make_shared<std::vector<Value>>();
        elems->push_back(Value(std::move(yes)));
        elems->push_back(Value(std::move(no)));
        return Value(ListValuePtr(elems));
    };
    if (domain == Domain::Detail) {
        run.report("E204", bound.span, "separate: detail is not a per-element domain", "mask points, corners or faces");
        return pair(inPtr, inPtr);
    }
    const bool hasFaces = in.kind == GeoKind::Mesh && in.faceOffsets && in.cornerVerts;
    if (domain != Domain::Points && !hasFaces) {
        run.report("E204", bound.span,
                   std::string("separate on ") + domainName(domain) + " needs a geo<mesh>; " + geoKindName(in.kind) +
                       " has only points",
                   "use domain = points");
        return pair(inPtr, inPtr);
    }
    ConstBufferPtr where = convertBuffer(evalField(bound.fields[1], in, domain, run), ScalarType::Bool);
    const std::vector<uint8_t>& mask = std::get<BoolBuf>(*where);
    if (mask.size() != in.elementCount(domain)) return pair(inPtr, inPtr);
    std::vector<uint8_t> inv(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) inv[i] = mask[i] ? 0 : 1;
    // yes = drop the unselected, no = drop the selected.
    return pair(deleteByMask(in, domain, inv), deleteByMask(in, domain, mask));
}

// --- triangulate(geo) ------------------------------------------------------------
Value opTriangulate(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    if (!meshWithFaces(in)) {
        if (in.kind != GeoKind::Mesh) run.report("E204", bound.span, "triangulate needs a geo<mesh>", "realize() first");
        return Value(inPtr);
    }
    const auto& FO = *in.faceOffsets;
    const auto& CV = *in.cornerVerts;
    const auto& P = *in.positions;
    bool allTris = true;
    for (size_t f = 0; f < in.faceCount() && allTris; ++f) allTris = FO[f + 1] - FO[f] == 3;
    if (allTris) return Value(inPtr);
    MeshBuild b;
    for (size_t i = 0; i < P.size(); ++i) b.addPoint(RowBlend::copy(static_cast<int32_t>(i)), P[i]);
    for (size_t f = 0; f < in.faceCount(); ++f) {
        const int32_t begin = FO[f], end = FO[f + 1], k = end - begin;
        if (k < 3) continue;
        if (k == 3) {
            b.addFace(RowSrc::copy(static_cast<int32_t>(f)),
                      {CV[static_cast<size_t>(begin)], CV[static_cast<size_t>(begin + 1)], CV[static_cast<size_t>(begin + 2)]},
                      {C(begin), C(begin + 1), C(begin + 2)});
            continue;
        }
        // Project onto the face plane and ear-clip (handles concave n-gons).
        const glm::vec3 n = unitNormal(in, f);
        const glm::vec3 helper = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 u = glm::normalize(glm::cross(n, helper));
        const glm::vec3 v = glm::cross(n, u);
        std::vector<glm::vec2> pts2(static_cast<size_t>(k));
        for (int32_t j = 0; j < k; ++j) {
            const glm::vec3& p = P[static_cast<size_t>(CV[static_cast<size_t>(begin + j)])];
            pts2[static_cast<size_t>(j)] = glm::vec2(glm::dot(p, u), glm::dot(p, v));
        }
        std::vector<std::array<int, 3>> tris;
        earClip(pts2, tris);
        for (const auto& t : tris)
            b.addFace(RowSrc::copy(static_cast<int32_t>(f)),
                      {CV[static_cast<size_t>(begin + t[0])], CV[static_cast<size_t>(begin + t[1])], CV[static_cast<size_t>(begin + t[2])]},
                      {C(begin + t[0]), C(begin + t[1]), C(begin + t[2])});
    }
    return Value(std::make_shared<const Geo>(b.finish(in, nullptr, nullptr)));
}

// --- subdivide(geo, level, scheme) ----------------------------------------------
//
// linear: every k-gon -> k quads around its centroid through the edge
// midpoints (positions and columns blend linearly). catmull_clark: the same
// topology with the Catmull-Clark position rules (face points, edge points
// averaged with the adjacent face points, original vertices by the valence
// formula; boundary edges/vertices use the B-spline boundary rules). loop:
// triangles only (E204 otherwise) -> 4 triangles each with the Loop weights.
// Columns always blend linearly by the topological weights (originals copy,
// edge rows 1/2 1/2, face rows 1/k).
struct EdgeInfo {
    int32_t a, b;
    std::vector<int32_t> faces;
    int32_t point = -1;  // output point row
};

Geo subdivideOnce(const Geo& in, const std::string& scheme, bool& badLoop) {
    const auto& FO = *in.faceOffsets;
    const auto& CV = *in.cornerVerts;
    const auto& P = *in.positions;
    const size_t np = P.size(), nf = in.faceCount();
    badLoop = false;
    if (scheme == "loop")
        for (size_t f = 0; f < nf; ++f)
            if (FO[f + 1] - FO[f] != 3) { badLoop = true; return in; }

    // Edge table.
    std::unordered_map<uint64_t, int32_t> edgeIdx;
    std::vector<EdgeInfo> edges;
    std::vector<std::vector<int32_t>> faceEdges(nf);
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = FO[f], end = FO[f + 1];
        for (int32_t c = begin; c < end; ++c) {
            const int32_t a = CV[static_cast<size_t>(c)], bb = CV[static_cast<size_t>(c + 1 < end ? c + 1 : begin)];
            const uint64_t key = edgeKey(a, bb);
            auto it = edgeIdx.find(key);
            if (it == edgeIdx.end()) {
                it = edgeIdx.emplace(key, static_cast<int32_t>(edges.size())).first;
                edges.push_back({std::min(a, bb), std::max(a, bb), {}, -1});
            }
            edges[static_cast<size_t>(it->second)].faces.push_back(static_cast<int32_t>(f));
            faceEdges[f].push_back(it->second);
        }
    }
    // Vertex adjacency (edges per vertex).
    std::vector<std::vector<int32_t>> vertEdges(np);
    for (size_t e = 0; e < edges.size(); ++e) {
        vertEdges[static_cast<size_t>(edges[e].a)].push_back(static_cast<int32_t>(e));
        vertEdges[static_cast<size_t>(edges[e].b)].push_back(static_cast<int32_t>(e));
    }
    std::vector<glm::vec3> facePos(nf, glm::vec3(0.0f));
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = FO[f], end = FO[f + 1];
        for (int32_t c = begin; c < end; ++c) facePos[f] += P[static_cast<size_t>(CV[static_cast<size_t>(c)])];
        facePos[f] /= static_cast<float>(std::max<int32_t>(1, end - begin));
    }

    MeshBuild b;
    const bool cc = scheme == "catmull_clark", loop = scheme == "loop";
    // Original vertices.
    for (size_t v = 0; v < np; ++v) {
        glm::vec3 pos = P[v];
        const auto& ve = vertEdges[v];
        const int n = static_cast<int>(ve.size());
        std::vector<int32_t> boundaryNb;
        for (int32_t e : ve)
            if (edges[static_cast<size_t>(e)].faces.size() == 1)
                boundaryNb.push_back(edges[static_cast<size_t>(e)].a == static_cast<int32_t>(v) ? edges[static_cast<size_t>(e)].b : edges[static_cast<size_t>(e)].a);
        if (n > 0 && (cc || loop)) {
            if (!boundaryNb.empty()) {
                // Boundary vertex: 3/4 v + 1/8 each boundary neighbour (two of them).
                if (boundaryNb.size() == 2)
                    pos = P[v] * 0.75f + (P[static_cast<size_t>(boundaryNb[0])] + P[static_cast<size_t>(boundaryNb[1])]) * 0.125f;
            } else if (cc) {
                glm::vec3 F(0.0f), R(0.0f);
                std::unordered_set<int32_t> incidentFaces;
                for (int32_t e : ve) {
                    const EdgeInfo& ei = edges[static_cast<size_t>(e)];
                    R += (P[static_cast<size_t>(ei.a)] + P[static_cast<size_t>(ei.b)]) * 0.5f;
                    for (int32_t f : ei.faces) incidentFaces.insert(f);
                }
                for (int32_t f : incidentFaces) F += facePos[static_cast<size_t>(f)];
                const float nn = static_cast<float>(n);
                F /= static_cast<float>(std::max<size_t>(1, incidentFaces.size()));
                R /= nn;
                pos = (F + 2.0f * R + (nn - 3.0f) * P[v]) / nn;
            } else {  // loop interior
                const float nn = static_cast<float>(n);
                const float t = 3.0f / 8.0f + 0.25f * std::cos(2.0f * 3.14159265f / nn);
                const float beta = (5.0f / 8.0f - t * t) / nn;
                glm::vec3 sum(0.0f);
                for (int32_t e : ve) {
                    const EdgeInfo& ei = edges[static_cast<size_t>(e)];
                    sum += P[static_cast<size_t>(ei.a == static_cast<int32_t>(v) ? ei.b : ei.a)];
                }
                pos = P[v] * (1.0f - nn * beta) + sum * beta;
            }
        }
        b.addPoint(RowBlend::copy(static_cast<int32_t>(v)), pos);
    }
    // Edge points.
    for (EdgeInfo& e : edges) {
        glm::vec3 pos = (P[static_cast<size_t>(e.a)] + P[static_cast<size_t>(e.b)]) * 0.5f;
        if (cc && e.faces.size() == 2) {
            pos = (P[static_cast<size_t>(e.a)] + P[static_cast<size_t>(e.b)] + facePos[static_cast<size_t>(e.faces[0])] +
                   facePos[static_cast<size_t>(e.faces[1])]) * 0.25f;
        } else if (loop && e.faces.size() == 2) {
            glm::vec3 opp(0.0f);
            for (int32_t f : e.faces) {
                for (int32_t c = FO[f]; c < FO[f + 1]; ++c) {
                    const int32_t v = CV[static_cast<size_t>(c)];
                    if (v != e.a && v != e.b) opp += P[static_cast<size_t>(v)];
                }
            }
            pos = (P[static_cast<size_t>(e.a)] + P[static_cast<size_t>(e.b)]) * 0.375f + opp * 0.125f;
        }
        e.point = b.addPoint(RowBlend{{{e.a, 0.5f}, {e.b, 0.5f}}}, pos);
    }
    if (loop) {
        for (size_t f = 0; f < nf; ++f) {
            const int32_t c0 = FO[f];
            const int32_t v0 = CV[static_cast<size_t>(c0)], v1 = CV[static_cast<size_t>(c0 + 1)], v2 = CV[static_cast<size_t>(c0 + 2)];
            const int32_t e01 = edges[static_cast<size_t>(faceEdges[f][0])].point;
            const int32_t e12 = edges[static_cast<size_t>(faceEdges[f][1])].point;
            const int32_t e20 = edges[static_cast<size_t>(faceEdges[f][2])].point;
            const RowBlend r0 = C(c0), r1 = C(c0 + 1), r2 = C(c0 + 2);
            const RowBlend m01 = M(c0, c0 + 1), m12 = M(c0 + 1, c0 + 2), m20 = M(c0 + 2, c0);
            const RowSrc fr = RowSrc::copy(static_cast<int32_t>(f));
            b.addFace(fr, {v0, e01, e20}, {r0, m01, m20});
            b.addFace(fr, {e01, v1, e12}, {m01, r1, m12});
            b.addFace(fr, {e20, e12, v2}, {m20, m12, r2});
            b.addFace(fr, {e01, e12, e20}, {m01, m12, m20});
        }
        return b.finish(in, nullptr, nullptr);
    }
    // Face points + k quads per face.
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = FO[f], end = FO[f + 1], k = end - begin;
        if (k < 3) continue;
        std::vector<int32_t> fv;
        for (int32_t c = begin; c < end; ++c) fv.push_back(CV[static_cast<size_t>(c)]);
        const int32_t fp = b.addPoint(RowBlend::average(fv), facePos[f]);
        std::vector<int32_t> fc;
        for (int32_t c = begin; c < end; ++c) fc.push_back(c);
        const RowBlend centreCorner = RowBlend::average(fc);  // face-centre corner: mean of the face's corners
        const RowSrc fr = RowSrc::copy(static_cast<int32_t>(f));
        for (int32_t j = 0; j < k; ++j) {
            const int32_t jp = (j + k - 1) % k;
            const int32_t ePrev = edges[static_cast<size_t>(faceEdges[f][static_cast<size_t>(jp)])].point;
            const int32_t eNext = edges[static_cast<size_t>(faceEdges[f][static_cast<size_t>(j)])].point;
            const int32_t cj = begin + j, cn = begin + (j + 1) % k, cp = begin + jp;
            b.addFace(fr, {fv[static_cast<size_t>(j)], eNext, fp, ePrev},
                      {C(cj), M(cj, cn), centreCorner, M(cp, cj)});
        }
    }
    return b.finish(in, nullptr, nullptr);
}

Value opSubdivide(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const int64_t level = asInt(bound.values[1]);
    const std::string scheme = asString(bound.values[2]);
    if (!meshWithFaces(in)) {
        if (in.kind != GeoKind::Mesh) run.report("E204", bound.span, "subdivide needs a geo<mesh>", "realize() first");
        return Value(inPtr);
    }
    if (level < 0) {
        run.report("E204", bound.span, "subdivide: level must be >= 0, got " + std::to_string(level));
        return Value(inPtr);
    }
    if (level == 0) return Value(inPtr);
    GeoPtr cur = inPtr;
    for (int64_t i = 0; i < level; ++i) {
        bool badLoop = false;
        Geo next = subdivideOnce(*cur, scheme, badLoop);
        if (badLoop) {
            run.report("E204", bound.span, "subdivide(scheme = loop) needs a triangle mesh",
                       "triangulate() first, or use catmull_clark / linear");
            return Value(inPtr);
        }
        cur = std::make_shared<const Geo>(std::move(next));
    }
    return Value(cur);
}

// --- merge_by_distance(geo, dist) ------------------------------------------------
//
// Welds points closer than `dist` (union-find over a uniform grid, clusters
// keep their lowest-index point: position and columns of that point), remaps
// corners, drops repeated consecutive corners and faces left with < 3 corners.
Value opMergeByDistance(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const float dist = asF32(bound.values[1]);
    if (in.kind == GeoKind::Instances) {
        run.report("E204", bound.span, "merge_by_distance on geo<instances> is not defined", "realize() first");
        return Value(inPtr);
    }
    if (!(dist >= 0.0f) || !std::isfinite(dist)) {
        run.report("E204", bound.span, "merge_by_distance: dist must be a finite number >= 0");
        return Value(inPtr);
    }
    if (!in.positions || in.positions->size() < 2) return Value(inPtr);
    const auto& P = *in.positions;
    const size_t np = P.size();
    std::vector<int32_t> parent(np);
    for (size_t i = 0; i < np; ++i) parent[i] = static_cast<int32_t>(i);
    auto find = [&](int32_t x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    };
    auto unite = [&](int32_t a, int32_t bb) {
        a = find(a);
        bb = find(bb);
        if (a == bb) return;
        if (a < bb) parent[static_cast<size_t>(bb)] = a; else parent[static_cast<size_t>(a)] = bb;  // lowest index roots
    };
    const float cell = std::max(dist, 1e-9f);
    auto cellKey = [&](const glm::vec3& p) {
        const int64_t x = static_cast<int64_t>(std::floor(p.x / cell));
        const int64_t y = static_cast<int64_t>(std::floor(p.y / cell));
        const int64_t z = static_cast<int64_t>(std::floor(p.z / cell));
        return (static_cast<uint64_t>(x) * 73856093ull) ^ (static_cast<uint64_t>(y) * 19349663ull) ^ (static_cast<uint64_t>(z) * 83492791ull);
    };
    std::unordered_multimap<uint64_t, int32_t> grid;
    grid.reserve(np);
    for (size_t i = 0; i < np; ++i) grid.emplace(cellKey(P[i]), static_cast<int32_t>(i));
    const float d2 = dist * dist;
    for (size_t i = 0; i < np; ++i) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    const glm::vec3 q = P[i] + glm::vec3(dx, dy, dz) * cell;
                    auto range = grid.equal_range(cellKey(q));
                    for (auto it = range.first; it != range.second; ++it) {
                        const size_t j = static_cast<size_t>(it->second);
                        if (j <= i) continue;
                        const glm::vec3 diff = P[j] - P[i];
                        if (glm::dot(diff, diff) <= d2) unite(static_cast<int32_t>(i), static_cast<int32_t>(j));
                    }
                }
    }
    // Representatives in index order.
    std::vector<int32_t> keep, remap(np, -1);
    for (size_t i = 0; i < np; ++i)
        if (find(static_cast<int32_t>(i)) == static_cast<int32_t>(i)) {
            remap[i] = static_cast<int32_t>(keep.size());
            keep.push_back(static_cast<int32_t>(i));
        }
    if (keep.size() == np) return Value(inPtr);
    for (size_t i = 0; i < np; ++i) remap[i] = remap[static_cast<size_t>(find(static_cast<int32_t>(i)))];

    Geo out = in;
    out.positions = std::get<Vec3Col>(gatherColumn(ColumnData(in.positions), keep));
    if (in.normals && in.normals->size() == np) out.normals = std::get<Vec3Col>(gatherColumn(ColumnData(in.normals), keep));
    out.pointAttrs = gatherAttrs(in.pointAttrs.get(), keep);
    out.pointGroups = gatherGroups(in.pointGroups.get(), keep);
    if (meshWithFaces(in)) {
        const auto& FO = *in.faceOffsets;
        const auto& CV = *in.cornerVerts;
        std::vector<int32_t> keepFace, keepCorner, verts, offsets{0};
        for (size_t f = 0; f < in.faceCount(); ++f) {
            std::vector<int32_t> vs, cs;
            for (int32_t c = FO[f]; c < FO[f + 1]; ++c) {
                const int32_t v = remap[static_cast<size_t>(CV[static_cast<size_t>(c)])];
                if (!vs.empty() && vs.back() == v) continue;
                vs.push_back(v);
                cs.push_back(c);
            }
            while (vs.size() > 1 && vs.front() == vs.back()) { vs.pop_back(); cs.pop_back(); }
            if (vs.size() < 3) continue;
            keepFace.push_back(static_cast<int32_t>(f));
            for (size_t j = 0; j < vs.size(); ++j) { verts.push_back(vs[j]); keepCorner.push_back(cs[j]); }
            offsets.push_back(static_cast<int32_t>(verts.size()));
        }
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(verts));
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
        out.cornerAttrs = gatherAttrs(in.cornerAttrs.get(), keepCorner);
        out.cornerGroups = gatherGroups(in.cornerGroups.get(), keepCorner);
        out.faceAttrs = gatherAttrs(in.faceAttrs.get(), keepFace);
        out.faceGroups = gatherGroups(in.faceGroups.get(), keepFace);
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// --- mirror(geo, origin, normal, weld) ---------------------------------------------
//
// Appends the reflection of the geometry across the plane (origin, normal):
// positions and point-tagged columns reflect as points, vector/normal-tagged
// columns reflect as directions, face winding flips so normals keep facing
// out. Points within `weld` of the plane are shared by both halves (the seam
// is welded, no duplicate points on the symmetry plane). Quaternion columns
// (@orient) cannot be reflected into a rotation; geo<instances> -> E204.
Value opMirror(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const glm::vec3 origin = asVec3(bound.values[1]);
    glm::vec3 n = asVec3(bound.values[2]);
    const float weld = asF32(bound.values[3]);
    const float nlen = glm::length(n);
    if (!(nlen > 1e-12f) || !std::isfinite(nlen)) {
        run.report("E612", bound.span, "mirror: normal must be a non-zero finite vector",
                   "pass the plane normal, e.g. normal = (1, 0, 0)");
        return Value(inPtr);
    }
    n /= nlen;
    if (in.kind == GeoKind::Instances) {
        run.report("E204", bound.span, "mirror on geo<instances> is not defined (a reflection is not a rotation for @orient)",
                   "realize() first, then mirror");
        return Value(inPtr);
    }
    if (!in.positions || in.positions->empty()) return Value(inPtr);
    const auto& P = *in.positions;
    const size_t np = P.size();
    auto reflectPoint = [&](const glm::vec3& p) { return p - 2.0f * glm::dot(p - origin, n) * n; };
    auto reflectDir = [&](const glm::vec3& v) { return v - 2.0f * glm::dot(v, n) * n; };

    // Point rows: originals, then the mirrored copies of the non-welded points.
    std::vector<RowSrc> rows;
    std::vector<int32_t> mirrorIdx(np);
    for (size_t i = 0; i < np; ++i) rows.push_back(RowSrc::copy(static_cast<int32_t>(i)));
    for (size_t i = 0; i < np; ++i) {
        if (std::abs(glm::dot(P[i] - origin, n)) <= weld) {
            mirrorIdx[i] = static_cast<int32_t>(i);
            continue;
        }
        mirrorIdx[i] = static_cast<int32_t>(rows.size());
        rows.push_back(RowSrc::copy(static_cast<int32_t>(i)));
    }
    const size_t nOut = rows.size();
    Geo out = in;
    {
        std::vector<glm::vec3> pos = *std::get<Vec3Col>(buildColumn(ColumnData(in.positions), rows, nullptr));
        for (size_t i = np; i < nOut; ++i) pos[i] = reflectPoint(pos[i]);
        out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
        if (in.normals && in.normals->size() == np) {
            std::vector<glm::vec3> nn = *std::get<Vec3Col>(buildColumn(ColumnData(in.normals), rows, nullptr));
            for (size_t i = np; i < nOut; ++i) nn[i] = reflectDir(nn[i]);
            out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nn));
        }
    }
    // Typed reflection of vec3 columns of the mirrored rows.
    auto reflectAttrs = [&](std::shared_ptr<const AttrSet> set, size_t firstMirrored) {
        if (!set) return set;
        AttrSet copy = *set;
        for (auto& [name, col] : copy.columns) {
            if (col.typeInfo == AttrTypeInfo::None || col.typeInfo == AttrTypeInfo::Quaternion) continue;
            auto* v = std::get_if<Vec3Col>(&col.data);
            if (!v) continue;
            std::vector<glm::vec3> data = **v;
            for (size_t i = firstMirrored; i < data.size(); ++i)
                data[i] = col.typeInfo == AttrTypeInfo::Point ? reflectPoint(data[i]) : reflectDir(data[i]);
            col.data = std::make_shared<const std::vector<glm::vec3>>(std::move(data));
        }
        return std::make_shared<const AttrSet>(std::move(copy));
    };
    out.pointAttrs = reflectAttrs(buildAttrs(in.pointAttrs.get(), rows, nullptr), np);
    out.pointGroups = buildGroups(in.pointGroups.get(), rows);
    if (meshWithFaces(in)) {
        const auto& FO = *in.faceOffsets;
        const auto& CV = *in.cornerVerts;
        const size_t nc = CV.size(), nf = in.faceCount();
        std::vector<RowSrc> cornerRows, faceRows;
        std::vector<int32_t> verts, offsets{0};
        for (size_t f = 0; f < nf; ++f) {
            for (int32_t c = FO[f]; c < FO[f + 1]; ++c) { verts.push_back(CV[static_cast<size_t>(c)]); cornerRows.push_back(RowSrc::copy(c)); }
            faceRows.push_back(RowSrc::copy(static_cast<int32_t>(f)));
            offsets.push_back(static_cast<int32_t>(verts.size()));
        }
        for (size_t f = 0; f < nf; ++f) {  // reversed winding
            for (int32_t c = FO[f + 1] - 1; c >= FO[f]; --c) {
                verts.push_back(mirrorIdx[static_cast<size_t>(CV[static_cast<size_t>(c)])]);
                cornerRows.push_back(RowSrc::copy(c));
            }
            faceRows.push_back(RowSrc::copy(static_cast<int32_t>(f)));
            offsets.push_back(static_cast<int32_t>(verts.size()));
        }
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(verts));
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
        out.cornerAttrs = reflectAttrs(buildAttrs(in.cornerAttrs.get(), cornerRows, nullptr), nc);
        out.cornerGroups = buildGroups(in.cornerGroups.get(), cornerRows);
        out.faceAttrs = reflectAttrs(buildAttrs(in.faceAttrs.get(), faceRows, nullptr), nf);
        out.faceGroups = buildGroups(in.faceGroups.get(), faceRows);
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

}  // namespace

Value evalTopologyOpsBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Extrude: return opExtrude(bound, run);
        case BuiltinId::Inset: return opInset(bound, run);
        case BuiltinId::Separate: return opSeparate(bound, run);
        case BuiltinId::Triangulate: return opTriangulate(bound, run);
        case BuiltinId::Subdivide: return opSubdivide(bound, run);
        case BuiltinId::MergeByDistance: return opMergeByDistance(bound, run);
        case BuiltinId::Mirror: return opMirror(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
