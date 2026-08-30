#include "pch.h"

#include "ast.h"

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

std::string typeText(const TypeRef* t) {
    if (!t) return "<null-type>";
    std::string out = t->base;
    if (t->base == "geo" && !t->geoKind.empty()) out += "<" + t->geoKind + ">";
    if (t->base == "field" && t->arg) out += "<" + typeText(t->arg) + ">";
    if (t->base == "enum") {
        out += "{";
        for (size_t i = 0; i < t->enumValues.size(); ++i) {
            if (i) out += ", ";
            out += t->enumValues[i];
        }
        out += "}";
    }
    if (t->optional) out += "?";
    if (t->list) out += "[]";
    return out;
}

std::string exprText(const Expr* e) {
    if (!e) return "<null>";
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
            std::string out = "(";
            for (size_t i = 0; i < v->elems.size(); ++i) {
                if (i) out += ", ";
                out += exprText(v->elems[i]);
            }
            return out + ")";
        }
        case NodeKind::ListLit: {
            const auto* v = static_cast<const ListLit*>(e);
            std::string out = "[";
            for (size_t i = 0; i < v->elems.size(); ++i) {
                if (i) out += ", ";
                out += exprText(v->elems[i]);
            }
            return out + "]";
        }
        case NodeKind::Paren:
            return "(" + exprText(static_cast<const Paren*>(e)->inner) + ")";
        case NodeKind::Unary: {
            const auto* u = static_cast<const Unary*>(e);
            return "(" + u->op + " " + exprText(u->operand) + ")";
        }
        case NodeKind::Binary: {
            const auto* b = static_cast<const Binary*>(e);
            return "(" + exprText(b->lhs) + " " + b->op + " " + exprText(b->rhs) + ")";
        }
        case NodeKind::Ternary: {
            const auto* t = static_cast<const Ternary*>(e);
            return "(" + exprText(t->cond) + " ? " + exprText(t->thenExpr) + " : " +
                   exprText(t->elseExpr) + ")";
        }
        case NodeKind::Call: {
            const auto* c = static_cast<const Call*>(e);
            std::string out;
            for (size_t i = 0; i < c->path.size(); ++i) {
                if (i) out += ".";
                out += c->path[i];
            }
            out += "(";
            for (size_t i = 0; i < c->args.size(); ++i) {
                if (i) out += ", ";
                if (c->args[i].hasName) out += c->args[i].name + " = ";
                out += exprText(c->args[i].value);
            }
            return out + ")";
        }
        case NodeKind::ErrorExpr: return "<error>";
        default: return "<expr?>";
    }
}

}  // namespace

std::string pathText(const std::vector<PathElem>& path) {
    std::string out;
    for (const PathElem& el : path) {
        if (el.isIndex) {
            out += "[" + std::to_string(el.index) + "]";
        } else {
            if (!out.empty()) out += ".";
            out += el.name;
        }
    }
    return out;
}

