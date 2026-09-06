#include "../../pch.h"

// §8.3 merge (E2 subset of topology) and §8.8 scatter/instancing.
//
// merge<K>: mesh (index shift) and points concatenation, no welding. Columns
// and groups unite by name with zero-fill for the side that lacks them
// (detail: single element, a wins). A name carried by BOTH operands must sit
// on the same domain set and compare equal on detail — otherwise E609 (silent
// zero-fill / shadowing / left-wins data loss is refused). Instances-merge is
// a clean stage error; a kind mismatch is E204.
//
// distribute_points: deterministic priority scheme (spec §19):
//   1. per-face candidates K_f = ceil(area_f * dMax_f * kOversample),
//      dMax_f = max density over the face's vertices and centroid;
//   2. density thinning with d(candidate)/dMax_f — the density field is
//      evaluated once on a probe geometry of all candidates, so it sees
//      barycentrically interpolated surface attributes (and groups at > 0.5);
//   3. poisson min_dist: a thinning-accepted candidate is kept iff no
//      thinning-accepted candidate with a strictly higher priority (ties by
//      candidate index) lies within min_dist — checked against a read-only
//      spatial hash of all accepted candidates, so the kept set is unique
//      and the result is order-independent by construction (N1); uniform
//      mode skips this stage.
//   Poisson rate is not guaranteed: the contract is determinism by rng,
//   min_dist compliance, density response and zero density -> zero points.
//   Candidate address: (rng, face_id, attempt*5 + lane) with lanes
//   {triangle pick, u, v, priority, thinning}.
//
// Parallel structure (N7): candidate generation per face (precomputed slice
// offsets), probe/thinning per candidate, the poisson check per candidate
// (read-only hash); only the hash build and the final kept-set compaction
// stay sequential (compaction keeps ascending candidate order = face,
// attempt).
//
// instance_on_points: lightweight geo<instances> — anchor points share the
// stamp attributes (@scale/@orient/@variant/@tint with defaults), sources
// are shared by pointer (variant list or the single source); nothing is
// copied until realize. realize applies T(P)*R(orient)*S(scale) per point in
// @index order, materializes @tint as a point attribute, and copies the
// source point attributes (union across variants, zero-filled).

#include <glm/gtc/quaternion.hpp>

#include <set>

#include "builtins.h"
#include "parallel.h"

namespace pgg {
namespace {

// ---------------------------------------------------------------- merge ----

void mergeAttrDomain(const Geo& a, const Geo& b, Domain d, AttrSet& out) {
    const AttrSet* as = a.attrs(d);
    const AttrSet* bs = b.attrs(d);
    if (d == Domain::Detail) {
        // One element: union of names, a wins.
        if (bs)
            for (const auto& [n, c] : bs->columns) out.columns[n] = c;
        if (as)
            for (const auto& [n, c] : as->columns) out.columns[n] = c;
        return;
    }
    const size_t aCount = a.elementCount(d);
    const size_t bCount = b.elementCount(d);
    // One-sided names get the typed neutral fill (v1.14): identity for
    // quaternions, derived normals for Normal-tagged columns, zeros otherwise.
    if (as) {
        for (const auto& [n, c] : as->columns) {
            const AttrColumn* cb = bs ? bs->find(n) : nullptr;
            out.columns[n] = AttrColumn{cb ? concatColumns(c.data, cb->data)
                                           : concatColumns(c.data, neutralColumnLike(n, c, b, d, bCount)),
                                        c.typeInfo};
        }
    }
    if (bs) {
        for (const auto& [n, c] : bs->columns) {
            if (as && as->find(n)) continue;  // handled above
            out.columns[n] = AttrColumn{concatColumns(neutralColumnLike(n, c, a, d, aCount), c.data), c.typeInfo};
        }
    }
}

void mergeGroupDomain(const Geo& a, const Geo& b, Domain d, GroupSet& out) {
    const GroupSet* as = a.groups(d);
    const GroupSet* bs = b.groups(d);
    if (d == Domain::Detail) {
        if (bs)
            for (const auto& [n, c] : bs->columns) out.columns[n] = c;
        if (as)
            for (const auto& [n, c] : as->columns) out.columns[n] = c;
        return;
    }
    const size_t aCount = a.elementCount(d);
    const size_t bCount = b.elementCount(d);
    if (as) {
        for (const auto& [n, c] : as->columns) {
            ConstBoolColumnPtr cb = bs ? bs->find(n) : nullptr;
            ColumnData merged = cb ? concatColumns(c, cb)
                                   : concatColumns(c, zeroColumnLike(ColumnData(c), bCount));
            out.columns[n] = std::get<ConstBoolColumnPtr>(merged);
        }
    }
    if (bs) {
        for (const auto& [n, c] : bs->columns) {
            if (as && as->find(n)) continue;
            ColumnData merged = concatColumns(zeroColumnLike(ColumnData(c), aCount), c);
            out.columns[n] = std::get<ConstBoolColumnPtr>(merged);
        }
    }
}

std::shared_ptr<const AttrSet> mergedAttrs(const Geo& a, const Geo& b, Domain d) {
    if (!a.attrs(d) && !b.attrs(d)) return nullptr;
    AttrSet out;
    mergeAttrDomain(a, b, d, out);
    return std::make_shared<const AttrSet>(std::move(out));
}

std::shared_ptr<const GroupSet> mergedGroups(const Geo& a, const Geo& b, Domain d) {
    if (!a.groups(d) && !b.groups(d)) return nullptr;
    GroupSet out;
    mergeGroupDomain(a, b, d, out);
    return std::make_shared<const GroupSet>(std::move(out));
}

std::shared_ptr<const std::vector<glm::vec3>> mergedNormals(const Geo& a, const Geo& b) {
    if (!a.normals && !b.normals) return nullptr;
    std::vector<glm::vec3> out;
    out.reserve(a.pointCount() + b.pointCount());
    // The side without @N gets derived normals (v1.14) — a zero normal shaded
    // black and broke @N-driven displacement of the merged result.
    const glm::vec3 zero(0.0f);
    auto append = [&](const Geo& g) {
        std::shared_ptr<const std::vector<glm::vec3>> n = g.normals ? g.normals : derivedNormals(g, Domain::Points);
        if (n && n->size() == g.pointCount()) out.insert(out.end(), n->begin(), n->end());
        else out.resize(out.size() + g.pointCount(), zero);
    };
    append(a);
    append(b);
    return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
}

// --- E609 merge conflicts (§8.3, §19 v1.12) ------------------------------------
// A name carried by BOTH operands must live on the same domain set, and a
// detail column must compare equal element-wise: merge concatenates the
// per-element domains and keeps the LEFT detail value, so a mismatch would
// silently zero-fill one side (the fresh column then shadows it on the read
// path) or destroy one side's global value. One-sided names stay the
// documented zero-fill/union case and are not conflicts.

unsigned attrDomainMask(const Geo& g, const std::string& name) {
    unsigned m = 0;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* as = g.attrs(d);
        if (as && as->find(name)) m |= 1u << static_cast<unsigned>(d);
    }
    return m;
}

unsigned groupDomainMask(const Geo& g, const std::string& name) {
    unsigned m = 0;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const GroupSet* gs = g.groups(d);
        if (gs && gs->find(name)) m |= 1u << static_cast<unsigned>(d);
    }
    return m;
}

