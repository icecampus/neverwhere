#include "../../pch.h"

#include "typecheck.h"

#include <unordered_map>

#include "builtins.h"

namespace pgg {
namespace {

class Typechecker {
public:
    Typechecker(const std::vector<std::string>& boundParams, std::vector<Diagnostic>& diags)
        : boundParams_(boundParams), diags_(diags) {}

    void file(const File* f) {
        if (!f) return;
        for (const Node* item : f->items) {
            switch (item->kind) {
                case NodeKind::Import:
                    error("E201", item->span, "import is not supported at stage E4", "modules are stage E5");
                    break;
                case NodeKind::ParamDecl: {
                    const auto* p = static_cast<const ParamDecl*>(item);
                    define(p->name, typeFromRef(*p->type));
                    if (!p->hasDefault && !isBound(p->name)) {
                        error("E604", item->span,
                              "param '" + p->name + "' has no default and was not bound at launch",
                              "pass --param " + p->name + "=<value>");
                    }
                    break;
                }
                case NodeKind::Def:
                    error("E201", item->span, "def is not supported at stage E4", "composition is stage E5");
                    break;
                case NodeKind::OutputDecl:
                    output(static_cast<const OutputDecl*>(item));
                    break;
                default:
                    stmt(static_cast<const Stmt*>(item));
                    break;
            }
        }
        if (outputs_.empty()) {
            Span s;
            if (f && !f->items.empty()) s = f->items.front()->span;
            error("E605", s, "executable file has no output declaration",
                  "add `output <name>` for the graph roots (spec §6.7)");
        }
    }

private:
    const std::vector<std::string>& boundParams_;
    std::vector<Diagnostic>& diags_;
    std::unordered_map<std::string, Type> env_;
    std::vector<std::pair<std::string, Span>> outputs_;
    const BuiltinSig* lastSig_ = nullptr;  // sig of the last inferred call (multi-output check)
    bool allowMulti_ = false;              // destructuring position accepts multi-output calls

    bool isBound(const std::string& name) const {
        return std::find(boundParams_.begin(), boundParams_.end(), name) != boundParams_.end();
    }

    void error(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
    }

    void define(const std::string& name, Type t) { env_[name] = t; }

    void output(const OutputDecl* o) {
        for (const auto& [name, span] : outputs_) {
            if (name == o->name) {
                error("E102", o->span, "output '" + o->name + "' is declared twice");
                return;
            }
        }
        if (auto it = env_.find(o->name); it != env_.end()) {
            const Type& t = it->second;
            if (t.isField) {
                error("E204", o->span,
                      "output '" + o->name + "' cannot export " + typeName(t) + " (spec §6.7)",
                      "bind a concrete geo/value root; materialize the field with set()");
            } else if (t.base == ScalarType::Rng) {
                error("E204", o->span, "output '" + o->name + "' cannot export rng (spec §6.7)",
                      "rng is an internal generator value, not an exportable result");
            }
        }
        // A missing name was already reported as E103 by the parser-stage validator.
        outputs_.push_back({o->name, o->span});
    }

    void stmt(const Stmt* s) {
        if (!s) return;
        switch (s->kind) {
            case NodeKind::Binding: {
                const auto* b = static_cast<const Binding*>(s);
                const size_t nTargets = b->targets.names.size();
                lastSig_ = nullptr;
                allowMulti_ = nTargets > 1;
                Type t = infer(b->value);
                allowMulti_ = false;
                if (nTargets > 1) {
                    // Multi-output destructuring (E2): arity and per-target
                    // types come from the registry entry of the call.
                    if (!lastSig_ || lastSig_->results.empty()) {
                        error("E202", s->span, "destructuring needs a multi-output operation",
                              "single-result operations bind exactly one target");
                    } else if (lastSig_->results.size() != nTargets) {
                        error("E202", s->span,
                              "'" + std::string(lastSig_->name) + "' returns " +
                                  std::to_string(lastSig_->results.size()) + " value(s), got " +
                                  std::to_string(nTargets) + " targets");
                    } else {
                        for (size_t i = 0; i < nTargets; ++i) define(b->targets.names[i], lastSig_->results[i]);
                    }
                    break;
                }
                if (nTargets == 1) define(b->targets.names[0], t);
                break;
            }
            case NodeKind::RepeatZone:
                error("E201", s->span, "repeat zones are not supported at stage E4", "zones are stage E7");
                break;
            case NodeKind::ForeachZone:
                error("E201", s->span, "foreach zones are not supported at stage E4", "zones are stage E7");
                break;
            default:
                break;  // tap: diagnostics layer, a no-op at E2
        }
    }

