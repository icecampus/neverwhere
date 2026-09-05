#include "../../pch.h"

#include "sdf.h"

#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "builtins.h"
#include "mc_tables.h"
#include "parallel.h"

namespace pgg {
namespace {

// --- pinned formulas (spec §19 v1.2) -----------------------------------------

// Polynomial smooth-min (iq): h = clamp(0.5 + 0.5*(b-a)/k, 0, 1);
// smin(a,b,k) = mix(b,a,h) - k*h*(1-h). k <= 0 falls back to the hard op.
// +inf is the identity (empty instance field; inf*0 would be NaN otherwise).
float sdfSmin(float a, float b, float k) {
    if (k <= 0.0f) return std::min(a, b);
    if (a == std::numeric_limits<float>::infinity()) return b;
    if (b == std::numeric_limits<float>::infinity()) return a;
    const float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return b + (a - b) * h - k * h * (1.0f - h);
}

// subtract_smooth(a,b,k) = smax(a,-b,k) with smax(a,b,k) = -smin(-a,-b,k).
float sdfSmax(float a, float b, float k) {
    if (k <= 0.0f) return std::max(a, b);
    return -sdfSmin(-a, -b, k);
}

float distToAABB(const glm::vec3& p, const glm::vec3& mn, const glm::vec3& mx) {
    float d2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float v = p[i] < mn[i] ? mn[i] - p[i] : p[i] > mx[i] ? p[i] - mx[i] : 0.0f;
        d2 += v * v;
    }
    return std::sqrt(d2);
}

// Closest point on a triangle (Ericson, Real-Time Collision Detection §5.1.5;
// same region logic as geometry.cpp's pointTriangleDistance).
glm::vec3 closestPointTriangle(const glm::vec3& p, const glm::vec3& a,
                               const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

glm::vec3 triCentroid(const MeshBvh::Tri& t) { return (t.a + t.b + t.c) / 3.0f; }

const FieldNode* deepCopyField(const FieldNode* node, std::vector<std::unique_ptr<FieldNode>>& arena,
                               std::unordered_map<const FieldNode*, const FieldNode*>& memo) {
    if (!node) return nullptr;
    if (auto it = memo.find(node); it != memo.end()) return it->second;
    arena.push_back(std::make_unique<FieldNode>(*node));
    FieldNode* copy = arena.back().get();
    memo[node] = copy;
    copy->args.clear();
    for (const FieldNode* a : node->args) copy->args.push_back(deepCopyField(a, arena, memo));
    return copy;
}

}  // namespace

float SdfNode::eval(const glm::vec3& p) const {
    switch (kind) {
        case SdfKind::Sphere:
            return glm::length(p) - r;
        case SdfKind::Box: {
            // iq box SDF against size/2
            const glm::vec3 q = glm::abs(p) - size * 0.5f;
            const glm::vec3 clamped = glm::max(q, glm::vec3(0.0f));
            return glm::length(clamped) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
        }
        case SdfKind::Union:
            return std::min(a->eval(p), b->eval(p));
        case SdfKind::UnionSmooth:
            return sdfSmin(a->eval(p), b->eval(p), k);
        case SdfKind::Subtract:
            return std::max(a->eval(p), -b->eval(p));
        case SdfKind::SubtractSmooth:
            return sdfSmax(a->eval(p), -b->eval(p), k);
        case SdfKind::Intersect:
            return std::max(a->eval(p), b->eval(p));
        case SdfKind::Grind: {
            // max(a, a - b + gap): a keeps the half of the penetration where
            // it lies deeper than b (middle surface a == b), backed off by
            // gap/2 per side (|grad(a - b)| ~ 2 across a facing contact).
            const float fa = a->eval(p);
            return std::max(fa, fa - b->eval(p) + k);
        }
        case SdfKind::Displace: {
            const float base = a->eval(p);
            if (!amount) return base;  // E307 recovery: amount counts as 0
            return base + numericValueF32(evalFieldAtPoint(amount, p));
        }
        case SdfKind::Instance: {
            float cur = std::numeric_limits<float>::infinity();
            for (const SdfInstanceAnchor& an : anchors) {
                const float bd = distToAABB(p, an.aabbMin, an.aabbMax);
                // Exact culling: outside the anchor's AABB the contribution
                // dominates the box distance, and smin(a,b,k) == a exactly
                // when b-a >= k — skipping is bit-identical to evaluating.
                if (bd > 0.0f && bd >= cur + k) continue;
                const glm::vec3 pl = glm::inverse(an.orient) * (p - an.pos) / an.scale;
                const float d = a->eval(pl) * an.scale;
                cur = k > 0.0f ? sdfSmin(cur, d, k) : std::min(cur, d);
            }
            return cur;
        }
        case SdfKind::Grid: {
            const glm::vec3 mx = origin + glm::vec3(dims - 1) * voxel;
            const glm::vec3 pc = glm::clamp(p, origin, mx);
            const glm::vec3 t = (pc - origin) / voxel;
            const glm::vec3 i0f = glm::floor(t);
            const glm::ivec3 i0(i0f);
            const glm::ivec3 i1 = glm::min(i0 + 1, dims - 1);
            const glm::vec3 f = glm::clamp(t - i0f, 0.0f, 1.0f);
            auto at = [&](int x, int y, int z) {
                return (*values)[(static_cast<size_t>(z) * dims.y + y) * dims.x + x];
            };
            const float x00 = at(i0.x, i0.y, i0.z) + (at(i1.x, i0.y, i0.z) - at(i0.x, i0.y, i0.z)) * f.x;
            const float x10 = at(i0.x, i1.y, i0.z) + (at(i1.x, i1.y, i0.z) - at(i0.x, i1.y, i0.z)) * f.x;
            const float x01 = at(i0.x, i0.y, i1.z) + (at(i1.x, i0.y, i1.z) - at(i0.x, i0.y, i1.z)) * f.x;
            const float x11 = at(i0.x, i1.y, i1.z) + (at(i1.x, i1.y, i1.z) - at(i0.x, i1.y, i1.z)) * f.x;
            const float y0 = x00 + (x10 - x00) * f.y;
            const float y1 = x01 + (x11 - x01) * f.y;
            const float s = y0 + (y1 - y0) * f.z;
            // Outside the lattice: extend conservatively (the boundary slab
            // holds positive distances for the 3-voxel margin).
            return s + glm::length(p - pc);
        }
        case SdfKind::VoronoiCell: {
            float cur = -std::numeric_limits<float>::infinity();
            for (size_t i = 0; i < cellNormals.size(); ++i)
                cur = std::max(cur, glm::dot(p - cellPoints[i], cellNormals[i]));
            return cur;
        }
    }
    return 0.0f;
}

void SdfNode::conservativeBBox(glm::vec3& outMin, glm::vec3& outMax) const {
    switch (kind) {
        case SdfKind::Sphere:
            outMin = glm::vec3(-r);
            outMax = glm::vec3(r);
            return;
        case SdfKind::Box:
            outMin = size * -0.5f;
            outMax = size * 0.5f;
            return;
        case SdfKind::Union:
        case SdfKind::UnionSmooth: {
            glm::vec3 amn, amx, bmn, bmx;
            a->conservativeBBox(amn, amx);
            b->conservativeBBox(bmn, bmx);
            // The smooth blend bulges outward by at most k from the hull.
            const float m = kind == SdfKind::UnionSmooth ? std::max(k, 0.0f) : 0.0f;
            outMin = glm::min(amn, bmn) - m;
            outMax = glm::max(amx, bmx) + m;
            return;
        }
        case SdfKind::Subtract:
        case SdfKind::SubtractSmooth:
            // max(a, -b) >= a: the first child's box is cull-safe.
            a->conservativeBBox(outMin, outMax);
            return;
        case SdfKind::Grind:
            // max(a, a - b + gap) >= a: same invariant as Subtract.
            a->conservativeBBox(outMin, outMax);
            return;
        case SdfKind::Intersect:
            // The bbox intersection is tighter and contains the iso, but it
            // is NOT cull-safe (max(a,b) can fall below the distance to the
            // intersection outside it); bbox(a) satisfies both invariants.
            a->conservativeBBox(outMin, outMax);
            return;
        case SdfKind::Displace: {
            a->conservativeBBox(outMin, outMax);
            const float m = ampEstimate * 1.3f;
            outMin -= m;
            outMax += m;
            return;
        }
        case SdfKind::Instance: {
            outMin = glm::vec3(std::numeric_limits<float>::infinity());
            outMax = glm::vec3(-std::numeric_limits<float>::infinity());
            const float m = std::max(k, 0.0f);
            for (const SdfInstanceAnchor& an : anchors) {
                outMin = glm::min(outMin, an.aabbMin - m);
                outMax = glm::max(outMax, an.aabbMax + m);
            }
            return;
        }
        case SdfKind::Grid:
            outMin = origin;
            outMax = origin + glm::vec3(dims - 1) * voxel;
            return;
        case SdfKind::VoronoiCell:
            // Unbounded field: no finite conservative bbox. The node is only
            // valid inside an Intersect (bbox(a) wins there); reaching this
            // case directly yields the invalid box (mesh extraction -> empty).
            outMin = glm::vec3(std::numeric_limits<float>::infinity());
            outMax = glm::vec3(-std::numeric_limits<float>::infinity());
            return;
    }
}

// --- factories -----------------------------------------------------------------

SdfPtr sdfSphere(float r) {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::Sphere;
    n->r = r;
    return n;
}

SdfPtr sdfBox(glm::vec3 size) {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::Box;
    n->size = size;
    return n;
}

namespace {

SdfPtr sdfCsg(SdfKind kind, SdfPtr a, SdfPtr b, float k) {
    auto n = std::make_shared<SdfNode>();
    n->kind = kind;
    n->a = std::move(a);
    n->b = std::move(b);
    n->k = k;
    return n;
}

}  // namespace

SdfPtr sdfUnion(SdfPtr a, SdfPtr b) { return sdfCsg(SdfKind::Union, std::move(a), std::move(b), 0.0f); }
SdfPtr sdfUnionSmooth(SdfPtr a, SdfPtr b, float k) {
    return sdfCsg(SdfKind::UnionSmooth, std::move(a), std::move(b), k);
}
SdfPtr sdfSubtract(SdfPtr a, SdfPtr b) { return sdfCsg(SdfKind::Subtract, std::move(a), std::move(b), 0.0f); }
SdfPtr sdfSubtractSmooth(SdfPtr a, SdfPtr b, float k) {
    return sdfCsg(SdfKind::SubtractSmooth, std::move(a), std::move(b), k);
}
SdfPtr sdfIntersect(SdfPtr a, SdfPtr b) { return sdfCsg(SdfKind::Intersect, std::move(a), std::move(b), 0.0f); }
SdfPtr sdfGrind(SdfPtr a, SdfPtr b, float gap) { return sdfCsg(SdfKind::Grind, std::move(a), std::move(b), gap); }

SdfPtr sdfVoronoiCell(const std::vector<glm::vec3>& sites, int32_t site) {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::VoronoiCell;
    const glm::vec3& pi = sites[static_cast<size_t>(site)];
    for (size_t j = 0; j < sites.size(); ++j) {
        if (static_cast<int32_t>(j) == site) continue;
        const glm::vec3 d = sites[j] - pi;
        const float len = glm::length(d);
        if (len <= 0.0f) continue;  // degenerate pair (defensive; sites are deduped)
        n->cellNormals.push_back(d / len);
        n->cellPoints.push_back((pi + sites[j]) * 0.5f);
    }
    return n;
}

SdfPtr sdfDisplace(SdfPtr child, const FieldNode* amount) {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::Displace;
    n->a = std::move(child);
    if (amount) {
        std::unordered_map<const FieldNode*, const FieldNode*> memo;
        n->amount = deepCopyField(amount, n->fieldArena, memo);
        // Amplitude estimate for the conservative bbox: max |amount| over a
        // 4^3 probe of the child bbox (margin = estimate * 1.3, §19 v1.2).
        glm::vec3 mn, mx;
        n->a->conservativeBBox(mn, mx);
        float amp = 0.0f;
        if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z) {
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    for (int l = 0; l < 4; ++l) {
                        const glm::vec3 t(static_cast<float>(i) / 3.0f, static_cast<float>(j) / 3.0f,
                                          static_cast<float>(l) / 3.0f);
                        const glm::vec3 p = mn + (mx - mn) * t;
                        amp = std::max(amp, std::fabs(numericValueF32(evalFieldAtPoint(n->amount, p))));
                    }
        }
        n->ampEstimate = amp;
    }
    return n;
}