std::string domainMaskString(unsigned mask) {
    std::string out;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        if (!(mask & (1u << static_cast<unsigned>(d)))) continue;
        if (!out.empty()) out += "+";
        out += domainName(d);
    }
    return out;
}

bool attrColumnsEqual(const ColumnData& a, const ColumnData& b) {
    if (a.index() != b.index()) return false;
    return std::visit([&](const auto& x) { return *x == *std::get<std::decay_t<decltype(x)>>(b); },
                      a);
}

Value opMerge(const BoundCall& bound, RunContext& run) {
    const Geo& a = *asGeo(bound.values[0]);
    const Geo& b = *asGeo(bound.values[1]);
    if (a.kind == GeoKind::Instances || b.kind == GeoKind::Instances) {
        run.report("E201", bound.span, "merge of geo<instances> is not supported at stage E4",
                   "realize the instances first, then merge meshes");
        return Value(asGeo(bound.values[0]));
    }
    if (a.kind != b.kind) {
        run.report("E204", bound.span,
                   "merge<K> needs two geometries of the same kind, got geo<" +
                       std::string(geoKindName(a.kind)) + "> and geo<" + std::string(geoKindName(b.kind)) + ">");
        return Value(asGeo(bound.values[0]));
    }

    // E609: report every conflicting shared name, then recover on the left.
    bool conflict = false;
    auto checkSharedName = [&](const std::string& name, bool isGroup) {
        const unsigned ma = isGroup ? groupDomainMask(a, name) : attrDomainMask(a, name);
        const unsigned mb = isGroup ? groupDomainMask(b, name) : attrDomainMask(b, name);
        if (!ma || !mb) return;
        const char* what = isGroup ? "group" : "attribute";
        if (ma != mb) {
            run.report("E609", bound.span,
                       std::string("merge: ") + what + " '@" + name +
                           "' sits on different domains in the two operands (" + domainMaskString(ma) + " vs " +
                           domainMaskString(mb) + ") — one side would be zero-filled and shadow the other",
                       isGroup ? "mark it on the same domain on both sides, or unmark one side"
                               : "promote it to the same domain on both sides (promote(g, \"" + name +
                                     "\", from = detail, to = points)), or remove_attr/rename_attr one side");
            conflict = true;
            return;
        }
        if (!isGroup) {
            // Same domains: the typeinfo tag must agree too, otherwise transform
            // would move one half of the merged column and leave the other.
            for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
                if (!(ma & (1u << static_cast<unsigned>(d)))) continue;
                const AttrColumn* ca = a.attrs(d)->find(name);
                const AttrColumn* cb = b.attrs(d)->find(name);
                if (ca->typeInfo != cb->typeInfo) {
                    run.report("E609", bound.span,
                               "merge: attribute '@" + name + "' has typeinfo " +
                                   std::string(attrTypeInfoName(ca->typeInfo)) + " on the left and " +
                                   std::string(attrTypeInfoName(cb->typeInfo)) + " on the right",
                               "write both sides with the same set(..., typeinfo = ...)");
                    conflict = true;
                    return;
                }
            }
        }
        if (!(ma & (1u << static_cast<unsigned>(Domain::Detail)))) return;
        if (!isGroup) {
            if (!attrColumnsEqual(a.detailAttrs->find(name)->data, b.detailAttrs->find(name)->data)) {
                run.report("E609", bound.span,
                           "merge: detail attribute '@" + name +
                               "' differs between the operands — the left value would silently win",
                           "for per-element data write it per point (set(..., domain = points) or promote to "
                           "points) before merging; remove_attr disposable metadata");
                conflict = true;
            }
        } else if (*a.detailGroups->find(name) != *b.detailGroups->find(name)) {
            run.report("E609", bound.span,
                       "merge: detail group '@" + name +
                           "' differs between the operands — the left value would silently win",
                       "mark it per element (domain = points/faces) before merging, or unmark one side");
            conflict = true;
        }
    };
    std::set<std::string> attrNames, groupNames;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        if (const AttrSet* as = a.attrs(d))
            for (const auto& [n, c] : as->columns) attrNames.insert(n);
        if (const GroupSet* gs = a.groups(d))
            for (const auto& [n, c] : gs->columns) groupNames.insert(n);
    }
    for (const std::string& n : attrNames) checkSharedName(n, false);
    for (const std::string& n : groupNames) checkSharedName(n, true);
    if (conflict) return Value(asGeo(bound.values[0]));

    Geo out;
    out.kind = a.kind;
    {
        std::vector<glm::vec3> pos;
        pos.reserve(a.pointCount() + b.pointCount());
        pos.insert(pos.end(), a.positions->begin(), a.positions->end());
        pos.insert(pos.end(), b.positions->begin(), b.positions->end());
        out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    }
    out.normals = mergedNormals(a, b);
    if (a.kind == GeoKind::Mesh) {
        std::vector<int32_t> corners;
        corners.reserve(a.cornerCount() + b.cornerCount());
        corners.insert(corners.end(), a.cornerVerts->begin(), a.cornerVerts->end());
        const int32_t shift = static_cast<int32_t>(a.pointCount());
        for (int32_t v : *b.cornerVerts) corners.push_back(v + shift);
        std::vector<int32_t> offsets(*a.faceOffsets);
        const int32_t cornerBase = static_cast<int32_t>(a.cornerCount());
        for (size_t i = 1; i < b.faceOffsets->size(); ++i)
            offsets.push_back((*b.faceOffsets)[i] + cornerBase);
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(corners));
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    }
    out.pointAttrs = mergedAttrs(a, b, Domain::Points);
    out.cornerAttrs = mergedAttrs(a, b, Domain::Corners);
    out.faceAttrs = mergedAttrs(a, b, Domain::Faces);
    out.detailAttrs = mergedAttrs(a, b, Domain::Detail);
    out.pointGroups = mergedGroups(a, b, Domain::Points);
    out.cornerGroups = mergedGroups(a, b, Domain::Corners);
    out.faceGroups = mergedGroups(a, b, Domain::Faces);
    out.detailGroups = mergedGroups(a, b, Domain::Detail);
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// ------------------------------------------------------- distribute_points ----

