#include "../../pch.h"

#include "expand.h"

#include <unordered_set>

#include "builtins.h"

namespace pgg {
namespace {

// Def-resolution context of one source file (the main file or a module).
struct FileScope {
    std::unordered_map<std::string, const Def*> localDefs;
    std::unordered_map<std::string, const ModuleInfo*> namespaces;
};

using RenameMap = std::unordered_map<std::string, std::string>;

std::string qualified(const std::vector<std::string>& path) {
    std::string q;
    for (const std::string& p : path) q += (q.empty() ? "" : ".") + p;
    return q;
}

// Dotted string of a def-body tap path with the root renamed through the
// instance map (`raw` -> `make_rock[1].raw`); the remaining path elements
// (instance indices, attr terminals) pass through unchanged. A root not in
// the map (not a param/local — e.g. a def name) is kept as written.
std::string renamedTapPath(const std::vector<PathElem>& path, const RenameMap& renames) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        const PathElem& el = path[i];
        if (el.isIndex) {
            out += "[" + std::to_string(el.index) + "]";
            continue;
        }
        if (!out.empty()) out += ".";
        if (i == 0) {
            if (auto it = renames.find(el.name); it != renames.end()) {
                out += it->second;
                continue;
            }
        }
        out += el.name;
    }
    return out;
}

// Collects the defs a def body directly refers to (its own resolution scope).
struct CalleeWalk {
    const FileScope* scope = nullptr;
    std::vector<const Def*> defs;
    bool stochastic = false;  // directly calls a stochastic built-in

    void stmt(const Stmt* s) {
        if (!s) return;
        if (s->kind == NodeKind::Binding) {
            expr(static_cast<const Binding*>(s)->value);
        } else if (s->kind == NodeKind::RepeatZone) {
            const auto* z = static_cast<const RepeatZone*>(s);
            expr(z->value);
            expr(z->iterations);
            for (const Stmt* b : z->body) stmt(b);
        } else if (s->kind == NodeKind::ForeachZone) {
            const auto* z = static_cast<const ForeachZone*>(s);
            expr(z->collection);
            for (const Stmt* b : z->body) stmt(b);
        }
        // tap carries no calls
    }

    void contract(const ContractStmt* c) {
        if (c && !c->attrForm) expr(c->cond);
    }

    void expr(const Expr* e) {
        if (!e) return;
        switch (e->kind) {
            case NodeKind::Paren: expr(static_cast<const Paren*>(e)->inner); break;
            case NodeKind::Unary: expr(static_cast<const Unary*>(e)->operand); break;
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                expr(b->lhs);
                expr(b->rhs);
                break;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                expr(t->cond);
                expr(t->thenExpr);
                expr(t->elseExpr);
                break;
            }
            case NodeKind::VecLit:
                for (const Expr* el : static_cast<const VecLit*>(e)->elems) expr(el);
                break;
            case NodeKind::ListLit:
                for (const Expr* el : static_cast<const ListLit*>(e)->elems) expr(el);
                break;
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                for (const CallArg& a : c->args) expr(a.value);
                if (c->path.size() == 1) {
                    if (auto it = scope->localDefs.find(c->path[0]); it != scope->localDefs.end()) {
                        defs.push_back(it->second);
                    } else if (const BuiltinSig* sig = findBuiltin(c->path[0])) {
                        switch (sig->id) {
                            case BuiltinId::Fbm:
                            case BuiltinId::Vnoise:
                            case BuiltinId::Random:
                            case BuiltinId::RandomVec:
                            case BuiltinId::RandomInt:
                            case BuiltinId::PointCloud:
                            case BuiltinId::DistributePoints:
                                stochastic = true;
                                break;
                            default:
                                break;
                        }
                    }
                } else if (c->path.size() == 2) {
                    if (auto ns = scope->namespaces.find(c->path[0]); ns != scope->namespaces.end()) {
                        if (auto d = ns->second->defs.find(c->path[1]); d != ns->second->defs.end())
                            defs.push_back(d->second);
                    }
                }
                break;
            }
            default:
                break;  // idents, attr refs, literals
        }
    }
};