SdfPtr sdfInstance(SdfPtr source, std::vector<SdfInstanceAnchor> anchors, float k) {
    auto n = std::make_shared<SdfNode>();
    n->kind = SdfKind::Instance;
    n->a = std::move(source);
    n->k = k;
    n->anchors = std::move(anchors);
    // Per-anchor world AABB: the transformed conservative bbox of the source.
    glm::vec3 mn, mx;
    n->a->conservativeBBox(mn, mx);
    const bool valid = mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z;
    for (SdfInstanceAnchor& an : n->anchors) {
        an.aabbMin = glm::vec3(std::numeric_limits<float>::infinity());
        an.aabbMax = glm::vec3(-std::numeric_limits<float>::infinity());
        if (!valid) continue;
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            const glm::vec3 w = an.pos + an.orient * (corner * an.scale);
            an.aabbMin = glm::min(an.aabbMin, w);
            an.aabbMax = glm::max(an.aabbMax, w);
        }
    }
    return n;
}

size_t sdfNodeCount(const SdfNode& root) {
    std::unordered_set<const SdfNode*> seen;
    std::function<void(const SdfNode*)> walk = [&](const SdfNode* n) {
        if (!n || !seen.insert(n).second) return;
        walk(n->a.get());
        walk(n->b.get());
    };
    walk(&root);
    return seen.size();
}

