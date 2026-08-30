#include "pch.h"

#include "graph.h"

#include <unordered_map>
#include <utility>

#include "eval/modules.h"
#include "layout.h"
#include "pgg/pgg.h"

namespace pgg {

const GraphScope* GraphProject::scopeOf(const std::string& instancePath) const {
    if (instancePath.empty()) return &top;
    for (size_t i = 0; i < instancePaths.size(); ++i)
        if (instancePaths[i] == instancePath) return &instanceScopes[i];
    return nullptr;
}

GraphScope* GraphProject::scopeOf(const std::string& instancePath) {
    return const_cast<GraphScope*>(std::as_const(*this).scopeOf(instancePath));
}

namespace {

// A name bound in the walked scope: the node that defines it and the out pin
// carrying it (the target index of a multi-output binding).
struct NameDef {
    int node = -1;
    int pin = 0;
};

// Resolution context of one source file (mirrors Expander::FileScope): the
// defs callable unqualified, the import namespaces for qualified calls, the
// file's comments for hint attachment and its identity for write-back.
struct FileCtx {
    std::unordered_map<std::string, const Def*> localDefs;
    std::unordered_map<std::string, const ModuleInfo*> namespaces;
    const std::vector<Comment>* comments = nullptr;
    std::string originFile;  // "" = the main file
};

// Mutable state of one scope walk (the top level, a def body or a zone body).
struct ScopeState {
    GraphScope* g = nullptr;
    std::unordered_map<std::string, NameDef> names;
    int zone = -1;
    // State/item ports of the enclosing zone still waiting for their single
    // output-port binding (§5.4): port name -> ZoneOutput node.
    std::unordered_map<std::string, int> pendingPorts;
};

std::string qualified(const std::vector<std::string>& path) {
    std::string q;
    for (const std::string& p : path) q += (q.empty() ? "" : ".") + p;
    return q;
}

class GraphBuilder {
public:
    GraphBuilder(const Document& doc, const ModuleClosure* closure) {
        mainCtx_.comments = &doc.comments;
        if (doc.file) {
            for (const Node* item : doc.file->items) {
                if (item->kind != NodeKind::Def) continue;
                const auto* d = static_cast<const Def*>(item);
                mainCtx_.localDefs[d->name] = d;
                defCtx_[d] = &mainCtx_;
            }
        }
        if (closure) {
            mainCtx_.namespaces = closure->mainNamespaces;
            moduleCtxs_.reserve(closure->modules.size());
            for (const ModuleInfo* m : closure->modules) {
                FileCtx fc;
                fc.localDefs = m->defs;
                fc.namespaces = m->namespaces;
                fc.comments = m->doc ? &m->doc->comments : nullptr;
                fc.originFile = m->canonicalPath;
                moduleCtxs_.push_back(std::move(fc));
                const FileCtx* stored = &moduleCtxs_.back();
                for (const Node* item : m->doc->file->items)
                    if (item->kind == NodeKind::Def)
                        defCtx_[static_cast<const Def*>(item)] = stored;
            }
        }
    }

    GraphProject run(const File& file) {
        buildTopScope(file);
        return std::move(project_);
    }

private:
    GraphProject project_;
    FileCtx mainCtx_;
    std::vector<FileCtx> moduleCtxs_;
    std::unordered_map<const Def*, const FileCtx*> defCtx_;
    // Per-def instance counters, global to the whole walk — the expansion
    // numbers instances of one def across every call site with a single
    // counter, so the dive views must too.
    std::unordered_map<const Def*, size_t> counters_;

    const FileCtx& ctxOf(const Def* def) const {
        if (auto it = defCtx_.find(def); it != defCtx_.end()) return *it->second;
        return mainCtx_;
    }

    // Silent def resolution (no E505 side effects): unknown names stay
    // ordinary op nodes and let the diagnostics panel speak.
    const Def* resolveDef(const Call& c, const FileCtx& ctx) const {
        if (c.path.size() == 1) {
            if (auto it = ctx.localDefs.find(c.path[0]); it != ctx.localDefs.end()) return it->second;
            return nullptr;
        }
        if (c.path.size() == 2) {
            if (auto ns = ctx.namespaces.find(c.path[0]); ns != ctx.namespaces.end())
                if (auto d = ns->second->defs.find(c.path[1]); d != ns->second->defs.end())
                    return d->second;
        }
        return nullptr;
    }

    // --- node/edge helpers ------------------------------------------------------

