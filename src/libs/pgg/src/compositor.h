#pragma once

// Grammar compositor (spec §13.2): the only place where parse actions
// assemble the AST. Two data channels:
//   - fixed-arity factories  (semantic values: expressions, types, headers)
//   - frames                 (open bodies: file, def, repeat/foreach zones)
// Also owns the node arena and the syntax-level diagnostics sink (E100/E101
// for relaxed tokens and frame imbalance). No semantic (E2xx+) validation
// here — that is a separate pass over the finished AST.
// Plain C++: unit-tested directly, without ANTLR.

#include "ast.h"

namespace antlr4 {
class Token;
}

namespace pgg {

class GrammarCompositor {
public:
    GrammarCompositor() = default;

    // --- arena -------------------------------------------------------------

    template<typename T, typename... Args>
    T* make(Args&&... args) {
        auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = ptr.get();
        arena_.push_back(std::move(ptr));
        return raw;
    }

    std::vector<std::unique_ptr<Node>> takeArena() { return std::move(arena_); }

    // Extracts `->result` of every context in an ANTLR `+=` label list.
    template<typename CtxT>
    static auto resultsOf(const std::vector<CtxT*>& ctxs) {
        using R = std::decay_t<decltype(ctxs.front()->result)>;
        std::vector<R> out;
        out.reserve(ctxs.size());
        for (const CtxT* c : ctxs) out.push_back(c->result);
        return out;
    }

    static std::vector<std::string> namesOf(const std::vector<antlr4::Token*>& toks);
    static NameList nameListOf(const std::vector<antlr4::Token*>& toks);

    // --- diagnostics sink ---------------------------------------------------

    std::vector<Diagnostic>& diagnostics() { return diags_; }
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }
    void syntaxError(Span, std::string message, std::string hint = {});
    void checkKeyword(const antlr4::Token* tok, const char* expected);

    static std::string textOf(const antlr4::Token* tok);
    static Span spanOfTok(const antlr4::Token* tok);

    // --- fixed-arity channel -------------------------------------------------

    Expr* newNumber(const std::string& text, Span);
    Expr* newString(const std::string& raw, Span);  // raw token text, quotes included
    Expr* newTripleString(const std::string& raw, Span);
    Expr* newBool(const std::string& text, Span);
    Expr* newNone(Span);
    Expr* newEnumLit(const std::string& name, Span);
    Expr* newIdent(const std::string& name, Span);
    Expr* newAttr(const std::string& name, Span);
    Expr* newVec(std::vector<Expr*> elems, Span);
    Expr* newNumberVec(const std::vector<antlr4::Token*>& numbers, Span);
    Expr* newParen(Expr* inner, Span);
    Expr* newUnary(std::string op, Expr* operand, Span);
    Expr* newBinary(std::string op, Expr* lhs, Expr* rhs, Span);
    Expr* newTernary(Expr* cond, Expr* thenExpr, Expr* elseExpr, Span);
    Expr* newError(Span);
    Expr* newCall(std::vector<std::string> path, std::vector<CallArg> args, Span);

    TypeRef* newTypeName(const std::string& base, Span);
    TypeRef* newTypeGeneric(const std::string& base, Span baseSpan, TypeRef* arg, Span);
    TypeRef* newTypeEnum(const antlr4::Token* kw, std::vector<std::string> values, Span);
    TypeRef* typeOptional(TypeRef*, Span);
    TypeRef* typeList(TypeRef*, Span);

    Stmt* newBinding(NameList targets, Expr* value, Span);
    Stmt* newTap(std::string label, bool hasLabel, std::vector<PathElem> path, Span);
    ContractStmt* newContract(NodeKind kind, bool attrForm, std::string ident, Expr* attr,
                              Expr* cond, std::string message, bool hasMessage, Span);
    PathElem pathName(const std::string& name);
    PathElem pathIndex(const std::string& numberText);

    Import* newImport(std::vector<std::string> path, const antlr4::Token* aliasTok,
                      const antlr4::Token* versionTok, Span);
    ParamDecl* newParam(const std::string& name, Span nameSpan, TypeRef* type, Expr* def,
                        bool hasDefault, Span);
    OutputDecl* newOutput(const std::string& name, Span nameSpan, Span);
    DefParam newDefParam(std::string name, TypeRef* type, Expr* def, bool hasDefault);
    OutDecl newOutDecl(std::string name, TypeRef* type);
    std::string stringValue(const std::string& raw, Span);  // decoded STRING literal

    // --- frame channel -------------------------------------------------------

    void beginFile();
    File* endFile(Span);

    void beginDef(std::string name, Span nameSpan, std::vector<DefParam> params,
                  std::vector<OutDecl> outputs, Span openSpan);
    void defDoc(const std::string& rawTriple, Span);
    void addExpect(ContractStmt*);
    void addEnsure(ContractStmt*);
    Def* endDef(Span);

    void beginRepeat(NameList targets, Expr* value, Expr* iterations, NameList state,
                     Span openSpan);
    Stmt* endRepeat(Span);

    void beginForeach(std::string target, Span targetSpan, std::string item, Span itemSpan,
                      Expr* collection, Span openSpan);
    Stmt* endForeach(Span);

    void addNode(Node*);  // body element into the top frame (nullptr is skipped)

private:
    struct Frame {
        NodeKind kind;
        Span openSpan;
        std::vector<Node*> nodes;
        // def
        std::string name;
        Span nameSpan{};
        std::vector<DefParam> params;
        std::vector<OutDecl> outputs;
        std::string doc;
        bool hasDoc = false;
        std::vector<ContractStmt*> expects;
        std::vector<ContractStmt*> ensures;
        // repeat
        NameList targets;
        Expr* value = nullptr;
        Expr* iterations = nullptr;
        NameList state;
        // foreach
        std::string item;
        Span itemSpan{};
        Expr* collection = nullptr;
    };

    Frame popTo(NodeKind kind, Span endSpan);
    std::string decodeString(const std::string& raw, Span);

    std::vector<std::unique_ptr<Node>> arena_;
    std::vector<Diagnostic> diags_;
    std::vector<Frame> frames_;
};

}  // namespace pgg