constexpr float kScatterOversample = 4.0f;  // v0 candidate oversampling (spec §19)

struct Candidate {
    int32_t face = 0;
    int32_t v0 = 0, v1 = 0, v2 = 0;  // triangle point indices (fan)
    int32_t c0 = 0, c1 = 0, c2 = 0;  // triangle corner indices
    glm::vec3 bary{0.0f};
    glm::vec3 pos{0.0f};
    float dMax = 0.0f;
    float thin = 0.0f;
    uint32_t priority = 0;
};

enum class TriIdx { Points, Corners };

// Barycentric gather over the triangle indices of every candidate: int rounds
// to nearest, bool thresholds at > 0.5, strings take the max-weight value.
ColumnData barySample(const ColumnData& col, const std::vector<Candidate>& cands, TriIdx which,
                      unsigned threads) {
    return std::visit(
        [&](const auto& ptr) -> ColumnData {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            VecT out(cands.size());
            parallelFor(cands.size(), threads, [&](size_t s, size_t e) {
                for (size_t i = s; i < e; ++i) {
                    const Candidate& c = cands[i];
                    const int32_t ia = which == TriIdx::Points ? c.v0 : c.c0;
                    const int32_t ib = which == TriIdx::Points ? c.v1 : c.c1;
                    const int32_t ic = which == TriIdx::Points ? c.v2 : c.c2;
                    const ElemT& va = (*ptr)[static_cast<size_t>(ia)];
                    const ElemT& vb = (*ptr)[static_cast<size_t>(ib)];
                    const ElemT& vc = (*ptr)[static_cast<size_t>(ic)];
                    if constexpr (std::is_same_v<ElemT, float>) {
                        out[i] = c.bary.x * va + c.bary.y * vb + c.bary.z * vc;
                    } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                        const double w = c.bary.x * static_cast<double>(va) + c.bary.y * static_cast<double>(vb) +
                                         c.bary.z * static_cast<double>(vc);
                        out[i] = static_cast<int64_t>(std::llround(w));
                    } else if constexpr (std::is_same_v<ElemT, uint8_t>) {
                        const float w = c.bary.x * (va ? 1.0f : 0.0f) + c.bary.y * (vb ? 1.0f : 0.0f) +
                                        c.bary.z * (vc ? 1.0f : 0.0f);
                        out[i] = w > 0.5f ? 1 : 0;
                    } else if constexpr (std::is_same_v<ElemT, std::string>) {
                        out[i] = c.bary.x >= c.bary.y && c.bary.x >= c.bary.z ? va
                                 : c.bary.y >= c.bary.z                      ? vb
                                                                              : vc;
                    } else {  // glm vectors
                        out[i] = c.bary.x * va + c.bary.y * vb + c.bary.z * vc;
                    }
                }
            });
            return std::make_shared<const VecT>(std::move(out));
        },
        col);
}