namespace {

std::string joinNames(const NameList& l) {
    std::string out;
    for (size_t i = 0; i < l.names.size(); ++i) {
        if (i) out += ", ";
        out += l.names[i];
    }
    return out;
}

void dumpNode(const Node* n, std::string& out, int indent) {
    const std::string pad(static_cast<size_t>(indent) * 2, ' ');
    const std::string pad2(static_cast<size_t>(indent + 1) * 2, ' ');
    if (!n) {
        out += pad + "<null>\n";
        return;
    }
    switch (n->kind) {
        case NodeKind::File: {
            out += pad + "File\n";
            const auto* f = static_cast<const File*>(n);
            for (const Node* item : f->items) dumpNode(item, out, indent + 1);
            break;
        }
        case NodeKind::Import: {
            const auto* im = static_cast<const Import*>(n);
            out += pad + "Import ";
            for (size_t i = 0; i < im->path.size(); ++i) {
                if (i) out += ".";
                out += im->path[i];
            }
            if (im->hasAlias) out += " as " + im->alias;
            if (im->hasVersion) out += " @" + im->version;
            out += "\n";
            break;
        }
        case NodeKind::ParamDecl: {
            const auto* p = static_cast<const ParamDecl*>(n);
            out += pad + "Param " + p->name + ": " + typeText(p->type);
            if (p->hasDefault) out += " = " + exprText(p->def);
            out += "\n";
            break;
        }
        case NodeKind::OutputDecl:
            out += pad + "Output " + static_cast<const OutputDecl*>(n)->name + "\n";
            break;
        case NodeKind::Def: {
            const auto* d = static_cast<const Def*>(n);
            out += pad + "Def " + d->name + "(";
            for (size_t i = 0; i < d->params.size(); ++i) {
                if (i) out += ", ";
                out += d->params[i].name + ": " + typeText(d->params[i].type);
                if (d->params[i].hasDefault) out += " = " + exprText(d->params[i].def);
            }
            out += ") -> (";
            for (size_t i = 0; i < d->outputs.size(); ++i) {
                if (i) out += ", ";
                out += d->outputs[i].name + ": " + typeText(d->outputs[i].type);
            }
            out += ")\n";
            if (d->hasDoc) out += pad2 + "Doc \"\"\"" + d->docstring + "\"\"\"\n";
            for (const ContractStmt* e : d->expects) dumpNode(e, out, indent + 1);
            for (const Stmt* s : d->body) dumpNode(s, out, indent + 1);
            for (const ContractStmt* e : d->ensures) dumpNode(e, out, indent + 1);
            break;
        }
        case NodeKind::Binding: {
            const auto* b = static_cast<const Binding*>(n);
            out += pad + "Binding " + joinNames(b->targets) + " = " + exprText(b->value) + "\n";
            break;
        }
        case NodeKind::Tap: {
            const auto* t = static_cast<const Tap*>(n);
            out += pad + "Tap ";
            if (t->hasLabel) out += t->label + ": ";
            out += pathText(t->path) + "\n";
            break;
        }
        case NodeKind::Expect:
        case NodeKind::Ensure: {
            const auto* c = static_cast<const ContractStmt*>(n);
            out += pad + std::string(n->kind == NodeKind::Expect ? "Expect " : "Ensure ");
            if (c->attrForm) {
                out += c->ident + " has " + exprText(c->attr);
            } else {
                out += exprText(c->cond);
            }
            if (c->hasMessage) out += ": \"" + escapeString(c->message) + "\"";
            out += "\n";
            break;
        }
        case NodeKind::RepeatZone: {
            const auto* r = static_cast<const RepeatZone*>(n);
            out += pad + "Repeat " + joinNames(r->targets) + " = repeat(" + exprText(r->value) +
                   ", iterations = " + exprText(r->iterations) + ") |" + joinNames(r->state) + "|\n";
            for (const Stmt* s : r->body) dumpNode(s, out, indent + 1);
            break;
        }
        case NodeKind::ForeachZone: {
            const auto* f = static_cast<const ForeachZone*>(n);
            out += pad + "Foreach " + f->target + " = foreach " + f->item + " in " +
                   exprText(f->collection) + "\n";
            for (const Stmt* s : f->body) dumpNode(s, out, indent + 1);
            break;
        }
        default:
            out += pad + "Expr " + exprText(static_cast<const Expr*>(n)) + "\n";
            break;
    }
}

bool equalNameLists(const NameList& a, const NameList& b) {
    return a.names == b.names;
}

bool equalTypes(const TypeRef* a, const TypeRef* b) {
    if (!a || !b) return a == b;
    return a->base == b->base && a->geoKind == b->geoKind && equalTypes(a->arg, b->arg) &&
           a->enumValues == b->enumValues && a->optional == b->optional && a->list == b->list;
}

bool equalPaths(const std::vector<PathElem>& a, const std::vector<PathElem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].isIndex != b[i].isIndex || a[i].name != b[i].name || a[i].index != b[i].index)
            return false;
    }
    return true;
}

bool equalContracts(const ContractStmt* a, const ContractStmt* b) {
    return a->kind == b->kind && a->attrForm == b->attrForm && a->ident == b->ident &&
           astEqual(a->attr, b->attr) && astEqual(a->cond, b->cond) &&
           a->message == b->message && a->hasMessage == b->hasMessage;
}

}  // namespace