    int addNode(ScopeState& s, GraphNode n) {
        n.id = static_cast<int>(s.g->nodes.size());
        n.zone = s.zone;
        const int id = n.id;
        s.g->nodes.push_back(std::move(n));
        if (s.zone >= 0) s.g->zones[s.zone].members.push_back(id);
        return id;
    }

    // Finds or appends an in pin. `distinct` forces a fresh slot (positional
    // arguments: merge(a, b) has two unlabeled pins, not one shared).
    int inPin(GraphScope& g, int nodeId, const std::string& name, bool distinct) {
        GraphNode& n = g.nodes[nodeId];
        if (!distinct) {
            for (size_t i = 0; i < n.inputs.size(); ++i)
                if (n.inputs[i] == name) return static_cast<int>(i);
        }
        n.inputs.push_back(name);
        return static_cast<int>(n.inputs.size()) - 1;
    }

    void addEdge(GraphScope& g, int from, int fromPin, int to, int toPin, bool loop = false) {
        if (from < 0 || to < 0) return;
        g.edges.push_back(GraphEdge{from, fromPin, to, toPin, loop});
    }

    void attachHint(GraphNode& n, const FileCtx& ctx, int32_t line) {
        if (!ctx.comments) return;
        for (const Comment& c : *ctx.comments) {
            if (c.span.line != line) continue;
            int x = 0, y = 0;
            if (parsePosHint(c.text, x, y)) {
                n.hasHint = true;
                n.hintX = static_cast<float>(x);
                n.hintY = static_cast<float>(y);
                return;  // first match wins (§10)
            }
        }
    }

    void defineTargets(const std::vector<std::string>& targets, ScopeState& s, int node) {
        for (size_t i = 0; i < targets.size(); ++i) {
            const std::string& t = targets[i];
            s.names[t] = NameDef{node, static_cast<int>(i)};
            if (auto pp = s.pendingPorts.find(t); pp != s.pendingPorts.end()) {
                // The single output-port binding of a zone state channel
                // (§5.4): wire it to the iteration-output port.
                const int toPin = inPin(*s.g, pp->second, t, false);
                addEdge(*s.g, node, static_cast<int>(i), pp->second, toPin);
                s.pendingPorts.erase(pp);
            }
        }
    }

    // --- expression walk ---------------------------------------------------------

    // Walks an expression in AST order; every read of a defined name becomes a
    // wire into `target` at pin `pin`, every def call becomes its own node.
    // Undefined idents produce no wire: in named-arg position they are enum
    // literals (the validator's readNamedArgIdent rule), elsewhere the file
    // has an E103 the diagnostics panel shows — the projection forgives both.
    void walkExpr(const Expr* e, ScopeState& s, const FileCtx& ctx, int target,
                  const std::string& pin, bool pinDistinct, const std::string& chain) {
        if (!e) return;
        switch (e->kind) {
            case NodeKind::Ident: {
                if (target < 0) return;
                if (auto it = s.names.find(static_cast<const Ident*>(e)->name); it != s.names.end()) {
                    const int toPin = inPin(*s.g, target, pin, pinDistinct);
                    addEdge(*s.g, it->second.node, it->second.pin, target, toPin);
                }
                return;
            }
            case NodeKind::Paren:
                walkExpr(static_cast<const Paren*>(e)->inner, s, ctx, target, pin, pinDistinct, chain);
                return;
            case NodeKind::Unary:
                walkExpr(static_cast<const Unary*>(e)->operand, s, ctx, target, pin, pinDistinct, chain);
                return;
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                walkExpr(b->lhs, s, ctx, target, pin, pinDistinct, chain);
                walkExpr(b->rhs, s, ctx, target, pin, pinDistinct, chain);
                return;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                walkExpr(t->cond, s, ctx, target, pin, pinDistinct, chain);
                walkExpr(t->thenExpr, s, ctx, target, pin, pinDistinct, chain);
                walkExpr(t->elseExpr, s, ctx, target, pin, pinDistinct, chain);
                return;
            }
            case NodeKind::VecLit:
                for (const Expr* el : static_cast<const VecLit*>(e)->elems)
                    walkExpr(el, s, ctx, target, pin, pinDistinct, chain);
                return;
            case NodeKind::ListLit:
                for (const Expr* el : static_cast<const ListLit*>(e)->elems)
                    walkExpr(el, s, ctx, target, pin, pinDistinct, chain);
                return;
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                if (const Def* def = resolveDef(*c, ctx)) {
                    // Expression-internal def call: its own collapsed node
                    // (the expansion lifts it into a `_lift<N>` binding).
                    const int dn = addDefCallNode(*c, def, s, ctx, outPinsOf(def), chain);
                    if (target >= 0) {
                        const int toPin = inPin(*s.g, target, pin, pinDistinct);
                        addEdge(*s.g, dn, 0, target, toPin);
                    }
                    return;
                }
                for (const CallArg& a : c->args)
                    walkExpr(a.value, s, ctx, target, a.hasName ? a.name : "", !a.hasName, chain);
                return;
            }
            default:
                return;  // literals, attr refs, error sentinels: no name reads
        }
    }

