#include "pch.h"

#include "formatter.h"

#include <unordered_map>

namespace pgg {
namespace {

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

class Formatter {
public:
    Formatter(const std::vector<Comment>& comments, const File* file)
        : comments_(comments) {
        for (const Node* item : file->items) {
            if (item->kind == NodeKind::Def) {
                const auto* d = static_cast<const Def*>(item);
                defs_[d->name] = d;
            }
        }
    }

    std::string run(const File* file) {
        int32_t prevEnd = 0;
        for (const Node* item : file->items) {
            // A blank line is kept (one, max) when the source had a gap before
            // the item or before a pending own-line comment attached to it.
            int32_t nextLine = item->span.line;
            if (nextComment_ < comments_.size())
                nextLine = std::min(nextLine, comments_[nextComment_].span.line);
            if (prevEnd > 0 && nextLine - prevEnd > 1) line(0, "");
            flushCommentsBefore(item->span.line, 0);
            emitItem(item, 0);
            prevEnd = item->span.endLine;
        }
        flushCommentsBefore(INT32_MAX, 0);
        return out_;
    }

private:
    const std::vector<Comment>& comments_;
    std::unordered_map<std::string, const Def*> defs_;
    size_t nextComment_ = 0;
    std::string out_;

    static std::string pad(int indent) { return std::string(static_cast<size_t>(indent) * 4, ' '); }

    void line(int indent, const std::string& text) { out_ += pad(indent) + text + "\n"; }

    // Own-line comments above `line` are emitted at the current indent.
    void flushCommentsBefore(int32_t line, int indent) {
        while (nextComment_ < comments_.size() && comments_[nextComment_].span.line < line) {
            this->line(indent, "# " + comments_[nextComment_].text);
            ++nextComment_;
        }
    }

    // A comment sitting on the same line as the statement is trailing.
    std::string trailing(int32_t stmtLine) {
        if (nextComment_ < comments_.size() && comments_[nextComment_].span.line == stmtLine) {
            const std::string t = "  # " + comments_[nextComment_].text;
            ++nextComment_;
            return t;
        }
        return {};
    }

    void emitItem(const Node* n, int indent) {
        switch (n->kind) {
            case NodeKind::Import: {
                const auto* im = static_cast<const Import*>(n);
                std::string s = "import ";
                for (size_t i = 0; i < im->path.size(); ++i) {
                    if (i) s += ".";
                    s += im->path[i];
                }
                if (im->hasAlias) s += " as " + im->alias;
                if (im->hasVersion) s += " @ " + im->version;
                line(indent, s + trailing(n->span.line));
                break;
            }
            case NodeKind::ParamDecl: {
                const auto* p = static_cast<const ParamDecl*>(n);
                std::string s = "param " + p->name + ": " + type(p->type);
                if (p->hasDefault) s += " = " + expr(p->def);
                line(indent, s + trailing(n->span.line));
                break;
            }
            case NodeKind::OutputDecl:
                line(indent, "output " + static_cast<const OutputDecl*>(n)->name +
                                 trailing(n->span.line));
                break;
            case NodeKind::Def:
                emitDef(static_cast<const Def*>(n), indent);
                break;
            default:
                emitStmt(static_cast<const Stmt*>(n), indent);
                break;
        }
    }

    void emitDef(const Def* d, int indent) {
        std::string head = "def " + d->name + "(";
        for (size_t i = 0; i < d->params.size(); ++i) {
            if (i) head += ", ";
            head += d->params[i].name + ": " + type(d->params[i].type);
            if (d->params[i].hasDefault) head += " = " + expr(d->params[i].def);
        }
        head += ") -> (";
        for (size_t i = 0; i < d->outputs.size(); ++i) {
            if (i) head += ", ";
            head += d->outputs[i].name + ": " + type(d->outputs[i].type);
        }
        head += ") {";
        line(indent, head + trailing(d->span.line));
        if (d->hasDoc) {
            flushCommentsBefore(d->expects.empty() ? firstBodyLine(d) : d->expects[0]->span.line,
                                indent + 1);
            line(indent + 1, "\"\"\"" + d->docstring + "\"\"\"");
        }
        for (const ContractStmt* e : d->expects) emitContract("expect", e, indent + 1);
        int32_t prevEnd = 0;
        for (const Stmt* s : d->body) {
            flushCommentsBefore(s->span.line, indent + 1);
            emitStmt(s, indent + 1);
            prevEnd = s->span.endLine;
        }
        (void)prevEnd;
        for (const ContractStmt* e : d->ensures) emitContract("ensure", e, indent + 1);
        line(indent, "}");
    }

