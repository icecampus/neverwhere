#include "../../pch.h"

#include "typecheck.h"

#include <unordered_map>
#include <unordered_set>

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
    std::unordered_set<size_t> zoneInstances_;  // def instances expanded inside zone bodies
    std::unordered_map<std::string, Type> env_;
    std::unordered_map<std::string, GeoSchema> schemas_;
    std::unordered_map<const Expr*, GeoSchema> callSchemas_;
    std::unordered_map<std::string, ExprClosure> closures_;
    std::vector<std::pair<std::string, Span>> outputs_;
    const BuiltinSig* lastSig_ = nullptr;  // sig of the last inferred call (multi-output check)
    bool allowMulti_ = false;              // destructuring position accepts multi-output calls
    std::vector<bool> zoneStack_;          // enclosing zones: true = repeat, false = foreach (§5.4)

    bool isBound(const std::string& name) const {
        return std::find(boundParams_.begin(), boundParams_.end(), name) != boundParams_.end();
    }

    void error(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
    }
    void warn(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), true});
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
                repeatZone(static_cast<const RepeatZone*>(s));
                break;
            case NodeKind::ForeachZone:
                foreachZone(static_cast<const ForeachZone*>(s));
                break;
            default:
                break;  // tap: diagnostics layer, a no-op while debug=off (§9.2)
        }
    }

    // --- zones (E7, spec §5.4) ---------------------------------------------------
    //
    // Body names are globally unique (validator E102 forbids shadowing), so
    // the body typechecks in the same flat env_/schemas_/closures_; the names
    // it introduces are erased on exit. State/item ports behave as value
    // channels: defined before the body (the input port), bound exactly once
    // in it (the output port), with the channel type preserved (drift = E204).

    void eraseBodyNames(const std::vector<std::string>& names) {
        for (const std::string& n : names) {
            env_.erase(n);
            schemas_.erase(n);
            closures_.erase(n);
        }
    }

    // Every name a statement introduces into the enclosing (zone-body) scope:
    // binding targets and nested-zone targets. Nested zone bodies clean their
    // own names up.
    static void collectStmtDefs(const Stmt* s, std::vector<std::string>& out) {
        if (!s) return;
        if (s->kind == NodeKind::Binding) {
            for (const std::string& n : static_cast<const Binding*>(s)->targets.names) out.push_back(n);
        } else if (s->kind == NodeKind::RepeatZone) {
            for (const std::string& n : static_cast<const RepeatZone*>(s)->targets.names) out.push_back(n);
        } else if (s->kind == NodeKind::ForeachZone) {
            out.push_back(static_cast<const ForeachZone*>(s)->target);
        }
    }

    // Counts bindings of each port name in the body (validator pins the max
    // at 1; 0 = unbound output port, E204).
    static std::unordered_map<std::string, int> portBindCounts(const std::vector<Stmt*>& body) {
        std::unordered_map<std::string, int> counts;
        for (const Stmt* s : body) {
            if (!s || s->kind != NodeKind::Binding) continue;
            for (const std::string& n : static_cast<const Binding*>(s)->targets.names) counts[n] += 1;
        }
        return counts;
    }

    void checkPortsBound(const std::vector<std::string>& ports, const std::vector<Span>& spans,
                         const std::unordered_map<std::string, int>& counts, const char* zoneWord) {
        for (size_t i = 0; i < ports.size(); ++i) {
            auto it = counts.find(ports[i]);
            if (it == counts.end() || it->second == 0) {
                error("E204", i < spans.size() ? spans[i] : Span{},
                      "state port '" + ports[i] + "' of the " + zoneWord +
                          " zone is never bound in the body",
                      "bind it exactly once to wire the output port of the iteration (§5.4)");
            }
        }
    }

    void repeatZone(const RepeatZone* z) {
        const size_t nState = z->state.names.size();
        const size_t nTargets = z->targets.names.size();
        if (nTargets != nState) {
            error("E202", z->span,
                  "repeat zone has " + std::to_string(nTargets) + " target(s) but " +
                      std::to_string(nState) + " state port(s)",
                  "destructure one target per state port (§5.4)");
        }
        // Header: value-expr types of the input ports. Multi-state needs a
        // multi-output call destructured across the ports.
        lastSig_ = nullptr;
        allowMulti_ = nState > 1;
        const Type valueT = infer(z->value);
        allowMulti_ = false;
        std::vector<Type> portTypes(nState);
        if (nState == 1) {
            portTypes[0] = valueT;
        } else if (!lastSig_ || lastSig_->results.size() != nState) {
            error("E202", z->span,
                  "repeat with " + std::to_string(nState) +
                      " state ports needs a multi-output value of the same arity",
                  "a, b = repeat(bbox(g), iterations = N) |a, b| { ... }");
        } else {
            for (size_t i = 0; i < nState; ++i) portTypes[i] = lastSig_->results[i];
        }
        const Type iterT = infer(z->iterations);
        if (iterT.base != ScalarType::None &&
            (iterT.isField || !canConvert(iterT, Type{ScalarType::Int, false, GeoKind::Any}))) {
            error(iterT.isField ? "E205" : "E204", z->iterations ? z->iterations->span : z->span,
                  "repeat iterations must be a value int, got " + typeName(iterT));
        }
        // Input-port schemas (per port).
        const GeoSchema& valueSchema = schemaOfExpr(z->value);
        // Body.
        zoneStack_.push_back(true);
        std::vector<std::string> bodyDefs;
        for (size_t i = 0; i < nState; ++i) {
            define(z->state.names[i], portTypes[i]);
            bodyDefs.push_back(z->state.names[i]);
            if (nState == 1) {
                schemas_[z->state.names[i]] = valueSchema;
                closures_[z->state.names[i]] = gather(z->value);
            }
        }
        for (const Stmt* s : z->body) {
            collectStmtDefs(s, bodyDefs);
            stmt(s);
        }
        checkPortsBound(z->state.names, z->state.spans, portBindCounts(z->body), "repeat");
        // Channel type preservation: the output port keeps the input's type.
        for (size_t i = 0; i < nState; ++i) {
            auto it = env_.find(z->state.names[i]);
            if (it == env_.end() || portTypes[i].base == ScalarType::None) continue;
            if (it->second.base != ScalarType::None && !canConvert(it->second, portTypes[i])) {
                error("E204", z->span,
                      "state port '" + z->state.names[i] + "' changes type across the iteration: " +
                          typeName(portTypes[i]) + " in, " + typeName(it->second) + " out",
                      "the state channel keeps one type — introduce a fresh name for other data");
                continue;
            }
            // Targets inherit the output port's type and steady-state schema.
            if (i < nTargets) {
                define(z->targets.names[i], portTypes[i]);
                if (auto sit = schemas_.find(z->state.names[i]); sit != schemas_.end())
                    schemas_[z->targets.names[i]] = sit->second;
                if (auto cit = closures_.find(z->state.names[i]); cit != closures_.end())
                    closures_[z->targets.names[i]] = cit->second;
            }
        }
        zoneBodyContracts(bodyDefs);
        zoneStack_.pop_back();
        eraseBodyNames(bodyDefs);
    }

    void foreachZone(const ForeachZone* z) {
        // Collection: geo<mesh> (connected pieces) or geo<points> (one piece
        // per point — the row model, §5.4 v1.20); instances -> E204.
        const Type collT = infer(z->collection);
        if (collT.base != ScalarType::None && collT.base != ScalarType::Geo) {
            error("E204", z->collection ? z->collection->span : z->span,
                  "foreach collection must be a geometry, got " + typeName(collT));
        } else if (collT.base == ScalarType::Geo && collT.geoKind == GeoKind::Instances) {
            error("E204", z->collection ? z->collection->span : z->span,
                  "foreach over geo<instances> is not defined",
                  "realize() first (pieces of the mesh) or iterate the anchor points instead");
        }
        const GeoKind itemKind = collT.base == ScalarType::Geo && collT.geoKind != GeoKind::Instances
                                     ? collT.geoKind
                                     : GeoKind::Any;
        const Type kItem{ScalarType::Geo, false, itemKind};
        zoneStack_.push_back(false);
        std::vector<std::string> bodyDefs{z->item};
        define(z->item, kItem);
        schemas_[z->item] = schemaOfExpr(z->collection);
        closures_[z->item] = gather(z->collection);
        for (const Stmt* s : z->body) {
            collectStmtDefs(s, bodyDefs);
            stmt(s);
        }
        checkPortsBound({z->item}, {z->itemSpan}, portBindCounts(z->body), "foreach");
        // The body may rebuild the piece as any mesh/points geometry (a point
        // row may become a mesh); the target takes the body's kind.
        Type outT = kItem;
        if (auto it = env_.find(z->item); it != env_.end() && it->second.base != ScalarType::None) {
            if (it->second.base != ScalarType::Geo || it->second.isField ||
                it->second.geoKind == GeoKind::Instances) {
                error("E204", z->span,
                      "item port '" + z->item + "' of the foreach zone must end as a geo<mesh> or geo<points>, got " +
                          typeName(it->second),
                      "the piece channel carries geometry — introduce a fresh name for other data; realize() "
                      "instances");
            } else {
                outT = it->second;
            }
        }
        define(z->target, outT);
        if (auto sit = schemas_.find(z->item); sit != schemas_.end()) schemas_[z->target] = sit->second;
        if (auto cit = closures_.find(z->item); cit != closures_.end()) closures_[z->target] = cit->second;
        zoneBodyContracts(bodyDefs);
        zoneStack_.pop_back();
        eraseBodyNames(bodyDefs);
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
        if (arg->value->kind == NodeKind::NoneLit) {
            out = "none";  // enum value spelled like the none literal (typeinfo = none)
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
                // v1.14: meshes derive @N from their faces when no column is
                // stored; only face-less kinds (points/instances) lack @N.
                if (!s.hasN && !s.hasAttr("N") && s.kind != GeoKind::Mesh) {
                    error("E302", span, "attribute @N is not present on this geometry (static schema)",
                          "compute normals upstream (compute_normals)");
                } else if (s.nStale) {
                    warn("W006", span,
                         "@N is stale: the points were moved (set_position/smooth) after the normals were "
                         "stored and compute_normals did not follow",
                         "insert compute_normals(g) after the displacement, or drop the stale column with "
                         "remove_attr(g, \"N\") to read normals derived from the faces");
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

    // Static mirror of the runtime E609 check for one binary merge step: a
    // name present in both operands must sit on the same domain set with the
    // same typeinfo (detail-value equality is only decidable at runtime).
    // Returns the merged schema (open if either side is open).
    GeoSchema mergeSchemas(const GeoSchema& a, const GeoSchema& bb, const Span& span) {
        if (a.open || bb.open) return openSchema();
        auto maskOf = [](const auto& perDomain, const std::string& n) -> unsigned {
            unsigned m = 0;
            for (size_t d = 0; d < 4; ++d)
                if (perDomain[d].count(n)) m |= 1u << d;
            return m;
        };
        for (size_t d = 0; d < 4; ++d) {
            for (const auto& [n, t] : a.attrs[d]) {
                const unsigned ma = maskOf(a.attrs, n), mb = maskOf(bb.attrs, n);
                if (ma && mb && ma != mb) {
                    error("E609", span,
                          "merge: attribute '@" + n +
                              "' sits on different domains in the operands (static schema)",
                          "promote it to the same domain on all sides, or remove_attr/rename_attr one side");
                } else if (ma && mb) {
                    const auto itB = bb.attrs[d].find(n);
                    if (itB != bb.attrs[d].end() && itB->second.info != t.info)
                        error("E609", span,
                              "merge: attribute '@" + n + "' has typeinfo " +
                                  std::string(attrTypeInfoName(t.info)) + " on the left and " +
                                  std::string(attrTypeInfoName(itB->second.info)) + " on the right (static schema)",
                              "write both sides with the same set(..., typeinfo = ...)");
                }
            }
            for (const std::string& n : a.groups[d]) {
                const unsigned ma = maskOf(a.groups, n), mb = maskOf(bb.groups, n);
                if (ma && mb && ma != mb)
                    error("E609", span,
                          "merge: group '@" + n + "' sits on different domains in the operands (static schema)",
                          "mark it on the same domain on all sides, or unmark one side");
            }
        }
        GeoSchema s = a;
        s.kind = a.kind == bb.kind ? a.kind : GeoKind::Any;
        s.hasN = a.hasN || bb.hasN;
        s.nStale = a.nStale || bb.nStale;
        for (size_t d = 0; d < 4; ++d) {
            for (const auto& [n, t] : bb.attrs[d]) s.attrs[d].emplace(n, t);
            for (const std::string& n : bb.groups[d]) s.groups[d].insert(n);
        }
        s.instanceSource = nullptr;
        return s;
    }

    void computeCallSchema(const BuiltinSig& sig, const Call& c,
                           const std::vector<const CallArg*>& byParam, const std::vector<Type>& argTypes,
                           const std::vector<const CallArg*>& variadicArgs = {}) {
        std::string lit, lit2, lit3;
        switch (sig.id) {
            case BuiltinId::IcoSphere:
            case BuiltinId::Box:
                storeSchema(&c, sourceSchema(GeoKind::Mesh, true));
                break;
            case BuiltinId::Grid: {
                GeoSchema s = sourceSchema(GeoKind::Mesh, true);
                s.attrs[domainIndex("points")]["uv"] = ScalarType::Vec2;  // v1.23
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::MeshLine:
            case BuiltinId::PointCloud:
                storeSchema(&c, sourceSchema(GeoKind::Points, false));
                break;
            case BuiltinId::Transform: {
                GeoSchema s = argSchema(byParam, 0);
                if (s.kind == GeoKind::Instances && byParam.size() > 3 && byParam[3] && byParam[3]->value) {
                    // Static mirror of E611: a constant non-uniform scale on
                    // geo<instances> is rejected before the run.
                    Value sc;
                    if (constEval(byParam[3]->value, sc) && valueBase(sc) == ScalarType::Vec3) {
                        const glm::vec3 v = asVec3(sc);
                        if (glm::abs(v.x - v.y) > 1e-6f || glm::abs(v.x - v.z) > 1e-6f)
                            error("E611", c.span,
                                  "transform: non-uniform scale on geo<instances> cannot be stamped into @scale",
                                  "use a uniform scale, scale the source geometry, or realize() first");
                    }
                }
                if (!s.open && s.kind == GeoKind::Instances) {
                    // transform on instances composes the stamps (§19 v1.13):
                    // @orient / @scale are materialized even when absent.
                    s.attrs[domainIndex("points")]["orient"] = SchemaAttrType(ScalarType::Vec4, AttrTypeInfo::Quaternion);
                    s.attrs[domainIndex("points")]["scale"] = ScalarType::F32;
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Smooth: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open && s.hasN) s.nStale = true;  // stored normals no longer match the surface
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::SetPosition: {
                GeoSchema s = argSchema(byParam, 0);
                checkFieldArgs(s, byParam, {1, 2, 3});  // offset, pos, where
                if (!s.open && s.hasN) s.nStale = true;  // stored normals no longer match the surface
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::ComputeNormals: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) && (lit == "flat" || lit == "auto")) {
                        // flat / auto modes write per-corner normals as a corner
                        // attribute and leave @N (points) untouched (§19 v0.8, v1.24).
                        s.attrs[domainIndex("corners")]["N"] = SchemaAttrType(ScalarType::Vec3, AttrTypeInfo::Normal);
                    } else {
                        s.hasN = true;
                        s.nStale = false;
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
                // Static mirror of the runtime E610 contract (typeinfo v1.14):
                // decidable whenever the name is a literal and the value type
                // is known — independent of the schema being open.
                {
                    const Type vt = byParam.size() > 2 && byParam[2] ? argTypes[2] : Type{};
                    const ScalarType st = vt.base;
                    std::string nm, ti;
                    const bool nameLit = literalString(byParam.size() > 1 ? byParam[1] : nullptr, nm);
                    const bool tiGiven = byParam.size() > 4 && byParam[4];
                    const bool tiLit = tiGiven && literalString(byParam[4], ti);
                    if (nameLit && st != ScalarType::None && st != ScalarType::Any) {
                        if (const char* need = reservedAttrTypeName(nm)) {
                            const bool fits = (nm == "orient" && st == ScalarType::Vec4) ||
                                              (nm == "tint" && st == ScalarType::Vec3) ||
                                              (nm == "scale" && (st == ScalarType::F32 || st == ScalarType::Int)) ||
                                              (nm == "variant" && st == ScalarType::Int);
                            if (!fits)
                                error("E610", c.span,
                                      "attribute '@" + nm + "' is a reserved instance stamp and must be " +
                                          std::string(need) + " (got " + typeName(vt) + ")",
                                      "orient: vec4 quaternion (orient_from_euler), tint: vec3, scale: f32, "
                                      "variant: int");
                        }
                        const bool isVec = st == ScalarType::Vec3 || st == ScalarType::Vec4;
                        if ((!tiGiven || (tiLit && ti == "auto")) && isVec && !reservedAttrTypeName(nm) &&
                            nm != "color" && nm != "Cd" && nm != "N") {
                            error("E610", c.span,
                                  "attribute '@" + nm + "' is a " + typeName(vt) +
                                      ": its typeinfo must be explicit",
                                  "set(..., typeinfo = vector | normal | point | quaternion | none) — "
                                  "vector/normal/point/quaternion ride with transform/realize, none is plain "
                                  "data (colors, sizes)");
                        }
                        if (tiLit && ti != "auto") {
                            const AttrTypeInfo info = attrTypeInfoFromName(ti).value_or(AttrTypeInfo::None);
                            const bool fits = info == AttrTypeInfo::None ||
                                              (info == AttrTypeInfo::Quaternion ? st == ScalarType::Vec4
                                                                                 : st == ScalarType::Vec3);
                            if (!fits)
                                error("E610", c.span,
                                      "typeinfo " + ti + " does not fit attribute '@" + nm + "' of type " +
                                          typeName(vt),
                                      info == AttrTypeInfo::Quaternion ? "quaternion needs a vec4 value"
                                                                        : "vector/normal/point need a vec3 value");
                            if (nm == "orient" && info != AttrTypeInfo::Quaternion)
                                error("E610", c.span, "attribute '@orient' must have typeinfo quaternion",
                                      "drop the typeinfo argument (auto infers quaternion for orient)");
                        }
                    }
                }
                if (!s.open) {
                    if (literalString(byParam.size() > 1 ? byParam[1] : nullptr, lit) &&
                        lit != "P" && lit != "N" && lit != "index") {
                        // The explicit domain argument wins (§8.7 v1.12);
                        // otherwise inference mirrors the runtime rule:
                        // constant field -> detail, otherwise points (§19
                        // v0.9). The name is global: other domains drop the
                        // column.
                        const Type vt = byParam.size() > 2 && byParam[2] ? argTypes[2] : Type{};
                        size_t dom = vt.isField ? domainIndex("points") : domainIndex("detail");
                        bool domOpen = false;
                        if (byParam.size() > 3 && byParam[3]) {
                            if (literalString(byParam[3], lit3)) {
                                if (lit3 != "auto") dom = domainIndex(lit3);
                            } else {
                                domOpen = true;  // non-literal domain argument
                            }
                        }
                        if (domOpen) {
                            s = openSchema();
                        } else {
                            for (size_t d2 = 0; d2 < s.attrs.size(); ++d2)
                                if (d2 != dom) s.attrs[d2].erase(lit);
                            const ScalarType st = vt.base == ScalarType::None ? ScalarType::F32 : vt.base;
                            // typeinfo (v1.14): literal tag, or the runtime
                            // inference mirrored (reserved names, vec3 -> vector).
                            AttrTypeInfo info = AttrTypeInfo::None;
                            bool infoOpen = false;
                            std::string lit4;
                            if (byParam.size() > 4 && byParam[4]) {
                                if (literalString(byParam[4], lit4)) {
                                    if (lit4 != "auto") info = attrTypeInfoFromName(lit4).value_or(AttrTypeInfo::None);
                                    else infoOpen = true;  // handled below
                                } else {
                                    domOpen = true;  // non-literal typeinfo: schema opens
                                }
                            } else {
                                infoOpen = true;
                            }
                            if (infoOpen) {
                                ColumnData probe = st == ScalarType::Vec3
                                                       ? ColumnData(std::shared_ptr<const std::vector<glm::vec3>>())
                                                   : st == ScalarType::Vec4
                                                       ? ColumnData(std::shared_ptr<const std::vector<glm::vec4>>())
                                                       : ColumnData(std::shared_ptr<const std::vector<float>>());
                                // nullopt = ambiguous vec3/vec4 — E610 above already reported.
                                info = inferAttrTypeInfo(lit, probe).value_or(AttrTypeInfo::None);
                            }
                            if (domOpen) s = openSchema();
                            else s.attrs[dom][lit] = SchemaAttrType(st, info);
                        }
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
                        if (lit == "N") { s.hasN = false; s.nStale = false; }  // dedicated column dropped too
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
                // Left fold over all operands (variadic tail included).
                GeoSchema s = mergeSchemas(argSchema(byParam, 0), argSchema(byParam, 1), c.span);
                for (const CallArg* arg : variadicArgs) {
                    if (s.open) break;
                    s = mergeSchemas(s, arg ? schemaOfExpr(arg->value) : openSchema(), c.span);
                }
                if (!s.open) storeSchema(&c, std::move(s));
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
            case BuiltinId::Islands: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) s.attrs[domainIndex("faces")]["island_id"] = ScalarType::Int;
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Clip: {
                // Half-space clip keeps the schema (columns interpolate, never
                // drop) and may add the cap group on faces. Static mirrors of the
                // runtime checks: instances -> E204, constant zero normal -> E612.
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open && s.kind == GeoKind::Instances)
                    error("E204", c.span, "clip on geo<instances> is not defined", "realize() first, then clip");
                // Point @N on the cut edge is interpolated, not recomputed (W006 on read).
                if (!s.open && s.hasN) s.nStale = true;
                if (byParam.size() > 2 && byParam[2] && byParam[2]->value) {
                    Value nv;
                    if (constEval(byParam[2]->value, nv) && valueBase(nv) == ScalarType::Vec3) {
                        const glm::vec3 v = asVec3(nv);
                        if (!(glm::length(v) > 1e-12f) || !std::isfinite(glm::length(v)))
                            error("E612", c.span, "clip: normal must be a non-zero finite vector",
                                  "pass the plane normal, e.g. normal = (1, 0, 0)");
                    }
                }
                if (!s.open && byParam.size() > 3 && byParam[3]) {
                    if (literalString(byParam[3], lit)) {
                        if (!lit.empty()) s.groups[domainIndex("faces")].insert(lit);
                    } else {
                        s = openSchema();
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Extrude:
            case BuiltinId::Inset: {
                // Face rebuilds keep the schema (rows copy/blend); named side/
                // top (inner/rim) groups are added on faces. Field args live on
                // the faces domain of the input.
                const bool ex = sig.id == BuiltinId::Extrude;
                GeoSchema s = argSchema(byParam, 0);
                if (ex) checkFieldArgs(s, byParam, {1, 2});
                else checkFieldArgs(s, byParam, {1, 2, 3});
                // Kind: the signature's geoArg(GeoKind::Mesh) already reports E204.
                if (!s.open) {
                    for (size_t gi : {size_t(4), size_t(5)}) {  // side/top or inner/rim group names
                        if (byParam.size() <= gi || !byParam[gi]) continue;
                        if (literalString(byParam[gi], lit)) {
                            if (!lit.empty()) s.groups[domainIndex("faces")].insert(lit);
                        } else {
                            s = openSchema();
                            break;
                        }
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Bevel: {
                // Rows copy/blend; @N is dropped (surface changed); bevel_group on faces.
                GeoSchema s = argSchema(byParam, 0);
                checkFieldArgs(s, byParam, {1, 2});
                if (!s.open) {
                    s.hasN = false;
                    s.nStale = false;
                    s.attrs[domainIndex("corners")].erase("N");
                    if (byParam.size() > 3 && byParam[3]) {
                        if (literalString(byParam[3], lit)) {
                            if (!lit.empty()) s.groups[domainIndex("faces")].insert(lit);
                        } else {
                            s = openSchema();
                        }
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Subdivide: {
                GeoSchema s = argSchema(byParam, 0);
                // catmull_clark / loop move the original points: a stored @N is stale.
                if (!s.open && s.hasN && !(byParam.size() > 2 && byParam[2] && literalString(byParam[2], lit) && lit == "linear"))
                    s.nStale = true;
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Triangulate:
            case BuiltinId::MergeByDistance:
            case BuiltinId::Mirror: {
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open && s.kind == GeoKind::Instances)
                    error("E204", c.span, std::string(sig.name) + " on geo<instances> is not defined", "realize() first");
                if (sig.id == BuiltinId::Mirror && byParam.size() > 2 && byParam[2] && byParam[2]->value) {
                    Value nv;
                    if (constEval(byParam[2]->value, nv) && valueBase(nv) == ScalarType::Vec3) {
                        const glm::vec3 v = asVec3(nv);
                        if (!(glm::length(v) > 1e-12f) || !std::isfinite(glm::length(v)))
                            error("E612", c.span, "mirror: normal must be a non-zero finite vector",
                                  "pass the plane normal, e.g. normal = (1, 0, 0)");
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Separate: {
                const GeoSchema& in = argSchema(byParam, 0);
                checkFieldArgs(in, byParam, {1});
                if (!in.open && in.kind != GeoKind::Mesh && byParam.size() > 2 && byParam[2] &&
                    literalString(byParam[2], lit) && lit != "points") {
                    error("E204", c.span,
                          "separate on " + lit + " needs a geo<mesh>; " + std::string(geoKindName(in.kind)) +
                              " has only points",
                          "use domain = points");
                }
                storeSchema(&c, in);  // both halves keep the input schema
                break;
            }
            case BuiltinId::Circle:
                storeSchema(&c, sourceSchema(GeoKind::Points, false));
                break;
            case BuiltinId::BakeAo: {
                // Adds the f32 column on points or corners (auto: corners when a
                // corner N exists, else points); non-literal name/domain opens.
                GeoSchema s = argSchema(byParam, 0);
                if (!s.open) {
                    std::string dom = "auto", nm = "ao";
                    const bool domLit = byParam.size() <= 4 || !byParam[4] || literalString(byParam[4], dom);
                    const bool nmLit = byParam.size() <= 5 || !byParam[5] || literalString(byParam[5], nm);
                    if (!domLit || !nmLit) {
                        s = openSchema();
                    } else {
                        if (dom == "auto") dom = s.attrs[domainIndex("corners")].count("N") ? "corners" : "points";
                        s.attrs[domainIndex(dom)][nm] = ScalarType::F32;
                    }
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::BezierPoints: {
                GeoSchema s = sourceSchema(GeoKind::Points, false);
                s.attrs[domainIndex("points")]["t"] = ScalarType::F32;
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::ResamplePoints: {
                // Point columns/groups of the path lerp along it; @t is added.
                const GeoSchema& path = argSchema(byParam, 0);
                GeoSchema s = sourceSchema(GeoKind::Points, false);
                if (path.open) {
                    s = openSchema();
                } else {
                    if (path.kind == GeoKind::Instances)
                        error("E204", c.span, "resample_points: path must be a point set", "realize() first or pass anchor points");
                    s.attrs[domainIndex("points")] = path.attrs[domainIndex("points")];
                    s.attrs[domainIndex("detail")] = path.attrs[domainIndex("detail")];
                    s.groups[domainIndex("points")] = path.groups[domainIndex("points")];
                    s.groups[domainIndex("detail")] = path.groups[domainIndex("detail")];
                    s.attrs[domainIndex("points")]["t"] = ScalarType::F32;
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Sweep: {
                // Ring points inherit the path's point columns/groups; no @N.
                const GeoSchema& path = argSchema(byParam, 0);
                GeoSchema s = sourceSchema(GeoKind::Mesh, false);
                if (path.open) {
                    s = openSchema();
                } else {
                    if (path.kind == GeoKind::Instances)
                        error("E204", c.span, "sweep: path must be a point set", "realize() first or pass anchor points");
                    s.attrs[domainIndex("points")] = path.attrs[domainIndex("points")];
                    s.attrs[domainIndex("detail")] = path.attrs[domainIndex("detail")];
                    s.groups[domainIndex("points")] = path.groups[domainIndex("points")];
                    s.groups[domainIndex("detail")] = path.groups[domainIndex("detail")];
                    // @scale/@twist are consumed; the sheet gets @uv (v1.23).
                    auto& pts = s.attrs[domainIndex("points")];
                    pts.erase("scale");  // f32 by the E610 stamp contract
                    if (auto it = pts.find("profile_scale"); it != pts.end()) {
                        if (it->second.type != ScalarType::Vec2)
                            error("E204", c.span, "sweep: @profile_scale on the path must be vec2",
                                  "x scales the profile X (width), y its Y (depth)");
                        pts.erase(it);
                    }
                    if (auto it = pts.find("twist"); it != pts.end()) {
                        if (it->second.type != ScalarType::F32)
                            error("E204", c.span, "sweep: @twist on the path must be f32 (degrees)");
                        pts.erase(it);
                    }
                    pts["uv"] = ScalarType::Vec2;
                }
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Delete: {
                // Element removal keeps the schema (columns are gathered, never
                // dropped); the mask is checked against the input schema.
                const GeoSchema& in = argSchema(byParam, 0);
                checkFieldArgs(in, byParam, {1});  // where
                if (!in.open && in.kind != GeoKind::Mesh && byParam.size() > 2 && byParam[2] &&
                    literalString(byParam[2], lit) && lit != "points") {
                    error("E204", c.span,
                          "delete on " + lit + " needs a geo<mesh>; " + std::string(geoKindName(in.kind)) +
                              " has only points",
                          "use domain = points");
                }
                storeSchema(&c, in);
                break;
            }
            case BuiltinId::Fracture: {
                // SDF extraction barrier: the pieces carry @P + @island_id only.
                GeoSchema s = sourceSchema(GeoKind::Mesh, false);
                s.attrs[domainIndex("faces")]["island_id"] = ScalarType::Int;
                storeSchema(&c, std::move(s));
                break;
            }
            case BuiltinId::Count:
                checkFieldArgs(argSchema(byParam, 0), byParam, {2});  // where
                break;
            case BuiltinId::MinOf:
            case BuiltinId::MaxOf:
            case BuiltinId::AvgOf:
            case BuiltinId::SumOf:
            case BuiltinId::ValueOf:
                checkFieldArgs(argSchema(byParam, 1), byParam, {0, 2});  // field, where (on `on`)
                break;
            default:
                break;
        }
    }

    // --- contracts (static half; the rest goes to the engine) ---------------------

    void contracts() {
        for (size_t i = 0; i < flat_.contracts.size(); ++i)
            if (!zoneInstances_.count(flat_.contracts[i].instance)) contract(i);
    }

    // Contracts of def instances expanded into a zone body are checked while
    // the body names are still in scope (called by the zone walkers before
    // eraseBodyNames); the instances are then skipped by contracts().
    void zoneBodyContracts(const std::vector<std::string>& bodyDefs) {
        std::unordered_set<size_t> insts;
        for (const std::string& n : bodyDefs)
            if (auto it = flat_.instanceOfBinding.find(n); it != flat_.instanceOfBinding.end())
                insts.insert(it->second);
        if (insts.empty()) return;
        for (size_t i = 0; i < flat_.contracts.size(); ++i)
            if (insts.count(flat_.contracts[i].instance) && !zoneInstances_.count(flat_.contracts[i].instance))
                contract(i);
        zoneInstances_.insert(insts.begin(), insts.end());
    }

    void contract(size_t i) {
        {
            const FlatContract& c = flat_.contracts[i];
            if (c.attrForm) {
                if (env_.find(c.ident) == env_.end()) {
                    error("E103", c.span, "'" + c.ident + "' is not defined");
                    return;
                }
                auto sit = schemas_.find(c.ident);
                if (sit == schemas_.end() || sit->second.open) {
                    runtimeContracts_.push_back(i);  // open schema: runtime column check
                    return;
                }
                const GeoSchema& gs = sit->second;
                const bool present = c.attrName == "P" || c.attrName == "index" ||
                                     (c.attrName == "N" ? gs.hasN || gs.hasAttr("N") || gs.kind == GeoKind::Mesh
                                                        : gs.hasAttr(c.attrName));
                if (!present) contractError(c);
                return;
            }
            const Type t = infer(c.cond);
            if (t.base == ScalarType::None) return;  // already reported
            if (t.isField || t.base != ScalarType::Bool) {
                error("E204", c.span, "expect/ensure condition must be a value bool, got " + typeName(t));
                return;
            }
            Value v;
            if (constEval(c.cond, v)) {
                if (valueBase(v) == ScalarType::Bool && !asBool(v)) contractError(c);
                return;  // constant-true: satisfied statically
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
            case NodeKind::AttrRef: {
                const std::string& n = static_cast<const AttrRef*>(e)->name;
                // @iteration/@piece_index are zone-provided int values (§6.3),
                // not attribute reads — the static schema check skips them.
                if (n == "iteration" || n == "piece_index") break;
                out.attrs.push_back({n, e->span});
                break;
            }
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
                // Aggregators (§8.7: value/min_of/max_of/avg_of/sum_of) are VALUES
                // in the enclosing field: their @attr/ingroup reads bind to the
                // `on` geometry (checked by the aggregator's own schema check),
                // not to the geometry this field is consumed on.
                if (c->path.size() == 1 && (c->path[0] == "value" || c->path[0] == "min_of" ||
                                            c->path[0] == "max_of" || c->path[0] == "avg_of" ||
                                            c->path[0] == "sum_of"))
                    break;
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
            // Zone constants are VALUES (int), the only @-names with value
            // semantics (§6.3) — valid when an enclosing zone provides them.
            const bool needRepeat = ref->name == "iteration";
            for (auto it = zoneStack_.rbegin(); it != zoneStack_.rend(); ++it)
                if (*it == needRepeat) return scalar(ScalarType::Int);
            error("E302", ref->span,
                  "'@" + ref->name + "' is only valid inside a " +
                      (needRepeat ? std::string("repeat") : std::string("foreach")) + " zone body",
                  "the zone provides it as an int constant (§5.4)");
            return scalar(ScalarType::Int);
        }
        // Named attributes: reserved / conventional names have a known type,
        // user names are Any (unknown until the runtime sees the column —
        // expression typing is permissive for Any, the runtime re-checks with
        // the actual types); a missing attribute is a runtime E302; closed
        // schemas are checked statically at the consuming node (§7.6).
        return field(knownAttrType(ref->name));
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
            if (sig->id == BuiltinId::Merge) {
                if (t.base != ScalarType::None && (t.isField || t.base != ScalarType::Geo))
                    error("E204", arg->value->span, "merge operands must be geometries, got " + typeName(t));
            } else if (t.isField) {
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
        computeCallSchema(*sig, *c, byParam, argTypes, variadicArgs);
        Type rt = sig->result;
        // value(field, on): the result has the field's own type (§8.10). A bare
        // attribute read is provisionally f32 in inferAttr; when `on` has a
        // closed schema the column's real type is known and wins.
        if (sig->id == BuiltinId::ValueOf && np > 0 && argTypes[0].base != ScalarType::None) {
            rt.base = argTypes[0].base;
            if (byParam[0] && byParam[0]->value && byParam[0]->value->kind == NodeKind::AttrRef) {
                const std::string& an = static_cast<const AttrRef*>(byParam[0]->value)->name;
                const GeoSchema& on = argSchema(byParam, 1);
                if (!on.open)
                    for (size_t d = 0; d < 4; ++d)
                        if (auto it = on.attrs[d].find(an); it != on.attrs[d].end()) {
                            rt.base = it->second.type;
                            break;
                        }
            }
        }
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