bool astEqual(const Node* a, const Node* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case NodeKind::File: {
            const auto* fa = static_cast<const File*>(a);
            const auto* fb = static_cast<const File*>(b);
            if (fa->items.size() != fb->items.size()) return false;
            for (size_t i = 0; i < fa->items.size(); ++i)
                if (!astEqual(fa->items[i], fb->items[i])) return false;
            return true;
        }
        case NodeKind::Import: {
            const auto* ia = static_cast<const Import*>(a);
            const auto* ib = static_cast<const Import*>(b);
            return ia->path == ib->path && ia->alias == ib->alias && ia->hasAlias == ib->hasAlias &&
                   ia->version == ib->version && ia->hasVersion == ib->hasVersion;
        }
        case NodeKind::ParamDecl: {
            const auto* pa = static_cast<const ParamDecl*>(a);
            const auto* pb = static_cast<const ParamDecl*>(b);
            return pa->name == pb->name && equalTypes(pa->type, pb->type) &&
                   pa->hasDefault == pb->hasDefault && astEqual(pa->def, pb->def);
        }
        case NodeKind::OutputDecl:
            return static_cast<const OutputDecl*>(a)->name == static_cast<const OutputDecl*>(b)->name;
        case NodeKind::Def: {
            const auto* da = static_cast<const Def*>(a);
            const auto* db = static_cast<const Def*>(b);
            if (da->name != db->name || da->hasDoc != db->hasDoc || da->docstring != db->docstring ||
                da->params.size() != db->params.size() || da->outputs.size() != db->outputs.size() ||
                da->expects.size() != db->expects.size() || da->body.size() != db->body.size() ||
                da->ensures.size() != db->ensures.size())
                return false;
            for (size_t i = 0; i < da->params.size(); ++i) {
                const DefParam& pa = da->params[i];
                const DefParam& pb = db->params[i];
                if (pa.name != pb.name || pa.hasDefault != pb.hasDefault ||
                    !equalTypes(pa.type, pb.type) || !astEqual(pa.def, pb.def))
                    return false;
            }
            for (size_t i = 0; i < da->outputs.size(); ++i) {
                if (da->outputs[i].name != db->outputs[i].name ||
                    !equalTypes(da->outputs[i].type, db->outputs[i].type))
                    return false;
            }
            for (size_t i = 0; i < da->expects.size(); ++i)
                if (!equalContracts(da->expects[i], db->expects[i])) return false;
            for (size_t i = 0; i < da->body.size(); ++i)
                if (!astEqual(da->body[i], db->body[i])) return false;
            for (size_t i = 0; i < da->ensures.size(); ++i)
                if (!equalContracts(da->ensures[i], db->ensures[i])) return false;
            return true;
        }
        case NodeKind::Binding: {
            const auto* ba = static_cast<const Binding*>(a);
            const auto* bb = static_cast<const Binding*>(b);
            return equalNameLists(ba->targets, bb->targets) && astEqual(ba->value, bb->value);
        }
        case NodeKind::Tap: {
            const auto* ta = static_cast<const Tap*>(a);
            const auto* tb = static_cast<const Tap*>(b);
            return ta->label == tb->label && ta->hasLabel == tb->hasLabel &&
                   equalPaths(ta->path, tb->path);
        }
        case NodeKind::Expect:
        case NodeKind::Ensure:
            return equalContracts(static_cast<const ContractStmt*>(a), static_cast<const ContractStmt*>(b));
        case NodeKind::RepeatZone: {
            const auto* ra = static_cast<const RepeatZone*>(a);
            const auto* rb = static_cast<const RepeatZone*>(b);
            if (!equalNameLists(ra->targets, rb->targets) || !equalNameLists(ra->state, rb->state) ||
                !astEqual(ra->value, rb->value) || !astEqual(ra->iterations, rb->iterations) ||
                ra->body.size() != rb->body.size())
                return false;
            for (size_t i = 0; i < ra->body.size(); ++i)
                if (!astEqual(ra->body[i], rb->body[i])) return false;
            return true;
        }
        case NodeKind::ForeachZone: {
            const auto* fa = static_cast<const ForeachZone*>(a);
            const auto* fb = static_cast<const ForeachZone*>(b);
            if (fa->target != fb->target || fa->item != fb->item ||
                !astEqual(fa->collection, fb->collection) || fa->body.size() != fb->body.size())
                return false;
            for (size_t i = 0; i < fa->body.size(); ++i)
                if (!astEqual(fa->body[i], fb->body[i])) return false;
            return true;
        }
        case NodeKind::NumberLit: {
            const auto* na = static_cast<const NumberLit*>(a);
            const auto* nb = static_cast<const NumberLit*>(b);
            return na->text == nb->text && na->isFloat == nb->isFloat;
        }
        case NodeKind::StringLit:
            return static_cast<const StringLit*>(a)->value == static_cast<const StringLit*>(b)->value;
        case NodeKind::BoolLit:
            return static_cast<const BoolLit*>(a)->value == static_cast<const BoolLit*>(b)->value;
        case NodeKind::NoneLit:
            return true;
        case NodeKind::EnumLit:
            return static_cast<const EnumLit*>(a)->name == static_cast<const EnumLit*>(b)->name;
        case NodeKind::Ident:
            return static_cast<const Ident*>(a)->name == static_cast<const Ident*>(b)->name;
        case NodeKind::AttrRef:
            return static_cast<const AttrRef*>(a)->name == static_cast<const AttrRef*>(b)->name;
        case NodeKind::VecLit: {
            const auto* va = static_cast<const VecLit*>(a);
            const auto* vb = static_cast<const VecLit*>(b);
            if (va->elems.size() != vb->elems.size()) return false;
            for (size_t i = 0; i < va->elems.size(); ++i)
                if (!astEqual(va->elems[i], vb->elems[i])) return false;
            return true;
        }
        case NodeKind::ListLit: {
            const auto* va = static_cast<const ListLit*>(a);
            const auto* vb = static_cast<const ListLit*>(b);
            if (va->elems.size() != vb->elems.size()) return false;
            for (size_t i = 0; i < va->elems.size(); ++i)
                if (!astEqual(va->elems[i], vb->elems[i])) return false;
            return true;
        }
        case NodeKind::Paren:
            return astEqual(static_cast<const Paren*>(a)->inner, static_cast<const Paren*>(b)->inner);
        case NodeKind::Unary: {
            const auto* ua = static_cast<const Unary*>(a);
            const auto* ub = static_cast<const Unary*>(b);
            return ua->op == ub->op && astEqual(ua->operand, ub->operand);
        }
        case NodeKind::Binary: {
            const auto* ba = static_cast<const Binary*>(a);
            const auto* bb = static_cast<const Binary*>(b);
            return ba->op == bb->op && astEqual(ba->lhs, bb->lhs) && astEqual(ba->rhs, bb->rhs);
        }
        case NodeKind::Ternary: {
            const auto* ta = static_cast<const Ternary*>(a);
            const auto* tb = static_cast<const Ternary*>(b);
            return astEqual(ta->cond, tb->cond) && astEqual(ta->thenExpr, tb->thenExpr) &&
                   astEqual(ta->elseExpr, tb->elseExpr);
        }
        case NodeKind::Call: {
            const auto* ca = static_cast<const Call*>(a);
            const auto* cb = static_cast<const Call*>(b);
            if (ca->path != cb->path || ca->args.size() != cb->args.size()) return false;
            for (size_t i = 0; i < ca->args.size(); ++i) {
                if (ca->args[i].name != cb->args[i].name || ca->args[i].hasName != cb->args[i].hasName ||
                    !astEqual(ca->args[i].value, cb->args[i].value))
                    return false;
            }
            return true;
        }
        case NodeKind::ErrorExpr:
            return true;
        case NodeKind::TypeRef:
            return equalTypes(static_cast<const TypeRef*>(a), static_cast<const TypeRef*>(b));
    }
    return false;
}

std::string dumpAst(const Node* node) {
    std::string out;
    dumpNode(node, out, 0);
    return out;
}

}  // namespace pgg
