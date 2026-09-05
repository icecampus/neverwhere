// PGG language grammar (spec docs/geometry_generation_language.md §13).
// Rules carry no logic — only one-call triggers into the grammar compositor
// (spec §13.2): fixed-arity constructs return semantic values, open bodies
// (file/def/zones) accumulate in gc frames between begin/end triggers.
// Regenerate parser_gen/ with tools/pgg/regen_parser.sh (pinned ANTLR 4.13.2).
grammar Pgg;

@parser::header {
#include "src/ast.h"
#include "src/compositor.h"
}

@parser::members {
public:
    pgg::GrammarCompositor* gc = nullptr;

    // ctx->getStop() is not yet set when an embedded action fires, so the end
    // of the span is taken from the last consumed token of the stream.
    pgg::Span spanOf(const antlr4::ParserRuleContext* c) {
        pgg::Span s{};
        if (!c || !c->getStart()) return s;
        s.line = static_cast<int32_t>(c->getStart()->getLine());
        s.col = static_cast<int32_t>(c->getStart()->getCharPositionInLine());
        antlr4::Token* stop = c->getStop();
        if (!stop) {
            antlr4::TokenStream* ts = getTokenStream();
            const size_t idx = ts->index();
            stop = idx > 0 ? ts->get(idx - 1) : c->getStart();
        }
        s.endLine = static_cast<int32_t>(stop->getLine());
        s.endCol = static_cast<int32_t>(stop->getCharPositionInLine() + stop->getText().size());
        return s;
    }

    static pgg::Span spanTok(antlr4::Token* t) { return pgg::GrammarCompositor::spanOfTok(t); }
}

// --- file --------------------------------------------------------------------

file returns [pgg::File* result = nullptr]
    : { gc->beginFile(); }
      ( NEWLINE
      | i=import_stmt  { gc->addNode($i.result); }
      | p=param_stmt   { gc->addNode($p.result); }
      | d=def_stmt     { gc->addNode($d.result); }
      | o=output_stmt  { gc->addNode($o.result); }
      | s=stmt         { gc->addNode($s.result); }
      )*
      EOF { $result = gc->endFile(spanOf(_localctx)); }
    ;

import_stmt returns [pgg::Import* result = nullptr]
    : IMPORT q=qualified_name (AS a=IDENT)? (AT v=NUMBER)? NEWLINE
      { $result = gc->newImport($q.result, $a, $v, spanOf(_localctx)); }
    ;

param_stmt returns [pgg::ParamDecl* result = nullptr]
    locals [pgg::Expr* def = nullptr, bool hasDef = false]
    : PARAM n=IDENT COLON t=type (ASSIGN d=literal { $def = $d.result; $hasDef = true; })? NEWLINE
      { $result = gc->newParam($n.text, spanTok($n), $t.result, $def, $hasDef, spanOf(_localctx)); }
    ;

output_stmt returns [pgg::OutputDecl* result = nullptr]
    : OUTPUT n=IDENT NEWLINE { $result = gc->newOutput($n.text, spanTok($n), spanOf(_localctx)); }
    ;

// --- def ---------------------------------------------------------------------

def_stmt returns [pgg::Def* result = nullptr]
    locals [pgg::DefParamList ps]
    : DEF n=IDENT LPAREN (p=params { $ps = $p.result; })? RPAREN ARROW LPAREN o=outputs RPAREN
      { gc->beginDef($n.text, spanTok($n), $ps, $o.result, spanOf(_localctx)); }
      LBRACE NEWLINE*
      ( NEWLINE
      | doc=TRIPLE_STRING NEWLINE { gc->defDoc($doc.text, spanTok($doc)); }
      | e=expect_stmt { gc->addExpect($e.result); }
      | en=ensure_stmt { gc->addEnsure($en.result); }
      | s=stmt { gc->addNode($s.result); }
      )*
      RBRACE (NEWLINE | EOF)
      { $result = gc->endDef(spanOf(_localctx)); }
    ;

params returns [pgg::DefParamList result]
    : p+=param (COMMA p+=param)* { $result = gc->resultsOf($p); }
    ;

