#include "pch.h"

#include "docs_lookup.h"

#include <filesystem>
#include <sstream>

#include "modules.h"

namespace {

std::string typeRefText(const pgg::TypeRef* t) {
    if (!t) return "?";
    std::string out = t->base;
    if (t->base == "geo" && !t->geoKind.empty()) out += "<" + t->geoKind + ">";
    if (t->base == "field" && t->arg) out = "field<" + typeRefText(t->arg) + ">";
    if (t->base == "enum" && !t->enumValues.empty()) {
        out = "enum {";
        for (size_t i = 0; i < t->enumValues.size(); ++i) out += (i ? ", " : "") + t->enumValues[i];
        out += "}";
    }
    if (t->optional) out += "?";
    if (t->list) out += "[]";
    return out;
}

// Minimal expression rendering for signature defaults (literals and simple
// arithmetic is all the grammar allows there).
std::string exprText(const pgg::Expr* e) {
    if (!e) return "?";
    switch (e->kind) {
        case pgg::NodeKind::NumberLit: return static_cast<const pgg::NumberLit*>(e)->text;
        case pgg::NodeKind::StringLit:
            return "\"" + static_cast<const pgg::StringLit*>(e)->value + "\"";
        case pgg::NodeKind::BoolLit:
            return static_cast<const pgg::BoolLit*>(e)->value ? "true" : "false";
        case pgg::NodeKind::NoneLit: return "none";
        case pgg::NodeKind::EnumLit: return static_cast<const pgg::EnumLit*>(e)->name;
        case pgg::NodeKind::Ident: return static_cast<const pgg::Ident*>(e)->name;
        case pgg::NodeKind::AttrRef: return "@" + static_cast<const pgg::AttrRef*>(e)->name;
        case pgg::NodeKind::VecLit: {
            std::string out = "(";
            const auto* v = static_cast<const pgg::VecLit*>(e);
            for (size_t i = 0; i < v->elems.size(); ++i) out += (i ? ", " : "") + exprText(v->elems[i]);
            return out + ")";
        }
        case pgg::NodeKind::ListLit: {
            std::string out = "[";
            const auto* l = static_cast<const pgg::ListLit*>(e);
            for (size_t i = 0; i < l->elems.size(); ++i) out += (i ? ", " : "") + exprText(l->elems[i]);
            return out + "]";
        }
        case pgg::NodeKind::Paren: return "(" + exprText(static_cast<const pgg::Paren*>(e)->inner) + ")";
        case pgg::NodeKind::Unary: {
            const auto* u = static_cast<const pgg::Unary*>(e);
            return u->op + exprText(u->operand);
        }
        case pgg::NodeKind::Binary: {
            const auto* b = static_cast<const pgg::Binary*>(e);
            return exprText(b->lhs) + " " + b->op + " " + exprText(b->rhs);
        }
        case pgg::NodeKind::Ternary: {
            const auto* t = static_cast<const pgg::Ternary*>(e);
            return exprText(t->cond) + " ? " + exprText(t->thenExpr) + " : " + exprText(t->elseExpr);
        }
        case pgg::NodeKind::Call: {
            const auto* c = static_cast<const pgg::Call*>(e);
            std::string out;
            for (const std::string& p : c->path) out += (out.empty() ? "" : ".") + p;
            out += "(";
            for (size_t i = 0; i < c->args.size(); ++i) {
                out += i ? ", " : "";
                if (c->args[i].hasName) out += c->args[i].name + " = ";
                out += exprText(c->args[i].value);
            }
            return out + ")";
        }
        default: return "?";
    }
}

}  // namespace

namespace pgg {

std::string signatureText(const Def* d) {
    std::string out = "def " + d->name + "(";
    for (size_t i = 0; i < d->params.size(); ++i) {
        const DefParam& p = d->params[i];
        out += (i ? ", " : "") + p.name + ": " + typeRefText(p.type);
        if (p.hasDefault) out += " = " + exprText(p.def);
    }
    out += ") -> (";
    for (size_t i = 0; i < d->outputs.size(); ++i) {
        const OutDecl& o = d->outputs[i];
        out += (i ? ", " : "") + o.name + ": " + typeRefText(o.type);
    }
    return out + ")";
}

std::string dedentDocstring(const std::string& docstring) {
    std::stringstream ss(docstring);
    std::string line;
    std::string out;
    while (std::getline(ss, line)) {
        const size_t first = line.find_first_not_of(" \t");
        out += first == std::string::npos ? "" : line.substr(first);
        out += "\n";
    }
    if (!out.empty()) out.pop_back();  // no trailing newline, mirror printDocCard
    return out;
}

DocsLookupResult findDef(const File& mainFile, const std::string& filePath,
                         const std::string& symbol, const std::vector<std::string>& importRoots) {
    DocsLookupResult result;
    const Def* found = nullptr;
    // Function scope on purpose: a qualified lookup's Def is owned by this
    // closure, so it must outlive the materialization below (an else-scope
    // local would dangle — the pre-refactor PggTool docs had that bug).
    ModuleClosure closure;
    const size_t dot = symbol.rfind('.');
    if (dot == std::string::npos) {
        for (const Node* item : mainFile.items) {
            if (item->kind != NodeKind::Def) continue;
            const auto* d = static_cast<const Def*>(item);
            if (d->name == symbol) found = d;
        }
        if (!found) result.error = "no def '" + symbol + "' in " + filePath;
    } else {
        // Qualified symbol: resolve through the file's import closure (same
        // roots as a run: importRoots + the file's own directory).
        const std::string ns = symbol.substr(0, dot);
        const std::string name = symbol.substr(dot + 1);
        std::vector<std::string> roots = importRoots;
        const std::string dir = std::filesystem::path(filePath).parent_path().string();
        if (!dir.empty()) roots.push_back(dir);
        closure = loadModuleClosure(mainFile, roots, result.diagnostics);
        bool errors = false;
        for (const Diagnostic& d : result.diagnostics) errors = errors || !d.isWarning;
        if (errors) return result;
        if (auto it = closure.mainNamespaces.find(ns); it != closure.mainNamespaces.end()) {
            if (auto d = it->second->defs.find(name); d != it->second->defs.end()) found = d->second;
        }
        if (!found) result.error = "unknown qualified symbol '" + symbol + "' (E505)";
    }
    if (found) {
        result.found = true;
        result.signature = signatureText(found);
        result.hasDoc = found->hasDoc;
        if (found->hasDoc) result.docstring = dedentDocstring(found->docstring);
    }
    return result;
}

}  // namespace pgg