    Type infer(const Expr* e) {
        if (!e) return {};
        switch (e->kind) {
            case NodeKind::NumberLit:
                return scalar(static_cast<const NumberLit*>(e)->isFloat ? ScalarType::F32 : ScalarType::Int);
            case NodeKind::StringLit:
                return scalar(ScalarType::String);
            case NodeKind::BoolLit:
                return scalar(ScalarType::Bool);
            case NodeKind::NoneLit:
                return scalar(ScalarType::None);
            case NodeKind::EnumLit:
                error("E204", e->span,
                      "enum literal '" + static_cast<const EnumLit*>(e)->name + "' has no target type here",
                      "enum literals are only valid as enum parameter values");
                return {};
            case NodeKind::Ident: {
                const auto* id = static_cast<const Ident*>(e);
                if (auto it = env_.find(id->name); it != env_.end()) return it->second;
                error("E103", e->span, "'" + id->name + "' is used before definition");
                return {};
            }
            case NodeKind::AttrRef:
                return inferAttr(static_cast<const AttrRef*>(e));
            case NodeKind::VecLit: {
                const size_t n = static_cast<const VecLit*>(e)->elems.size();
                if (n == 2) return scalar(ScalarType::Vec2);
                if (n == 3) return scalar(ScalarType::Vec3);
                if (n == 4) return scalar(ScalarType::Vec4);
                error("E100", e->span, "vector literals are vec2..vec4");
                return {};
            }
            case NodeKind::ListLit: {
                const auto* l = static_cast<const ListLit*>(e);
                ScalarType elemBase = ScalarType::None;
                GeoKind elemKind = GeoKind::Any;
                for (const Expr* el : l->elems) {
                    Type t = infer(el);
                    if (t.base == ScalarType::None) continue;
                    if (t.isField) {
                        error("E205", el->span, "list elements must be values, got " + typeName(t));
                        continue;
                    }
                    if (elemBase == ScalarType::None) {
                        elemBase = t.base;
                        elemKind = t.geoKind;
                    } else if (t.base != elemBase) {
                        error("E204", el->span, "list elements must share one type");
                    }
                }
                return Type{elemBase, false, elemKind, true};
            }
            case NodeKind::Paren:
                return infer(static_cast<const Paren*>(e)->inner);
            case NodeKind::Unary:
                return inferUnary(static_cast<const Unary*>(e));
            case NodeKind::Binary:
                return inferBinary(static_cast<const Binary*>(e));
            case NodeKind::Ternary:
                return inferTernary(static_cast<const Ternary*>(e));
            case NodeKind::Call:
                return inferCall(static_cast<const Call*>(e));
            default:
                return {};  // ErrorExpr: parse diagnostic already reported
        }
    }

    Type inferAttr(const AttrRef* ref) {
        if (ref->name == "P" || ref->name == "N") return field(ScalarType::Vec3);
        if (ref->name == "index") return field(ScalarType::Int);
        if (ref->name == "iteration" || ref->name == "piece_index") {
            error("E302", ref->span, "'@" + ref->name + "' is only valid inside a zone body",
                  "zones are stage E7");
            return field(ScalarType::Int);
        }
        // Named user attributes (E2): the schema is runtime data, so a read
        // gets a provisional f32 type statically; runtime buffers are
        // duck-typed and a missing attribute is a runtime E302.
        return field(ScalarType::F32);
    }

    Type inferUnary(const Unary* u) {
        const Type t = infer(u->operand);
        if (t.base == ScalarType::None) return {};
        if (u->op == "-") {
            if (!isNumericBase(t.base) && !isVectorBase(t.base)) {
                error("E204", u->span, "unary '-' needs a numeric operand, got " + typeName(t));
                return {};
            }
            // Negating a bool happens as int (rank 0 of the chain).
            Type r = t.base == ScalarType::Bool ? scalar(ScalarType::Int) : t;
            return r;
        }
        // "!"
        if (t.base != ScalarType::Bool) {
            error("E204", u->span, "unary '!' needs a bool operand, got " + typeName(t),
                  "compare first, e.g. !(x > 0)");
            return {};
        }
        return t;
    }

