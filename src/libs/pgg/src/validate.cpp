#include "pch.h"

#include "validate.h"

#include <unordered_map>
#include <unordered_set>

namespace pgg {
namespace {

const char* kTypeNames[] = {"sdf",   "rng", "f32",    "int",  "bool", "vec2",
                            "vec3",  "vec4", "string", "domain", "geo",  "field",
                            "enum"};

bool isTypeName(const std::string& s) {
    for (const char* t : kTypeNames)
        if (s == t) return true;
    return false;
}

struct BindingInfo {
    Span span;
    bool used = false;
    bool warnIfUnused = true;
    bool isStatePort = false;
    bool boundAsOutput = false;
    bool isRuntimeValue = false;  // param/binding: capturable (hermeticity, E105)
};

struct Scope {
    const Scope* parent = nullptr;
    bool isDefBoundary = false;  // def body scope: hermeticity boundary (§7.6)
    std::unordered_map<std::string, BindingInfo> defined;

    const BindingInfo* lookup(const std::string& name) const {
        for (const Scope* s = this; s; s = s->parent) {
            auto it = s->defined.find(name);
            if (it != s->defined.end()) return &it->second;
        }
        return nullptr;
    }

    BindingInfo* lookup(const std::string& name) {
        return const_cast<BindingInfo*>(std::as_const(*this).lookup(name));
    }
};

class Validator {
public:
    explicit Validator(std::vector<Diagnostic>& diags) : diags_(diags) {}

    void file(const File* f) {
        if (!f) return;
        Scope scope;
        for (const Node* item : f->items) {
            switch (item->kind) {
                case NodeKind::Import: {
                    const auto* im = static_cast<const Import*>(item);
                    // The namespace (alias or last path component) is a defined
                    // name: collisions with params/bindings/defs are E102.
                    define(scope, im->hasAlias ? im->alias : im->path.back(), item->span,
                           /*warnIfUnused=*/false);
                    break;
                }
                case NodeKind::ParamDecl: {
                    const auto* p = static_cast<const ParamDecl*>(item);
                    type(p->type);
                    define(scope, p->name, item->span, /*warnIfUnused=*/false, /*isRuntimeValue=*/true);
                    break;
                }
                case NodeKind::Def: {
                    const auto* d = static_cast<const Def*>(item);
                    define(scope, d->name, item->span, /*warnIfUnused=*/false);
                    def(d, scope);
                    break;
                }
                case NodeKind::OutputDecl: {
                    read(scope, static_cast<const OutputDecl*>(item)->name, item->span);
                    break;
                }
                default:
                    stmt(static_cast<const Stmt*>(item), scope);
                    break;
            }
        }
        reportUnused(scope);
    }

private:
    std::vector<Diagnostic>& diags_;

    void error(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
    }

    void warn(Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{"W001", span, std::move(msg), std::move(hint), true});
    }

    void define(Scope& scope, const std::string& name, Span span, bool warnIfUnused = true,
                bool isRuntimeValue = false) {
        auto it = scope.defined.find(name);
        if (it != scope.defined.end()) {
            BindingInfo& prev = it->second;
            if (prev.isStatePort && !prev.boundAsOutput) {
                // Zone state channel: the single output-port binding (§5.4).
                prev.boundAsOutput = true;
                prev.used = true;
                return;
            }
            error("E102", span, "name '" + name + "' is already defined",
                  "every name is a graph node — pick a fresh name (SSA)");
            return;
        }
        for (const Scope* s = scope.parent; s; s = s->parent) {
            if (s->defined.count(name)) {
                error("E102", span,
                      "name '" + name + "' shadows an outer definition",
                      "two nodes cannot share a name, even in nested scopes");
                return;
            }
        }
        BindingInfo info;
        info.span = span;
        info.warnIfUnused = warnIfUnused;
        info.isRuntimeValue = isRuntimeValue;
        scope.defined.emplace(name, info);
    }