class Expander {
public:
    Expander(const ModuleClosure* closure, std::vector<Diagnostic>& diags, FlatProgram& out)
        : closure_(closure), diags_(diags), out_(out) {}

    void run(const File& mainFile) {
        // Def table: main-file defs resolve in the main scope, module defs in
        // their module's scope (its own imports included).
        for (const Node* item : mainFile.items) {
            if (item->kind != NodeKind::Def) {
                continue;
            }
            const auto* d = static_cast<const Def*>(item);
            if (findBuiltin(d->name)) {
                error("E102", d->span, "def '" + d->name + "' collides with a built-in operation",
                      "pick a fresh name (spec §7.6)");
                continue;
            }
            mainScope_.localDefs[d->name] = d;
        }
        if (closure_) mainScope_.namespaces = closure_->mainNamespaces;
        for (const Node* item : mainFile.items) {
            if (item->kind != NodeKind::Def) continue;
            const auto* d = static_cast<const Def*>(item);
            if (mainScope_.localDefs.count(d->name)) {
                defScope_[d] = &mainScope_;
                allDefs_.push_back(d);  // source order: deterministic diagnostics
            }
        }
        if (closure_) {
            for (const ModuleInfo* m : closure_->modules) {
                moduleScopes_[m].localDefs = m->defs;
                moduleScopes_[m].namespaces = m->namespaces;
                for (const Node* item : m->doc->file->items) {
                    if (item->kind != NodeKind::Def) continue;
                    const auto* d = static_cast<const Def*>(item);
                    defScope_[d] = &moduleScopes_[m];
                    allDefs_.push_back(d);
                }
            }
        }

        const size_t diagsBefore = diags_.size();
        checkDefGraph();
        // A cyclic def graph cannot be expanded (the checks reported E503);
        // stop here — the run aborts on those errors.
        bool graphErrors = false;
        for (size_t i = diagsBefore; i < diags_.size(); ++i) graphErrors = graphErrors || !diags_[i].isWarning;
        if (graphErrors) {
            out_.arena.push_back(std::make_unique<File>(mainFile.span));
            out_.file = static_cast<File*>(out_.arena.back().get());
            out_.expanded = true;
            return;
        }

        // Expansion: main-file items in source order.
        std::vector<Node*> items;
        static const RenameMap kNoRename;
        for (const Node* item : mainFile.items) {
            switch (item->kind) {
                case NodeKind::Import:
                case NodeKind::Def:
                    break;  // resolved at expansion; instances are inlined on demand
                case NodeKind::Binding:
                    processBinding(static_cast<const Binding*>(item), kNoRename, mainScope_, "", items,
                                   kNoInstance);
                    break;
                case NodeKind::RepeatZone:
                case NodeKind::ForeachZone:
                    // Zone bodies are expanded in place (v1.20): def calls in
                    // the body become flat instance bindings *inside* the body
                    // (one static instance per call site, executed per
                    // iteration/piece), header def calls lift into the outer
                    // sink.
                    items.push_back(expandZone(static_cast<const Stmt*>(item), kNoRename, mainScope_, "", items,
                                               kNoInstance, ""));
                    break;
                default:
                    items.push_back(const_cast<Node*>(item));  // param/output/tap: shared as-is
                    break;
            }
        }
        out_.arena.push_back(std::make_unique<File>(mainFile.span));
        File* flat = static_cast<File*>(out_.arena.back().get());
        flat->items = std::move(items);
        out_.file = flat;
        out_.expanded = true;
    }

private:
    static constexpr size_t kNoInstance = ~size_t(0);

    const ModuleClosure* closure_;
    std::vector<Diagnostic>& diags_;
    FlatProgram& out_;
    FileScope mainScope_;
    std::unordered_map<const ModuleInfo*, FileScope> moduleScopes_;
    std::unordered_map<const Def*, FileScope*> defScope_;
    std::vector<const Def*> allDefs_;  // source order across main file + modules
    std::unordered_map<const Def*, size_t> instanceCounters_;
    size_t liftCounter_ = 0;