    Type inferBinary(const Binary* b) {
        const Type lt = infer(b->lhs);
        const Type rt = infer(b->rhs);
        if (lt.base == ScalarType::None || rt.base == ScalarType::None) return {};
        const bool isField = lt.isField || rt.isField;
        const std::string& op = b->op;
        if (op == "&" || op == "|") {
            const ScalarType t = promoteBase(lt.base, rt.base);
            if (t != ScalarType::Bool) {
                error("E204", b->span, "'" + op + "' combines bool masks, got " + typeName(lt) + " and " + typeName(rt),
                      "compare first, e.g. (@slope > 0.4) & (@h > 0.3)");
                return {};
            }
            return Type{ScalarType::Bool, isField, GeoKind::Any};
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=") {
            const ScalarType t = promoteBase(lt.base, rt.base);
            if (t == ScalarType::None) {
                error("E204", b->span, "cannot compare " + typeName(lt) + " and " + typeName(rt));
                return {};
            }
            if (isVectorBase(t) && op != "==" && op != "!=") {
                error("E204", b->span, "ordered comparison '" + op + "' is not defined for vectors",
                      "compare a scalar component, e.g. dot(@N, (0, 1, 0)) > 0.5");
                return {};
            }
            return Type{ScalarType::Bool, isField, GeoKind::Any};
        }
        ScalarType t = promoteBase(lt.base, rt.base);
        if (t == ScalarType::None) {
            error("E204", b->span, "incompatible operand types for '" + op + "': " + typeName(lt) + " and " + typeName(rt));
            return {};
        }
        if (t == ScalarType::Bool) t = ScalarType::Int;  // arithmetic on bools happens as int
        if (op == "%") {
            if (t != ScalarType::Int) {
                error("E204", b->span, "'%' is only defined for integers, got " + typeName(lt) + " and " + typeName(rt));
                return {};
            }
            return Type{ScalarType::Int, isField, GeoKind::Any};
        }
        return Type{t, isField, GeoKind::Any};
    }

    Type inferTernary(const Ternary* t) {
        const Type cond = infer(t->cond);
        const Type a = infer(t->thenExpr);
        const Type b = infer(t->elseExpr);
        if (cond.base == ScalarType::None || a.base == ScalarType::None || b.base == ScalarType::None) return {};
        if (cond.base != ScalarType::Bool) {
            error("E204", t->span, "ternary condition must be bool, got " + typeName(cond));
            return {};
        }
        const ScalarType r = promoteBase(a.base, b.base);
        if (r == ScalarType::None) {
            error("E204", t->span, "ternary branches have incompatible types: " + typeName(a) + " and " + typeName(b));
            return {};
        }
        return Type{r, cond.isField || a.isField || b.isField, GeoKind::Any};
    }