    static int32_t firstBodyLine(const Def* d) {
        if (!d->body.empty()) return d->body.front()->span.line;
        if (!d->ensures.empty()) return d->ensures.front()->span.line;
        return d->span.endLine;
    }

    void emitContract(const char* kw, const ContractStmt* c, int indent) {
        flushCommentsBefore(c->span.line, indent);
        std::string s = kw;
        s += " ";
        if (c->attrForm) {
            s += c->ident + " has " + expr(c->attr);
        } else {
            s += expr(c->cond);
        }
        if (c->hasMessage) s += ": \"" + escapeString(c->message) + "\"";
        line(indent, s + trailing(c->span.line));
    }

    void emitStmt(const Stmt* s, int indent) {
        switch (s->kind) {
            case NodeKind::Binding: {
                const auto* b = static_cast<const Binding*>(s);
                std::string text;
                for (size_t i = 0; i < b->targets.names.size(); ++i) {
                    if (i) text += ", ";
                    text += b->targets.names[i];
                }
                text += " = " + expr(b->value);
                line(indent, text + trailing(s->span.line));
                break;
            }
            case NodeKind::Tap: {
                const auto* t = static_cast<const Tap*>(s);
                std::string text = "tap ";
                if (t->hasLabel) text += t->label + ": ";
                bool first = true;
                for (const PathElem& el : t->path) {
                    if (el.isIndex) {
                        text += "[" + std::to_string(el.index) + "]";
                    } else {
                        if (!first) text += ".";
                        text += el.name;
                    }
                    first = false;
                }
                line(indent, text + trailing(s->span.line));
                break;
            }
            case NodeKind::RepeatZone: {
                const auto* z = static_cast<const RepeatZone*>(s);
                std::string head;
                for (size_t i = 0; i < z->targets.names.size(); ++i) {
                    if (i) head += ", ";
                    head += z->targets.names[i];
                }
                head += " = repeat (" + expr(z->value) + ", iterations = " +
                        expr(z->iterations) + ") |";
                for (size_t i = 0; i < z->state.names.size(); ++i) {
                    if (i) head += ", ";
                    head += z->state.names[i];
                }
                head += "| {";
                line(indent, head + trailing(s->span.line));
                for (const Stmt* b : z->body) {
                    flushCommentsBefore(b->span.line, indent + 1);
                    emitStmt(b, indent + 1);
                }
                line(indent, "}");
                break;
            }
            case NodeKind::ForeachZone: {
                const auto* z = static_cast<const ForeachZone*>(s);
                std::string head = z->target + " = foreach " + z->item + " in " +
                                   expr(z->collection) + " {";
                line(indent, head + trailing(s->span.line));
                for (const Stmt* b : z->body) {
                    flushCommentsBefore(b->span.line, indent + 1);
                    emitStmt(b, indent + 1);
                }
                line(indent, "}");
                break;
            }
            default:
                line(indent, "<stmt?>");
                break;
        }
    }

    std::string type(const TypeRef* t) {
        if (!t) return "?";
        std::string s = t->base;
        if (t->base == "geo" && !t->geoKind.empty()) s += "<" + t->geoKind + ">";
        if (t->base == "field" && t->arg) s += "<" + type(t->arg) + ">";
        if (t->base == "enum") {
            s += " {";
            for (size_t i = 0; i < t->enumValues.size(); ++i) {
                if (i) s += ", ";
                s += t->enumValues[i];
            }
            s += "}";
        }
        if (t->optional) s += "?";
        if (t->list) s += "[]";
        return s;
    }

