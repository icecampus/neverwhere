#include "../../pch.h"

#include "typecheck.h"

#include <unordered_map>

#include "builtins.h"
#include "schema.h"

namespace pgg {
namespace {

// Attribute/group reads of an expression, closed over local field bindings
// (the §7.2 field-closure case: a field passed as an argument is checked in
// its consumption context after expansion).
struct ExprClosure {
    std::vector<std::pair<std::string, Span>> attrs;   // @attr refs (via idents too)
    std::vector<std::pair<std::string, Span>> groups;  // literal ingroup("...") reads
};

class Typechecker {
public:
    Typechecker(const FlatProgram& flat, const std::vector<std::string>& boundParams,
                std::vector<Diagnostic>& diags, std::vector<size_t>& runtimeContracts)
        : flat_(flat), boundParams_(boundParams), diags_(diags), runtimeContracts_(runtimeContracts) {}

    void file(const File* f) {
        if (!f) return;
        for (const Node* item : f->items) {
            switch (item->kind) {
                case NodeKind::Import:
                    error("E201", item->span, "import reached the typecheck unexpanded",
                          "imports are resolved at expansion (stage E5)");
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
                    error("E201", item->span, "def reached the typecheck unexpanded",
                          "defs are inlined at expansion (stage E5)");
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
        contracts();
    }

private:
    const FlatProgram& flat_;
    const std::vector<std::string>& boundParams_;
    std::vector<Diagnostic>& diags_;
    std::vector<size_t>& runtimeContracts_;
    std::unordered_map<std::string, Type> env_;
    std::unordered_map<std::string, GeoSchema> schemas_;
    std::unordered_map<const Expr*, GeoSchema> callSchemas_;
    std::unordered_map<std::string, ExprClosure> closures_;
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

    // Basic instance-path context for diagnostics inside instances (§9.5;
    // the full stack formatting is stage E6).
    std::string instanceSuffix(const std::string& flatName) const {
        auto it = flat_.instanceOfBinding.find(flatName);
        if (it == flat_.instanceOfBinding.end()) return {};
        return " [instance " + flat_.instances[it->second].path + "]";
    }

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
                } else if (nTargets == 1) {
                    define(b->targets.names[0], t);
                    // Def-interface types (E5): the binding materializes a def
                    // parameter or output and must match its declaration.
                    if (auto dit = flat_.declaredTypes.find(b->targets.names[0]);
                        dit != flat_.declaredTypes.end()) {
                        if (t.base != ScalarType::None && !canConvert(t, dit->second)) {
                            error("E204", b->span,
                                  "'" + b->targets.names[0] + "' expects " + typeName(dit->second) +
                                      " (def interface), got " + typeName(t) + instanceSuffix(b->targets.names[0]));
                            // Recover with the declared type: downstream reads
                            // see the interface, not the mismatched argument.
                            define(b->targets.names[0], dit->second);
                        }
                    }
                }
                // Schema/closure tracking flows through both shapes.
                ExprClosure cl = gather(b->value);
                const GeoSchema& schema = schemaOfExpr(b->value);
                for (const std::string& n : b->targets.names) {
                    closures_[n] = cl;
                    schemas_[n] = schema;
                }
                break;
            }
            case NodeKind::RepeatZone:
                error("E201", s->span, "repeat zones are not supported at stage E5", "zones are stage E7");
                break;
            case NodeKind::ForeachZone:
                error("E201", s->span, "foreach zones are not supported at stage E5", "zones are stage E7");
                break;
            default:
                break;  // tap: diagnostics layer, a no-op while debug=off (§9.2)
        }
    }

    // --- schema inference (merged into the same topological pass) --------------

    const GeoSchema& openSchema() const {
        static const GeoSchema kOpen;
        return kOpen;
    }

    const GeoSchema& schemaOfExpr(const Expr* e) {
        if (!e) return openSchema();
        if (e->kind == NodeKind::Ident) {
            auto it = schemas_.find(static_cast<const Ident*>(e)->name);
            return it != schemas_.end() ? it->second : openSchema();
        }
        if (e->kind == NodeKind::Paren) return schemaOfExpr(static_cast<const Paren*>(e)->inner);
        if (e->kind == NodeKind::Call) {
            auto it = callSchemas_.find(e);
            return it != callSchemas_.end() ? it->second : openSchema();
        }
        return openSchema();
    }