    static std::vector<std::string> outPinsOf(const Def* def) {
        std::vector<std::string> pins;
        for (const OutDecl& o : def->outputs) pins.push_back(o.name);
        if (pins.empty()) pins.push_back("out");
        return pins;
    }

    // Positional-then-keyword argument binding of a def call (lite mirror of
    // the expander's binder; the error paths are the validator's business).
    static std::vector<const CallArg*> bindArgs(const Def& def, const Call& call) {
        std::vector<const CallArg*> byParam(def.params.size(), nullptr);
        size_t nextPositional = 0;
        for (const CallArg& arg : call.args) {
            if (!arg.hasName) {
                if (nextPositional < byParam.size()) byParam[nextPositional++] = &arg;
                continue;
            }
            for (size_t i = 0; i < def.params.size(); ++i) {
                if (def.params[i].name == arg.name) {
                    byParam[i] = &arg;
                    if (i >= nextPositional) nextPositional = i + 1;
                    break;
                }
            }
        }
        return byParam;
    }

    // --- statements -------------------------------------------------------------

    void walkStmt(const Stmt* st, ScopeState& s, const FileCtx& ctx, const std::string& chain) {
        if (!st) return;
        switch (st->kind) {
            case NodeKind::Binding:
                addBindingNode(static_cast<const Binding*>(st), s, ctx, chain);
                return;
            case NodeKind::Tap: {
                const auto* t = static_cast<const Tap*>(st);
                GraphNode chip;
                chip.kind = GraphNode::Kind::Tap;
                chip.op = "tap";
                chip.name = t->hasLabel ? t->label : pathText(t->path);
                chip.span = st->span;
                const int id = addNode(s, std::move(chip));
                if (!t->path.empty() && !t->path.front().isIndex) {
                    if (auto it = s.names.find(t->path.front().name); it != s.names.end()) {
                        const int toPin = inPin(*s.g, id, "", false);
                        addEdge(*s.g, it->second.node, it->second.pin, id, toPin);
                    }
                }
                return;
            }
            case NodeKind::RepeatZone:
            case NodeKind::ForeachZone:
                walkZone(st, s, ctx, chain);
                return;
            default:
                return;  // contract statements are not nodes (spec §10)
        }
    }

    int addBindingNode(const Binding* b, ScopeState& s, const FileCtx& ctx, const std::string& chain) {
        // Statement-level def call: the collapsed node carries the targets.
        if (b->value && b->value->kind == NodeKind::Call) {
            const auto* c = static_cast<const Call*>(b->value);
            if (const Def* def = resolveDef(*c, ctx)) {
                const int id = addDefCallNode(*c, def, s, ctx, b->targets.names, chain);
                defineTargets(b->targets.names, s, id);
                return id;
            }
        }
        GraphNode n;
        n.kind = GraphNode::Kind::Binding;
        if (b->value && b->value->kind == NodeKind::Call) {
            n.op = qualified(static_cast<const Call*>(b->value)->path);
        } else {
            n.op = "expr";
        }
        n.outputs = b->targets.names;
        n.span = b->span;
        attachHint(n, ctx, b->span.line);
        const int id = addNode(s, std::move(n));
        walkExpr(b->value, s, ctx, id, "", false, chain);
        defineTargets(b->targets.names, s, id);
        return id;
    }