// --- displace field machinery ---------------------------------------------------

Value evalFieldAtPoint(const FieldNode* node, const glm::vec3& p) {
    switch (node->kind) {
        case FKind::Const:
            return node->constValue;
        case FKind::AttrP:
            return Value(p);
        case FKind::Unary:
            return valueUnary(node->op, evalFieldAtPoint(node->args[0], p));
        case FKind::Binary:
            return valueBinary(node->op, evalFieldAtPoint(node->args[0], p), evalFieldAtPoint(node->args[1], p));
        case FKind::Ternary: {
            const Value c = evalFieldAtPoint(node->args[0], p);
            return asBool(c) ? evalFieldAtPoint(node->args[1], p) : evalFieldAtPoint(node->args[2], p);
        }
        case FKind::Call: {
            const BuiltinId id = static_cast<BuiltinId>(node->callId);
            switch (id) {
                case BuiltinId::Fbm: {
                    const glm::vec3 at = asVec3(evalFieldAtPoint(node->args[0], p));
                    const float scale = asF32(node->params[0]);
                    const int octaves = std::max(1, static_cast<int>(asInt(node->params[1])));
                    const Rng rng = asRng(node->params[2]);
                    const glm::vec3 q = at * scale;
                    return Value(fbmNoise(rng, q.x, q.y, q.z, octaves, asF32(node->params[3]),
                                          asF32(node->params[4])));
                }
                case BuiltinId::Vnoise: {
                    const glm::vec3 at = asVec3(evalFieldAtPoint(node->args[0], p));
                    const float scale = asF32(node->params[0]);
                    const Rng rng = asRng(node->params[1]);
                    const glm::vec3 q = at * scale;
                    return Value(glm::vec3(2.0f * valueNoise(rng, q.x, q.y, q.z, 0) - 1.0f,
                                           2.0f * valueNoise(rng, q.x, q.y, q.z, 1) - 1.0f,
                                           2.0f * valueNoise(rng, q.x, q.y, q.z, 2) - 1.0f));
                }
                case BuiltinId::Random: {
                    const int64_t c = numericValueInt(evalFieldAtPoint(node->args[0], p));
                    const float lo = asF32(node->params[0]);
                    const float hi = asF32(node->params[1]);
                    return Value(lo + (hi - lo) * rngF32(asRng(node->params[2]), static_cast<uint64_t>(c), 0));
                }
                case BuiltinId::RandomVec: {
                    const int64_t c = numericValueInt(evalFieldAtPoint(node->args[0], p));
                    const glm::vec3 lo = asVec3(node->params[0]);
                    const glm::vec3 hi = asVec3(node->params[1]);
                    const Rng rng = asRng(node->params[2]);
                    const uint64_t ctr = static_cast<uint64_t>(c);
                    return Value(glm::vec3(lo.x + (hi.x - lo.x) * rngF32(rng, ctr, 0),
                                           lo.y + (hi.y - lo.y) * rngF32(rng, ctr, 1),
                                           lo.z + (hi.z - lo.z) * rngF32(rng, ctr, 2)));
                }
                case BuiltinId::RandomInt: {
                    const int64_t c = numericValueInt(evalFieldAtPoint(node->args[0], p));
                    const uint64_t n = static_cast<uint64_t>(std::max<int64_t>(1, asInt(node->params[0])));
                    const uint32_t w = rngWord(asRng(node->params[1]), static_cast<uint64_t>(c), 0);
                    return Value(static_cast<int64_t>((static_cast<uint64_t>(w) * n) >> 32));
                }
                default: {
                    // §6.3 expression functions: the same 1-element-buffer path
                    // as constant folding in evalBuiltinCall (bit-identical).
                    std::vector<ConstBufferPtr> args;
                    args.reserve(node->args.size());
                    for (const FieldNode* a : node->args)
                        args.push_back(makeConstBuffer(evalFieldAtPoint(a, p), 1));
                    ConstBufferPtr out = evalExprFuncBuf(node->callId, args, node->params, 1, 1);
                    return bufferValueAt(*out, 0);
                }
            }
        }
        default:
            return Value();  // @N/@index/@attribute/distance_to/ingroup: E307 at creation
    }
}