Value opDistribute(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const FieldNode* densityField = bound.fields[1];
    const std::string& mode = asString(bound.values[2]);
    const float minDist = std::max(0.0f, asF32(bound.values[3]));
    const Rng rng = asRng(bound.values[4]);

    GeoPtr empty = makePoints({});
    if (in.kind != GeoKind::Mesh || in.faceCount() == 0 || !densityField) return Value(empty);

    // 1. density on the vertices and at the face centroids.
    ConstBufferPtr dPtsB = convertBuffer(evalField(densityField, in, Domain::Points, run), ScalarType::F32);
    ConstBufferPtr dFaceB = convertBuffer(evalField(densityField, in, Domain::Faces, run), ScalarType::F32);
    const auto& dPts = std::get<F32Buf>(*dPtsB);
    const auto& dFace = std::get<F32Buf>(*dFaceB);

    // 2. per-face candidates addressed by (rng, face_id, attempt). Counts and
    //    slice offsets are a cheap sequential pass; the fill runs per face on
    //    the pool (each face writes only its own slice, ascending candidate
    //    order = (face, attempt), identical to the old append order).
    std::vector<int32_t> faceK(in.faceCount(), 0);
    std::vector<float> faceDmax(in.faceCount(), 0.0f);
    std::vector<int32_t> candBegin(in.faceCount() + 1, 0);
    for (size_t f = 0; f < in.faceCount(); ++f) {
        const float area = 0.5f * glm::length(faceNormal(in, f));
        float dMax = dFace[f];
        const int32_t begin = (*in.faceOffsets)[f];
        const int32_t end = (*in.faceOffsets)[f + 1];
        for (int32_t c = begin; c < end; ++c) dMax = std::max(dMax, dPts[static_cast<size_t>((*in.cornerVerts)[c])]);
        dMax = std::max(dMax, 0.0f);
        if (area <= 0.0f || dMax <= 0.0f) continue;
        faceDmax[f] = dMax;
        faceK[f] = static_cast<int32_t>(std::ceil(area * dMax * kScatterOversample));
    }
    for (size_t f = 0; f < in.faceCount(); ++f) candBegin[f + 1] = candBegin[f] + faceK[f];
    std::vector<Candidate> cands(static_cast<size_t>(candBegin.back()));
    parallelFor(in.faceCount(), run.threads, [&](size_t s, size_t e) {
        for (size_t f = s; f < e; ++f) {
            const int32_t k = faceK[f];
            if (k <= 0) continue;
            const float dMax = faceDmax[f];
            const int32_t begin = (*in.faceOffsets)[f];
            const int32_t end = (*in.faceOffsets)[f + 1];

            // Fan triangles and their areas (for the area-weighted pick).
            std::vector<float> triAreas;
            float totalArea = 0.0f;
            const glm::vec3& p0 = (*in.positions)[static_cast<size_t>((*in.cornerVerts)[begin])];
            for (int32_t c = begin + 1; c + 1 < end; ++c) {
                const glm::vec3& pa = (*in.positions)[static_cast<size_t>((*in.cornerVerts)[c])];
                const glm::vec3& pb = (*in.positions)[static_cast<size_t>((*in.cornerVerts)[c + 1])];
                const float ta = 0.5f * glm::length(glm::cross(pa - p0, pb - p0));
                triAreas.push_back(ta);
                totalArea += ta;
            }

            Candidate* out = cands.data() + candBegin[f];
            for (int32_t j = 0; j < k; ++j) {
                const uint32_t lane = static_cast<uint32_t>(j) * 5u;
                const float wTri = rngF32(rng, static_cast<uint64_t>(f), lane + 0);
                const float u = rngF32(rng, static_cast<uint64_t>(f), lane + 1);
                const float v = rngF32(rng, static_cast<uint64_t>(f), lane + 2);
                Candidate cand;
                cand.priority = rngWord(rng, static_cast<uint64_t>(f), lane + 3);
                cand.thin = rngF32(rng, static_cast<uint64_t>(f), lane + 4);
                cand.face = static_cast<int32_t>(f);
                cand.dMax = dMax;
                // area-weighted triangle pick inside the fan
                float x = wTri * totalArea;
                size_t tri = 0;
                while (tri + 1 < triAreas.size() && x > triAreas[tri]) {
                    x -= triAreas[tri];
                    ++tri;
                }
                cand.c0 = begin;
                cand.c1 = begin + 1 + static_cast<int32_t>(tri);
                cand.c2 = begin + 2 + static_cast<int32_t>(tri);
                cand.v0 = (*in.cornerVerts)[cand.c0];
                cand.v1 = (*in.cornerVerts)[cand.c1];
                cand.v2 = (*in.cornerVerts)[cand.c2];
                const float su = std::sqrt(u);
                cand.bary = glm::vec3(1.0f - su, su * (1.0f - v), su * v);
                cand.pos = cand.bary.x * (*in.positions)[static_cast<size_t>(cand.v0)] +
                           cand.bary.y * (*in.positions)[static_cast<size_t>(cand.v1)] +
                           cand.bary.z * (*in.positions)[static_cast<size_t>(cand.v2)];
                out[j] = cand;
            }
        }
    });
    if (cands.empty()) return Value(empty);

    // 3. probe geometry: candidates with barycentrically inherited surface
    //    attributes and groups, so the density field evaluates at each
    //    candidate position (§8.8).
    Geo probe;
    probe.kind = GeoKind::Points;
    {
        std::vector<glm::vec3> pos(cands.size());
        parallelFor(cands.size(), run.threads, [&](size_t s, size_t e) {
            for (size_t i = s; i < e; ++i) pos[i] = cands[i].pos;
        });
        probe.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    }
    if (in.normals) {
        std::vector<glm::vec3> nrm(cands.size());
        parallelFor(cands.size(), run.threads, [&](size_t s, size_t e) {
            for (size_t i = s; i < e; ++i) {
                const Candidate& c = cands[i];
                glm::vec3 n = c.bary.x * (*in.normals)[static_cast<size_t>(c.v0)] +
                              c.bary.y * (*in.normals)[static_cast<size_t>(c.v1)] +
                              c.bary.z * (*in.normals)[static_cast<size_t>(c.v2)];
                const float len = glm::length(n);
                nrm[i] = len > 0.0f ? n / len : glm::vec3(0, 1, 0);
            }
        });
        probe.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nrm));
    }
    std::vector<int32_t> faceIdx(cands.size());
    parallelFor(cands.size(), run.threads, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) faceIdx[i] = cands[i].face;
    });
    {
        AttrSet attrs;
        if (in.pointAttrs)
            for (const auto& [n, c] : in.pointAttrs->columns)
                attrs.columns[n] = AttrColumn{barySample(c.data, cands, TriIdx::Points, run.threads), c.typeInfo};
        if (in.cornerAttrs)
            for (const auto& [n, c] : in.cornerAttrs->columns)
                if (!attrs.find(n))
                    attrs.columns[n] = AttrColumn{barySample(c.data, cands, TriIdx::Corners, run.threads), c.typeInfo};
        if (in.faceAttrs)
            for (const auto& [n, c] : in.faceAttrs->columns)
                if (!attrs.find(n)) attrs.columns[n] = AttrColumn{gatherColumn(c.data, faceIdx), c.typeInfo};
        if (!attrs.columns.empty()) probe.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
        GroupSet groups;
        if (in.pointGroups)
            for (const auto& [n, c] : in.pointGroups->columns)
                groups.columns[n] =
                    std::get<ConstBoolColumnPtr>(barySample(ColumnData(c), cands, TriIdx::Points, run.threads));
        if (in.cornerGroups)
            for (const auto& [n, c] : in.cornerGroups->columns)
                if (!groups.find(n))
                    groups.columns[n] =
                        std::get<ConstBoolColumnPtr>(barySample(ColumnData(c), cands, TriIdx::Corners, run.threads));
        if (in.faceGroups)
            for (const auto& [n, c] : in.faceGroups->columns)
                if (!groups.find(n))
                    groups.columns[n] = std::get<ConstBoolColumnPtr>(gatherColumn(ColumnData(c), faceIdx));
        if (!groups.columns.empty()) probe.pointGroups = std::make_shared<const GroupSet>(std::move(groups));
        probe.detailAttrs = in.detailAttrs;
        probe.detailGroups = in.detailGroups;
    }
    GeoPtr probeGeo = std::make_shared<const Geo>(std::move(probe));

    // 4. density at the candidates, then thinning by d/dMax.
    ConstBufferPtr dCandB =
        convertBuffer(evalField(densityField, *probeGeo, Domain::Points, run), ScalarType::F32);
    const auto& dCand = std::get<F32Buf>(*dCandB);
    std::vector<uint8_t> accept(cands.size(), 0);
    parallelFor(cands.size(), run.threads, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
            const float ratio = std::clamp(dCand[i] / cands[i].dMax, 0.0f, 1.0f);
            accept[i] = cands[i].thin < ratio ? 1 : 0;
        }
    });

    // 5. poisson: keep a thinning-accepted candidate iff no thinning-accepted
    //    candidate with a strictly higher priority (ties by candidate index)
    //    lies within min_dist. The spatial hash holds every accepted
    //    candidate and is read-only during the check, so the kept set is
    //    unique and the loop parallelizes (N1 by construction).
    if (mode == "poisson" && minDist > 0.0f) {
        struct CellKey {
            int64_t x, y, z;
            bool operator==(const CellKey&) const = default;
        };
        struct CellHash {
            size_t operator()(const CellKey& k) const {
                size_t h = std::hash<int64_t>()(k.x);
                h = h * 1000003 + std::hash<int64_t>()(k.y);
                h = h * 1000003 + std::hash<int64_t>()(k.z);
                return h;
            }
        };
        std::unordered_map<CellKey, std::vector<int32_t>, CellHash> grid;
        const float md2 = minDist * minDist;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (!accept[i]) continue;
            const Candidate& c = cands[i];
            const int64_t cx = static_cast<int64_t>(std::floor(c.pos.x / minDist));
            const int64_t cy = static_cast<int64_t>(std::floor(c.pos.y / minDist));
            const int64_t cz = static_cast<int64_t>(std::floor(c.pos.z / minDist));
            grid[CellKey{cx, cy, cz}].push_back(static_cast<int32_t>(i));
        }
        std::vector<uint8_t> kept(cands.size(), 0);
        parallelFor(cands.size(), run.threads, [&](size_t s, size_t e) {
            for (size_t i = s; i < e; ++i) {
                if (!accept[i]) continue;
                const Candidate& c = cands[i];
                const int64_t cx = static_cast<int64_t>(std::floor(c.pos.x / minDist));
                const int64_t cy = static_cast<int64_t>(std::floor(c.pos.y / minDist));
                const int64_t cz = static_cast<int64_t>(std::floor(c.pos.z / minDist));
                bool blocked = false;
                for (int64_t dx = -1; dx <= 1 && !blocked; ++dx)
                    for (int64_t dy = -1; dy <= 1 && !blocked; ++dy)
                        for (int64_t dz = -1; dz <= 1 && !blocked; ++dz) {
                            auto it = grid.find(CellKey{cx + dx, cy + dy, cz + dz});
                            if (it == grid.end()) continue;
                            for (int32_t other : it->second) {
                                if (static_cast<size_t>(other) == i) continue;
                                const Candidate& o = cands[static_cast<size_t>(other)];
                                const bool higher = o.priority > c.priority ||
                                                    (o.priority == c.priority && static_cast<size_t>(other) < i);
                                if (higher && glm::dot(o.pos - c.pos, o.pos - c.pos) < md2) {
                                    blocked = true;
                                    break;
                                }
                            }
                        }
                kept[i] = blocked ? 0 : 1;
            }
        });
        accept = std::move(kept);
    }

    // 6. emit the kept points with the inherited data.
    std::vector<int32_t> keptIdx;
    for (size_t i = 0; i < cands.size(); ++i)
        if (accept[i]) keptIdx.push_back(static_cast<int32_t>(i));
    Geo out;
    out.kind = GeoKind::Points;
    {
        std::vector<glm::vec3> pos;
        pos.reserve(keptIdx.size());
        for (int32_t i : keptIdx) pos.push_back(cands[static_cast<size_t>(i)].pos);
        out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    }
    if (probeGeo->normals) {
        std::vector<glm::vec3> nrm;
        nrm.reserve(keptIdx.size());
        for (int32_t i : keptIdx) nrm.push_back((*probeGeo->normals)[static_cast<size_t>(i)]);
        out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(nrm));
    }
    if (probeGeo->pointAttrs) {
        AttrSet attrs;
        for (const auto& [n, c] : probeGeo->pointAttrs->columns)
            attrs.columns[n] = AttrColumn{gatherColumn(c.data, keptIdx), c.typeInfo};
        out.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    }
    if (probeGeo->pointGroups) {
        GroupSet groups;
        for (const auto& [n, c] : probeGeo->pointGroups->columns)
            groups.columns[n] = std::get<ConstBoolColumnPtr>(gatherColumn(ColumnData(c), keptIdx));
        out.pointGroups = std::make_shared<const GroupSet>(std::move(groups));
    }
    out.detailAttrs = probeGeo->detailAttrs;
    out.detailGroups = probeGeo->detailGroups;
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// ------------------------------------------------------------- instancing ----