param returns [pgg::DefParam result]
    locals [pgg::Expr* def = nullptr, bool hasDef = false]
    : n=IDENT COLON t=type (ASSIGN d=literal { $def = $d.result; $hasDef = true; })?
      { $result = gc->newDefParam($n.text, $t.result, $def, $hasDef); }
    ;

outputs returns [pgg::OutDeclList result]
    : o+=out_decl (COMMA o+=out_decl)* { $result = gc->resultsOf($o); }
    ;

out_decl returns [pgg::OutDecl result]
    : n=IDENT COLON t=type { $result = gc->newOutDecl($n.text, $t.result); }
    ;

expect_stmt returns [pgg::ContractStmt* result = nullptr]
    locals [pgg::Expr* cond = nullptr, pgg::Expr* attr = nullptr, bool formA = false,
            std::string id, std::string msg, bool hasMsg = false]
    : EXPECT
      ( i=IDENT h=IDENT a=attr_ref
        { $formA = true; $id = $i.text; $attr = $a.result; gc->checkKeyword($h, "has"); }
      | c=aexpr { $cond = $c.result; }
      )
      (COLON m=STRING { $msg = gc->stringValue($m.text, spanTok($m)); $hasMsg = true; })?
      NEWLINE
      { $result = gc->newContract(pgg::NodeKind::Expect, $formA, $id, $attr, $cond, $msg, $hasMsg,
                                  spanOf(_localctx)); }
    ;

ensure_stmt returns [pgg::ContractStmt* result = nullptr]
    locals [pgg::Expr* cond = nullptr, pgg::Expr* attr = nullptr, bool formA = false,
            std::string id, std::string msg, bool hasMsg = false]
    : ENSURE
      ( i=IDENT h=IDENT a=attr_ref
        { $formA = true; $id = $i.text; $attr = $a.result; gc->checkKeyword($h, "has"); }
      | c=aexpr { $cond = $c.result; }
      )
      (COLON m=STRING { $msg = gc->stringValue($m.text, spanTok($m)); $hasMsg = true; })?
      NEWLINE
      { $result = gc->newContract(pgg::NodeKind::Ensure, $formA, $id, $attr, $cond, $msg, $hasMsg,
                                  spanOf(_localctx)); }
    ;

// --- statements -----------------------------------------------------------------

stmt returns [pgg::Stmt* result = nullptr]
    : b=binding (NEWLINE | EOF) { $result = $b.result; }
    | t=tap_stmt (NEWLINE | EOF) { $result = $t.result; }
    | r=repeat_zone { $result = $r.result; }
    | f=foreach_zone { $result = $f.result; }
    ;

binding returns [pgg::Stmt* result = nullptr]
    : t=targets ASSIGN v=aexpr { $result = gc->newBinding($t.result, $v.result, spanOf(_localctx)); }
    ;

targets returns [pgg::NameList result]
    : i+=IDENT (COMMA i+=IDENT)* { $result = gc->nameListOf($i); }
    ;

tap_stmt returns [pgg::Stmt* result = nullptr]
    locals [std::string label, bool hasLabel = false]
    : TAP (l=IDENT COLON { $label = $l.text; $hasLabel = true; })? p=path
      { $result = gc->newTap($label, $hasLabel, $p.result, spanOf(_localctx)); }
    ;

path returns [pgg::PathElemList result]
    locals [pgg::PathElemList elems]
    : i=IDENT { $elems.push_back(gc->pathName($i.text)); }
      ( DOT j=IDENT { $elems.push_back(gc->pathName($j.text)); }
      | LBRACKET n=NUMBER RBRACKET { $elems.push_back(gc->pathIndex($n.text)); }
      )*
      { $result = $elems; }
    ;

repeat_zone returns [pgg::Stmt* result = nullptr]
    : t=targets ASSIGN REPEAT LPAREN v=aexpr COMMA it=IDENT ASSIGN n=aexpr RPAREN
      { gc->checkKeyword($it, "iterations"); }
      PIPE (s+=IDENT (COMMA s+=IDENT)*)? PIPE
      { gc->beginRepeat($t.result, $v.result, $n.result, gc->nameListOf($s), spanOf(_localctx)); }
      LBRACE NEWLINE*
      (NEWLINE | b=stmt { gc->addNode($b.result); })*
      RBRACE (NEWLINE | EOF)
      { $result = gc->endRepeat(spanOf(_localctx)); }
    ;