const FieldNode* sdfContextViolation(const FieldNode* root) {
    std::unordered_set<const FieldNode*> seen;
    std::function<const FieldNode*(const FieldNode*)> walk = [&](const FieldNode* n) -> const FieldNode* {
        if (!n || !seen.insert(n).second) return nullptr;
        switch (n->kind) {
            case FKind::AttrN:
            case FKind::AttrIndex:
            case FKind::AttrNamed:
                return n;
            case FKind::Call: {
                const BuiltinId id = static_cast<BuiltinId>(n->callId);
                if (id == BuiltinId::DistanceTo || id == BuiltinId::Ingroup) return n;
                break;
            }
            default:
                break;
        }
        for (const FieldNode* a : n->args)
            if (const FieldNode* bad = walk(a)) return bad;
        return nullptr;
    };
    return walk(root);
}

// --- MeshBvh ----------------------------------------------------------------------

void MeshBvh::build(const Geo& geo) {
    tris_.clear();
    nodes_.clear();
    if (!geo.positions || !geo.cornerVerts || !geo.faceOffsets) return;
    const auto& pos = *geo.positions;
    for (size_t f = 0; f < geo.faceCount(); ++f) {
        const int32_t begin = (*geo.faceOffsets)[f];
        const int32_t end = (*geo.faceOffsets)[f + 1];
        if (end - begin < 3) continue;
        const glm::vec3& v0 = pos[(*geo.cornerVerts)[begin]];
        int32_t triIdx = 0;
        for (int32_t c = begin + 1; c + 1 < end; ++c, ++triIdx) {
            Tri t;
            t.a = v0;
            t.b = pos[(*geo.cornerVerts)[c]];
            t.c = pos[(*geo.cornerVerts)[c + 1]];
            const glm::vec3 n = glm::cross(t.b - t.a, t.c - t.a);
            const float len = glm::length(n);
            t.normal = len > 0.0f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
            t.face = static_cast<int32_t>(f);
            t.tri = triIdx;
            tris_.push_back(t);
        }
    }
    if (!tris_.empty()) buildRec(0, static_cast<int32_t>(tris_.size()));
}