    const GeoSchema& argSchema(const std::vector<const CallArg*>& byParam, size_t i) {
        if (i >= byParam.size() || !byParam[i]) return openSchema();
        return schemaOfExpr(byParam[i]->value);
    }

    // Literal string of a name/enum argument (attribute and group names must
    // be provable statically; anything else makes the schema open). Named-arg
    // bare idents are type-driven enum literals (spec §13): an ident that is
    // not a defined binding reads as the literal; a defined one is a computed
    // string and therefore not provable.
    bool literalString(const CallArg* arg, std::string& out) const {
        if (!arg || !arg->value) return false;
        if (arg->value->kind == NodeKind::StringLit) {
            out = static_cast<const StringLit*>(arg->value)->value;
            return true;
        }
        if (arg->value->kind == NodeKind::EnumLit) {
            out = static_cast<const EnumLit*>(arg->value)->name;
            return true;
        }
        if (arg->value->kind == NodeKind::Ident) {
            const std::string& n = static_cast<const Ident*>(arg->value)->name;
            if (env_.count(n)) return false;
            out = n;
            return true;
        }
        return false;
    }

    static size_t domainIndex(const std::string& name) {
        return static_cast<size_t>(domainFromName(name));
    }

    // Static E302/E305: every @attr/@N/ingroup read of a field consumed on a
    // closed-schema geometry must exist there (spec §7.6). Open schemas skip —
    // the runtime checks stay the fallback.
    void checkField(const GeoSchema& s, const Expr* fieldExpr) {
        if (s.open || !fieldExpr) return;
        const ExprClosure cl = gather(fieldExpr);
        for (const auto& [attr, span] : cl.attrs) {
            if (attr == "P" || attr == "index") continue;
            if (attr == "N") {
                if (!s.hasN && !s.hasAttr("N")) {
                    error("E302", span, "attribute @N is not present on this geometry (static schema)",
                          "compute normals upstream (compute_normals)");
                }
                continue;
            }
            if (!s.hasAttr(attr)) {
                error("E302", span,
                      "attribute '@" + attr + "' is not present on this geometry (static schema)",
                      "write it upstream with set() or check the def contract (§7.6)");
            }
        }
        for (const auto& [group, span] : cl.groups) {
            if (!s.hasGroup(group)) {
                error("E305", span, "group \"" + group + "\" does not exist on this geometry (static schema)",
                      "mark it upstream (§8.6)");
            }
        }
    }

    void checkFieldArgs(const GeoSchema& s, const std::vector<const CallArg*>& byParam,
                        std::initializer_list<size_t> fieldParams) {
        for (size_t i : fieldParams)
            if (i < byParam.size() && byParam[i]) checkField(s, byParam[i]->value);
    }

    void storeSchema(const Expr* call, GeoSchema s) { callSchemas_[call] = std::move(s); }

