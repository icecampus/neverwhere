#include "pch.h"

#include "compositor.h"

#include <antlr4-runtime.h>

namespace pgg {

// --- token helpers -----------------------------------------------------------

std::string GrammarCompositor::textOf(const antlr4::Token* tok) {
    return tok ? tok->getText() : std::string();
}

Span GrammarCompositor::spanOfTok(const antlr4::Token* tok) {
    Span s{};
    if (!tok) return s;
    s.line = static_cast<int32_t>(tok->getLine());
    s.col = static_cast<int32_t>(tok->getCharPositionInLine());
    s.endLine = s.line;
    s.endCol = s.col + static_cast<int32_t>(tok->getText().size());
    return s;
}

std::vector<std::string> GrammarCompositor::namesOf(const std::vector<antlr4::Token*>& toks) {
    std::vector<std::string> out;
    out.reserve(toks.size());
    for (const antlr4::Token* t : toks) out.push_back(textOf(t));
    return out;
}

NameList GrammarCompositor::nameListOf(const std::vector<antlr4::Token*>& toks) {
    NameList out;
    out.names.reserve(toks.size());
    out.spans.reserve(toks.size());
    for (const antlr4::Token* t : toks) {
        out.names.push_back(textOf(t));
        out.spans.push_back(spanOfTok(t));
    }
    return out;
}

// --- diagnostics --------------------------------------------------------------

void GrammarCompositor::syntaxError(Span span, std::string message, std::string hint) {
    diags_.push_back(Diagnostic{"E100", span, std::move(message), std::move(hint), false});
}

void GrammarCompositor::checkKeyword(const antlr4::Token* tok, const char* expected) {
    const std::string text = textOf(tok);
    if (text != expected) {
        syntaxError(spanOfTok(tok), std::string("expected '") + expected + "', got '" + text + "'");
    }
}

// --- literal decoding -----------------------------------------------------------

std::string GrammarCompositor::decodeString(const std::string& raw, Span span) {
    // raw includes surrounding double quotes
    if (raw.size() < 2) return {};
    const std::string body = raw.substr(1, raw.size() - 2);
    std::string out;
    out.reserve(body.size());
    for (size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= body.size()) {
            syntaxError(span, "dangling escape at end of string");
            break;
        }
        const char e = body[++i];
        switch (e) {
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            case 'n': out.push_back('\n'); break;
            default:
                syntaxError(span, std::string("unknown escape '\\") + e + "'",
                            "supported escapes: \\\\ \\\" \\n");
                out.push_back(e);
        }
    }
    return out;
}

std::string GrammarCompositor::stringValue(const std::string& raw, Span span) {
    return decodeString(raw, span);
}

// --- expressions -----------------------------------------------------------------

Expr* GrammarCompositor::newNumber(const std::string& text, Span span) {
    auto* n = make<NumberLit>(span);
    n->text = text;
    n->isFloat = text.find_first_of(".eE") != std::string::npos;
    return n;
}

Expr* GrammarCompositor::newString(const std::string& raw, Span span) {
    auto* n = make<StringLit>(span);
    n->value = decodeString(raw, span);
    return n;
}

Expr* GrammarCompositor::newTripleString(const std::string& raw, Span span) {
    auto* n = make<StringLit>(span);
    if (raw.size() >= 6) n->value = raw.substr(3, raw.size() - 6);
    return n;
}

Expr* GrammarCompositor::newBool(const std::string& text, Span span) {
    auto* n = make<BoolLit>(span);
    n->value = text == "true";
    return n;
}

Expr* GrammarCompositor::newNone(Span span) {
    return make<NoneLit>(span);
}

Expr* GrammarCompositor::newEnumLit(const std::string& name, Span span) {
    auto* n = make<EnumLit>(span);
    n->name = name;
    return n;
}

Expr* GrammarCompositor::newIdent(const std::string& name, Span span) {
    auto* n = make<Ident>(span);
    n->name = name;
    return n;
}

Expr* GrammarCompositor::newAttr(const std::string& name, Span span) {
    auto* n = make<AttrRef>(span);
    n->name = name;
    return n;
}

Expr* GrammarCompositor::newVec(std::vector<Expr*> elems, Span span) {
    auto* n = make<VecLit>(span);
    n->elems.reserve(elems.size());
    for (Expr* e : elems) n->elems.push_back(e ? e : newError(span));
    return n;
}