    void defineStatePort(Scope& scope, const std::string& name, Span span) {
        BindingInfo info;
        info.span = span;
        info.used = true;  // state channels are checked by a later stage (§5.4)
        info.warnIfUnused = false;
        info.isStatePort = true;
        scope.defined.emplace(name, info);
    }

    void read(Scope& scope, const std::string& name, Span span) {
        BindingInfo* info = scope.lookup(name);
        if (!info) {
            error("E103", span, "'" + name + "' is used before definition",
                  "define the name above this line (define-before-use)");
            return;
        }
        if (capturedAcrossDefBoundary(scope, name, *info)) {
            error("E105", span,
                  "'" + name + "' is a top-level runtime value captured by a def body",
                  "def bodies are hermetic — pass it through the def signature (§7.6)");
            return;
        }
        info->used = true;
    }

    // A def body may not capture top-level params/rng/bindings of its file
    // (spec §7.6); defs and namespaces stay free names. The boundary counts
    // only when the definition sits above it.
    bool capturedAcrossDefBoundary(Scope& scope, const std::string& name, const BindingInfo& info) {
        if (!info.isRuntimeValue) return false;
        for (const Scope* s = &scope; s; s = s->parent) {
            if (s->defined.count(name)) return false;  // defined at/below the boundary
            if (s->isDefBoundary) return true;
        }
        return false;
    }

    // Named-arg bare idents may be enum literals (mode = poisson) — the
    // distinction is type-driven (E206 stage): count as a use when defined,
    // forgive when not. The hermeticity rule still applies when defined.
    void readNamedArgIdent(Scope& scope, const std::string& name, Span span) {
        BindingInfo* info = scope.lookup(name);
        if (!info) return;
        if (capturedAcrossDefBoundary(scope, name, *info)) {
            error("E105", span,
                  "'" + name + "' is a top-level runtime value captured by a def body",
                  "def bodies are hermetic — pass it through the def signature (§7.6)");
            return;
        }
        info->used = true;
    }

    void reportUnused(const Scope& scope) {
        for (const auto& [name, info] : scope.defined) {
            if (info.warnIfUnused && !info.used) {
                warn(info.span, "name '" + name + "' is defined but never used",
                     "wire it downstream (output/tap/another node) or delete it");
            }
        }
    }

    void type(const TypeRef* t) {
        if (!t) return;
        if (!isTypeName(t->base)) {
            error("E100", t->span, "unknown type '" + t->base + "'");
        }
        type(t->arg);
    }

    void def(const Def* d, Scope& fileScope) {
        Scope scope;
        scope.parent = &fileScope;
        scope.isDefBoundary = true;  // hermeticity boundary for read() (E105)
        for (const DefParam& p : d->params) {
            type(p.type);
            define(scope, p.name, d->span, /*warnIfUnused=*/false, /*isRuntimeValue=*/true);
        }
        for (const OutDecl& o : d->outputs) type(o.type);
        for (const ContractStmt* e : d->expects) contract(e, scope);
        stmts(d->body, scope);
        for (const ContractStmt* e : d->ensures) contract(e, scope);
        // Output ports are part of the def interface — binding them is their
        // purpose, they are not "unused" when nothing reads them downstream.
        for (const OutDecl& o : d->outputs) {
            if (BindingInfo* info = scope.lookup(o.name)) info->used = true;
        }
        reportUnused(scope);
    }

    void contract(const ContractStmt* c, Scope& scope) {
        if (!c) return;
        if (c->attrForm) {
            read(scope, c->ident, c->span);
        } else {
            expr(c->cond, scope);
        }
    }

    void stmts(const std::vector<Stmt*>& body, Scope& scope) {
        for (const Stmt* s : body) stmt(s, scope);
    }