    void error(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
    }

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        out_.arena.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T*>(out_.arena.back().get());
    }

    Expr* ident(Span span, const std::string& name) {
        Ident* id = make<Ident>(span);
        id->name = name;
        return id;
    }

    Binding* assign(Span span, const std::string& target, Expr* value) {
        Binding* b = make<Binding>(span);
        b->targets.names.push_back(target);
        b->targets.spans.push_back(span);
        b->value = value;
        return b;
    }

    // --- def-graph checks (E503, E401) ----------------------------------------

    void checkDefGraph() {
        // One callee/stochasticity walk per def, shared by both checks.
        std::unordered_map<const Def*, CalleeWalk> walks;
        for (const Def* def : allDefs_) {
            CalleeWalk& w = walks[def];
            w.scope = defScope_[def];
            for (const ContractStmt* c : def->expects) w.contract(c);
            for (const Stmt* s : def->body) w.stmt(s);
            for (const ContractStmt* c : def->ensures) w.contract(c);
        }

        // E503: def recursion is forbidden (it is what makes full static
        // expansion possible, §7.6) — DFS for a cycle with its chain.
        std::unordered_map<const Def*, int> color;  // 0=white 1=gray 2=black
        std::vector<const Def*> stack;
        std::unordered_set<const Def*> cycleReported;
        auto dfs = [&](auto&& self, const Def* d) -> void {
            color[d] = 1;
            stack.push_back(d);
            for (const Def* callee : walks[d].defs) {
                if (color[callee] == 1 && cycleReported.insert(callee).second) {
                    std::string chain;
                    auto start = std::find(stack.begin(), stack.end(), callee);
                    for (auto it = start; it != stack.end(); ++it)
                        chain += (chain.empty() ? "" : " -> ") + (*it)->name;
                    chain += " -> " + callee->name;
                    error("E503", callee->span, "def recursion: " + chain,
                          "def calls must form a DAG (spec §7.6)");
                } else if (color[callee] == 0) {
                    self(self, callee);
                }
            }
            stack.pop_back();
            color[d] = 2;
        };
        for (const Def* d : allDefs_)
            if (color[d] == 0) dfs(dfs, d);

        // E401: stochasticity is transitive over the def DAG (§7.3); a
        // stochastic def must take an rng through its signature.
        std::unordered_map<const Def*, bool> memo;
        auto stochastic = [&](auto&& self, const Def* d) -> bool {
            if (auto it = memo.find(d); it != memo.end()) return it->second;
            memo[d] = false;  // cycles were rejected above; guards re-entry
            bool s = walks[d].stochastic;
            for (const Def* callee : walks[d].defs) s = self(self, callee) || s;
            memo[d] = s;
            return s;
        };
        for (const Def* def : allDefs_) {
            if (!stochastic(stochastic, def)) continue;
            bool hasRng = false;
            for (const DefParam& p : def->params)
                if (p.type && p.type->base == "rng") hasRng = true;
            if (!hasRng) {
                error("E401", def->span,
                      "def '" + def->name + "' is stochastic but has no rng parameter",
                      "add `rng: rng` to the signature and wire it to the stochastic nodes (§7.3)");
            }
        }
    }

    // --- call resolution --------------------------------------------------------

    enum class Resolution { NotDef, Def, Error };

    Resolution resolve(const Call& c, const FileScope& scope, const Def*& outDef) {
        outDef = nullptr;
        if (c.path.size() == 1) {
            if (auto it = scope.localDefs.find(c.path[0]); it != scope.localDefs.end()) {
                outDef = it->second;
                return Resolution::Def;
            }
            return Resolution::NotDef;  // builtin or unknown — the typecheck decides
        }
        if (c.path.size() == 2) {
            if (auto ns = scope.namespaces.find(c.path[0]); ns != scope.namespaces.end()) {
                if (auto d = ns->second->defs.find(c.path[1]); d != ns->second->defs.end()) {
                    outDef = d->second;
                    return Resolution::Def;
                }
                error("E505", c.span,
                      "module '" + c.path[0] + "' has no def '" + c.path[1] + "'",
                      "only top-level defs are visible through a namespace (§7.6)");
                return Resolution::Error;
            }
        }
        error("E505", c.span, "unknown qualified symbol '" + qualified(c.path) + "'",
              "import the module first (spec §7.6)");
        return Resolution::Error;
    }

    // --- expression expansion -----------------------------------------------------

    // Clones an expression applying the instance rename map and substituting
    // def calls (lifted into `_lift<N>` bindings appended to `sink`).
    // Unchanged subtrees keep their original pointers.
    static Expr* share(const Expr* e) { return const_cast<Expr*>(e); }  // arena/doc-owned, never mutated

    Expr* expandExpr(const Expr* e, const RenameMap& renames, const FileScope& scope,
                     const std::string& chain, std::vector<Node*>& sink, bool& changed) {
        if (!e) return nullptr;
        switch (e->kind) {
            case NodeKind::Ident: {
                const auto* id = static_cast<const Ident*>(e);
                if (auto it = renames.find(id->name); it != renames.end()) {
                    changed = true;
                    return ident(id->span, it->second);
                }
                return share(e);
            }
            case NodeKind::Paren: {
                const auto* p = static_cast<const Paren*>(e);
                Expr* inner = expandExpr(p->inner, renames, scope, chain, sink, changed);
                if (inner == p->inner) return share(e);
                Paren* n = make<Paren>(p->span);
                n->inner = inner;
                return n;
            }
            case NodeKind::Unary: {
                const auto* u = static_cast<const Unary*>(e);
                Expr* op = expandExpr(u->operand, renames, scope, chain, sink, changed);
                if (op == u->operand) return share(e);
                Unary* n = make<Unary>(u->span);
                n->op = u->op;
                n->operand = op;
                return n;
            }
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                Expr* l = expandExpr(b->lhs, renames, scope, chain, sink, changed);
                Expr* r = expandExpr(b->rhs, renames, scope, chain, sink, changed);
                if (l == b->lhs && r == b->rhs) return share(e);
                Binary* n = make<Binary>(b->span);
                n->op = b->op;
                n->lhs = l;
                n->rhs = r;
                return n;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                Expr* c = expandExpr(t->cond, renames, scope, chain, sink, changed);
                Expr* a = expandExpr(t->thenExpr, renames, scope, chain, sink, changed);
                Expr* bb = expandExpr(t->elseExpr, renames, scope, chain, sink, changed);
                if (c == t->cond && a == t->thenExpr && bb == t->elseExpr) return share(e);
                Ternary* n = make<Ternary>(t->span);
                n->cond = c;
                n->thenExpr = a;
                n->elseExpr = bb;
                return n;
            }
            case NodeKind::VecLit: {
                const auto* v = static_cast<const VecLit*>(e);
                std::vector<Expr*> elems;
                bool same = true;
                for (const Expr* el : v->elems) {
                    Expr* n = expandExpr(el, renames, scope, chain, sink, changed);
                    same = same && n == el;
                    elems.push_back(n);
                }
                if (same) return share(e);
                VecLit* n = make<VecLit>(v->span);
                n->elems = std::move(elems);
                return n;
            }
            case NodeKind::ListLit: {
                const auto* v = static_cast<const ListLit*>(e);
                std::vector<Expr*> elems;
                bool same = true;
                for (const Expr* el : v->elems) {
                    Expr* n = expandExpr(el, renames, scope, chain, sink, changed);
                    same = same && n == el;
                    elems.push_back(n);
                }
                if (same) return share(e);
                ListLit* n = make<ListLit>(v->span);
                n->elems = std::move(elems);
                return n;
            }
            case NodeKind::Call:
                return expandCall(static_cast<const Call*>(e), renames, scope, chain, sink, changed);
            default:
                return share(e);  // literals, attr refs, error sentinels
        }
    }

    Expr* expandCall(const Call* c, const RenameMap& renames, const FileScope& scope,
                     const std::string& chain, std::vector<Node*>& sink, bool& changed) {
        // Arguments first (caller context): nested def calls lift out here.
        std::vector<CallArg> args;
        bool same = true;
        for (const CallArg& a : c->args) {
            Expr* v = expandExpr(a.value, renames, scope, chain, sink, changed);
            same = same && v == a.value;
            args.push_back(CallArg{a.name, a.hasName, v});
        }

        const Def* def = nullptr;
        const Resolution r = resolve(*c, scope, def);
        if (r == Resolution::Error) {
            changed = true;
            return ident(c->span, "_error");  // E505 reported; the run stops on it
        }
        if (r == Resolution::Def) {
            changed = true;
            Call* flat = make<Call>(c->span);
            flat->path = c->path;
            flat->args = std::move(args);
            const std::vector<std::string> outputs = instantiate(def, *flat, chain, sink);
            if (outputs.size() != 1) {
                error("E204", c->span,
                      "'" + def->name + "' returns a tuple and must be destructured",
                      "a, b = " + def->name + "(...)");
                return ident(c->span, "_error");
            }
            // Expression-internal def call: lift to a generated binding.
            const std::string lift = "_lift" + std::to_string(liftCounter_++);
            sink.push_back(assign(c->span, lift, ident(c->span, outputs[0])));
            return ident(c->span, lift);
        }
        if (same) return share(c);
        Call* n = make<Call>(c->span);
        n->path = c->path;
        n->args = std::move(args);
        return n;
    }

    // --- statements ---------------------------------------------------------------

    void processBinding(const Binding* b, const RenameMap& renames, const FileScope& scope,
                        const std::string& chain, std::vector<Node*>& sink, size_t ownerInstance) {
        auto renamed = [&](const std::string& n) -> std::string {
            if (auto it = renames.find(n); it != renames.end()) return it->second;
            return n;
        };
        auto registerTargets = [&](const Binding* flat) {
            if (ownerInstance == kNoInstance) return;
            for (const std::string& n : flat->targets.names) out_.instanceOfBinding[n] = ownerInstance;
        };

        // Statement-level def call: bind the destructuring targets directly to
        // the instance outputs (no lift indirection).
        if (b->value && b->value->kind == NodeKind::Call) {
            const auto* c = static_cast<const Call*>(b->value);
            const Def* def = nullptr;
            const Resolution r = resolve(*c, scope, def);
            if (r == Resolution::Error) return;  // E505 reported
            if (r == Resolution::Def) {
                std::vector<CallArg> args;
                bool changed = false;
                for (const CallArg& a : c->args) {
                    Expr* v = expandExpr(a.value, renames, scope, chain, sink, changed);
                    args.push_back(CallArg{a.name, a.hasName, v});
                }
                Call* flat = make<Call>(c->span);
                flat->path = c->path;
                flat->args = std::move(args);
                const std::vector<std::string> outputs = instantiate(def, *flat, chain, sink);
                const size_t nTargets = b->targets.names.size();
                if (outputs.size() != nTargets) {
                    if (nTargets == 1) {
                        error("E204", b->span,
                              "'" + def->name + "' returns a tuple and must be destructured",
                              "a, b = " + def->name + "(...)");
                    } else {
                        error("E202", b->span,
                              "'" + def->name + "' returns " + std::to_string(outputs.size()) +
                                  " value(s), got " + std::to_string(nTargets) + " targets");
                    }
                    return;
                }
                for (size_t i = 0; i < nTargets; ++i) {
                    Expr* outRef = ident(b->span, outputs[i]);
                    Binding* nb = assign(b->span, renamed(b->targets.names[i]), outRef);
                    sink.push_back(nb);
                    registerTargets(nb);
                }
                return;
            }
        }

        bool changed = false;
        Expr* value = expandExpr(b->value, renames, scope, chain, sink, changed);
        bool targetsSame = true;
        for (const std::string& n : b->targets.names)
            if (renamed(n) != n) targetsSame = false;
        if (!changed && targetsSame) {
            sink.push_back(const_cast<Binding*>(b));
            registerTargets(b);
            return;
        }
        Binding* nb = make<Binding>(b->span);
        for (size_t i = 0; i < b->targets.names.size(); ++i) {
            nb->targets.names.push_back(renamed(b->targets.names[i]));
            nb->targets.spans.push_back(i < b->targets.spans.size() ? b->targets.spans[i] : b->span);
        }
        nb->value = value;
        sink.push_back(nb);
        registerTargets(nb);
    }

    // --- zone expansion (v1.20: def calls in bodies, zones in def bodies) ------

    // Names a zone body defines (bindings, nested zone targets and ports):
    // inside a def instance they are renamed under the instance prefix so the
    // flat program keeps global uniqueness.
    static void collectZoneDefinedNames(const Stmt* z, std::vector<std::string>& out) {
        auto body = [&](const std::vector<Stmt*>& stmts) {
            for (const Stmt* s : stmts) {
                if (!s) continue;
                if (s->kind == NodeKind::Binding) {
                    for (const std::string& n : static_cast<const Binding*>(s)->targets.names) out.push_back(n);
                } else if (s->kind == NodeKind::RepeatZone || s->kind == NodeKind::ForeachZone) {
                    collectZoneDefinedNames(s, out);
                }
            }
        };
        if (z->kind == NodeKind::RepeatZone) {
            const auto* r = static_cast<const RepeatZone*>(z);
            for (const std::string& n : r->targets.names) out.push_back(n);
            for (const std::string& n : r->state.names) out.push_back(n);
            body(r->body);
        } else {
            const auto* f = static_cast<const ForeachZone*>(z);
            out.push_back(f->target);
            out.push_back(f->item);
            body(f->body);
        }
    }

    // Clones a zone with its header expanded in the outer context (def calls
    // in `value`/`iterations`/`collection` lift into `outerSink`) and its body
    // expanded into a fresh statement list (def calls in the body become flat
    // instance bindings inside the body; nested zones recurse). `prefix` is the
    // def instance name ("" at top level): zone-defined names are renamed
    // `prefix.name` so they stay globally unique across instances.
    Stmt* expandZone(const Stmt* z, const RenameMap& renames, const FileScope& scope, const std::string& chain,
                     std::vector<Node*>& outerSink, size_t ownerInstance, const std::string& prefix) {
        RenameMap inner = renames;
        if (!prefix.empty()) {
            std::vector<std::string> defined;
            collectZoneDefinedNames(z, defined);
            for (const std::string& n : defined) inner[n] = prefix + "." + n;
        }
        auto renamed = [&](const std::string& n) -> std::string {
            if (auto it = inner.find(n); it != inner.end()) return it->second;
            return n;
        };
        auto expandBody = [&](const std::vector<Stmt*>& body, std::vector<Stmt*>& out) {
            std::vector<Node*> bodySink;
            for (const Stmt* s : body) {
                if (!s) continue;
                if (s->kind == NodeKind::Binding) {
                    processBinding(static_cast<const Binding*>(s), inner, scope, chain, bodySink, ownerInstance);
                } else if (s->kind == NodeKind::RepeatZone || s->kind == NodeKind::ForeachZone) {
                    bodySink.push_back(expandZone(s, inner, scope, chain, bodySink, ownerInstance, prefix));
                } else {
                    bodySink.push_back(const_cast<Stmt*>(s));  // tap: ignored inside bodies (v0)
                }
            }
            out.reserve(bodySink.size());
            for (Node* n : bodySink) out.push_back(static_cast<Stmt*>(n));
        };
        bool changed = false;
        if (z->kind == NodeKind::RepeatZone) {
            const auto* r = static_cast<const RepeatZone*>(z);
            RepeatZone* n = make<RepeatZone>(r->span);
            for (size_t i = 0; i < r->targets.names.size(); ++i) {
                n->targets.names.push_back(renamed(r->targets.names[i]));
                n->targets.spans.push_back(i < r->targets.spans.size() ? r->targets.spans[i] : r->span);
            }
            for (size_t i = 0; i < r->state.names.size(); ++i) {
                n->state.names.push_back(renamed(r->state.names[i]));
                n->state.spans.push_back(i < r->state.spans.size() ? r->state.spans[i] : r->span);
            }
            n->value = expandExpr(r->value, renames, scope, chain, outerSink, changed);
            n->iterations = expandExpr(r->iterations, renames, scope, chain, outerSink, changed);
            expandBody(r->body, n->body);
            if (ownerInstance != kNoInstance)
                for (const std::string& t : n->targets.names) out_.instanceOfBinding[t] = ownerInstance;
            return n;
        }
        const auto* f = static_cast<const ForeachZone*>(z);
        ForeachZone* n = make<ForeachZone>(f->span);
        n->target = renamed(f->target);
        n->targetSpan = f->targetSpan;
        n->item = renamed(f->item);
        n->itemSpan = f->itemSpan;
        n->collection = expandExpr(f->collection, renames, scope, chain, outerSink, changed);
        expandBody(f->body, n->body);
        if (ownerInstance != kNoInstance) out_.instanceOfBinding[n->target] = ownerInstance;
        return n;
    }

    // --- instantiation -------------------------------------------------------------

    // Inlines one def call: parameters become bindings of the caller's
    // (already expanded) argument expressions, body bindings are renamed to
    // `def[k].local` and expanded in the def's own resolution scope.
    // `call` must already have its arguments expanded in the caller context.
    std::vector<std::string> instantiate(const Def* def, const Call& call, const std::string& chainPrefix,
                                         std::vector<Node*>& sink) {
        FileScope& origin = *defScope_[def];
        const size_t k = instanceCounters_[def]++;
        const std::string iname = def->name + "[" + std::to_string(k) + "]";
        const std::string ipath = chainPrefix.empty() ? iname : chainPrefix + "." + iname;
        const size_t instIdx = out_.instances.size();
        out_.instances.push_back(FlatInstance{iname, ipath, def->name, {}});

        RenameMap renames;
        for (const DefParam& p : def->params) renames[p.name] = iname + "." + p.name;
        for (const Stmt* s : def->body) {
            if (s->kind == NodeKind::Binding) {
                for (const std::string& n : static_cast<const Binding*>(s)->targets.names)
                    renames[n] = iname + "." + n;
            } else if (s->kind == NodeKind::RepeatZone || s->kind == NodeKind::ForeachZone) {
                // Zone targets are def-body names too (read by later
                // statements of the body); ports/body names rename in
                // expandZone.
                std::vector<std::string> defined;
                collectZoneDefinedNames(s, defined);
                for (const std::string& n : defined) renames[n] = iname + "." + n;
            }
        }

        // Argument binding: positional-then-keyword with defaults (mirror of
        // bindCallArgs for the def signature).
        const size_t np = def->params.size();
        std::vector<const CallArg*> byParam(np, nullptr);
        size_t nextPositional = 0;
        for (const CallArg& arg : call.args) {
            if (!arg.hasName) {
                if (nextPositional >= np) {
                    error("E202", call.span, "too many arguments for '" + def->name + "'",
                          "the def takes " + std::to_string(np) + " argument(s)");
                    continue;
                }
                byParam[nextPositional++] = &arg;
                continue;
            }
            size_t idx = np;
            for (size_t i = 0; i < np; ++i)
                if (def->params[i].name == arg.name) idx = i;
            if (idx == np) {
                std::string names;
                for (size_t i = 0; i < np; ++i) names += (i ? ", " : "") + def->params[i].name;
                error("E203", call.span, "unknown parameter '" + arg.name + "' of '" + def->name + "'",
                      "parameters: " + names);
                continue;
            }
            if (byParam[idx]) {
                error("E202", call.span, "argument '" + arg.name + "' of '" + def->name + "' is passed twice");
                continue;
            }
            byParam[idx] = &arg;
            if (idx >= nextPositional) nextPositional = idx + 1;
        }

        for (size_t i = 0; i < np; ++i) {
            const DefParam& p = def->params[i];
            const std::string& flat = renames[p.name];
            Expr* value = nullptr;
            if (byParam[i]) {
                value = const_cast<Expr*>(byParam[i]->value);  // caller context, already expanded
            } else if (p.hasDefault) {
                bool changed = false;
                value = expandExpr(p.def, renames, origin, ipath, sink, changed);
            } else {
                error("E202", call.span, "missing required argument '" + p.name + "' of '" + def->name + "'",
                      "pass " + p.name + " = ...");
                continue;
            }
            sink.push_back(assign(call.span, flat, value));
            out_.declaredTypes[flat] = typeFromRef(*p.type);
            out_.instanceOfBinding[flat] = instIdx;
        }

        // Contracts and body, in source order.
        for (const ContractStmt* c : def->expects) addContract(c, false, renames, origin, ipath, instIdx, sink);
        for (const Stmt* s : def->body) {
            if (s->kind == NodeKind::Binding) {
                processBinding(static_cast<const Binding*>(s), renames, origin, ipath, sink, instIdx);
            } else if (s->kind == NodeKind::Tap) {
                // E6: def-body taps are no longer dropped — recorded per
                // instantiation, they fire on every instance in debug mode
                // (§9.4). The path root (a param or local of the def) renames
                // to this instance's flat binding.
                const auto* t = static_cast<const Tap*>(s);
                FlatTap ft;
                ft.label = t->label;
                ft.hasLabel = t->hasLabel;
                ft.instance = instIdx;
                ft.path = renamedTapPath(t->path, renames);
                out_.taps.push_back(std::move(ft));
            }
            else if (s->kind == NodeKind::RepeatZone || s->kind == NodeKind::ForeachZone) {
                // Zone inside a def body (v1.20): cloned per instance with every
                // zone-defined name (targets, ports, body bindings) renamed
                // under the instance prefix; the body is expanded like the
                // def body itself.
                sink.push_back(expandZone(static_cast<const Stmt*>(s), renames, origin, ipath, sink, instIdx, iname));
            }
        }
        for (const ContractStmt* c : def->ensures) addContract(c, true, renames, origin, ipath, instIdx, sink);

        for (const OutDecl& o : def->outputs) {
            const std::string flat = iname + "." + o.name;
            out_.declaredTypes[flat] = typeFromRef(*o.type);
            // Index, not a held reference: nested instantiations above may
            // have reallocated the instances vector.
            out_.instances[instIdx].outputs.push_back(flat);
        }
        return out_.instances[instIdx].outputs;
    }

    void addContract(const ContractStmt* c, bool isEnsure, const RenameMap& renames,
                     const FileScope& origin, const std::string& ipath, size_t instIdx,
                     std::vector<Node*>& sink) {
        FlatContract fc;
        fc.isEnsure = isEnsure;
        fc.attrForm = c->attrForm;
        fc.message = c->message;
        fc.hasMessage = c->hasMessage;
        fc.span = c->span;
        fc.instance = instIdx;
        if (c->attrForm) {
            fc.ident = c->ident;
            if (auto it = renames.find(c->ident); it != renames.end()) fc.ident = it->second;
            if (c->attr && c->attr->kind == NodeKind::AttrRef)
                fc.attrName = static_cast<const AttrRef*>(c->attr)->name;
        } else {
            bool changed = false;
            fc.cond = expandExpr(c->cond, renames, origin, ipath, sink, changed);
        }
        out_.contracts.push_back(std::move(fc));
    }
};

}  // namespace

FlatProgram expandProgram(const File& mainFile, const ModuleClosure* closure,
                          std::vector<Diagnostic>& diags) {
    FlatProgram out;
    // Pass-through: without defs/imports there is nothing to expand, and the
    // original file flows on bit-for-bit (fingerprints included).
    bool anything = false;
    for (const Node* item : mainFile.items)
        if (item->kind == NodeKind::Def || item->kind == NodeKind::Import) anything = true;
    if (!anything) {
        out.file = &mainFile;
        return out;
    }
    Expander exp(closure, diags, out);
    exp.run(const_cast<File&>(mainFile));  // read-only; the AST uses mutable pointers throughout
    return out;
}

}  // namespace pgg