Expr* GrammarCompositor::newNumberVec(const std::vector<antlr4::Token*>& numbers, Span span) {
    std::vector<Expr*> elems;
    elems.reserve(numbers.size());
    for (const antlr4::Token* t : numbers) elems.push_back(newNumber(textOf(t), spanOfTok(t)));
    return newVec(std::move(elems), span);
}

Expr* GrammarCompositor::newList(std::vector<Expr*> elems, Span span) {
    auto* n = make<ListLit>(span);
    n->elems.reserve(elems.size());
    for (Expr* e : elems) n->elems.push_back(e ? e : newError(span));
    return n;
}

Expr* GrammarCompositor::newParen(Expr* inner, Span span) {
    auto* n = make<Paren>(span);
    n->inner = inner ? inner : newError(span);
    return n;
}

Expr* GrammarCompositor::newUnary(std::string op, Expr* operand, Span span) {
    auto* n = make<Unary>(span);
    n->op = std::move(op);
    n->operand = operand ? operand : newError(span);
    return n;
}

Expr* GrammarCompositor::newBinary(std::string op, Expr* lhs, Expr* rhs, Span span) {
    auto* n = make<Binary>(span);
    n->op = std::move(op);
    n->lhs = lhs ? lhs : newError(span);
    n->rhs = rhs ? rhs : newError(span);
    return n;
}

Expr* GrammarCompositor::newTernary(Expr* cond, Expr* thenExpr, Expr* elseExpr, Span span) {
    auto* n = make<Ternary>(span);
    n->cond = cond ? cond : newError(span);
    n->thenExpr = thenExpr ? thenExpr : newError(span);
    n->elseExpr = elseExpr ? elseExpr : newError(span);
    return n;
}

Expr* GrammarCompositor::newError(Span span) {
    return make<ErrorExpr>(span);
}

Expr* GrammarCompositor::newCall(std::vector<std::string> path, std::vector<CallArg> args,
                                 Span span) {
    auto* n = make<Call>(span);
    n->path = std::move(path);
    n->args = std::move(args);
    for (CallArg& a : n->args)
        if (!a.value) a.value = newError(span);
    return n;
}

// --- types -------------------------------------------------------------------------

namespace {
const char* kGeoKinds[] = {"mesh", "points", "curve", "instances"};

bool isOneOf(const std::string& s, const char* const* list, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (s == list[i]) return true;
    return false;
}
}  // namespace

TypeRef* GrammarCompositor::newTypeName(const std::string& base, Span span) {
    // Base names are intentionally not diagnosed here: keywords were relaxed
    // to IDENT, so geo kinds (mesh/points/...) arrive through this path too
    // and are narrowed by newTypeGeneric. Unknown type names are reported by
    // the validator pass over the finished AST.
    auto* t = make<TypeRef>(span);
    t->base = base;
    return t;
}

TypeRef* GrammarCompositor::newTypeGeneric(const std::string& base, Span baseSpan, TypeRef* arg,
                                           Span span) {
    auto* t = make<TypeRef>(span);
    t->base = base;
    if (base == "field") {
        t->arg = arg;
    } else if (base == "geo") {
        // geo<K>: K is parsed as a type name and narrowed here
        if (arg && arg->kind == NodeKind::TypeRef && arg->arg == nullptr &&
            isOneOf(arg->base, kGeoKinds, std::size(kGeoKinds))) {
            t->geoKind = arg->base;
        } else {
            syntaxError(baseSpan, "geo<K> expects K in {mesh, points, curve, instances}");
        }
    } else {
        syntaxError(baseSpan, "unknown generic type '" + base + "'",
                    "expected field<T> or geo<K>");
    }
    return t;
}

TypeRef* GrammarCompositor::newTypeEnum(const antlr4::Token* kw,
                                        std::vector<std::string> values, Span span) {
    checkKeyword(kw, "enum");
    auto* t = make<TypeRef>(span);
    t->base = "enum";
    t->enumValues = std::move(values);
    return t;
}

TypeRef* GrammarCompositor::typeOptional(TypeRef* t, Span span) {
    if (!t) return nullptr;
    t->optional = true;
    t->span = span;
    return t;
}

TypeRef* GrammarCompositor::typeList(TypeRef* t, Span span) {
    if (!t) return nullptr;
    t->list = true;
    t->span = span;
    return t;
}