    void stmt(const Stmt* s, Scope& scope) {
        if (!s) return;
        switch (s->kind) {
            case NodeKind::Binding: {
                const auto* b = static_cast<const Binding*>(s);
                expr(b->value, scope);
                for (size_t i = 0; i < b->targets.names.size(); ++i) {
                    Span ts = i < b->targets.spans.size() ? b->targets.spans[i] : s->span;
                    define(scope, b->targets.names[i], ts, /*warnIfUnused=*/true, /*isRuntimeValue=*/true);
                }
                break;
            }
            case NodeKind::Tap: {
                const auto* t = static_cast<const Tap*>(s);
                if (!t->path.empty()) read(scope, t->path.front().name, s->span);
                break;
            }
            case NodeKind::RepeatZone: {
                const auto* z = static_cast<const RepeatZone*>(s);
                expr(z->value, scope);
                expr(z->iterations, scope);
                for (size_t i = 0; i < z->targets.names.size(); ++i) {
                    Span ts = i < z->targets.spans.size() ? z->targets.spans[i] : s->span;
                    define(scope, z->targets.names[i], ts, /*warnIfUnused=*/true, /*isRuntimeValue=*/true);
                }
                Scope body;
                body.parent = &scope;
                for (size_t i = 0; i < z->state.names.size(); ++i) {
                    Span ss = i < z->state.spans.size() ? z->state.spans[i] : s->span;
                    defineStatePort(body, z->state.names[i], ss);
                }
                stmts(z->body, body);
                reportUnused(body);
                break;
            }
            case NodeKind::ForeachZone: {
                const auto* z = static_cast<const ForeachZone*>(s);
                expr(z->collection, scope);
                define(scope, z->target, z->targetSpan, /*warnIfUnused=*/true, /*isRuntimeValue=*/true);
                Scope body;
                body.parent = &scope;
                define(body, z->item, z->itemSpan);
                stmts(z->body, body);
                reportUnused(body);
                break;
            }
            default:
                break;
        }
    }

    void expr(const Expr* e, Scope& scope) {
        if (!e) return;
        switch (e->kind) {
            case NodeKind::Ident:
                read(scope, static_cast<const Ident*>(e)->name, e->span);
                break;
            case NodeKind::Paren:
                expr(static_cast<const Paren*>(e)->inner, scope);
                break;
            case NodeKind::Unary:
                expr(static_cast<const Unary*>(e)->operand, scope);
                break;
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                expr(b->lhs, scope);
                expr(b->rhs, scope);
                break;
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                expr(t->cond, scope);
                expr(t->thenExpr, scope);
                expr(t->elseExpr, scope);
                break;
            }
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                // Qualified call: mark the namespace used when it is defined
                // (unknown namespaces are E505 at the module stage).
                if (c->path.size() >= 2) {
                    if (BindingInfo* info = scope.lookup(c->path.front())) info->used = true;
                }
                for (const CallArg& a : c->args) {
                    if (a.hasName && a.value && a.value->kind == NodeKind::Ident) {
                        // Named-arg bare ident may be an enum literal
                        // (mode = poisson) — the distinction is type-driven
                        // (E206 stage). Count as a use when defined, forgive
                        // when not; hermeticity still applies when defined.
                        readNamedArgIdent(scope, static_cast<const Ident*>(a.value)->name, a.value->span);
                        continue;
                    }
                    expr(a.value, scope);
                }
                break;
            }
            case NodeKind::VecLit: {
                const auto* v = static_cast<const VecLit*>(e);
                for (const Expr* el : v->elems) expr(el, scope);
                break;
            }
            case NodeKind::ListLit: {
                const auto* v = static_cast<const ListLit*>(e);
                for (const Expr* el : v->elems) expr(el, scope);
                break;
            }
            default:
                break;  // literals, attr refs, error sentinels: no name resolution
        }
    }
};

}  // namespace

void validate(const File* file, std::vector<Diagnostic>& diagnostics) {
    Validator v(diagnostics);
    v.file(file);
}

}  // namespace pgg
