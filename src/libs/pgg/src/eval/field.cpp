#include "../../pch.h"

#include "field.h"

#include <cmath>
#include <numeric>

#include "builtins.h"
#include "parallel.h"

namespace pgg {

FieldNode* RunContext::newNode() {
    fieldArena.push_back(std::make_unique<FieldNode>());
    FieldNode* n = fieldArena.back().get();
    n->id = static_cast<int>(fieldArena.size()) - 1;
    return n;
}

void RunContext::report(const std::string& code, Span span, std::string message, std::string hint) {
    if (diagnostics) diagnostics->push_back(Diagnostic{code, span, std::move(message), std::move(hint), false});
}

// --- buffer helpers ---------------------------------------------------------

ScalarType bufferType(const Buffer& buf) {
    switch (buf.index()) {
        case 0: return ScalarType::F32;
        case 1: return ScalarType::Int;
        case 2: return ScalarType::Bool;
        case 3: return ScalarType::Vec2;
        case 4: return ScalarType::Vec3;
        case 5: return ScalarType::Vec4;
        default: return ScalarType::None;
    }
}

size_t bufferSize(const Buffer& buf) {
    return std::visit([](const auto& v) { return v.size(); }, buf);
}

ConstBufferPtr makeConstBuffer(const Value& v, size_t count) {
    switch (valueBase(v)) {
        case ScalarType::Bool: return std::make_shared<const Buffer>(BoolBuf(count, asBool(v) ? 1 : 0));
        case ScalarType::Int: return std::make_shared<const Buffer>(IntBuf(count, asInt(v)));
        case ScalarType::F32: return std::make_shared<const Buffer>(F32Buf(count, asF32(v)));
        case ScalarType::Vec2: return std::make_shared<const Buffer>(Vec2Buf(count, asVec2(v)));
        case ScalarType::Vec3: return std::make_shared<const Buffer>(Vec3Buf(count, asVec3(v)));
        case ScalarType::Vec4: return std::make_shared<const Buffer>(Vec4Buf(count, asVec4(v)));
        default: return std::make_shared<const Buffer>(F32Buf(count, 0.0f));
    }
}

ConstBufferPtr makeZeroBuffer(ScalarType base, size_t count) {
    switch (base) {
        case ScalarType::Bool: return std::make_shared<const Buffer>(BoolBuf(count, 0));
        case ScalarType::Int: return std::make_shared<const Buffer>(IntBuf(count, 0));
        case ScalarType::Vec2: return std::make_shared<const Buffer>(Vec2Buf(count, glm::vec2(0.0f)));
        case ScalarType::Vec3: return std::make_shared<const Buffer>(Vec3Buf(count, glm::vec3(0.0f)));
        case ScalarType::Vec4: return std::make_shared<const Buffer>(Vec4Buf(count, glm::vec4(0.0f)));
        default: return std::make_shared<const Buffer>(F32Buf(count, 0.0f));
    }
}

ConstBufferPtr convertBuffer(ConstBufferPtr buf, ScalarType target) {
    const ScalarType from = bufferType(*buf);
    if (from == target) return buf;
    const size_t count = bufferSize(*buf);
    if (isNumericBase(from) && isNumericBase(target)) {
        if (target == ScalarType::F32) {
            F32Buf out(count);
            if (from == ScalarType::Bool) {
                const auto& s = std::get<BoolBuf>(*buf);
                for (size_t i = 0; i < count; ++i) out[i] = s[i] ? 1.0f : 0.0f;
            } else {
                const auto& s = std::get<IntBuf>(*buf);
                for (size_t i = 0; i < count; ++i) out[i] = static_cast<float>(s[i]);
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        if (target == ScalarType::Int && from == ScalarType::Bool) {
            const auto& s = std::get<BoolBuf>(*buf);
            IntBuf out(count);
            for (size_t i = 0; i < count; ++i) out[i] = s[i] ? 1 : 0;
            return std::make_shared<const Buffer>(std::move(out));
        }
        return nullptr;  // narrowing (f32 -> int, anything -> bool) needs a cast
    }
    if (isNumericBase(from) && isVectorBase(target)) {
        ConstBufferPtr asF = convertBuffer(buf, ScalarType::F32);
        const auto& s = std::get<F32Buf>(*asF);
        switch (target) {
            case ScalarType::Vec2: {
                Vec2Buf out(count);
                for (size_t i = 0; i < count; ++i) out[i] = glm::vec2(s[i]);
                return std::make_shared<const Buffer>(std::move(out));
            }
            case ScalarType::Vec3: {
                Vec3Buf out(count);
                for (size_t i = 0; i < count; ++i) out[i] = glm::vec3(s[i]);
                return std::make_shared<const Buffer>(std::move(out));
            }
            case ScalarType::Vec4: {
                Vec4Buf out(count);
                for (size_t i = 0; i < count; ++i) out[i] = glm::vec4(s[i]);
                return std::make_shared<const Buffer>(std::move(out));
            }
            default: return nullptr;
        }
    }
    return nullptr;
}

std::pair<ConstBufferPtr, ConstBufferPtr> promoteBuffers(ConstBufferPtr a, ConstBufferPtr b) {
    const ScalarType t = promoteBase(bufferType(*a), bufferType(*b));
    if (t == ScalarType::None) return {nullptr, nullptr};
    return {convertBuffer(std::move(a), t), convertBuffer(std::move(b), t)};
}

Value bufferValueAt(const Buffer& buf, size_t i) {
    switch (buf.index()) {
        case 0: return Value(std::get<F32Buf>(buf)[i]);
        case 1: return Value(std::get<IntBuf>(buf)[i]);
        case 2: return Value(std::get<BoolBuf>(buf)[i] != 0);
        case 3: return Value(std::get<Vec2Buf>(buf)[i]);
        case 4: return Value(std::get<Vec3Buf>(buf)[i]);
        case 5: return Value(std::get<Vec4Buf>(buf)[i]);
        default: return Value();
    }
}

namespace {

// --- elementwise kernels ----------------------------------------------------
//
// Every kernel loop visits disjoint elements with no shared writes, so the
// loops run chunked over the thread pool when the buffer is large enough
// (bit-identical to the sequential pass, N7).

template <typename T>
T arithElem(const std::string& op, const T& a, const T& b) {
    if constexpr (std::is_same_v<T, int64_t>) {
        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        // v0: integer division/modulo by zero yields 0 (kept deterministic).
        if (op == "/") return b == 0 ? int64_t(0) : a / b;
        if (op == "%") return b == 0 ? int64_t(0) : a % b;
    } else {
        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/") return a / b;  // f32: IEEE; glm vectors: component-wise
    }
    return T(0);
}

ConstBufferPtr bufferArith(const std::string& op, ConstBufferPtr a, ConstBufferPtr b, unsigned threads) {
    return std::make_shared<const Buffer>(std::visit(
        [&](const auto& va) -> Buffer {
            using VecT = std::decay_t<decltype(va)>;
            using ElemT = typename VecT::value_type;
            const auto& vb = std::get<VecT>(*b);
            VecT out(va.size());
            parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) out[i] = arithElem(op, va[i], vb[i]);
            });
            return out;
        },
        *a));
}

ConstBufferPtr bufferCompare(const std::string& op, ConstBufferPtr a, ConstBufferPtr b, unsigned threads) {
    return std::make_shared<const Buffer>(std::visit(
        [&](const auto& va) -> Buffer {
            using VecT = std::decay_t<decltype(va)>;
            using ElemT = typename VecT::value_type;
            const auto& vb = std::get<VecT>(*b);
            BoolBuf out(va.size());
            parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    uint8_t r = 0;
                    if constexpr (std::is_same_v<ElemT, uint8_t> || std::is_same_v<ElemT, int64_t> ||
                                  std::is_same_v<ElemT, float>) {
                        if (op == "<") r = va[i] < vb[i];
                        else if (op == "<=") r = va[i] <= vb[i];
                        else if (op == ">") r = va[i] > vb[i];
                        else if (op == ">=") r = va[i] >= vb[i];
                        else if (op == "==") r = va[i] == vb[i];
                        else if (op == "!=") r = va[i] != vb[i];
                    } else {  // glm vectors: equality only (all components)
                        if (op == "==") r = va[i] == vb[i];
                        else if (op == "!=") r = va[i] != vb[i];
                    }
                    out[i] = r;
                }
            });
            return out;
        },
        *a));
}