Value opInstanceOnPoints(const BoundCall& bound, RunContext& run) {
    const Geo& pts = *asGeo(bound.values[0]);
    if (pts.kind != GeoKind::Points) {
        run.report("E204", bound.span, "instance_on_points expects geo<points> anchors, got geo<" +
                                           std::string(geoKindName(pts.kind)) + ">");
        return Value(asGeo(bound.values[0]));
    }
    auto sources = std::make_shared<std::vector<GeoPtr>>();
    bool ok = true;
    auto checkSource = [&](const GeoPtr& g) {
        if (g->kind != GeoKind::Mesh) {
            run.report("E204", bound.span, "instance sources must be geo<mesh>, got geo<" +
                                               std::string(geoKindName(g->kind)) + ">");
            ok = false;
            return;
        }
        sources->push_back(g);
    };
    if (isListValue(bound.values[2]) && !asList(bound.values[2]).empty()) {
        for (const Value& v : asList(bound.values[2])) checkSource(asGeo(v));
    } else {
        checkSource(asGeo(bound.values[1]));
    }
    if (!ok || sources->empty()) return Value(asGeo(bound.values[0]));

    Geo out = pts;  // anchors + stamp attributes stay shared
    out.kind = GeoKind::Instances;
    out.cornerVerts = nullptr;
    out.faceOffsets = nullptr;
    out.cornerAttrs = nullptr;
    out.faceAttrs = nullptr;
    out.cornerGroups = nullptr;
    out.faceGroups = nullptr;
    out.instanceSources = std::move(sources);
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// Stamp attribute readers (defaults when the column is absent).
float stampF32(const ColumnData* col, size_t i, float def) {
    if (!col) return def;
    switch (col->index()) {
        case 0: return (*std::get<std::shared_ptr<const std::vector<float>>>(*col))[i];
        case 1: return static_cast<float>((*std::get<std::shared_ptr<const std::vector<int64_t>>>(*col))[i]);
        case 2: return (*std::get<std::shared_ptr<const std::vector<uint8_t>>>(*col))[i] ? 1.0f : 0.0f;
        default: return def;
    }
}

int64_t stampInt(const ColumnData* col, size_t i, int64_t def) {
    if (!col) return def;
    switch (col->index()) {
        case 1: return (*std::get<std::shared_ptr<const std::vector<int64_t>>>(*col))[i];
        case 2: return (*std::get<std::shared_ptr<const std::vector<uint8_t>>>(*col))[i] ? 1 : 0;
        case 0: return static_cast<int64_t>((*std::get<std::shared_ptr<const std::vector<float>>>(*col))[i]);
        default: return def;
    }
}

glm::vec3 stampVec3(const ColumnData* col, size_t i, glm::vec3 def) {
    if (!col) return def;
    if (col->index() == 4) return (*std::get<std::shared_ptr<const std::vector<glm::vec3>>>(*col))[i];
    return def;
}

glm::vec4 stampVec4(const ColumnData* col, size_t i, glm::vec4 def) {
    if (!col) return def;
    if (col->index() == 5) return (*std::get<std::shared_ptr<const std::vector<glm::vec4>>>(*col))[i];
    return def;
}

std::optional<ColumnData> stampColumn(const Geo& inst, const std::string& name) {
    return sampleAttrColumn(inst, name, Domain::Points);
}

// Mutable accumulator matching the ColumnData alternative order (union schema
// across variants: first variant's type wins, others convert). Columns are
// pre-sized to the total point count; each anchor writes only its own slice,
// so the realize fill parallelizes over anchors bit-identically (N7).
using MutColumn = std::variant<std::vector<float>, std::vector<int64_t>, std::vector<uint8_t>,
                               std::vector<glm::vec2>, std::vector<glm::vec3>, std::vector<glm::vec4>,
                               std::vector<std::string>>;

MutColumn allocLike(const ColumnData& exemplar, size_t count) {
    return std::visit(
        [count](const auto& ptr) -> MutColumn {
            using VecT = std::decay_t<decltype(*ptr)>;
            return VecT(count);  // zero value = the union zero-fill
        },
        exemplar);
}

void writeConverted(MutColumn& acc, size_t dstBegin, const ColumnData* src, const ColumnData& exemplar,
                    size_t count) {
    if (!src) return;  // variant lacks the column: the pre-sized zeros stand
    ColumnData converted = src->index() == exemplar.index() ? *src : convertColumn(*src, exemplar);
    std::visit(
        [&](auto& v) {
            using VecT = std::decay_t<decltype(v)>;
            const auto& srcVec = std::get<std::shared_ptr<const VecT>>(converted);
            std::copy_n(srcVec->begin(), static_cast<ptrdiff_t>(count), v.begin() + static_cast<ptrdiff_t>(dstBegin));
        },
        acc);
}

ColumnData freezeColumn(MutColumn&& acc) {
    return std::visit(
        [](auto&& v) -> ColumnData {
            using VecT = std::decay_t<decltype(v)>;
            return std::make_shared<const VecT>(std::move(v));
        },
        std::move(acc));
}

}  // namespace