    Type inferCall(const Call* c) {
        if (c->path.size() != 1) {
            std::string q;
            for (const std::string& p : c->path) q += (q.empty() ? "" : ".") + p;
            error("E201", c->span, "qualified operation '" + q + "' is not supported at stage E4",
                  "def/imports are stage E5");
            return {};
        }
        const std::string& name = c->path[0];
        const BuiltinSig* sig = findBuiltin(name);
        if (!sig) {
            error("E201", c->span, "unknown operation '" + name + "'", "see the built-in catalog (spec §8)");
            return {};
        }
        if (sig->deferredStage) {
            error("E201", c->span, "operation '" + name + "' is not supported at stage E4 (" + sig->deferredStage + ")");
            return {};
        }
        if (!sig->results.empty() && !allowMulti_) {
            error("E204", c->span, "'" + name + "' returns a tuple and must be destructured",
                  "min, max = bbox(g)");
            return {};
        }

        std::vector<const CallArg*> byParam;
        std::vector<const CallArg*> variadicArgs;
        bindCallArgs(*sig, *c, byParam, variadicArgs, diags_);

        const size_t np = sig->params.size();
        std::vector<Type> argTypes(np);
        bool anyField = false;
        for (size_t i = 0; i < np; ++i) {
            const ParamSig& p = sig->params[i];
            const CallArg* arg = byParam[i];
            if (!arg) continue;
            if (!p.enumValues.empty()) {
                checkEnumArg(p, arg);
                continue;
            }
            Type t = infer(arg->value);
            argTypes[i] = t;
            if (t.base == ScalarType::None) continue;
            if (p.optional && arg->value->kind == NodeKind::NoneLit) continue;
            const Type target{p.base, p.kind != ParamKind::Value, p.geoKind, p.isList};
            if (p.kind == ParamKind::Value) {
                if (t.isField) {
                    error("E205", arg->value->span,
                          "parameter '" + p.name + "' of '" + name + "' expects a value, got " + typeName(t),
                          "reduce the field with an aggregator (E2) or pass a constant");
                } else if (p.allowString && t.base == ScalarType::String) {
                    // split_rng key overload — fine
                } else if (!canConvert(t, target)) {
                    error("E204", arg->value->span,
                          "parameter '" + p.name + "' of '" + name + "' expects " + typeName(target) + ", got " +
                              typeName(t));
                }
            } else {
                if (!canConvert(t, target)) {
                    std::string hint;
                    if (isVectorBase(p.base) && isNumericBase(t.base))
                        hint = "multiply the scalar by a direction, e.g. @N * value";
                    error("E204", arg->value->span,
                          "parameter '" + p.name + "' of '" + name + "' expects " + typeName(target) + ", got " +
                              typeName(t),
                              hint);
                }
            }
            anyField = anyField || t.isField;
        }
        for (const CallArg* arg : variadicArgs) {
            Type t = infer(arg->value);
            if (t.isField) {
                error("E205", arg->value->span, "ramp points must be values, got " + typeName(t));
            }
            argTypes.push_back(t);
        }

        if (sig->exprFunc) {
            Type rt = inferExprFuncType(sig->id, argTypes, c->span, diags_);
            rt.isField = anyField;
            lastSig_ = sig;  // after arg inference: the root call's sig wins
            return rt;
        }
        Type rt = sig->result;
        if (sig->resultGeoKindOfFirstArg && np > 0 && argTypes[0].base == ScalarType::Geo)
            rt.geoKind = argTypes[0].geoKind;
        lastSig_ = sig;
        return rt;
    }

    void checkEnumArg(const ParamSig& p, const CallArg* arg) {
        // Bare idents are type-driven enum literals (spec §13, E0 note): an
        // ident that names an enum value IS the literal; an ident that names
        // a defined binding is a computed string (membership checked at bind).
        if (arg->value->kind == NodeKind::EnumLit || arg->value->kind == NodeKind::Ident) {
            const std::string& v = arg->value->kind == NodeKind::EnumLit
                                       ? static_cast<const EnumLit*>(arg->value)->name
                                       : static_cast<const Ident*>(arg->value)->name;
            if (std::find(p.enumValues.begin(), p.enumValues.end(), v) != p.enumValues.end()) return;
            const bool definedIdent = arg->value->kind == NodeKind::Ident && env_.count(v) > 0;
            if (!definedIdent) {
                std::string values;
                for (size_t i = 0; i < p.enumValues.size(); ++i) values += (i ? ", " : "") + p.enumValues[i];
                error("E206", arg->value->span,
                      "'" + v + "' is not a valid value of enum parameter '" + p.name + "'",
                      "one of: " + values);
                return;
            }
        }
        const Type t = infer(arg->value);
        if (t.base != ScalarType::None && (t.isField || t.base != ScalarType::String)) {
            error("E204", arg->value->span,
                  "enum parameter '" + p.name + "' expects an enum literal, got " + typeName(t));
        }
        // A computed string's membership is checked when the call is bound.
    }

    static Type scalar(ScalarType base) { return Type{base, false, GeoKind::Any}; }
    static Type field(ScalarType base) { return Type{base, true, GeoKind::Any}; }
};

}  // namespace

void typecheck(const File* file, const std::vector<std::string>& boundParams,
               std::vector<Diagnostic>& diagnostics) {
    Typechecker tc(boundParams, diagnostics);
    tc.file(file);
}

}  // namespace pgg