ConstBufferPtr bufferLogic(const std::string& op, ConstBufferPtr a, ConstBufferPtr b, unsigned threads) {
    const auto& va = std::get<BoolBuf>(*a);
    const auto& vb = std::get<BoolBuf>(*b);
    BoolBuf out(va.size());
    parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; ++i) out[i] = op == "&" ? va[i] && vb[i] : va[i] || vb[i];
    });
    return std::make_shared<const Buffer>(std::move(out));
}

ConstBufferPtr bufferUnary(const std::string& op, ConstBufferPtr a, unsigned threads) {
    return std::make_shared<const Buffer>(std::visit(
        [&](const auto& va) -> Buffer {
            using VecT = std::decay_t<decltype(va)>;
            using ElemT = typename VecT::value_type;
            constexpr bool isVec = std::is_same_v<ElemT, glm::vec2> || std::is_same_v<ElemT, glm::vec3> ||
                                   std::is_same_v<ElemT, glm::vec4>;
            if (op == "-") {
                if constexpr (std::is_same_v<ElemT, uint8_t>) {
                    IntBuf r(va.size());
                    parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                        for (size_t i = begin; i < end; ++i) r[i] = -static_cast<int64_t>(va[i]);
                    });
                    return r;
                } else {
                    VecT out(va.size());
                    parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                        for (size_t i = begin; i < end; ++i) out[i] = -va[i];
                    });
                    return out;
                }
            }
            // "!" — scalar operands only (vectors are a static E204).
            BoolBuf r(va.size());
            if constexpr (!isVec) {
                parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                    for (size_t i = begin; i < end; ++i) r[i] = !va[i];
                });
            }
            return r;
        },
        *a));
}

