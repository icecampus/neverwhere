#pragma once

// PGG abstract syntax tree (spec docs/pgg/geometry_generation_language.md §13).
// Nodes are owned by the arena in pgg::Document; all pointers are non-owning.
// Every node carries a source Span for diagnostics, provenance and the
// formatter. Structural comparison (astEqual) ignores spans — it backs the
// formatter round-trip criterion parse(format(x)) == parse(x).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pgg {

// 1-based lines, 0-based columns, end column exclusive (ANTLR convention).
struct Span {
    int32_t line = 0;
    int32_t col = 0;
    int32_t endLine = 0;
    int32_t endCol = 0;
};

enum class NodeKind {
    File,
    Import,
    ParamDecl,
    OutputDecl,
    Def,
    Binding,
    Tap,
    RepeatZone,
    ForeachZone,
    Expect,
    Ensure,
    NumberLit,
    StringLit,
    BoolLit,
    NoneLit,
    EnumLit,  // ident in literal position (enum/domain literal)
    Ident,
    AttrRef,  // @name
    VecLit,
    ListLit,  // [expr, ...]
    Paren,
    Unary,
    Binary,
    Ternary,
    Call,
    ErrorExpr,  // sentinel for unparseable subtrees (lint keeps going)
    TypeRef,
};

struct Node {
    const NodeKind kind;
    Span span;

    virtual ~Node() = default;

protected:
    Node(NodeKind k, Span s) : kind(k), span(s) {}
};

struct Expr : Node {
protected:
    using Node::Node;
};

struct Stmt : Node {
protected:
    using Node::Node;
};

// --- expressions -----------------------------------------------------------

struct NumberLit final : Expr {
    std::string text;  // as written
    bool isFloat = false;
    explicit NumberLit(Span s) : Expr(NodeKind::NumberLit, s) {}
};

struct StringLit final : Expr {
    std::string value;  // escapes decoded, quotes stripped
    explicit StringLit(Span s) : Expr(NodeKind::StringLit, s) {}
};

struct BoolLit final : Expr {
    bool value = false;
    explicit BoolLit(Span s) : Expr(NodeKind::BoolLit, s) {}
};

struct NoneLit final : Expr {
    explicit NoneLit(Span s) : Expr(NodeKind::NoneLit, s) {}
};

struct EnumLit final : Expr {
    std::string name;
    explicit EnumLit(Span s) : Expr(NodeKind::EnumLit, s) {}
};

struct Ident final : Expr {
    std::string name;
    explicit Ident(Span s) : Expr(NodeKind::Ident, s) {}
};

struct AttrRef final : Expr {
    std::string name;  // without '@'
    explicit AttrRef(Span s) : Expr(NodeKind::AttrRef, s) {}
};

struct VecLit final : Expr {
    std::vector<Expr*> elems;  // NumberLit only (spec: vec_literal is numeric)
    explicit VecLit(Span s) : Expr(NodeKind::VecLit, s) {}
};

struct ListLit final : Expr {
    std::vector<Expr*> elems;  // any expressions (spec §13: T[] list literal)
    explicit ListLit(Span s) : Expr(NodeKind::ListLit, s) {}
};

struct Paren final : Expr {
    Expr* inner = nullptr;
    explicit Paren(Span s) : Expr(NodeKind::Paren, s) {}
};

struct Unary final : Expr {
    std::string op;  // "-" | "!"
    Expr* operand = nullptr;
    explicit Unary(Span s) : Expr(NodeKind::Unary, s) {}
};

struct Binary final : Expr {
    std::string op;
    Expr* lhs = nullptr;
    Expr* rhs = nullptr;
    explicit Binary(Span s) : Expr(NodeKind::Binary, s) {}
};

struct Ternary final : Expr {
    Expr* cond = nullptr;
    Expr* thenExpr = nullptr;
    Expr* elseExpr = nullptr;
    explicit Ternary(Span s) : Expr(NodeKind::Ternary, s) {}
};

struct ErrorExpr final : Expr {
    explicit ErrorExpr(Span s) : Expr(NodeKind::ErrorExpr, s) {}
};

struct CallArg {
    std::string name;   // empty when positional
    bool hasName = false;
    Expr* value = nullptr;
};

struct Call final : Expr {
    std::vector<std::string> path;  // qualified name parts
    std::vector<CallArg> args;
    explicit Call(Span s) : Expr(NodeKind::Call, s) {}
};

// --- types -----------------------------------------------------------------

struct TypeRef final : Node {
    std::string base;    // sdf|rng|f32|int|bool|vec2|vec3|vec4|string|domain|geo|field|enum
    std::string geoKind; // geo<K>: mesh|points|curve|instances (empty = polymorphic geo)
    TypeRef* arg = nullptr;  // field<T>
    std::vector<std::string> enumValues;
    bool optional = false;   // T?
    bool list = false;       // T[]
    explicit TypeRef(Span s) : Node(NodeKind::TypeRef, s) {}
};