GeoPtr realizeInstances(const Geo& inst, unsigned threads) {
    if (inst.kind != GeoKind::Instances || !inst.instanceSources || inst.instanceSources->empty())
        return nullptr;
    const auto& sources = *inst.instanceSources;
    const size_t n = inst.pointCount();

    std::optional<ColumnData> scaleCol = stampColumn(inst, "scale");
    std::optional<ColumnData> orientCol = stampColumn(inst, "orient");
    std::optional<ColumnData> variantCol = stampColumn(inst, "variant");
    std::optional<ColumnData> tintCol = stampColumn(inst, "tint");

    // Per-anchor source pick and slice offsets (sequential prefix pass).
    std::vector<int32_t> srcIdx(n, 0);
    std::vector<size_t> ptBase(n, 0), cornerBase(n, 0), faceBase(n, 0);
    size_t totalPts = 0, totalCorners = 0, totalFaces = 0;
    bool anyNormals = false;
    for (const GeoPtr& s : sources) anyNormals = anyNormals || s->normals != nullptr;
    for (size_t i = 0; i < n; ++i) {
        const int64_t v = stampInt(variantCol ? &*variantCol : nullptr, i, 0);
        srcIdx[i] = static_cast<int32_t>(
            std::clamp<int64_t>(v, 0, static_cast<int64_t>(sources.size()) - 1));
        const Geo& src = *sources[static_cast<size_t>(srcIdx[i])];
        ptBase[i] = totalPts;
        cornerBase[i] = totalCorners;
        faceBase[i] = totalFaces;
        totalPts += src.pointCount();
        totalCorners += src.cornerCount();
        totalFaces += src.faceCount();
    }

    // Union schema of the source attributes/groups per domain (first variant
    // wins the type). Points, corners and faces are all carried: a stamped
    // source keeps its material face-groups and its corner N from
    // compute_normals(mode = flat) — realize used to drop everything but the
    // points domain. Detail follows merge's rule: the first variant's columns.
    struct DomainPlan {
        Domain domain;
        std::vector<std::pair<std::string, AttrColumn>> attrSchema;  // exemplar: type + typeinfo
        std::vector<std::pair<std::string, ColumnData>> groupSchema;
        std::vector<MutColumn> attrOut;
        std::vector<MutColumn> groupOut;
        const std::vector<size_t>* base = nullptr;
        size_t total = 0;
    };
    DomainPlan plans[3] = {{Domain::Points}, {Domain::Corners}, {Domain::Faces}};
    plans[0].base = &ptBase;
    plans[0].total = totalPts;
    plans[1].base = &cornerBase;
    plans[1].total = totalCorners;
    plans[2].base = &faceBase;
    plans[2].total = totalFaces;
    for (DomainPlan& plan : plans) {
        for (const GeoPtr& s : sources) {
            if (const AttrSet* as = s->attrs(plan.domain))
                for (const auto& [name, c] : as->columns) {
                    const bool seen = std::any_of(plan.attrSchema.begin(), plan.attrSchema.end(),
                                                  [&](const auto& e) { return e.first == name; });
                    if (!seen) plan.attrSchema.push_back({name, c});
                }
            if (const GroupSet* gs = s->groups(plan.domain))
                for (const auto& [name, c] : gs->columns) {
                    const bool seen = std::any_of(plan.groupSchema.begin(), plan.groupSchema.end(),
                                                  [&](const auto& e) { return e.first == name; });
                    if (!seen) plan.groupSchema.push_back({name, ColumnData(c)});
                }
        }
        for (const auto& [name, ex] : plan.attrSchema) plan.attrOut.push_back(allocLike(ex.data, plan.total));
        for (const auto& [name, ex] : plan.groupSchema) plan.groupOut.push_back(allocLike(ex, plan.total));
    }

    std::vector<glm::vec3> positions(totalPts);
    std::vector<glm::vec3> normals(anyNormals ? totalPts : 0);
    std::vector<int32_t> corners(totalCorners);
    std::vector<int32_t> offsets(totalFaces + 1);
    offsets[0] = 0;
    std::vector<glm::vec3> tint(totalPts);

    // Per-anchor fill: transform T(P)*R(orient)*S(scale) per anchor in @index
    // order, every anchor writing only its precomputed slices (N7).
    parallelFor(n, threads, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
            const Geo& src = *sources[static_cast<size_t>(srcIdx[i])];
            const float sc = stampF32(scaleCol ? &*scaleCol : nullptr, i, 1.0f);
            const glm::vec4 o = stampVec4(orientCol ? &*orientCol : nullptr, i, glm::vec4(0, 0, 0, 1));
            glm::quat q(o.w, o.x, o.y, o.z);
            const float qlen = glm::length(q);
            q = qlen > 0.0f ? q / qlen : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            const glm::vec3 anchor = (*inst.positions)[i];

            const size_t pb = ptBase[i];
            const size_t sp = src.pointCount();
            for (size_t k = 0; k < sp; ++k) positions[pb + k] = anchor + q * ((*src.positions)[k] * sc);
            if (anyNormals) {
                for (size_t k = 0; k < sp; ++k)
                    normals[pb + k] = src.normals ? q * (*src.normals)[k] : glm::vec3(0.0f);
            }
            if (src.cornerVerts) {
                const size_t cb = cornerBase[i];
                for (size_t k = 0; k < src.cornerVerts->size(); ++k)
                    corners[cb + k] = (*src.cornerVerts)[k] + static_cast<int32_t>(pb);
            }
            if (src.faceOffsets) {
                const size_t fb = faceBase[i];
                const int32_t cornerShift = static_cast<int32_t>(cornerBase[i]);
                for (size_t k = 1; k < src.faceOffsets->size(); ++k)
                    offsets[fb + k] = (*src.faceOffsets)[k] + cornerShift;
            }

            const glm::vec3 t = stampVec3(tintCol ? &*tintCol : nullptr, i, glm::vec3(1.0f));
            std::fill(tint.begin() + static_cast<ptrdiff_t>(pb),
                      tint.begin() + static_cast<ptrdiff_t>(pb + sp), t);
            for (DomainPlan& plan : plans) {
                const size_t db = (*plan.base)[i];
                const size_t count = src.elementCount(plan.domain);
                const AttrSet* as = src.attrs(plan.domain);
                const GroupSet* gs = src.groups(plan.domain);
                for (size_t a = 0; a < plan.attrSchema.size(); ++a) {
                    const AttrColumn& ex = plan.attrSchema[a].second;
                    const AttrColumn* col = as ? as->find(plan.attrSchema[a].first) : nullptr;
                    if (col) {
                        // Tagged columns follow the stamp T(P)*R(orient)*S(scale)
                        // (v1.14): the exemplar's tag rules, like its type.
                        AttrColumn tagged{col->data, ex.typeInfo};
                        ColumnData moved = ex.typeInfo == AttrTypeInfo::None
                                               ? col->data
                                               : transformColumn(tagged, q, glm::vec3(sc), anchor);
                        writeConverted(plan.attrOut[a], db, &moved, ex.data, count);
                    } else {
                        // Variant lacks the column: typed neutral fill.
                        ColumnData fill = neutralColumnLike(plan.attrSchema[a].first, ex, src, plan.domain, count);
                        if (ex.typeInfo != AttrTypeInfo::None)
                            fill = transformColumn(AttrColumn{fill, ex.typeInfo}, q, glm::vec3(sc), anchor);
                        writeConverted(plan.attrOut[a], db, &fill, ex.data, count);
                    }
                }
                for (size_t g = 0; g < plan.groupSchema.size(); ++g) {
                    ConstBoolColumnPtr col = gs ? gs->find(plan.groupSchema[g].first) : nullptr;
                    ColumnData asCol = ColumnData(col ? col : ConstBoolColumnPtr());
                    writeConverted(plan.groupOut[g], db, col ? &asCol : nullptr, plan.groupSchema[g].second, count);
                }
            }
        }
    });

    Geo out;
    out.kind = GeoKind::Mesh;
    out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(positions));
    if (anyNormals) out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(normals));
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(corners));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    for (DomainPlan& plan : plans) {
        AttrSet attrs;
        for (size_t a = 0; a < plan.attrSchema.size(); ++a)
            attrs.columns[plan.attrSchema[a].first] =
                AttrColumn{freezeColumn(std::move(plan.attrOut[a])), plan.attrSchema[a].second.typeInfo};
        // @tint materializes as a point attribute (default white).
        if (plan.domain == Domain::Points)
            attrs.columns["tint"] = AttrColumn{std::make_shared<const std::vector<glm::vec3>>(std::move(tint))};
        std::shared_ptr<const AttrSet> attrSet;
        if (!attrs.columns.empty()) attrSet = std::make_shared<const AttrSet>(std::move(attrs));
        std::shared_ptr<const GroupSet> groupSet;
        if (!plan.groupSchema.empty()) {
            GroupSet groups;
            for (size_t g = 0; g < plan.groupSchema.size(); ++g)
                groups.columns[plan.groupSchema[g].first] =
                    std::get<ConstBoolColumnPtr>(freezeColumn(std::move(plan.groupOut[g])));
            groupSet = std::make_shared<const GroupSet>(std::move(groups));
        }
        switch (plan.domain) {
            case Domain::Points: out.pointAttrs = attrSet; out.pointGroups = groupSet; break;
            case Domain::Corners: out.cornerAttrs = attrSet; out.cornerGroups = groupSet; break;
            default: out.faceAttrs = attrSet; out.faceGroups = groupSet; break;
        }
    }
    out.detailAttrs = sources.front()->detailAttrs;
    out.detailGroups = sources.front()->detailGroups;
    return std::make_shared<const Geo>(std::move(out));
}