ConstBufferPtr bufferTernary(ConstBufferPtr cond, ConstBufferPtr a, ConstBufferPtr b, unsigned threads) {
    const auto& vc = std::get<BoolBuf>(*cond);
    return std::make_shared<const Buffer>(std::visit(
        [&](const auto& va) -> Buffer {
            using VecT = std::decay_t<decltype(va)>;
            const auto& vb = std::get<VecT>(*b);
            VecT out(va.size());
            parallelFor(va.size(), threads, [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) out[i] = vc[i] ? va[i] : vb[i];
            });
            return out;
        },
        *a));
}

ConstBufferPtr computeField(const FieldNode& node, EvalContext& ctx) {
    RunContext& run = ctx.run;
    const size_t count = ctx.elementCount;
    switch (node.kind) {
        case FKind::Const:
            return makeConstBuffer(node.constValue, count);
        case FKind::AttrP: {
            std::shared_ptr<const std::vector<glm::vec3>> p = samplePositions(ctx.geo, ctx.domain);
            if (!p) return makeZeroBuffer(ScalarType::Vec3, count);
            return std::make_shared<const Buffer>(Vec3Buf(*p));
        }
        case FKind::AttrN: {
            std::shared_ptr<const std::vector<glm::vec3>> n = sampleNormals(ctx.geo, ctx.domain);
            if (!n) {
                run.report("E302", node.span, "attribute @N is not present on this geometry",
                           "run compute_normals (or use a surface source that writes @N) first");
                return makeZeroBuffer(ScalarType::Vec3, count);
            }
            return std::make_shared<const Buffer>(Vec3Buf(*n));
        }
        case FKind::AttrIndex: {
            IntBuf out(count);
            std::iota(out.begin(), out.end(), int64_t(0));
            return std::make_shared<const Buffer>(std::move(out));
        }
        case FKind::AttrNamed: {
            std::optional<ColumnData> col = sampleAttrColumn(ctx.geo, node.name, ctx.domain);
            if (!col) {
                if (attrExistsOnAnyDomain(ctx.geo, node.name)) {
                    run.report("E301", node.span,
                               "attribute '@" + node.name + "' cannot be read on the " +
                                   std::string(domainName(ctx.domain)) + " domain",
                               "string attributes do not interpolate across domains");
                } else {
                    run.report("E302", node.span,
                               "attribute '@" + node.name + "' is not present on this geometry",
                               "write it with set(geo, \"" + node.name + "\", ...) first");
                }
                return makeZeroBuffer(node.type, count);
            }
            return std::visit(
                [&](const auto& ptr) -> ConstBufferPtr {
                    using VecT = std::decay_t<decltype(*ptr)>;
                    if constexpr (std::is_same_v<VecT, std::vector<std::string>>) {
                        run.report("E301", node.span, "string attributes are not readable as fields");
                        return makeZeroBuffer(node.type, count);
                    } else {
                        return std::make_shared<const Buffer>(VecT(*ptr));
                    }
                },
                *col);
        }
        case FKind::Unary: {
            ConstBufferPtr a = evalField(node.args[0], ctx.geo, ctx.domain, run);
            return bufferUnary(node.op, std::move(a), run.threads);
        }
        case FKind::Binary: {
            ConstBufferPtr a = evalField(node.args[0], ctx.geo, ctx.domain, run);
            ConstBufferPtr b = evalField(node.args[1], ctx.geo, ctx.domain, run);
            auto [pa, pb] = promoteBuffers(std::move(a), std::move(b));
            if (!pa) {
                run.report("E204", node.span, "incompatible operand types for '" + node.op + "'");
                return makeZeroBuffer(node.type, count);
            }
            const std::string& op = node.op;
            if (op == "&" || op == "|")
                return bufferLogic(op, convertBuffer(pa, ScalarType::Bool), convertBuffer(pb, ScalarType::Bool),
                                   run.threads);
            if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=") {
                if (isVectorBase(bufferType(*pa)) && op != "==" && op != "!=") {
                    run.report("E204", node.span, "ordered comparison '" + op + "' is not defined for vectors");
                    return makeZeroBuffer(ScalarType::Bool, count);
                }
                return bufferCompare(op, std::move(pa), std::move(pb), run.threads);
            }
            // Arithmetic on booleans happens as int (bool is rank 0 of the chain).
            if (bufferType(*pa) == ScalarType::Bool) {
                pa = convertBuffer(pa, ScalarType::Int);
                pb = convertBuffer(pb, ScalarType::Int);
            }
            if (op == "%" && bufferType(*pa) != ScalarType::Int) {
                run.report("E204", node.span, "'%' is only defined for integers");
                return makeZeroBuffer(node.type, count);
            }
            return bufferArith(op, std::move(pa), std::move(pb), run.threads);
        }
        case FKind::Ternary: {
            ConstBufferPtr cond = evalField(node.args[0], ctx.geo, ctx.domain, run);
            ConstBufferPtr a = evalField(node.args[1], ctx.geo, ctx.domain, run);
            ConstBufferPtr b = evalField(node.args[2], ctx.geo, ctx.domain, run);
            auto [pa, pb] = promoteBuffers(std::move(a), std::move(b));
            if (!pa) {
                run.report("E204", node.span, "ternary branches have incompatible types");
                return makeZeroBuffer(node.type, count);
            }
            return bufferTernary(convertBuffer(std::move(cond), ScalarType::Bool), std::move(pa), std::move(pb),
                                 run.threads);
        }
        case FKind::Call: {
            std::vector<ConstBufferPtr> args;
            args.reserve(node.args.size());
            for (const FieldNode* a : node.args) args.push_back(evalField(a, ctx.geo, ctx.domain, run));
            return evalFieldCall(node.callId, node, args, ctx);
        }
    }
    return makeZeroBuffer(node.type, count);
}

}  // namespace