int32_t MeshBvh::buildRec(int32_t begin, int32_t end) {
    glm::vec3 mn(std::numeric_limits<float>::infinity());
    glm::vec3 mx(-std::numeric_limits<float>::infinity());
    glm::vec3 cmn = mn, cmx = mx;
    for (int32_t i = begin; i < end; ++i) {
        const Tri& t = tris_[i];
        mn = glm::min(mn, glm::min(t.a, glm::min(t.b, t.c)));
        mx = glm::max(mx, glm::max(t.a, glm::max(t.b, t.c)));
        const glm::vec3 c = triCentroid(t);
        cmn = glm::min(cmn, c);
        cmx = glm::max(cmx, c);
    }
    const int32_t idx = static_cast<int32_t>(nodes_.size());
    nodes_.push_back(Node{mn, mx, -1, -1, begin, end - begin});
    const int32_t count = end - begin;
    if (count <= 8) return idx;

    const glm::vec3 ext = cmx - cmn;
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    const float mid = (cmn[axis] + cmx[axis]) * 0.5f;
    auto it = std::stable_partition(tris_.begin() + begin, tris_.begin() + end,
                                    [&](const Tri& t) { return triCentroid(t)[axis] < mid; });
    int32_t split = static_cast<int32_t>(it - tris_.begin());
    if (split == begin || split == end) {
        // Empty side: fall back to the median by (axis, face, tri).
        std::stable_sort(tris_.begin() + begin, tris_.begin() + end,
                         [&](const Tri& x, const Tri& y) {
                             const float cx = triCentroid(x)[axis];
                             const float cy = triCentroid(y)[axis];
                             if (cx != cy) return cx < cy;
                             if (x.face != y.face) return x.face < y.face;
                             return x.tri < y.tri;
                         });
        split = begin + count / 2;
    }
    nodes_[idx].left = buildRec(begin, split);
    nodes_[idx].right = buildRec(split, end);
    return idx;
}