// --- statements ------------------------------------------------------------

struct NameList {
    std::vector<std::string> names;
    std::vector<Span> spans;
};

struct Binding final : Stmt {
    NameList targets;
    Expr* value = nullptr;
    explicit Binding(Span s) : Stmt(NodeKind::Binding, s) {}
};

struct PathElem {
    std::string name;      // when !isIndex
    int32_t index = -1;    // when isIndex
    bool isIndex = false;
};

struct Tap final : Stmt {
    std::string label;
    bool hasLabel = false;
    std::vector<PathElem> path;
    explicit Tap(Span s) : Stmt(NodeKind::Tap, s) {}
};

// expect/ensure share one shape; kind distinguishes them.
struct ContractStmt final : Stmt {
    bool attrForm = false;  // `ident has @attr` (true) vs condition aexpr (false)
    std::string ident;
    Expr* attr = nullptr;   // AttrRef
    Expr* cond = nullptr;
    std::string message;
    bool hasMessage = false;
    ContractStmt(Span s, NodeKind k) : Stmt(k, s) {}
};

struct RepeatZone final : Stmt {
    NameList targets;
    Expr* value = nullptr;
    Expr* iterations = nullptr;
    NameList state;
    std::vector<Stmt*> body;
    explicit RepeatZone(Span s) : Stmt(NodeKind::RepeatZone, s) {}
};

struct ForeachZone final : Stmt {
    std::string target;
    Span targetSpan;
    std::string item;  // loop variable
    Span itemSpan;
    Expr* collection = nullptr;
    std::vector<Stmt*> body;
    explicit ForeachZone(Span s) : Stmt(NodeKind::ForeachZone, s) {}
};

// --- top-level items -------------------------------------------------------

struct Import final : Node {
    std::vector<std::string> path;
    std::string alias;
    bool hasAlias = false;
    std::string version;  // as written, e.g. "1.2"
    bool hasVersion = false;
    explicit Import(Span s) : Node(NodeKind::Import, s) {}
};

struct ParamDecl final : Node {
    std::string name;
    TypeRef* type = nullptr;
    Expr* def = nullptr;  // literal only (spec §6.6)
    bool hasDefault = false;
    explicit ParamDecl(Span s) : Node(NodeKind::ParamDecl, s) {}
};

struct OutputDecl final : Node {
    std::string name;
    explicit OutputDecl(Span s) : Node(NodeKind::OutputDecl, s) {}
};

struct DefParam {
    std::string name;
    TypeRef* type = nullptr;
    Expr* def = nullptr;
    bool hasDefault = false;
};

struct OutDecl {
    std::string name;
    TypeRef* type = nullptr;
};

struct Def final : Node {
    std::string name;
    std::vector<DefParam> params;
    std::vector<OutDecl> outputs;
    std::string docstring;  // raw inner text of """..."""
    bool hasDoc = false;
    std::vector<ContractStmt*> expects;
    std::vector<Stmt*> body;
    std::vector<ContractStmt*> ensures;
    explicit Def(Span s) : Node(NodeKind::Def, s) {}
};

struct File final : Node {
    std::vector<Node*> items;  // source order: Import/ParamDecl/Def/OutputDecl/Stmt
    explicit File(Span s) : Node(NodeKind::File, s) {}
};

// Aliases for ANTLR `returns`/`locals` declarations — template-argument commas
// are not tracked by the ANTLR attribute parser, so list types need names.
using StringList = std::vector<std::string>;
using CallArgList = std::vector<CallArg>;
using DefParamList = std::vector<DefParam>;
using OutDeclList = std::vector<OutDecl>;
using PathElemList = std::vector<PathElem>;

// --- diagnostics -----------------------------------------------------------

// A `#` comment recovered from the hidden token channel, for the formatter.
struct Comment {
    Span span;
    std::string text;  // content after '#', trimmed
};

struct Diagnostic {
    std::string code;  // "E100", "W001", ...
    Span span;
    std::string message;
    std::string hint;
    bool isWarning = false;
};

// --- utilities -------------------------------------------------------------

// Dotted text form of a tap path (`make_rock[1].disp`); instance indices
// render as `[k]` suffixes. Used by the E6 debug subsystem (tap -> probe).
std::string pathText(const std::vector<PathElem>& path);

// Indented tree dump for debugging (`PggTool ast`) and test goldens.
std::string dumpAst(const Node* node);

// Structural equality ignoring spans (round-trip criterion).
bool astEqual(const Node* a, const Node* b);

}  // namespace pgg