    std::string expr(const Expr* e) {
        if (!e) return "<error>";
        switch (e->kind) {
            case NodeKind::NumberLit: return static_cast<const NumberLit*>(e)->text;
            case NodeKind::StringLit:
                return "\"" + escapeString(static_cast<const StringLit*>(e)->value) + "\"";
            case NodeKind::BoolLit:
                return static_cast<const BoolLit*>(e)->value ? "true" : "false";
            case NodeKind::NoneLit: return "none";
            case NodeKind::EnumLit: return static_cast<const EnumLit*>(e)->name;
            case NodeKind::Ident: return static_cast<const Ident*>(e)->name;
            case NodeKind::AttrRef: return "@" + static_cast<const AttrRef*>(e)->name;
            case NodeKind::VecLit: {
                const auto* v = static_cast<const VecLit*>(e);
                std::string s = "(";
                for (size_t i = 0; i < v->elems.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(v->elems[i]);
                }
                return s + ")";
            }
            case NodeKind::ListLit: {
                const auto* v = static_cast<const ListLit*>(e);
                std::string s = "[";
                for (size_t i = 0; i < v->elems.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(v->elems[i]);
                }
                return s + "]";
            }
            case NodeKind::Paren:
                return "(" + expr(static_cast<const Paren*>(e)->inner) + ")";
            case NodeKind::Unary: {
                const auto* u = static_cast<const Unary*>(e);
                return u->op + expr(u->operand);
            }
            case NodeKind::Binary: {
                const auto* b = static_cast<const Binary*>(e);
                return expr(b->lhs) + " " + b->op + " " + expr(b->rhs);
            }
            case NodeKind::Ternary: {
                const auto* t = static_cast<const Ternary*>(e);
                return expr(t->cond) + " ? " + expr(t->thenExpr) + " : " + expr(t->elseExpr);
            }
            case NodeKind::Call: {
                const auto* c = static_cast<const Call*>(e);
                std::string s;
                for (size_t i = 0; i < c->path.size(); ++i) {
                    if (i) s += ".";
                    s += c->path[i];
                }
                s += "(" + callArgs(c) + ")";
                return s;
            }
            default: return "<error>";
        }
    }

    std::string callArgs(const Call* c) {
        std::vector<const CallArg*> order;
        order.reserve(c->args.size());
        for (const CallArg& a : c->args) order.push_back(&a);

        // Reorder to the declared parameter order when the callee is a
        // same-file def and every argument is named (spec §6.4).
        if (c->path.size() == 1 && !c->args.empty()) {
            auto it = defs_.find(c->path[0]);
            bool allNamed = true;
            for (const CallArg& a : c->args)
                if (!a.hasName) allNamed = false;
            if (it != defs_.end() && allNamed) {
                const Def* d = it->second;
                std::vector<const CallArg*> reordered;
                for (const DefParam& p : d->params) {
                    for (const CallArg* a : order) {
                        if (a->name == p.name) {
                            reordered.push_back(a);
                            break;
                        }
                    }
                }
                for (const CallArg* a : order) {  // unknown names keep source order at the end
                    bool used = false;
                    for (const DefParam& p : d->params)
                        if (a->name == p.name) used = true;
                    if (!used) reordered.push_back(a);
                }
                order = std::move(reordered);
            }
        }

        std::string s;
        for (size_t i = 0; i < order.size(); ++i) {
            if (i) s += ", ";
            if (order[i]->hasName) s += order[i]->name + " = ";
            s += expr(order[i]->value);
        }
        return s;
    }
};

}  // namespace

std::string format(const File* file, const std::vector<Comment>& comments) {
    if (!file) return {};
    Formatter f(comments, file);
    return f.run(file);
}

}  // namespace pgg