bool MeshBvh::closest(const glm::vec3& p, float& outDist, glm::vec3& outPoint, glm::vec3& outNormal) const {
    if (nodes_.empty()) return false;
    float best = std::numeric_limits<float>::infinity();
    int32_t bestFace = std::numeric_limits<int32_t>::max();
    int32_t bestTri = std::numeric_limits<int32_t>::max();
    glm::vec3 bestP(0.0f);
    glm::vec3 bestN(0.0f, 1.0f, 0.0f);
    int32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& n = nodes_[stack[--top]];
        // Strict prune: ties stay explored for the (dist, face, tri) priority.
        if (distToAABB(p, n.mn, n.mx) > best) continue;
        if (n.left < 0) {
            for (int32_t i = n.begin; i < n.begin + n.count; ++i) {
                const Tri& t = tris_[i];
                const glm::vec3 q = closestPointTriangle(p, t.a, t.b, t.c);
                const float d = glm::length(p - q);
                if (d < best || (d == best && (t.face < bestFace || (t.face == bestFace && t.tri < bestTri)))) {
                    best = d;
                    bestFace = t.face;
                    bestTri = t.tri;
                    bestP = q;
                    bestN = t.normal;
                }
            }
        } else {
            stack[top++] = n.left;
            stack[top++] = n.right;
        }
    }
    outDist = best;
    outPoint = bestP;
    outNormal = bestN;
    return true;
}

// --- mesh <-> sdf bridges ----------------------------------------------------------