ConstBufferPtr evalField(const FieldNode* node, const Geo& geo, Domain domain, RunContext& run) {
    const RunContext::FieldKey key{node, &geo, static_cast<int>(domain)};
    if (auto it = run.fieldCache.find(key); it != run.fieldCache.end()) return it->second;
    EvalContext ctx{geo, domain, geo.elementCount(domain), run};
    ConstBufferPtr buf = computeField(*node, ctx);
    run.nodeEvals[node->id] += 1;
    run.fieldsEvaluated += 1;
    run.fieldCache.emplace(key, buf);
    return buf;
}

// --- expression compiler ------------------------------------------------------

const FieldNode* asFieldNode(const TypedValue& tv, RunContext& run) {
    if (tv.field) return tv.field;
    FieldNode* n = run.newNode();
    n->kind = FKind::Const;
    n->type = valueBase(tv.value);
    n->constValue = tv.value;
    return n;
}

namespace {

ScalarType binaryResultBase(const std::string& op, ScalarType a, ScalarType b) {
    if (op == "&" || op == "|") return ScalarType::Bool;
    if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=")
        return ScalarType::Bool;
    if (op == "%") return ScalarType::Int;
    return promoteBase(a, b);
}

TypedValue makeFieldResult(const FieldNode* n) {
    TypedValue tv;
    tv.type = Type{n->type, true, GeoKind::Any};
    tv.field = n;
    return tv;
}

TypedValue makeValueResult(const Value& v) {
    TypedValue tv;
    tv.type = Type{valueBase(v), false, GeoKind::Any};
    tv.value = v;
    return tv;
}

TypedValue compileAttrRef(const AttrRef* ref, RunContext& run) {
    // Zone constants (§6.3): @iteration/@piece_index are int VALUES, resolved
    // from the enclosing zone's constant stack (innermost wins). In a field
    // context the value becomes a Const node via the usual conversion.
    if (ref->name == "iteration" || ref->name == "piece_index") {
        for (auto it = run.zoneConstants.rbegin(); it != run.zoneConstants.rend(); ++it)
            if (it->first == ref->name) return makeValueResult(it->second);
        run.report("E302", ref->span,
                   "'@" + ref->name + "' is only valid inside a " +
                       std::string(ref->name == "iteration" ? "repeat" : "foreach") + " zone body",
                   "the zone provides it as an int constant (§5.4)");
        return makeValueResult(Value(int64_t(0)));  // recovery: zero constant
    }
    FieldNode* n = run.newNode();
    n->span = ref->span;
    if (ref->name == "P") {
        n->kind = FKind::AttrP;
        n->type = ScalarType::Vec3;
    } else if (ref->name == "N") {
        n->kind = FKind::AttrN;
        n->type = ScalarType::Vec3;
    } else if (ref->name == "index") {
        n->kind = FKind::AttrIndex;
        n->type = ScalarType::Int;
    } else {
        // Static typecheck rejects unknown attributes at E1; the node stays
        // for defensive completeness (runtime E302 on evaluation).
        n->kind = FKind::AttrNamed;
        n->name = ref->name;
        n->type = ScalarType::F32;
    }
    return makeFieldResult(n);
}

TypedValue compileCall(const Call* c, RunContext& run, const IdentResolver& resolve);

TypedValue compileExprImpl(const Expr* e, RunContext& run, const IdentResolver& resolve) {
    if (!e) return {};
    switch (e->kind) {
        case NodeKind::NumberLit: {
            const auto* n = static_cast<const NumberLit*>(e);
            if (n->isFloat) return makeValueResult(Value(std::stof(n->text)));
            return makeValueResult(Value(static_cast<int64_t>(std::stoll(n->text))));
        }
        case NodeKind::StringLit:
            return makeValueResult(Value(static_cast<const StringLit*>(e)->value));
        case NodeKind::BoolLit:
            return makeValueResult(Value(static_cast<const BoolLit*>(e)->value));
        case NodeKind::NoneLit:
            return makeValueResult(Value());
        case NodeKind::EnumLit: {
            const auto* en = static_cast<const EnumLit*>(e);
            run.report("E204", e->span, "enum literal '" + en->name + "' has no target type here",
                       "enum literals are only valid as enum parameter values");
            return {};
        }
        case NodeKind::Ident: {
            const auto* id = static_cast<const Ident*>(e);
            return resolve(id->name, e->span);
        }
        case NodeKind::AttrRef:
            return compileAttrRef(static_cast<const AttrRef*>(e), run);
        case NodeKind::VecLit: {
            const auto* v = static_cast<const VecLit*>(e);
            std::vector<float> f;
            for (const Expr* el : v->elems) {
                const auto* num = static_cast<const NumberLit*>(el);
                f.push_back(std::stof(num->text));
            }
            if (f.size() == 2) return makeValueResult(Value(glm::vec2(f[0], f[1])));
            if (f.size() == 3) return makeValueResult(Value(glm::vec3(f[0], f[1], f[2])));
            if (f.size() == 4) return makeValueResult(Value(glm::vec4(f[0], f[1], f[2], f[3])));
            run.report("E100", e->span, "vector literals are vec2..vec4");
            return {};
        }
        case NodeKind::ListLit: {
            const auto* l = static_cast<const ListLit*>(e);
            auto elems = std::make_shared<std::vector<Value>>();
            ScalarType elemBase = ScalarType::None;
            GeoKind elemKind = GeoKind::Any;
            for (const Expr* el : l->elems) {
                TypedValue tv = compileExprImpl(el, run, resolve);
                if (!tv) continue;
                if (tv.field) {
                    run.report("E205", el->span, "list elements must be values, got " + typeName(tv.type));
                    continue;
                }
                const ScalarType eb = valueBase(tv.value);
                if (elemBase == ScalarType::None) {
                    elemBase = eb;
                    elemKind = tv.type.geoKind;
                } else if (eb != elemBase) {
                    run.report("E204", el->span, "list elements must share one type");
                }
                elems->push_back(tv.value);
            }
            TypedValue tv = makeValueResult(Value(ListValuePtr(elems)));
            tv.type = Type{elemBase, false, elemKind, true};
            return tv;
        }
        case NodeKind::Paren:
            return compileExprImpl(static_cast<const Paren*>(e)->inner, run, resolve);
        case NodeKind::Unary: {
            const auto* u = static_cast<const Unary*>(e);
            TypedValue a = compileExprImpl(u->operand, run, resolve);
            if (!a) return {};
            if (a.field) {
                FieldNode* n = run.newNode();
                n->kind = FKind::Unary;
                n->op = u->op;
                n->args = {a.field};
                n->span = e->span;
                n->type = u->op == "!" ? ScalarType::Bool : a.field->type;
                return makeFieldResult(n);
            }
            Value r = valueUnary(u->op, a.value);
            return makeValueResult(r);
        }
        case NodeKind::Binary: {
            const auto* b = static_cast<const Binary*>(e);
            TypedValue l = compileExprImpl(b->lhs, run, resolve);
            TypedValue r = compileExprImpl(b->rhs, run, resolve);
            if (!l || !r) return {};
            if (l.field || r.field) {
                const FieldNode* fl = asFieldNode(l, run);
                const FieldNode* fr = asFieldNode(r, run);
                FieldNode* n = run.newNode();
                n->kind = FKind::Binary;
                n->op = b->op;
                n->args = {fl, fr};
                n->span = e->span;
                n->type = binaryResultBase(b->op, fl->type, fr->type);
                if (n->type == ScalarType::None) {
                    run.report("E204", e->span, "incompatible operand types for '" + b->op + "'");
                    return {};
                }
                return makeFieldResult(n);
            }
            Value v = valueBinary(b->op, l.value, r.value);
            return makeValueResult(v);
        }
        case NodeKind::Ternary: {
            const auto* t = static_cast<const Ternary*>(e);
            TypedValue cond = compileExprImpl(t->cond, run, resolve);
            TypedValue a = compileExprImpl(t->thenExpr, run, resolve);
            TypedValue b = compileExprImpl(t->elseExpr, run, resolve);
            if (!cond || !a || !b) return {};
            if (cond.field || a.field || b.field) {
                FieldNode* n = run.newNode();
                n->kind = FKind::Ternary;
                n->args = {asFieldNode(cond, run), asFieldNode(a, run), asFieldNode(b, run)};
                n->span = e->span;
                n->type = promoteBase(n->args[1]->type, n->args[2]->type);
                if (n->type == ScalarType::None) {
                    run.report("E204", e->span, "ternary branches have incompatible types");
                    return {};
                }
                return makeFieldResult(n);
            }
            const ScalarType rt = promoteBase(valueBase(a.value), valueBase(b.value));
            const Value& picked = asBool(cond.value) ? a.value : b.value;
            return makeValueResult(convertValue(picked, rt));
        }
        case NodeKind::Call:
            return compileCall(static_cast<const Call*>(e), run, resolve);
        case NodeKind::ErrorExpr:
            return {};  // the parse diagnostic was already reported
        default:
            return {};
    }
}

TypedValue compileCall(const Call* c, RunContext& run, const IdentResolver& resolve) {
    if (c->path.size() != 1) {
        std::string q;
        for (const std::string& p : c->path) q += (q.empty() ? "" : ".") + p;
        run.report("E201", c->span, "qualified operation '" + q + "' is not supported at stage E4",
                   "def/imports are stage E5");
        return {};
    }
    const std::string& name = c->path[0];
    const BuiltinSig* sig = findBuiltin(name);
    if (!sig) {
        run.report("E201", c->span, "unknown operation '" + name + "'",
                   "see the built-in catalog (spec §8)");
        return {};
    }
    if (sig->deferredStage) {
        run.report("E201", c->span,
                   "operation '" + name + "' is not supported at stage E4 (" + sig->deferredStage + ")");
        return {};
    }

    std::vector<const CallArg*> byParam;
    std::vector<const CallArg*> variadicArgs;
    bindCallArgs(*sig, *c, byParam, variadicArgs, *run.diagnostics);

    const size_t np = sig->params.size();
    std::vector<TypedValue> argTV(np);
    std::vector<Value> enumValues(np);
    for (size_t i = 0; i < np; ++i) {
        const ParamSig& p = sig->params[i];
        const CallArg* arg = byParam[i];
        if (!arg) continue;
        if (!p.enumValues.empty()) {
            // Bare idents are type-driven enum literals (spec §13): an ident
            // naming an enum value is the literal; an ident naming a defined
            // binding is a computed string (membership checked below).
            std::string enumName;
            if (arg->value->kind == NodeKind::EnumLit) {
                enumName = static_cast<const EnumLit*>(arg->value)->name;
            } else if (arg->value->kind == NodeKind::Ident &&
                       std::find(p.enumValues.begin(), p.enumValues.end(),
                                 static_cast<const Ident*>(arg->value)->name) != p.enumValues.end()) {
                enumName = static_cast<const Ident*>(arg->value)->name;
            } else {
                TypedValue tv = compileExprImpl(arg->value, run, resolve);
                if (tv && !tv.field && valueBase(tv.value) == ScalarType::String)
                    enumName = asString(tv.value);
            }
            if (!enumName.empty()) {
                if (std::find(p.enumValues.begin(), p.enumValues.end(), enumName) == p.enumValues.end()) {
                    std::string values;
                    for (size_t k = 0; k < p.enumValues.size(); ++k) values += (k ? ", " : "") + p.enumValues[k];
                    run.report("E206", arg->value->span,
                               "'" + enumName + "' is not a valid value of enum parameter '" + p.name + "'",
                               "one of: " + values);
                    continue;
                }
                enumValues[i] = Value(enumName);
            }
            continue;
        }
        TypedValue tv = compileExprImpl(arg->value, run, resolve);
        if (!tv) continue;
        if (p.kind == ParamKind::Value && tv.field) {
            run.report("E205", arg->value->span,
                       "parameter '" + p.name + "' of '" + name + "' expects a value, got " +
                           typeName(tv.type),
                       "reduce the field with an aggregator (E2) or pass a constant");
            continue;
        }
        argTV[i] = tv;
    }

    std::vector<Value> variadic;
    for (const CallArg* arg : variadicArgs) {
        TypedValue tv = compileExprImpl(arg->value, run, resolve);
        if (!tv) continue;
        if (tv.field) {
            run.report("E205", arg->value->span,
                       "ramp points must be values, got " + typeName(tv.type));
            continue;
        }
        variadic.push_back(tv.value);
    }

    // §6.3 expression functions: field-polymorphic — a call with only value
    // arguments constant-folds into a value.
    if (sig->exprFunc) {
        std::vector<Type> argTypes;
        bool anyField = false;
        for (size_t i = 0; i < np; ++i) {
            argTypes.push_back(argTV[i].type);
            anyField = anyField || argTV[i].field != nullptr;
        }
        for (const Value& v : variadic) argTypes.push_back(Type{valueBase(v), false, GeoKind::Any});
        Type rt = inferExprFuncType(sig->id, argTypes, c->span, *run.diagnostics);
        if (!anyField) {
            BoundCall bound;
            bound.sig = sig;
            bound.span = c->span;
            for (size_t i = 0; i < np; ++i) {
                if (byParam[i] && argTV[i]) {
                    // Only supplied arguments (missing optionals mean "absent",
                    // e.g. the vec3(1) broadcast form — not a none value).
                    bound.values.push_back(argTV[i].value);
                    bound.present.push_back(true);
                } else {
                    bound.present.push_back(false);
                }
                bound.fields.push_back(nullptr);
            }
            bound.values.insert(bound.values.end(), variadic.begin(), variadic.end());
            Value v = evalBuiltinCall(bound, run);
            return makeValueResult(v);
        }
        FieldNode* n = run.newNode();
        n->kind = FKind::Call;
        n->callId = static_cast<int>(sig->id);
        n->span = c->span;
        n->type = rt.base;
        for (size_t i = 0; i < np; ++i) {
            if (byParam[i] && argTV[i]) n->args.push_back(asFieldNode(argTV[i], run));
        }
        n->params = std::move(variadic);
        return makeFieldResult(n);
    }

    // Pack value args / field args with defaults.
    BoundCall bound;
    bound.sig = sig;
    bound.span = c->span;
    for (size_t i = 0; i < np; ++i) {
        const ParamSig& p = sig->params[i];
        const bool present = byParam[i] != nullptr && argTV[i].operator bool();
        bound.present.push_back(byParam[i] != nullptr);
        if (!p.enumValues.empty()) {
            bound.values.push_back(byParam[i] ? enumValues[i] : p.defValue);
            bound.fields.push_back(nullptr);
            continue;
        }
        if (p.kind == ParamKind::Value) {
            Value val = present ? argTV[i].value : p.defValue;
            // Normalize to the declared base (int literal for an f32 knob,
            // scalar for a vec, ...); convertibility was checked statically.
            if (isNumericBase(p.base) || isVectorBase(p.base)) {
                const Value cv = convertValue(val, p.base);
                if (!isNone(cv)) val = cv;
            }
            bound.values.push_back(val);
            bound.fields.push_back(nullptr);
            continue;
        }
        // Field parameter of a generator/transform.
        bound.values.push_back(Value());
        if (present && isNone(argTV[i].value) && !argTV[i].field && p.optional) {
            bound.fields.push_back(nullptr);  // explicit none
        } else if (present) {
            bound.fields.push_back(asFieldNode(argTV[i], run));
        } else if (p.defPosition) {
            FieldNode* n = run.newNode();
            n->kind = FKind::AttrP;
            n->type = ScalarType::Vec3;
            n->span = c->span;
            bound.fields.push_back(n);
        } else if (p.defIndex) {
            FieldNode* n = run.newNode();
            n->kind = FKind::AttrIndex;
            n->type = ScalarType::Int;
            n->span = c->span;
            bound.fields.push_back(n);
        } else if (p.hasDefValue) {
            TypedValue def = makeValueResult(p.defValue);
            bound.fields.push_back(asFieldNode(def, run));
        } else {
            bound.fields.push_back(nullptr);  // optional without argument
        }
    }

    if (sig->result.isField) {
        // §8.5 field generator. position()/normal()/index() map straight onto
        // the attribute nodes so they share memoization with @P/@N/@index.
        if (sig->id == BuiltinId::Position || sig->id == BuiltinId::Normal ||
            sig->id == BuiltinId::Index) {
            FieldNode* n = run.newNode();
            n->span = c->span;
            if (sig->id == BuiltinId::Position) {
                n->kind = FKind::AttrP;
                n->type = ScalarType::Vec3;
            } else if (sig->id == BuiltinId::Normal) {
                n->kind = FKind::AttrN;
                n->type = ScalarType::Vec3;
            } else {
                n->kind = FKind::AttrIndex;
                n->type = ScalarType::Int;
            }
            return makeFieldResult(n);
        }
        FieldNode* n = run.newNode();
        n->kind = FKind::Call;
        n->callId = static_cast<int>(sig->id);
        n->span = c->span;
        n->type = sig->result.base;
        for (size_t i = 0; i < np; ++i) {
            if (sig->params[i].kind == ParamKind::Field) {
                if (bound.fields[i]) n->args.push_back(bound.fields[i]);
            } else {
                n->params.push_back(bound.values[i]);
            }
        }
        return makeFieldResult(n);
    }

    Value v = evalBuiltinCall(bound, run);
    TypedValue tv = makeValueResult(v);
    if (sig->results.empty()) {
        // Multi-output nodes keep the tuple type from the payload; their
        // declared `result` is empty by construction.
        tv.type = sig->result;
        tv.type.isField = false;
        if (sig->resultGeoKindOfFirstArg && !sig->params.empty() && argTV[0])
            tv.type.geoKind = argTV[0].type.geoKind;
    }
    return tv;
}

}  // namespace

TypedValue compileExpr(const Expr* expr, RunContext& run, const IdentResolver& resolve) {
    return compileExprImpl(expr, run, resolve);
}

Value compileExprToValue(const Expr* expr, RunContext& run, const IdentResolver& resolve) {
    TypedValue tv = compileExprImpl(expr, run, resolve);
    if (!tv) return {};
    if (tv.field) {
        run.report("E205", expr ? expr->span : Span{},
                   "field used where a value is required",
                   "reduce it with an aggregator (E2) or use a constant");
        return {};
    }
    return tv.value;
}

}  // namespace pgg