// --- statements ---------------------------------------------------------------------

Stmt* GrammarCompositor::newBinding(NameList targets, Expr* value, Span span) {
    auto* n = make<Binding>(span);
    n->targets = std::move(targets);
    n->value = value ? value : newError(span);
    return n;
}

Stmt* GrammarCompositor::newTap(std::string label, bool hasLabel, std::vector<PathElem> path,
                                Span span) {
    auto* n = make<Tap>(span);
    n->label = std::move(label);
    n->hasLabel = hasLabel;
    n->path = std::move(path);
    return n;
}

ContractStmt* GrammarCompositor::newContract(NodeKind kind, bool attrForm, std::string ident,
                                             Expr* attr, Expr* cond, std::string message,
                                             bool hasMessage, Span span) {
    auto* n = make<ContractStmt>(span, kind);
    n->attrForm = attrForm;
    n->ident = std::move(ident);
    n->attr = attr ? attr : newError(span);
    n->cond = cond ? cond : newError(span);
    n->message = std::move(message);
    n->hasMessage = hasMessage;
    return n;
}

PathElem GrammarCompositor::pathName(const std::string& name) {
    PathElem el;
    el.name = name;
    el.isIndex = false;
    return el;
}

PathElem GrammarCompositor::pathIndex(const std::string& numberText) {
    PathElem el;
    el.isIndex = true;
    el.index = std::atoi(numberText.c_str());
    return el;
}

// --- top-level items ------------------------------------------------------------------

Import* GrammarCompositor::newImport(std::vector<std::string> path, const antlr4::Token* aliasTok,
                                     const antlr4::Token* versionTok, Span span) {
    auto* n = make<Import>(span);
    n->path = std::move(path);
    n->alias = textOf(aliasTok);
    n->hasAlias = aliasTok != nullptr;
    n->version = textOf(versionTok);
    n->hasVersion = versionTok != nullptr;
    return n;
}

ParamDecl* GrammarCompositor::newParam(const std::string& name, Span nameSpan, TypeRef* type,
                                       Expr* def, bool hasDefault, Span span) {
    auto* n = make<ParamDecl>(span);
    (void)nameSpan;  // the decl span already covers the name
    n->name = name;
    n->type = type;
    n->def = def;
    n->hasDefault = hasDefault;
    return n;
}

OutputDecl* GrammarCompositor::newOutput(const std::string& name, Span nameSpan, Span span) {
    auto* n = make<OutputDecl>(span);
    (void)nameSpan;
    n->name = name;
    return n;
}

DefParam GrammarCompositor::newDefParam(std::string name, TypeRef* type, Expr* def,
                                        bool hasDefault) {
    DefParam p;
    p.name = std::move(name);
    p.type = type;
    p.def = def;
    p.hasDefault = hasDefault;
    return p;
}

OutDecl GrammarCompositor::newOutDecl(std::string name, TypeRef* type) {
    OutDecl o;
    o.name = std::move(name);
    o.type = type;
    return o;
}

// --- frames -----------------------------------------------------------------------------

void GrammarCompositor::beginFile() {
    Frame f;
    f.kind = NodeKind::File;
    frames_.push_back(std::move(f));
}

File* GrammarCompositor::endFile(Span span) {
    Frame f = popTo(NodeKind::File, span);
    auto* file = make<File>(span);
    file->items = std::move(f.nodes);
    return file;
}

void GrammarCompositor::beginDef(std::string name, Span nameSpan, std::vector<DefParam> params,
                                 std::vector<OutDecl> outputs, Span openSpan) {
    Frame f;
    f.kind = NodeKind::Def;
    f.openSpan = openSpan;
    f.name = std::move(name);
    f.nameSpan = nameSpan;
    f.params = std::move(params);
    f.outputs = std::move(outputs);
    frames_.push_back(std::move(f));
}

void GrammarCompositor::defDoc(const std::string& rawTriple, Span span) {
    if (frames_.empty() || frames_.back().kind != NodeKind::Def) {
        syntaxError(span, "docstring outside def");
        return;
    }
    Frame& f = frames_.back();
    if (f.hasDoc) {
        syntaxError(span, "duplicate docstring");
        return;
    }
    if (rawTriple.size() >= 6) f.doc = rawTriple.substr(3, rawTriple.size() - 6);
    f.hasDoc = true;
}