SdfPtr sdfFromMeshVoxelize(const Geo& geo, float voxel, unsigned threads) {
    auto node = std::make_shared<SdfNode>();
    node->kind = SdfKind::Grid;
    node->voxel = voxel;
    glm::vec3 mn, mx;
    geoBBox(geo, mn, mx);
    const glm::vec3 margin(3.0f * voxel);
    node->origin = mn - margin;
    const glm::vec3 span = (mx - mn) + 2.0f * margin;
    glm::ivec3 dims;
    for (int i = 0; i < 3; ++i)
        dims[i] = std::max(2, static_cast<int>(std::ceil(span[i] / voxel)) + 1);
    node->dims = dims;
    const size_t total = static_cast<size_t>(dims.x) * dims.y * dims.z;
    auto values = std::make_shared<std::vector<float>>(total, std::numeric_limits<float>::infinity());

    MeshBvh bvh;
    bvh.build(geo);
    const int nx = dims.x;
    const int ny = dims.y;
    const glm::vec3 origin = node->origin;
    parallelFor(total, threads, [&](size_t s, size_t e) {
        for (size_t idx = s; idx < e; ++idx) {
            const int z = static_cast<int>(idx / (static_cast<size_t>(nx) * ny));
            const int rem = static_cast<int>(idx % (static_cast<size_t>(nx) * ny));
            const int y = rem / nx;
            const int x = rem % nx;
            const glm::vec3 p = origin + glm::vec3(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(z)) * voxel;
            float dist;
            glm::vec3 cp, n;
            if (bvh.closest(p, dist, cp, n)) {
                const float sign = glm::dot(p - cp, n) >= 0.0f ? 1.0f : -1.0f;
                (*values)[idx] = sign * dist;
            }
        }
    });
    node->values = std::move(values);
    return node;
}

