#include "../../pch.h"

#include "fingerprint.h"

#include <bit>

namespace pgg {
namespace {

// splitmix64 finalizer: full 64-bit avalanche (same integer-hash family as
// rng.cpp; fingerprints are cache keys, not random words, so they get their
// own instance — greenfield, spec §18).
uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// Node/payload tags: distinct seeds so different structures can never collide
// by construction.
constexpr uint64_t kTagNumber = 0x10e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagString = 0x20e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagBool = 0x30e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagNone = 0x40e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagEnum = 0x50e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagIdent = 0x60e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagAttr = 0x70e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagVec = 0x80e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagList = 0x90e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagParen = 0xa0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagUnary = 0xb0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagBinary = 0xc0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagTernary = 0xd0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagCall = 0xe0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagError = 0xf0e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagBinding = 0x11e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagParam = 0x22e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagBound = 0x33e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagDefault = 0x44e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagUnbound = 0x55e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagGeo = 0x66e3a1b2c3d4e5f6ull;
constexpr uint64_t kTagRng = 0x77e3a1b2c3d4e5f6ull;

struct Mixer {
    uint64_t h;
    void add(uint64_t v) { h = mix64(h ^ v); }
    void addStr(const std::string& s) { add(fnv1a64(s)); }
    void addF32(float f) { add(static_cast<uint64_t>(std::bit_cast<uint32_t>(f))); }
};

void addColumnData(Mixer& m, const ColumnData& col) {
    m.add(static_cast<uint64_t>(col.index()));
    std::visit(
        [&](const auto& ptr) {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            m.add(static_cast<uint64_t>(ptr->size()));
            for (const ElemT& e : *ptr) {
                if constexpr (std::is_same_v<ElemT, float>) {
                    m.addF32(e);
                } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                    m.add(static_cast<uint64_t>(e));
                } else if constexpr (std::is_same_v<ElemT, uint8_t>) {
                    m.add(static_cast<uint64_t>(e));
                } else if constexpr (std::is_same_v<ElemT, std::string>) {
                    m.addStr(e);
                } else {  // glm vectors
                    for (int c = 0; c < e.length(); ++c) m.addF32(e[c]);
                }
            }
        },
        col);
}

void addAttrSet(Mixer& m, const AttrSet* set) {
    if (!set) {
        m.add(0ull);
        return;
    }
    // Column names sorted: the hash must not depend on the unordered_map
    // layout (insertion history differs across pipelines).
    std::vector<const std::string*> names;
    for (const auto& [n, c] : set->columns) names.push_back(&n);
    std::sort(names.begin(), names.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
    m.add(static_cast<uint64_t>(names.size()));
    for (const std::string* n : names) {
        m.addStr(*n);
        addColumnData(m, set->find(*n)->data);
    }
}

void addGroupSet(Mixer& m, const GroupSet* set) {
    if (!set) {
        m.add(0ull);
        return;
    }
    std::vector<const std::string*> names;
    for (const auto& [n, c] : set->columns) names.push_back(&n);
    std::sort(names.begin(), names.end(), [](const std::string* a, const std::string* b) { return *a < *b; });
    m.add(static_cast<uint64_t>(names.size()));
    for (const std::string* n : names) {
        m.addStr(*n);
        const BoolColumn& col = *set->find(*n);
        m.add(static_cast<uint64_t>(col.size()));
        for (uint8_t v : col) m.add(static_cast<uint64_t>(v));
    }
}

void addGeo(Mixer& m, const Geo& g) {
    m.add(kTagGeo);
    m.add(static_cast<uint64_t>(g.kind));
    m.add(static_cast<uint64_t>(g.pointCount()));
    if (g.positions)
        for (const glm::vec3& p : *g.positions) {
            m.addF32(p.x);
            m.addF32(p.y);
            m.addF32(p.z);
        }
    m.add(g.normals ? 1ull : 0ull);
    if (g.normals)
        for (const glm::vec3& n : *g.normals) {
            m.addF32(n.x);
            m.addF32(n.y);
            m.addF32(n.z);
        }
    m.add(static_cast<uint64_t>(g.cornerCount()));
    if (g.cornerVerts)
        for (int32_t v : *g.cornerVerts) m.add(static_cast<uint64_t>(static_cast<uint32_t>(v)));
    m.add(static_cast<uint64_t>(g.faceOffsets ? g.faceOffsets->size() : 0));
    if (g.faceOffsets)
        for (int32_t v : *g.faceOffsets) m.add(static_cast<uint64_t>(static_cast<uint32_t>(v)));
    addAttrSet(m, g.pointAttrs.get());
    addAttrSet(m, g.cornerAttrs.get());
    addAttrSet(m, g.faceAttrs.get());
    addAttrSet(m, g.detailAttrs.get());
    addGroupSet(m, g.pointGroups.get());
    addGroupSet(m, g.cornerGroups.get());
    addGroupSet(m, g.faceGroups.get());
    addGroupSet(m, g.detailGroups.get());
    m.add(static_cast<uint64_t>(g.instanceSources ? g.instanceSources->size() : 0));
    if (g.instanceSources)
        for (const GeoPtr& s : *g.instanceSources) addGeo(m, *s);
}

uint64_t finish(uint64_t h) { return h == 0 ? 1 : h; }

}  // namespace

bool fingerprintValue(const Value& v, uint64_t& out) {
    Mixer m{0};
    switch (v.data.index()) {
        case 0: m.add(kTagNone); break;
        case 1:
            m.add(kTagBool);
            m.add(asBool(v) ? 1ull : 0ull);
            break;
        case 2:
            m.add(kTagNumber);
            m.add(static_cast<uint64_t>(asInt(v)));
            break;
        case 3:
            m.add(kTagNumber);
            m.addF32(asF32(v));
            break;
        case 4:
            m.add(kTagVec);
            m.addF32(asVec2(v).x);
            m.addF32(asVec2(v).y);
            break;
        case 5:
            m.add(kTagVec);
            m.addF32(asVec3(v).x);
            m.addF32(asVec3(v).y);
            m.addF32(asVec3(v).z);
            break;
        case 6:
            m.add(kTagVec);
            m.addF32(asVec4(v).x);
            m.addF32(asVec4(v).y);
            m.addF32(asVec4(v).z);
            m.addF32(asVec4(v).w);
            break;
        case 7:
            m.add(kTagString);
            m.addStr(asString(v));
            break;
        case 8:
            m.add(kTagRng);
            m.add(asRng(v).lo);
            m.add(asRng(v).hi);
            break;
        case 9: addGeo(m, *asGeo(v)); break;
        case 10: return false;  // compiled fields have no structural hash
        case 11: {
            m.add(kTagList);
            const auto& elems = asList(v);
            m.add(static_cast<uint64_t>(elems.size()));
            for (const Value& e : elems) {
                uint64_t eh = 0;
                if (!fingerprintValue(e, eh)) return false;
                m.add(eh);
            }
            break;
        }
        default: return false;
    }
    out = finish(m.h);
    return true;
}

BindingFingerprinter::BindingFingerprinter(
    const File& file, const std::vector<std::pair<std::string, Value>>& boundParams, uint64_t profileId)
    : boundParams_(boundParams), profileId_(profileId) {
    for (const Node* item : file.items) {
        switch (item->kind) {
            case NodeKind::ParamDecl:
                topLevel_[static_cast<const ParamDecl*>(item)->name] = item;
                break;
            case NodeKind::Def:
                topLevel_[static_cast<const Def*>(item)->name] = item;
                break;
            case NodeKind::Import: {
                const auto* im = static_cast<const Import*>(item);
                topLevel_[im->hasAlias ? im->alias : im->path.back()] = item;
                break;
            }
            case NodeKind::Binding: {
                const auto* b = static_cast<const Binding*>(item);
                for (const std::string& n : b->targets.names) topLevel_[n] = item;
                break;
            }
            default:
                break;  // zones, taps, outputs: not evaluable bindings
        }
    }
}

uint64_t BindingFingerprinter::fingerprint(const std::string& name) {
    if (auto it = memo_.find(name); it != memo_.end()) return it->second;
    auto it = topLevel_.find(name);
    if (it == topLevel_.end()) {
        memo_[name] = 0;
        return 0;
    }
    if (inProgress_.count(name)) return finish(mix64(profileId_ ^ fnv1a64(name)));  // unreachable post-checks
    inProgress_.insert(name);
    const uint64_t h = fingerprintNode(name, it->second);
    inProgress_.erase(name);
    memo_[name] = h;
    return h;
}

uint64_t BindingFingerprinter::fingerprintNode(const std::string& /*name*/, const Node* node) {
    Mixer m{profileId_};
    if (node->kind == NodeKind::ParamDecl) {
        const auto* p = static_cast<const ParamDecl*>(node);
        m.add(kTagParam);
        // The declared type is part of the identity: the same bound value
        // converts differently under a different declaration.
        const Type declT = typeFromRef(*p->type);
        m.add(static_cast<uint64_t>(declT.base));
        m.add(static_cast<uint64_t>(declT.geoKind));
        m.add(declT.isList ? 1ull : 0ull);
        m.add(declT.isField ? 1ull : 0ull);
        for (const auto& [pn, pv] : boundParams_) {
            if (pn != p->name) continue;
            m.add(kTagBound);
            uint64_t vh = 0;
            if (!fingerprintValue(pv, vh)) return 0;
            m.add(vh);
            return finish(m.h);
        }
        if (p->hasDefault) {
            m.add(kTagDefault);
            bool ok = true;
            m.add(fingerprintExpr(p->def, ok));
            return ok ? finish(m.h) : 0;
        }
        m.add(kTagUnbound);  // E604 at evaluation; still structural
        return finish(m.h);
    }
    if (node->kind == NodeKind::Binding) {
        const auto* b = static_cast<const Binding*>(node);
        m.add(kTagBinding);
        bool ok = true;
        m.add(fingerprintExpr(b->value, ok));
        return ok ? finish(m.h) : 0;
    }
    return 0;  // def/import/zones: not evaluable at this stage
}

uint64_t BindingFingerprinter::fingerprintExpr(const Expr* e, bool& ok) {
    if (!e || !ok) {
        ok = false;
        return 0;
    }
    Mixer m{0};
    switch (e->kind) {
        case NodeKind::NumberLit: {
            const auto* n = static_cast<const NumberLit*>(e);
            m.add(kTagNumber);
            m.addStr(n->text);  // as written (structural identity)
            m.add(n->isFloat ? 1ull : 0ull);
            break;
        }
        case NodeKind::StringLit:
            m.add(kTagString);
            m.addStr(static_cast<const StringLit*>(e)->value);
            break;
        case NodeKind::BoolLit:
            m.add(kTagBool);
            m.add(static_cast<const BoolLit*>(e)->value ? 1ull : 0ull);
            break;
        case NodeKind::NoneLit: m.add(kTagNone); break;
        case NodeKind::EnumLit:
            m.add(kTagEnum);
            m.addStr(static_cast<const EnumLit*>(e)->name);
            break;
        case NodeKind::Ident: {
            m.add(kTagIdent);
            const uint64_t dep = fingerprint(static_cast<const Ident*>(e)->name);
            if (dep == 0) {
                ok = false;
                return 0;
            }
            m.add(dep);
            break;
        }
        case NodeKind::AttrRef:
            m.add(kTagAttr);
            m.addStr(static_cast<const AttrRef*>(e)->name);
            break;
        case NodeKind::VecLit: {
            const auto* v = static_cast<const VecLit*>(e);
            m.add(kTagVec);
            m.add(static_cast<uint64_t>(v->elems.size()));
            for (const Expr* el : v->elems) m.add(fingerprintExpr(el, ok));
            break;
        }
        case NodeKind::ListLit: {
            const auto* l = static_cast<const ListLit*>(e);
            m.add(kTagList);
            m.add(static_cast<uint64_t>(l->elems.size()));
            for (const Expr* el : l->elems) m.add(fingerprintExpr(el, ok));
            break;
        }
        case NodeKind::Paren:
            m.add(kTagParen);
            m.add(fingerprintExpr(static_cast<const Paren*>(e)->inner, ok));
            break;
        case NodeKind::Unary: {
            const auto* u = static_cast<const Unary*>(e);
            m.add(kTagUnary);
            m.addStr(u->op);
            m.add(fingerprintExpr(u->operand, ok));
            break;
        }
        case NodeKind::Binary: {
            const auto* b = static_cast<const Binary*>(e);
            m.add(kTagBinary);
            m.addStr(b->op);
            m.add(fingerprintExpr(b->lhs, ok));
            m.add(fingerprintExpr(b->rhs, ok));
            break;
        }
        case NodeKind::Ternary: {
            const auto* t = static_cast<const Ternary*>(e);
            m.add(kTagTernary);
            m.add(fingerprintExpr(t->cond, ok));
            m.add(fingerprintExpr(t->thenExpr, ok));
            m.add(fingerprintExpr(t->elseExpr, ok));
            break;
        }
        case NodeKind::Call: {
            const auto* c = static_cast<const Call*>(e);
            m.add(kTagCall);
            m.add(static_cast<uint64_t>(c->path.size()));
            for (const std::string& part : c->path) m.addStr(part);
            m.add(static_cast<uint64_t>(c->args.size()));
            for (const CallArg& arg : c->args) {
                m.add(arg.hasName ? 1ull : 0ull);
                if (arg.hasName) m.addStr(arg.name);
                m.add(fingerprintExpr(arg.value, ok));
            }
            break;
        }
        case NodeKind::ErrorExpr: m.add(kTagError); break;
        default:
            ok = false;
            return 0;
    }
    return ok ? finish(m.h) : 0;
}

}  // namespace pgg