foreach_zone returns [pgg::Stmt* result = nullptr]
    : tgt=IDENT ASSIGN FOREACH item=IDENT IN c=aexpr
      { gc->beginForeach($tgt.text, spanTok($tgt), $item.text, spanTok($item), $c.result,
                         spanOf(_localctx)); }
      LBRACE NEWLINE*
      (NEWLINE | b=stmt { gc->addNode($b.result); })*
      RBRACE (NEWLINE | EOF)
      { $result = gc->endForeach(spanOf(_localctx)); }
    ;

// --- expressions ------------------------------------------------------------------

aexpr returns [pgg::Expr* result = nullptr]
    : t=ternary { $result = $t.result; }
    ;

ternary returns [pgg::Expr* result = nullptr]
    : c=or_expr { $result = $c.result; }
      (QUESTION t=aexpr COLON e=aexpr
       { $result = gc->newTernary($c.result, $t.result, $e.result, spanOf(_localctx)); })?
    ;

or_expr returns [pgg::Expr* result = nullptr]
    : l=or_expr PIPE r=and_expr { $result = gc->newBinary("|", $l.result, $r.result, spanOf(_localctx)); }
    | a=and_expr { $result = $a.result; }
    ;

and_expr returns [pgg::Expr* result = nullptr]
    : l=and_expr AMP r=cmp_expr { $result = gc->newBinary("&", $l.result, $r.result, spanOf(_localctx)); }
    | a=cmp_expr { $result = $a.result; }
    ;

cmp_expr returns [pgg::Expr* result = nullptr]
    : l=add_expr { $result = $l.result; }
      (op=(LT|GT|LTE|GTE|EQ|NEQ) r=add_expr
       { $result = gc->newBinary($op.text, $l.result, $r.result, spanOf(_localctx)); })?
    ;

add_expr returns [pgg::Expr* result = nullptr]
    : l=add_expr op=(PLUS|MINUS) r=mul_expr
      { $result = gc->newBinary($op.text, $l.result, $r.result, spanOf(_localctx)); }
    | a=mul_expr { $result = $a.result; }
    ;

mul_expr returns [pgg::Expr* result = nullptr]
    : l=mul_expr op=(STAR|SLASH|PERCENT) r=unary
      { $result = gc->newBinary($op.text, $l.result, $r.result, spanOf(_localctx)); }
    | a=unary { $result = $a.result; }
    ;

unary returns [pgg::Expr* result = nullptr]
    : op=(MINUS|BANG) u=unary { $result = gc->newUnary($op.text, $u.result, spanOf(_localctx)); }
    | p=postfix { $result = $p.result; }
    ;

postfix returns [pgg::Expr* result = nullptr]
    : c=call { $result = $c.result; }
    | p=primary { $result = $p.result; }
    ;

call returns [pgg::Expr* result = nullptr]
    : q=qualified_name LPAREN (a+=arg (COMMA a+=arg)*)? RPAREN
      { $result = gc->newCall($q.result, gc->resultsOf($a), spanOf(_localctx)); }
    ;

qualified_name returns [pgg::StringList result]
    : i+=IDENT (DOT i+=IDENT)* { $result = gc->namesOf($i); }
    ;

arg returns [pgg::CallArg result]
    locals [pgg::CallArg a]
    : (n=IDENT ASSIGN { $a.name = $n.text; $a.hasName = true; })? v=aexpr
      { $a.value = $v.result; $result = $a; }
    ;

primary returns [pgg::Expr* result = nullptr]
    : n=NUMBER { $result = gc->newNumber($n.text, spanTok($n)); }
    | s=STRING { $result = gc->newString($s.text, spanTok($s)); }
    | b=(TRUE|FALSE) { $result = gc->newBool($b.text, spanTok($b)); }
    | v=vec_literal { $result = $v.result; }
    | l=list_literal { $result = $l.result; }
    | NONE { $result = gc->newNone(spanOf(_localctx)); }
    | i=IDENT { $result = gc->newIdent($i.text, spanTok($i)); }
    | a=attr_ref { $result = $a.result; }
    | LPAREN e=aexpr RPAREN { $result = gc->newParen($e.result, spanOf(_localctx)); }
    ;