    void computeCallSchema(const BuiltinSig& sig, const Call& c,
                           const std::vector<const CallArg*>& byParam, const std::vector<Type>& argTypes) {
        std::string lit, lit2, lit3;
        switch (sig.id) {
            case BuiltinId::IcoSphere:
            case BuiltinId::Box:
            case BuiltinId::Grid:
                storeSchema(&c, sourceSchema(GeoKind::Mesh, true));
                break;
            case BuiltinId::MeshLine:
            case BuiltinId::PointCloud:
                storeSchema(&c, sourceSchema(GeoKind::Points, false));
                break;
            case BuiltinId::Transform:
            case BuiltinId::Smooth:
                storeSchema(&c, argSchema(byParam, 0));
                break;
            case BuiltinId::SetPosition: {
                const GeoSchema& s = argSchema(byParam, 0);
                checkFieldArgs(s, byParam, {1, 2, 3});  // offset, pos, where
                storeSchema(&c, s);
                break;
            }
            case BuiltinId::ComputeNormals: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) && lit == "flat") {
                        // flat mode writes faceted normals as a corner attribute
                        // and leaves @N (points) untouched (§19 v0.8).
                        s.attrs[domainIndex("corners")]["N"] = ScalarType::Vec3;
                    } else {
                        s.hasN = true;
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Mark: {
                const GeoSchema& in = argSchema(byParam, 0);
                checkFieldArgs(in, byParam, {2});  // where
                GeoSchema s = in;
                if (!s.open) {
                    std::string dom = "points";
                    const bool hasName = literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit);
                    const bool hasDom = byParam.size() <= 3 || !byParam[3] ||
                                        (literalString(byParam[3], dom));
                    if (hasName && hasDom) {
                        s.groups[domainIndex(dom)].insert(lit);
                    } else {
                        s = openSchema();  // non-literal name: could mark anything
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Unmark: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit)) {
                        for (auto& perDomain : s.groups) perDomain.erase(lit);
                    } else {
                        s = openSchema();
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::SetAttr: {
                const GeoSchema& in = argSchema(byParam, 0);
                checkFieldArgs(in, byParam, {2});  // value
                GeoSchema s = in;
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) &&
                        lit != "P" && lit != "N" && lit != "index") {
                        // Domain inference mirrors the runtime rule: constant
                        // field -> detail, otherwise points (§19 v0.9).
                        const Type vt = byParam.size() > 2 && byParam[2] ? argTypes[2] : Type{};
                        const size_t dom = vt.isField ? domainIndex("points") : domainIndex("detail");
                        s.attrs[dom][lit] = vt.base == ScalarType::None ? ScalarType::F32 : vt.base;
                    } else {
                        s = openSchema();
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::RemoveAttr: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit)) {
                        for (auto& perDomain : s.attrs) perDomain.erase(lit);
                    } else {
                        s = openSchema();
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::RenameAttr: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) &&
                        literalString(byParam.size() > 2 ? byParam[2] : nullptr, lit2)) {
                        for (auto& perDomain : s.attrs) {
                            if (auto it = perDomain.find(lit); it != perDomain.end()) {
                                perDomain[lit2] = it->second;
                                perDomain.erase(it);
                            }
                        }
                    } else {
                        s = openSchema();
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Promote: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    const bool allLit = literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) &&
                                        literalString(byParam.size() > 2 ? byParam[2] : nullptr, lit2) &&
                                        literalString(byParam.size() > 3 ? byParam[3] : nullptr, lit3);
                    if (allLit) {
                        auto& from = s.attrs[domainIndex(lit2)];
                        auto& to = s.attrs[domainIndex(lit3)];
                        // Mirror of the runtime check: the attribute must be
                        // present on the `from` domain specifically.
                        if (auto it = from.find(lit); it != from.end()) {
                            to[lit] = it->second;
                        } else {
                            error("E302", c.span,
                                  "attribute '@" + lit + "' is not present on the " + lit2 +
                                      " domain (static schema)",
                                  "write it with set() first, or promote from the domain that has it");
                        }
                    }  // non-literal: promote keeps every attribute somewhere — s stays valid
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Merge: {
                const GeoSchema& a = argSchema(byParam, 0);
                const GeoSchema& bb = argSchema(byParam, 1);
                if (a.open || bb.open) break;
                GeoSchema s = a;
                s.kind = a.kind == bb.kind ? a.kind : GeoKind::Any;
                s.hasN = a.hasN || bb.hasN;
                for (size_t d = 0; d < 4; ++d) {
                    for (const auto& [n, t] : bb.attrs[d]) s.attrs[d].emplace(n, t);
                    for (const std::string& n : bb.groups[d]) s.groups[d].insert(n);
                }
                s.instanceSource = nullptr;
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::DistributePoints: {
                const GeoSchema& surface = argSchema(byParam, 0);
                checkFieldArgs(surface, byParam, {1});  // density
                if (surface.open) break;
                GeoSchema s = sourceSchema(GeoKind::Points, false);
                // Candidates inherit surface point attributes/groups (§19 v0.9).
                s.attrs[domainIndex("points")] = surface.attrs[domainIndex("points")];
                s.groups[domainIndex("points")] = surface.groups[domainIndex("points")];
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::InstanceOnPoints: {
                const GeoSchema& anchors = argSchema(byParam, 0);
                const GeoSchema& source = argSchema(byParam, 1);
                if (anchors.open) break;
                GeoSchema s = sourceSchema(GeoKind::Instances, false);
                s.attrs[domainIndex("points")] = anchors.attrs[domainIndex("points")];
                s.groups[domainIndex("points")] = anchors.groups[domainIndex("points")];
                // A non-empty variants list picks sources per anchor: the
                // realized shape is their union, not provable from `source`.
                bool hasVariants = false;
                if (byParam.size() > 2 && byParam[2] && byParam[2]->value) {
                    const Expr* v = byParam[2]->value;
                    hasVariants = v->kind != NodeKind::ListLit ||
                                  !static_cast<const ListLit*>(v)->elems.empty();
                }
                if (!source.open && !hasVariants) s.instanceSource = std::make_shared<GeoSchema>(source);
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Realize: {
                const GeoSchema& inst = argSchema(byParam, 0);
                if (inst.open || !inst.instanceSource) break;
                GeoSchema s = *inst.instanceSource;
                s.kind = GeoKind::Mesh;
                s.instanceSource = nullptr;
                s.attrs[domainIndex("points")]["tint"] = ScalarType::Vec3;  // §19 v0.9
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::MeshFromSdf:
                // Attribute barrier (§8.4): the extracted mesh carries @P only.
                storeSchema(&c, sourceSchema(GeoKind::Mesh, false));
                break;
            case BuiltinId::Count:
                checkFieldArgs(argSchema(byParam, 0), byParam, {2});  // where
                break;
            case BuiltinId::MinOf:
            case BuiltinId::MaxOf:
            case BuiltinId::AvgOf:
            case BuiltinId::SumOf:
                checkFieldArgs(argSchema(byParam, 1), byParam, {0, 2});  // field, where (on `on`)
                break;
            default:
                break;
        }
    }

    // --- contracts (static half; the rest goes to the engine) ---------------------

    void contracts() {
        for (size_t i = 0; i < flat_.contracts.size(); ++i) {
            const FlatContract& c = flat_.contracts[i];
            if (c.attrForm) {
                if (env_.find(c.ident) == env_.end()) {
                    error("E103", c.span, "'" + c.ident + "' is not defined");
                    continue;
                }
                auto sit = schemas_.find(c.ident);
                if (sit == schemas_.end() || sit->second.open) {
                    runtimeContracts_.push_back(i);  // open schema: runtime column check
                    continue;
                }
                const GeoSchema& gs = sit->second;
                const bool present = c.attrName == "P" || c.attrName == "index" ||
                                     (c.attrName == "N" ? gs.hasN || gs.hasAttr("N")
                                                        : gs.hasAttr(c.attrName));
                if (!present) contractError(c);
                continue;
            }
            const Type t = infer(c.cond);
            if (t.base == ScalarType::None) continue;  // already reported
            if (t.isField || t.base != ScalarType::Bool) {
                error("E204", c.span, "expect/ensure condition must be a value bool, got " + typeName(t));
                continue;
            }
            Value v;
            if (constEval(c.cond, v)) {
                if (valueBase(v) == ScalarType::Bool && !asBool(v)) contractError(c);
                continue;  // constant-true: satisfied statically
            }
            runtimeContracts_.push_back(i);
        }
    }

    void contractError(const FlatContract& c) {
        const FlatInstance& inst = flat_.instances[c.instance];
        std::string msg = c.hasMessage ? c.message
                                       : (c.isEnsure ? "ensure of '" + inst.defName + "' failed"
                                                     : "expect of '" + inst.defName + "' failed");
        error(c.isEnsure ? "E304" : "E303", c.span, msg + " [instance " + inst.path + "]");
    }

    // Literal-only constant folding for statically decidable conditions.
    static bool constEval(const Expr* e, Value& out) {
        if (!e) return false;
        switch (e->kind) {
            case NodeKind::NumberLit: {
                const auto* n = static_cast<const NumberLit*>(e);
                try {
                    out = n->isFloat ? Value(std::stof(n->text))
                                     : Value(static_cast<int64_t>(std::stoll(n->text)));
                } catch (...) {
                    return false;
                }
                return true;
            }
            case NodeKind::StringLit:
                out = Value(static_cast<const StringLit*>(e)->value);
                return true;
            case NodeKind::BoolLit:
                out = Value(static_cast<const BoolLit*>(e)->value);
                return true;
            case NodeKind::VecLit: {
                const auto* v = static_cast<const VecLit*>(e);
                float comps[4] = {0, 0, 0, 0};
                const size_t n = v->elems.size();
                if (n < 2 || n > 4) return false;
                for (size_t i = 0; i < n; ++i) {
                    Value c;
                    if (!constEval(v->elems[i], c) || !isNumericBase(valueBase(c))) return false;
                    comps[i] = numericValueF32(c);
                }
                if (n == 2) out = Value(glm::vec2(comps[0], comps[1]));
                if (n == 3) out = Value(glm::vec3(comps[0], comps[1], comps[2]));
                if (n == 4) out = Value(glm::vec4(comps[0], comps[1], comps[2], comps[3]));
                return true;
            }
            case NodeKind::Paren:
                return constEval(static_cast<const Paren*>(e)->inner, out);
            case NodeKind::Unary: {
                const auto* u = static_cast<const Unary*>(e);
                Value v;
                if (!constEval(u->operand, v)) return false;
                out = valueUnary(u->op, v);
                return !isNone(out);
            }
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                Value l, r;
                if (!constEval(b->lhs, l) || !constEval(b->rhs, r)) return false;
                out = valueBinary(b->op, l, r);
                return !isNone(out);
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                Value c;
                if (!constEval(t->cond, c) || valueBase(c) != ScalarType::Bool) return false;
                return constEval(asBool(c) ? t->thenExpr : t->elseExpr, out);
            }
            default:
                return false;
        }
    }

    // --- expression closure (attr/group reads through local bindings) -----------

    ExprClosure gather(const Expr* e) {
        ExprClosure out;
        gatherInto(e, out);
        return out;
    }

    void gatherInto(const Expr* e, ExprClosure& out) {
        if (!e) return;
        switch (e->kind) {
            case NodeKind::Ident: {
                if (auto it = closures_.find(static_cast<const Ident*>(e)->name); it != closures_.end()) {
                    out.attrs.insert(out.attrs.end(), it->second.attrs.begin(), it->second.attrs.end());
                    out.groups.insert(out.groups.end(), it->second.groups.begin(), it->second.groups.end());
                }
                break;
            }
            case NodeKind::AttrRef:
                out.attrs.push_back({static_cast<const AttrRef*>(e)->name, e->span});
                break;
            case NodeKind::Paren:
                gatherInto(static_cast<const Paren*>(e)->inner, out);
                break;
            case NodeKind::Unary:
                gatherInto(static_cast<const Unary*>(e)->operand, out);
                break;
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                gatherInto(b->lhs, out);
                gatherInto(b->rhs, out);
                break;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                gatherInto(t->cond, out);
                gatherInto(t->thenExpr, out);
                gatherInto(t->elseExpr, out);
                break;
            }
            case NodeKind::VecLit:
                for (const Expr* el : static_cast<const VecLit*>(e)->elems) gatherInto(el, out);
                break;
            case NodeKind::ListLit:
                for (const Expr* el : static_cast<const ListLit*>(e)->elems) gatherInto(el, out);
                break;
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                if (c->path.size() == 1 && c->path[0] == "ingroup" && !c->args.empty() &&
                    c->args[0].value && c->args[0].value->kind == NodeKind::StringLit) {
                    out.groups.push_back(
                        {static_cast<const StringLit*>(c->args[0].value)->value, c->span});
                }
                for (const CallArg& a : c->args) gatherInto(a.value, out);
                break;
            }
            default:
                break;  // literals
        }
    }

    // --- expression type inference (E1-E4 logic, unchanged) -----------------------

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
        // Named user attributes: at an open schema a read gets a provisional
        // f32 type and a missing attribute is a runtime E302; closed schemas
        // are checked statically at the consuming node (§7.6).
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
            error("E201", c->span, "qualified operation '" + q + "' reached the typecheck",
                  "qualified calls are resolved at expansion (stage E5)");
            return {};
        }
        const std::string& name = c->path[0];
        const BuiltinSig* sig = findBuiltin(name);
        if (!sig) {
            error("E201", c->span, "unknown operation '" + name + "'", "see the built-in catalog (spec §8)");
            return {};
        }
        if (sig->deferredStage) {
            error("E201", c->span, "operation '" + name + "' is not supported at stage E5 (" + sig->deferredStage + ")");
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
        computeCallSchema(*sig, *c, byParam, argTypes);
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

void typecheckFlat(const FlatProgram& flat, const std::vector<std::string>& boundParams,
                   std::vector<Diagnostic>& diagnostics, std::vector<size_t>& runtimeContracts) {
    Typechecker tc(flat, boundParams, diagnostics, runtimeContracts);
    tc.file(flat.file);
}

}  // namespace pgg