MeshFromSdfResult meshFromSdfExtract(const SdfNode& root, float voxel, float iso, unsigned threads) {
    MeshFromSdfResult res;
    glm::vec3 bbn, bbx;
    root.conservativeBBox(bbn, bbx);
    if (!(bbn.x <= bbx.x && bbn.y <= bbx.y && bbn.z <= bbx.z)) {
        res.mesh = makeMesh({}, {}, {0});  // empty field (e.g. zero instance anchors)
        return res;
    }
    const glm::vec3 mn = bbn - glm::vec3(voxel);
    const glm::vec3 mx = bbx + glm::vec3(voxel);
    glm::ivec3 dims;
    for (int i = 0; i < 3; ++i)
        dims[i] = static_cast<int>(std::ceil((mx[i] - mn[i]) / voxel)) + 1;
    constexpr int kMaxAxisVoxels = 4096;
    if (dims.x > kMaxAxisVoxels || dims.y > kMaxAxisVoxels || dims.z > kMaxAxisVoxels) {
        res.axisOverflow = true;
        res.mesh = makeMesh({}, {}, {0});
        return res;
    }
    const size_t total = static_cast<size_t>(dims.x) * dims.y * dims.z;
    std::vector<float> values(total);
    const int nx = dims.x;
    const int ny = dims.y;
    parallelFor(total, threads, [&](size_t s, size_t e) {
        for (size_t idx = s; idx < e; ++idx) {
            const int z = static_cast<int>(idx / (static_cast<size_t>(nx) * ny));
            const int rem = static_cast<int>(idx % (static_cast<size_t>(nx) * ny));
            const int y = rem / nx;
            const int x = rem % nx;
            const glm::vec3 p = mn + glm::vec3(static_cast<float>(x), static_cast<float>(y),
                                               static_cast<float>(z)) * voxel;
            values[idx] = root.eval(p);
        }
    });

    auto latIdx = [&](int x, int y, int z) {
        return (static_cast<size_t>(z) * ny + y) * nx + x;
    };
    for (int z = 0; z < dims.z && !res.boundaryTouch; ++z)
        for (int y = 0; y < dims.y && !res.boundaryTouch; ++y)
            for (int x = 0; x < dims.x; ++x) {
                const bool boundary = x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == dims.z - 1;
                if (boundary && values[latIdx(x, y, z)] <= iso) {
                    res.boundaryTouch = true;
                    break;
                }
            }

    // Marching cubes: sequential emission in fixed (z,y,x) cell order; vertex
    // dedup by grid-edge key (axis of the edge's lower lattice endpoint).
    static constexpr int kCornerOff[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                             {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
    // edge -> {axis, dx, dy, dz} of its lower lattice endpoint
    static constexpr int kEdgeInfo[12][4] = {{0, 0, 0, 0}, {1, 1, 0, 0}, {0, 0, 1, 0}, {1, 0, 0, 0},
                                             {0, 0, 0, 1}, {1, 1, 0, 1}, {0, 0, 1, 1}, {1, 0, 0, 1},
                                             {2, 0, 0, 0}, {2, 1, 0, 0}, {2, 1, 1, 0}, {2, 0, 1, 0}};
    std::vector<glm::vec3> positions;
    std::vector<int32_t> corners;
    std::unordered_map<uint64_t, int32_t> edgeVert;
    auto edgeVertex = [&](int edge, int x, int y, int z) -> int32_t {
        const int axis = kEdgeInfo[edge][0];
        const int gx = x + kEdgeInfo[edge][1];
        const int gy = y + kEdgeInfo[edge][2];
        const int gz = z + kEdgeInfo[edge][3];
        const uint64_t key = (static_cast<uint64_t>(latIdx(gx, gy, gz)) << 2) | static_cast<uint64_t>(axis);
        auto [it, inserted] = edgeVert.emplace(key, static_cast<int32_t>(positions.size()));
        if (!inserted) return it->second;
        const glm::vec3 pa = mn + glm::vec3(static_cast<float>(gx), static_cast<float>(gy),
                                            static_cast<float>(gz)) * voxel;
        glm::vec3 pb = pa;
        pb[axis] += voxel;
        const float va = values[latIdx(gx, gy, gz)];
        glm::ivec3 bi(gx, gy, gz);
        bi[axis] += 1;
        const float vb = values[latIdx(bi.x, bi.y, bi.z)];
        const float t = std::fabs(vb - va) < 1e-12f ? 0.5f : (iso - va) / (vb - va);
        positions.push_back(pa + (pb - pa) * t);
        return it->second;
    };
    for (int z = 0; z + 1 < dims.z; ++z) {
        for (int y = 0; y + 1 < dims.y; ++y) {
            for (int x = 0; x + 1 < dims.x; ++x) {
                float v[8];
                int cubeindex = 0;
                for (int i = 0; i < 8; ++i) {
                    v[i] = values[latIdx(x + kCornerOff[i][0], y + kCornerOff[i][1], z + kCornerOff[i][2])];
                    if (v[i] < iso) cubeindex |= 1 << i;
                }
                if (cubeindex == 0 || cubeindex == 255) continue;
                for (int t = 0; kMcTriTable[cubeindex][t] != -1; t += 3) {
                    const int32_t v0 = edgeVertex(kMcTriTable[cubeindex][t], x, y, z);
                    const int32_t v1 = edgeVertex(kMcTriTable[cubeindex][t + 1], x, y, z);
                    const int32_t v2 = edgeVertex(kMcTriTable[cubeindex][t + 2], x, y, z);
                    // The published table is wound toward the inside under
                    // (bit set = inside): reverse for outward normals.
                    corners.push_back(v0);
                    corners.push_back(v2);
                    corners.push_back(v1);
                }
            }
        }
    }
    std::vector<int32_t> offsets;
    offsets.reserve(corners.size() / 3 + 1);
    for (size_t i = 0; i <= corners.size(); i += 3) offsets.push_back(static_cast<int32_t>(i));
    res.mesh = makeMesh(std::move(positions), std::move(corners), std::move(offsets));
    return res;
}

}  // namespace pgg