    int addDefCallNode(const Call& c, const Def* def, ScopeState& s, const FileCtx& callerCtx,
                       const std::vector<std::string>& outPins, const std::string& chain) {
        GraphNode n;
        n.kind = GraphNode::Kind::DefCall;
        n.op = qualified(c.path);
        n.outputs = outPins;
        n.span = c.span;
        attachHint(n, callerCtx, c.span.line);
        const int id = addNode(s, std::move(n));

        // Arguments first (caller context): nested def calls number before
        // this one — the expansion order (eval/expand.cpp expandCall).
        for (const CallArg& a : c.args)
            walkExpr(a.value, s, callerCtx, id, a.hasName ? a.name : "", !a.hasName, chain);

        // The k-th call of this def in expansion order becomes foo[k]; the
        // path grows when diving (§7.7).
        const size_t k = counters_[def]++;
        const std::string iname = def->name + "[" + std::to_string(k) + "]";
        const std::string ipath = chain.empty() ? iname : chain + "." + iname;
        GraphNode& node = s.g->nodes[id];
        node.def = def;
        node.name = iname;
        node.instanceName = iname;
        node.instancePath = ipath;

        // The dive target: one body scope per instance. Built into a local
        // scope first — nested instances append to the project vectors
        // during the build (parent path precedes its nested paths, exactly
        // like FlatProgram::instances).
        const size_t idx = project_.instanceScopes.size();
        project_.instancePaths.push_back(ipath);
        project_.instanceScopes.emplace_back();
        project_.instanceScopes[idx] = buildInstanceScope(*def, c, ipath);
        return id;
    }

    GraphScope buildInstanceScope(const Def& def, const Call& call, const std::string& ipath) {
        GraphScope g;
        const FileCtx& dctx = ctxOf(&def);
        g.originFile = dctx.originFile;
        ScopeState s{&g, {}, -1, {}};

        // Params are the instance's input chips.
        std::vector<int> paramChips;
        for (const DefParam& p : def.params) {
            GraphNode chip;
            chip.kind = GraphNode::Kind::Param;
            chip.op = "param";
            chip.name = p.name;
            chip.outputs = {p.name};
            chip.span = def.span;
            paramChips.push_back(addNode(s, std::move(chip)));
            s.names[p.name] = NameDef{paramChips.back(), 0};
        }

        // Defaults of unpassed parameters evaluate in the instance context
        // (expansion: foo[k].param = <default expr>) — they wire into the
        // chip and any def call inside them numbers here.
        const std::vector<const CallArg*> byParam = bindArgs(def, call);
        for (size_t i = 0; i < def.params.size(); ++i)
            if (!byParam[i] && def.params[i].hasDefault)
                walkExpr(def.params[i].def, s, dctx, paramChips[i], "", false, ipath);

        // Contracts are not nodes; their expressions still walk for def calls.
        for (const ContractStmt* c : def.expects)
            if (c && !c->attrForm) walkExpr(c->cond, s, dctx, -1, "", false, ipath);

        for (const Stmt* st : def.body) walkStmt(st, s, dctx, ipath);

        // Output chips: the def's interface sinks.
        for (const OutDecl& o : def.outputs) {
            GraphNode chip;
            chip.kind = GraphNode::Kind::Output;
            chip.op = "output";
            chip.name = o.name;
            chip.span = def.span;
            const int id = addNode(s, std::move(chip));
            if (auto it = s.names.find(o.name); it != s.names.end()) {
                const int toPin = inPin(g, id, o.name, false);
                addEdge(g, it->second.node, it->second.pin, id, toPin);
            }
        }

        for (const ContractStmt* c : def.ensures)
            if (c && !c->attrForm) walkExpr(c->cond, s, dctx, -1, "", false, ipath);
        return g;
    }

    // --- zones (§5.4, §10) --------------------------------------------------------