void GrammarCompositor::addExpect(ContractStmt* e) {
    if (!e) return;
    if (frames_.empty() || frames_.back().kind != NodeKind::Def) {
        syntaxError(e->span, "expect outside def");
        return;
    }
    Frame& f = frames_.back();
    if (!f.nodes.empty() || !f.ensures.empty()) {
        syntaxError(e->span, "expect must precede the def body");
        return;
    }
    f.expects.push_back(e);
}

void GrammarCompositor::addEnsure(ContractStmt* e) {
    if (!e) return;
    if (frames_.empty() || frames_.back().kind != NodeKind::Def) {
        syntaxError(e->span, "ensure outside def");
        return;
    }
    frames_.back().ensures.push_back(e);
}

Def* GrammarCompositor::endDef(Span span) {
    Frame f = popTo(NodeKind::Def, span);
    auto* d = make<Def>(span);
    d->name = std::move(f.name);
    d->params = std::move(f.params);
    d->outputs = std::move(f.outputs);
    d->docstring = std::move(f.doc);
    d->hasDoc = f.hasDoc;
    d->expects = std::move(f.expects);
    d->ensures = std::move(f.ensures);
    d->body.reserve(f.nodes.size());
    for (Node* n : f.nodes) d->body.push_back(static_cast<Stmt*>(n));
    return d;
}

void GrammarCompositor::beginRepeat(NameList targets, Expr* value, Expr* iterations,
                                    NameList state, Span openSpan) {
    Frame f;
    f.kind = NodeKind::RepeatZone;
    f.openSpan = openSpan;
    f.targets = std::move(targets);
    f.value = value;
    f.iterations = iterations;
    f.state = std::move(state);
    frames_.push_back(std::move(f));
}

Stmt* GrammarCompositor::endRepeat(Span span) {
    Frame f = popTo(NodeKind::RepeatZone, span);
    auto* z = make<RepeatZone>(span);
    z->targets = std::move(f.targets);
    z->value = f.value ? f.value : newError(span);
    z->iterations = f.iterations ? f.iterations : newError(span);
    z->state = std::move(f.state);
    z->body.reserve(f.nodes.size());
    for (Node* n : f.nodes) z->body.push_back(static_cast<Stmt*>(n));
    return z;
}

void GrammarCompositor::beginForeach(std::string target, Span targetSpan, std::string item,
                                     Span itemSpan, Expr* collection, Span openSpan) {
    Frame f;
    f.kind = NodeKind::ForeachZone;
    f.openSpan = openSpan;
    f.name = std::move(target);
    f.nameSpan = targetSpan;
    f.item = std::move(item);
    f.itemSpan = itemSpan;
    f.collection = collection;
    frames_.push_back(std::move(f));
}

Stmt* GrammarCompositor::endForeach(Span span) {
    Frame f = popTo(NodeKind::ForeachZone, span);
    auto* z = make<ForeachZone>(span);
    z->target = std::move(f.name);
    z->targetSpan = f.nameSpan;
    z->item = std::move(f.item);
    z->itemSpan = f.itemSpan;
    z->collection = f.collection ? f.collection : newError(span);
    z->body.reserve(f.nodes.size());
    for (Node* n : f.nodes) z->body.push_back(static_cast<Stmt*>(n));
    return z;
}

void GrammarCompositor::addNode(Node* n) {
    if (!n || frames_.empty()) return;
    frames_.back().nodes.push_back(n);
}

GrammarCompositor::Frame GrammarCompositor::popTo(NodeKind kind, Span endSpan) {
    // Recovery tolerance (spec §13.2): ANTLR error recovery may skip an endX
    // trigger, leaving a frame unclosed. Stray frames above the requested one
    // are discarded with a diagnostic instead of corrupting the parents.
    while (!frames_.empty() && frames_.back().kind != kind) {
        const Frame& stray = frames_.back();
        syntaxError(stray.openSpan, "unclosed block discarded during recovery",
                    "the construct opened here never saw its closing brace");
        frames_.pop_back();
    }
    if (frames_.empty()) {
        syntaxError(endSpan, "internal: end without matching begin");
        Frame f;
        f.kind = kind;
        f.openSpan = endSpan;
        return f;
    }
    Frame f = std::move(frames_.back());
    frames_.pop_back();
    return f;
}

}  // namespace pgg