attr_ref returns [pgg::Expr* result = nullptr]
    : AT n=IDENT { $result = gc->newAttr($n.text, spanOf(_localctx)); }
    ;

vec_literal returns [pgg::Expr* result = nullptr]
    : LPAREN e+=vec_elem (COMMA e+=vec_elem)+ RPAREN
      { $result = gc->newVec(gc->resultsOf($e), spanOf(_localctx)); }
    ;

// A vec component is a signed numeric literal; at least one COMMA is required,
// so a parenthesized `(-1)` stays a scalar expression, not a broken vec1.
vec_elem returns [pgg::Expr* result = nullptr]
    : (m=MINUS)? n=NUMBER { $result = gc->newSignedNumber($m, $n, spanOf(_localctx)); }
    ;

// list literal (spec §13: T[] values, e.g. variants = [a, b]). Any expressions
// as elements; an optional trailing comma is accepted (multiline canonical
// form, §6.4 — the E2 formatter still emits single-line statements).
list_literal returns [pgg::Expr* result = nullptr]
    : LBRACKET (e+=aexpr (COMMA e+=aexpr)* COMMA?)? RBRACKET
      { $result = gc->newList(gc->resultsOf($e), spanOf(_localctx)); }
    ;

// literal: defaults in param positions (spec §6.6). A bare ident here is an
// enum/domain literal; in general expressions idents parse as Ident and the
// enum reading is a type-driven decision of a later stage.
literal returns [pgg::Expr* result = nullptr]
    : n=NUMBER { $result = gc->newNumber($n.text, spanTok($n)); }
    | s=STRING { $result = gc->newString($s.text, spanTok($s)); }
    | b=(TRUE|FALSE) { $result = gc->newBool($b.text, spanTok($b)); }
    | v=vec_literal { $result = $v.result; }
    | NONE { $result = gc->newNone(spanOf(_localctx)); }
    | e=IDENT { $result = gc->newEnumLit($e.text, spanTok($e)); }
    ;

// --- types ---------------------------------------------------------------------------

type returns [pgg::TypeRef* result = nullptr]
    : t=type QUESTION { $result = gc->typeOptional($t.result, spanOf(_localctx)); }
    | t=type LBRACKET RBRACKET { $result = gc->typeList($t.result, spanOf(_localctx)); }
    | b=IDENT LT a=type GT
      { $result = gc->newTypeGeneric($b.text, spanTok($b), $a.result, spanOf(_localctx)); }
    | e=IDENT LBRACE (v+=IDENT (COMMA v+=IDENT)*)? RBRACE
      { $result = gc->newTypeEnum($e, gc->namesOf($v), spanOf(_localctx)); }
    | b=IDENT { $result = gc->newTypeName($b.text, spanTok($b)); }
    ;

// --- lexer -----------------------------------------------------------------------------

DEF: 'def';
EXPECT: 'expect';
ENSURE: 'ensure';
TAP: 'tap';
IMPORT: 'import';
AS: 'as';
REPEAT: 'repeat';
FOREACH: 'foreach';
IN: 'in';
PARAM: 'param';
OUTPUT: 'output';
NONE: 'none';
TRUE: 'true';
FALSE: 'false';

TRIPLE_STRING: '"""' .*? '"""';
STRING: '"' ('\\' . | ~["\\\r\n])* '"';
NUMBER: [0-9]+ ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?;

ARROW: '->';
LTE: '<=';
GTE: '>=';
EQ: '==';
NEQ: '!=';

LT: '<';
GT: '>';
PLUS: '+';
MINUS: '-';
STAR: '*';
SLASH: '/';
PERCENT: '%';
BANG: '!';
PIPE: '|';
AMP: '&';
AT: '@';
QUESTION: '?';
COLON: ':';
DOT: '.';
COMMA: ',';
ASSIGN: '=';
LPAREN: '(';
RPAREN: ')';
LBRACE: '{';
RBRACE: '}';
LBRACKET: '[';
RBRACKET: ']';

IDENT: [A-Za-z_] [A-Za-z0-9_]*;
NEWLINE: '\r'? '\n';
COMMENT: '#' ~[\r\n]* -> channel(HIDDEN);
WS: [ \t]+ -> channel(HIDDEN);