    void walkZone(const Stmt* st, ScopeState& s, const FileCtx& ctx, const std::string& chain) {
        const auto* rz = st->kind == NodeKind::RepeatZone ? static_cast<const RepeatZone*>(st) : nullptr;
        const auto* fz = st->kind == NodeKind::ForeachZone ? static_cast<const ForeachZone*>(st) : nullptr;

        // The header is the zone seen from the parent scope: inputs → outputs.
        const std::vector<std::string> targetNames =
            rz ? rz->targets.names : std::vector<std::string>{fz->target};
        GraphNode h;
        h.kind = GraphNode::Kind::ZoneHeader;
        h.op = rz ? "repeat" : "foreach";
        h.outputs = targetNames;
        h.span = st->span;
        attachHint(h, ctx, st->span.line);
        const int hid = addNode(s, std::move(h));

        // Header expressions evaluate once, in the parent scope.
        if (rz) {
            walkExpr(rz->value, s, ctx, hid, "", true, chain);
            walkExpr(rz->iterations, s, ctx, hid, "iterations", false, chain);
        } else {
            walkExpr(fz->collection, s, ctx, hid, "", true, chain);
        }

        // Targets define in the parent scope before the body walks (the
        // validator's order); downstream reads resolve to the header's pins.
        defineTargets(targetNames, s, hid);

        const int zid = static_cast<int>(s.g->zones.size());
        s.g->zones.push_back(GraphZone{});
        s.g->zones[zid].id = zid;
        s.g->zones[zid].header = hid;
        s.g->zones[zid].parent = s.zone;
        if (s.zone >= 0) s.g->zones[s.zone].children.push_back(zid);

        // The body scope: outer names stay visible (wires cross the frame),
        // state/item names start mapped to the iteration-input ports — the
        // validator's defineStatePort mechanics.
        ScopeState body{s.g, s.names, zid, {}};
        const std::vector<std::string>& portNames = rz ? rz->state.names : std::vector<std::string>{fz->item};
        std::vector<int> inIds, outIds;
        for (const std::string& port : portNames) {
            GraphNode in;
            in.kind = GraphNode::Kind::ZoneInput;
            in.op = "in";
            in.name = port;
            in.outputs = {port};
            in.span = st->span;
            const int inId = addNode(body, std::move(in));
            inIds.push_back(inId);
            s.g->zones[zid].inputPorts.push_back(inId);
            body.names[port] = NameDef{inId, 0};
            // The header's input value feeds the first iteration.
            const int initPin = inPin(*s.g, inId, rz ? "init" : "collection", false);
            addEdge(*s.g, hid, 0, inId, initPin);

            GraphNode out;
            out.kind = GraphNode::Kind::ZoneOutput;
            out.op = "out";
            out.name = port;
            out.outputs = {port};
            out.span = st->span;
            const int outId = addNode(body, std::move(out));
            outIds.push_back(outId);
            s.g->zones[zid].outputPorts.push_back(outId);
            body.pendingPorts[port] = outId;
        }

        const std::vector<Stmt*>& stmts = rz ? rz->body : fz->body;
        for (const Stmt* bs : stmts) walkStmt(bs, body, ctx, chain);

        for (size_t i = 0; i < portNames.size(); ++i) {
            // The state loop: iteration output feeds the next iteration's
            // input (drawn as a back edge).
            const int loopPin = inPin(*s.g, inIds[i], "loop", false);
            addEdge(*s.g, outIds[i], 0, inIds[i], loopPin, /*loop=*/true);
            // The final state becomes the zone's result at the header.
            const int resultPin = inPin(*s.g, hid, "result", false);
            addEdge(*s.g, outIds[i], 0, hid, resultPin);
        }
    }

    // --- top level -----------------------------------------------------------------

    void buildTopScope(const File& file) {
        GraphScope g;  // originFile "" — the main file
        ScopeState s{&g, {}, -1, {}};
        for (const Node* item : file.items) {
            switch (item->kind) {
                case NodeKind::Import: {
                    const auto* im = static_cast<const Import*>(item);
                    GraphNode chip;
                    chip.kind = GraphNode::Kind::Import;
                    chip.op = "import";
                    chip.name = im->hasAlias ? im->alias : im->path.back();
                    chip.span = item->span;
                    addNode(s, std::move(chip));
                    break;
                }
                case NodeKind::ParamDecl: {
                    const auto* p = static_cast<const ParamDecl*>(item);
                    GraphNode chip;
                    chip.kind = GraphNode::Kind::Param;
                    chip.op = "param";
                    chip.name = p->name;
                    chip.outputs = {p->name};
                    chip.span = item->span;
                    const int id = addNode(s, std::move(chip));
                    s.names[p->name] = NameDef{id, 0};
                    break;
                }
                case NodeKind::Def:
                    break;  // callable, not a node — reached through its call sites
                case NodeKind::OutputDecl: {
                    const auto* o = static_cast<const OutputDecl*>(item);
                    GraphNode chip;
                    chip.kind = GraphNode::Kind::Output;
                    chip.op = "output";
                    chip.name = o->name;
                    chip.span = item->span;
                    const int id = addNode(s, std::move(chip));
                    if (auto it = s.names.find(o->name); it != s.names.end()) {
                        const int toPin = inPin(g, id, o->name, false);
                        addEdge(g, it->second.node, it->second.pin, id, toPin);
                    }
                    break;
                }
                default:
                    walkStmt(static_cast<const Stmt*>(item), s, mainCtx_, "");
                    break;
            }
        }
        project_.top = std::move(g);
    }
};

}  // namespace

GraphProject buildGraph(const Document& doc, const ModuleClosure* closure) {
    GraphProject p;
    if (!doc.file) return p;
    GraphBuilder b(doc, closure);
    return b.run(*doc.file);
}

}  // namespace pgg