namespace {

// realize contracts that realizeInstances (a library helper without a run
// context) resolves silently — surfaced as errors at the builtin boundary:
//  * @variant outside [0, variants) — clamped by the helper, E611 here;
//  * a name shared by several variants must sit on the same domain set with
//    the same typeinfo (union schema is first-variant-wins) — E609, the
//    merge rule, since realize IS a merge of the stamped copies.
bool checkRealizeContracts(const Geo& inst, const BoundCall& bound, RunContext& run) {
    const auto& sources = *inst.instanceSources;
    bool ok = true;
    if (std::optional<ColumnData> variantCol = stampColumn(inst, "variant"); variantCol && sources.size() > 0) {
        for (size_t i = 0; i < inst.pointCount(); ++i) {
            const int64_t v = stampInt(&*variantCol, i, 0);
            if (v < 0 || v >= static_cast<int64_t>(sources.size())) {
                run.report("E611", bound.span,
                           "realize: @variant = " + std::to_string(v) + " at anchor " + std::to_string(i) +
                               " is outside the variants list (" + std::to_string(sources.size()) + " entries)",
                           "keep @variant in [0, count) — e.g. @index % " + std::to_string(sources.size()));
                ok = false;
                break;
            }
        }
    }
    auto maskAttr = [](const Geo& g, const std::string& n, AttrTypeInfo& info) -> unsigned {
        unsigned m = 0;
        for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
            const AttrSet* as = g.attrs(d);
            if (const AttrColumn* c = as ? as->find(n) : nullptr) {
                m |= 1u << static_cast<unsigned>(d);
                info = c->typeInfo;
            }
        }
        return m;
    };
    auto maskGroup = [](const Geo& g, const std::string& n) -> unsigned {
        unsigned m = 0;
        for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
            const GroupSet* gs = g.groups(d);
            if (gs && gs->find(n)) m |= 1u << static_cast<unsigned>(d);
        }
        return m;
    };
    for (size_t a = 0; a < sources.size() && ok; ++a) {
        for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
            if (const AttrSet* as = sources[a]->attrs(d)) {
                for (const auto& [n, col] : as->columns) {
                    for (size_t b = a + 1; b < sources.size() && ok; ++b) {
                        AttrTypeInfo ia = AttrTypeInfo::None, ib = AttrTypeInfo::None;
                        const unsigned ma = maskAttr(*sources[a], n, ia), mb = maskAttr(*sources[b], n, ib);
                        if (!mb) continue;
                        if (ma != mb) {
                            run.report("E609", bound.span,
                                       "realize: attribute '@" + n + "' sits on different domains in variants " +
                                           std::to_string(a) + " and " + std::to_string(b),
                                       "promote/remove_attr so every variant carries it on the same domain");
                            ok = false;
                        } else if (ia != ib) {
                            run.report("E609", bound.span,
                                       "realize: attribute '@" + n + "' has typeinfo " + attrTypeInfoName(ia) +
                                           " in variant " + std::to_string(a) + " and " + attrTypeInfoName(ib) +
                                           " in variant " + std::to_string(b),
                                       "write it with the same set(..., typeinfo = ...) in every variant");
                            ok = false;
                        }
                    }
                }
            }
            if (const GroupSet* gs = sources[a]->groups(d)) {
                for (const auto& [n, col] : gs->columns) {
                    for (size_t b = a + 1; b < sources.size() && ok; ++b) {
                        const unsigned ma = maskGroup(*sources[a], n), mb = maskGroup(*sources[b], n);
                        if (mb && ma != mb) {
                            run.report("E609", bound.span,
                                       "realize: group '@" + n + "' sits on different domains in variants " +
                                           std::to_string(a) + " and " + std::to_string(b),
                                       "mark it on the same domain in every variant");
                            ok = false;
                        }
                    }
                }
            }
        }
    }
    return ok;
}

Value opRealize(const BoundCall& bound, RunContext& run) {
    const Geo& inst = *asGeo(bound.values[0]);
    if (inst.kind == GeoKind::Instances && inst.instanceSources && !inst.instanceSources->empty() &&
        !checkRealizeContracts(inst, bound, run))
        return Value(asGeo(bound.values[0]));
    GeoPtr realized = realizeInstances(inst, run.threads);
    if (!realized) {
        run.report("E204", bound.span, "realize expects geo<instances>, got geo<" +
                                           std::string(geoKindName(inst.kind)) + ">");
        return Value(asGeo(bound.values[0]));
    }
    return Value(realized);
}

}  // namespace

Value evalScatterBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Merge: return opMerge(bound, run);
        case BuiltinId::DistributePoints: return opDistribute(bound, run);
        case BuiltinId::InstanceOnPoints: return opInstanceOnPoints(bound, run);
        case BuiltinId::Realize: return opRealize(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
