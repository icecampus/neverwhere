
#include "src/ast.h"
#include "src/compositor.h"


// Generated from /Users/neuro/sources/neverwhere/src/libs/pgg/grammar/Pgg.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  PggParser : public antlr4::Parser {
public:
  enum {
    DEF = 1, EXPECT = 2, ENSURE = 3, TAP = 4, IMPORT = 5, AS = 6, REPEAT = 7, 
    FOREACH = 8, IN = 9, PARAM = 10, OUTPUT = 11, NONE = 12, TRUE = 13, 
    FALSE = 14, TRIPLE_STRING = 15, STRING = 16, NUMBER = 17, ARROW = 18, 
    LTE = 19, GTE = 20, EQ = 21, NEQ = 22, LT = 23, GT = 24, PLUS = 25, 
    MINUS = 26, STAR = 27, SLASH = 28, PERCENT = 29, BANG = 30, PIPE = 31, 
    AMP = 32, AT = 33, QUESTION = 34, COLON = 35, DOT = 36, COMMA = 37, 
    ASSIGN = 38, LPAREN = 39, RPAREN = 40, LBRACE = 41, RBRACE = 42, LBRACKET = 43, 
    RBRACKET = 44, IDENT = 45, NEWLINE = 46, COMMENT = 47, WS = 48
  };

  enum {
    RuleFile = 0, RuleImport_stmt = 1, RuleParam_stmt = 2, RuleOutput_stmt = 3, 
    RuleDef_stmt = 4, RuleParams = 5, RuleParam = 6, RuleOutputs = 7, RuleOut_decl = 8, 
    RuleExpect_stmt = 9, RuleEnsure_stmt = 10, RuleStmt = 11, RuleBinding = 12, 
    RuleTargets = 13, RuleTap_stmt = 14, RulePath = 15, RuleRepeat_zone = 16, 
    RuleForeach_zone = 17, RuleAexpr = 18, RuleTernary = 19, RuleOr_expr = 20, 
    RuleAnd_expr = 21, RuleCmp_expr = 22, RuleAdd_expr = 23, RuleMul_expr = 24, 
    RuleUnary = 25, RulePostfix = 26, RuleCall = 27, RuleQualified_name = 28, 
    RuleArg = 29, RulePrimary = 30, RuleAttr_ref = 31, RuleVec_literal = 32, 
    RuleLiteral = 33, RuleType = 34
  };

  explicit PggParser(antlr4::TokenStream *input);

  PggParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~PggParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


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


  class FileContext;
  class Import_stmtContext;
  class Param_stmtContext;
  class Output_stmtContext;
  class Def_stmtContext;
  class ParamsContext;
  class ParamContext;
  class OutputsContext;
  class Out_declContext;
  class Expect_stmtContext;
  class Ensure_stmtContext;
  class StmtContext;
  class BindingContext;
  class TargetsContext;
  class Tap_stmtContext;
  class PathContext;
  class Repeat_zoneContext;
  class Foreach_zoneContext;
  class AexprContext;
  class TernaryContext;
  class Or_exprContext;
  class And_exprContext;
  class Cmp_exprContext;
  class Add_exprContext;
  class Mul_exprContext;
  class UnaryContext;
  class PostfixContext;
  class CallContext;
  class Qualified_nameContext;
  class ArgContext;
  class PrimaryContext;
  class Attr_refContext;
  class Vec_literalContext;
  class LiteralContext;
  class TypeContext; 

  class  FileContext : public antlr4::ParserRuleContext {
  public:
    pgg::File* result;
    PggParser::Import_stmtContext *i = nullptr;
    PggParser::Param_stmtContext *p = nullptr;
    PggParser::Def_stmtContext *d = nullptr;
    PggParser::Output_stmtContext *o = nullptr;
    PggParser::StmtContext *s = nullptr;
    FileContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<antlr4::tree::TerminalNode *> NEWLINE();
    antlr4::tree::TerminalNode* NEWLINE(size_t i);
    std::vector<Import_stmtContext *> import_stmt();
    Import_stmtContext* import_stmt(size_t i);
    std::vector<Param_stmtContext *> param_stmt();
    Param_stmtContext* param_stmt(size_t i);
    std::vector<Def_stmtContext *> def_stmt();
    Def_stmtContext* def_stmt(size_t i);
    std::vector<Output_stmtContext *> output_stmt();
    Output_stmtContext* output_stmt(size_t i);
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);

   
  };

  FileContext* file();

  class  Import_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::Import* result;
    PggParser::Qualified_nameContext *q = nullptr;
    antlr4::Token *a = nullptr;
    antlr4::Token *v = nullptr;
    Import_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IMPORT();
    antlr4::tree::TerminalNode *NEWLINE();
    Qualified_nameContext *qualified_name();
    antlr4::tree::TerminalNode *AS();
    antlr4::tree::TerminalNode *AT();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *NUMBER();

   
  };

  Import_stmtContext* import_stmt();

  class  Param_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::ParamDecl* result;
    pgg::Expr* def = nullptr;
    bool hasDef = false;
    antlr4::Token *n = nullptr;
    PggParser::TypeContext *t = nullptr;
    PggParser::LiteralContext *d = nullptr;
    Param_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PARAM();
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *NEWLINE();
    antlr4::tree::TerminalNode *IDENT();
    TypeContext *type();
    antlr4::tree::TerminalNode *ASSIGN();
    LiteralContext *literal();

   
  };

  Param_stmtContext* param_stmt();

  class  Output_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::OutputDecl* result;
    antlr4::Token *n = nullptr;
    Output_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *OUTPUT();
    antlr4::tree::TerminalNode *NEWLINE();
    antlr4::tree::TerminalNode *IDENT();

   
  };

  Output_stmtContext* output_stmt();

  class  Def_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::Def* result;
    pgg::DefParamList ps;
    antlr4::Token *n = nullptr;
    PggParser::ParamsContext *p = nullptr;
    PggParser::OutputsContext *o = nullptr;
    antlr4::Token *doc = nullptr;
    PggParser::Expect_stmtContext *e = nullptr;
    PggParser::Ensure_stmtContext *en = nullptr;
    PggParser::StmtContext *s = nullptr;
    Def_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *DEF();
    std::vector<antlr4::tree::TerminalNode *> LPAREN();
    antlr4::tree::TerminalNode* LPAREN(size_t i);
    std::vector<antlr4::tree::TerminalNode *> RPAREN();
    antlr4::tree::TerminalNode* RPAREN(size_t i);
    antlr4::tree::TerminalNode *ARROW();
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    antlr4::tree::TerminalNode *IDENT();
    OutputsContext *outputs();
    std::vector<antlr4::tree::TerminalNode *> NEWLINE();
    antlr4::tree::TerminalNode* NEWLINE(size_t i);
    antlr4::tree::TerminalNode *EOF();
    ParamsContext *params();
    std::vector<antlr4::tree::TerminalNode *> TRIPLE_STRING();
    antlr4::tree::TerminalNode* TRIPLE_STRING(size_t i);
    std::vector<Expect_stmtContext *> expect_stmt();
    Expect_stmtContext* expect_stmt(size_t i);
    std::vector<Ensure_stmtContext *> ensure_stmt();
    Ensure_stmtContext* ensure_stmt(size_t i);
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);

   
  };

  Def_stmtContext* def_stmt();

  class  ParamsContext : public antlr4::ParserRuleContext {
  public:
    pgg::DefParamList result;
    PggParser::ParamContext *paramContext = nullptr;
    std::vector<ParamContext *> p;
    ParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ParamContext *> param();
    ParamContext* param(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

   
  };

  ParamsContext* params();

  class  ParamContext : public antlr4::ParserRuleContext {
  public:
    pgg::DefParam result;
    pgg::Expr* def = nullptr;
    bool hasDef = false;
    antlr4::Token *n = nullptr;
    PggParser::TypeContext *t = nullptr;
    PggParser::LiteralContext *d = nullptr;
    ParamContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *IDENT();
    TypeContext *type();
    antlr4::tree::TerminalNode *ASSIGN();
    LiteralContext *literal();

   
  };

  ParamContext* param();

  class  OutputsContext : public antlr4::ParserRuleContext {
  public:
    pgg::OutDeclList result;
    PggParser::Out_declContext *out_declContext = nullptr;
    std::vector<Out_declContext *> o;
    OutputsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<Out_declContext *> out_decl();
    Out_declContext* out_decl(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

   
  };

  OutputsContext* outputs();

  class  Out_declContext : public antlr4::ParserRuleContext {
  public:
    pgg::OutDecl result;
    antlr4::Token *n = nullptr;
    PggParser::TypeContext *t = nullptr;
    Out_declContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *IDENT();
    TypeContext *type();

   
  };

  Out_declContext* out_decl();

  class  Expect_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::ContractStmt* result;
    pgg::Expr* cond = nullptr;
    pgg::Expr* attr = nullptr;
    bool formA = false;
    std::string id;
    std::string msg;
    bool hasMsg = false;
    antlr4::Token *i = nullptr;
    antlr4::Token *h = nullptr;
    PggParser::Attr_refContext *a = nullptr;
    PggParser::AexprContext *c = nullptr;
    antlr4::Token *m = nullptr;
    Expect_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EXPECT();
    antlr4::tree::TerminalNode *NEWLINE();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    Attr_refContext *attr_ref();
    AexprContext *aexpr();
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *STRING();

   
  };

  Expect_stmtContext* expect_stmt();

  class  Ensure_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::ContractStmt* result;
    pgg::Expr* cond = nullptr;
    pgg::Expr* attr = nullptr;
    bool formA = false;
    std::string id;
    std::string msg;
    bool hasMsg = false;
    antlr4::Token *i = nullptr;
    antlr4::Token *h = nullptr;
    PggParser::Attr_refContext *a = nullptr;
    PggParser::AexprContext *c = nullptr;
    antlr4::Token *m = nullptr;
    Ensure_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ENSURE();
    antlr4::tree::TerminalNode *NEWLINE();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    Attr_refContext *attr_ref();
    AexprContext *aexpr();
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *STRING();

   
  };

  Ensure_stmtContext* ensure_stmt();

  class  StmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::Stmt* result;
    PggParser::BindingContext *b = nullptr;
    PggParser::Tap_stmtContext *t = nullptr;
    PggParser::Repeat_zoneContext *r = nullptr;
    PggParser::Foreach_zoneContext *f = nullptr;
    StmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    BindingContext *binding();
    antlr4::tree::TerminalNode *NEWLINE();
    antlr4::tree::TerminalNode *EOF();
    Tap_stmtContext *tap_stmt();
    Repeat_zoneContext *repeat_zone();
    Foreach_zoneContext *foreach_zone();

   
  };

  StmtContext* stmt();

  class  BindingContext : public antlr4::ParserRuleContext {
  public:
    pgg::Stmt* result;
    PggParser::TargetsContext *t = nullptr;
    PggParser::AexprContext *v = nullptr;
    BindingContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ASSIGN();
    TargetsContext *targets();
    AexprContext *aexpr();

   
  };

  BindingContext* binding();

  class  TargetsContext : public antlr4::ParserRuleContext {
  public:
    pgg::NameList result;
    antlr4::Token *identToken = nullptr;
    std::vector<antlr4::Token *> i;
    TargetsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

   
  };

  TargetsContext* targets();

  class  Tap_stmtContext : public antlr4::ParserRuleContext {
  public:
    pgg::Stmt* result;
    std::string label;
    bool hasLabel = false;
    antlr4::Token *l = nullptr;
    PggParser::PathContext *p = nullptr;
    Tap_stmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TAP();
    PathContext *path();
    antlr4::tree::TerminalNode *COLON();
    antlr4::tree::TerminalNode *IDENT();

   
  };

  Tap_stmtContext* tap_stmt();

  class  PathContext : public antlr4::ParserRuleContext {
  public:
    pgg::PathElemList result;
    pgg::PathElemList elems;
    antlr4::Token *i = nullptr;
    antlr4::Token *j = nullptr;
    antlr4::Token *n = nullptr;
    PathContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode* DOT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> LBRACKET();
    antlr4::tree::TerminalNode* LBRACKET(size_t i);
    std::vector<antlr4::tree::TerminalNode *> RBRACKET();
    antlr4::tree::TerminalNode* RBRACKET(size_t i);
    std::vector<antlr4::tree::TerminalNode *> NUMBER();
    antlr4::tree::TerminalNode* NUMBER(size_t i);

   
  };

  PathContext* path();

  class  Repeat_zoneContext : public antlr4::ParserRuleContext {
  public:
    pgg::Stmt* result;
    PggParser::TargetsContext *t = nullptr;
    PggParser::AexprContext *v = nullptr;
    antlr4::Token *it = nullptr;
    PggParser::AexprContext *n = nullptr;
    antlr4::Token *identToken = nullptr;
    std::vector<antlr4::Token *> s;
    PggParser::StmtContext *b = nullptr;
    Repeat_zoneContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> ASSIGN();
    antlr4::tree::TerminalNode* ASSIGN(size_t i);
    antlr4::tree::TerminalNode *REPEAT();
    antlr4::tree::TerminalNode *LPAREN();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);
    antlr4::tree::TerminalNode *RPAREN();
    std::vector<antlr4::tree::TerminalNode *> PIPE();
    antlr4::tree::TerminalNode* PIPE(size_t i);
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    TargetsContext *targets();
    std::vector<AexprContext *> aexpr();
    AexprContext* aexpr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> NEWLINE();
    antlr4::tree::TerminalNode* NEWLINE(size_t i);
    antlr4::tree::TerminalNode *EOF();
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);

   
  };

  Repeat_zoneContext* repeat_zone();

  class  Foreach_zoneContext : public antlr4::ParserRuleContext {
  public:
    pgg::Stmt* result;
    antlr4::Token *tgt = nullptr;
    antlr4::Token *item = nullptr;
    PggParser::AexprContext *c = nullptr;
    PggParser::StmtContext *b = nullptr;
    Foreach_zoneContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ASSIGN();
    antlr4::tree::TerminalNode *FOREACH();
    antlr4::tree::TerminalNode *IN();
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    AexprContext *aexpr();
    std::vector<antlr4::tree::TerminalNode *> NEWLINE();
    antlr4::tree::TerminalNode* NEWLINE(size_t i);
    antlr4::tree::TerminalNode *EOF();
    std::vector<StmtContext *> stmt();
    StmtContext* stmt(size_t i);

   
  };

  Foreach_zoneContext* foreach_zone();

  class  AexprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::TernaryContext *t = nullptr;
    AexprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TernaryContext *ternary();

   
  };

  AexprContext* aexpr();

  class  TernaryContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Or_exprContext *c = nullptr;
    PggParser::AexprContext *t = nullptr;
    PggParser::AexprContext *e = nullptr;
    TernaryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Or_exprContext *or_expr();
    antlr4::tree::TerminalNode *QUESTION();
    antlr4::tree::TerminalNode *COLON();
    std::vector<AexprContext *> aexpr();
    AexprContext* aexpr(size_t i);

   
  };

  TernaryContext* ternary();

  class  Or_exprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Or_exprContext *l = nullptr;
    PggParser::And_exprContext *a = nullptr;
    PggParser::And_exprContext *r = nullptr;
    Or_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    And_exprContext *and_expr();
    antlr4::tree::TerminalNode *PIPE();
    Or_exprContext *or_expr();

   
  };

  Or_exprContext* or_expr();
  Or_exprContext* or_expr(int precedence);
  class  And_exprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::And_exprContext *l = nullptr;
    PggParser::Cmp_exprContext *a = nullptr;
    PggParser::Cmp_exprContext *r = nullptr;
    And_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Cmp_exprContext *cmp_expr();
    antlr4::tree::TerminalNode *AMP();
    And_exprContext *and_expr();

   
  };

  And_exprContext* and_expr();
  And_exprContext* and_expr(int precedence);
  class  Cmp_exprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Add_exprContext *l = nullptr;
    antlr4::Token *op = nullptr;
    PggParser::Add_exprContext *r = nullptr;
    Cmp_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<Add_exprContext *> add_expr();
    Add_exprContext* add_expr(size_t i);
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *GT();
    antlr4::tree::TerminalNode *LTE();
    antlr4::tree::TerminalNode *GTE();
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *NEQ();

   
  };

  Cmp_exprContext* cmp_expr();

  class  Add_exprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Add_exprContext *l = nullptr;
    PggParser::Mul_exprContext *a = nullptr;
    antlr4::Token *op = nullptr;
    PggParser::Mul_exprContext *r = nullptr;
    Add_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    Mul_exprContext *mul_expr();
    Add_exprContext *add_expr();
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *MINUS();

   
  };

  Add_exprContext* add_expr();
  Add_exprContext* add_expr(int precedence);
  class  Mul_exprContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Mul_exprContext *l = nullptr;
    PggParser::UnaryContext *a = nullptr;
    antlr4::Token *op = nullptr;
    PggParser::UnaryContext *r = nullptr;
    Mul_exprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnaryContext *unary();
    Mul_exprContext *mul_expr();
    antlr4::tree::TerminalNode *STAR();
    antlr4::tree::TerminalNode *SLASH();
    antlr4::tree::TerminalNode *PERCENT();

   
  };

  Mul_exprContext* mul_expr();
  Mul_exprContext* mul_expr(int precedence);
  class  UnaryContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    antlr4::Token *op = nullptr;
    PggParser::UnaryContext *u = nullptr;
    PggParser::PostfixContext *p = nullptr;
    UnaryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    UnaryContext *unary();
    antlr4::tree::TerminalNode *MINUS();
    antlr4::tree::TerminalNode *BANG();
    PostfixContext *postfix();

   
  };

  UnaryContext* unary();

  class  PostfixContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::CallContext *c = nullptr;
    PggParser::PrimaryContext *p = nullptr;
    PostfixContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CallContext *call();
    PrimaryContext *primary();

   
  };

  PostfixContext* postfix();

  class  CallContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    PggParser::Qualified_nameContext *q = nullptr;
    PggParser::ArgContext *argContext = nullptr;
    std::vector<ArgContext *> a;
    CallContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    Qualified_nameContext *qualified_name();
    std::vector<ArgContext *> arg();
    ArgContext* arg(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

   
  };

  CallContext* call();

  class  Qualified_nameContext : public antlr4::ParserRuleContext {
  public:
    pgg::StringList result;
    antlr4::Token *identToken = nullptr;
    std::vector<antlr4::Token *> i;
    Qualified_nameContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode* DOT(size_t i);

   
  };

  Qualified_nameContext* qualified_name();

  class  ArgContext : public antlr4::ParserRuleContext {
  public:
    pgg::CallArg result;
    pgg::CallArg a;
    antlr4::Token *n = nullptr;
    PggParser::AexprContext *v = nullptr;
    ArgContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    AexprContext *aexpr();
    antlr4::tree::TerminalNode *ASSIGN();
    antlr4::tree::TerminalNode *IDENT();

   
  };

  ArgContext* arg();

  class  PrimaryContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    antlr4::Token *n = nullptr;
    antlr4::Token *s = nullptr;
    antlr4::Token *b = nullptr;
    PggParser::Vec_literalContext *v = nullptr;
    antlr4::Token *i = nullptr;
    PggParser::Attr_refContext *a = nullptr;
    PggParser::AexprContext *e = nullptr;
    PrimaryContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *NUMBER();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();
    Vec_literalContext *vec_literal();
    antlr4::tree::TerminalNode *NONE();
    antlr4::tree::TerminalNode *IDENT();
    Attr_refContext *attr_ref();
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    AexprContext *aexpr();

   
  };

  PrimaryContext* primary();

  class  Attr_refContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    antlr4::Token *n = nullptr;
    Attr_refContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *AT();
    antlr4::tree::TerminalNode *IDENT();

   
  };

  Attr_refContext* attr_ref();

  class  Vec_literalContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    antlr4::Token *numberToken = nullptr;
    std::vector<antlr4::Token *> n;
    Vec_literalContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    std::vector<antlr4::tree::TerminalNode *> NUMBER();
    antlr4::tree::TerminalNode* NUMBER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

   
  };

  Vec_literalContext* vec_literal();

  class  LiteralContext : public antlr4::ParserRuleContext {
  public:
    pgg::Expr* result;
    antlr4::Token *n = nullptr;
    antlr4::Token *s = nullptr;
    antlr4::Token *b = nullptr;
    PggParser::Vec_literalContext *v = nullptr;
    antlr4::Token *e = nullptr;
    LiteralContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *NUMBER();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();
    Vec_literalContext *vec_literal();
    antlr4::tree::TerminalNode *NONE();
    antlr4::tree::TerminalNode *IDENT();

   
  };

  LiteralContext* literal();

  class  TypeContext : public antlr4::ParserRuleContext {
  public:
    pgg::TypeRef* result;
    PggParser::TypeContext *t = nullptr;
    antlr4::Token *b = nullptr;
    PggParser::TypeContext *a = nullptr;
    antlr4::Token *e = nullptr;
    antlr4::Token *identToken = nullptr;
    std::vector<antlr4::Token *> v;
    TypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *GT();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    TypeContext *type();
    antlr4::tree::TerminalNode *LBRACE();
    antlr4::tree::TerminalNode *RBRACE();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);
    antlr4::tree::TerminalNode *QUESTION();
    antlr4::tree::TerminalNode *LBRACKET();
    antlr4::tree::TerminalNode *RBRACKET();

   
  };

  TypeContext* type();
  TypeContext* type(int precedence);

  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool or_exprSempred(Or_exprContext *_localctx, size_t predicateIndex);
  bool and_exprSempred(And_exprContext *_localctx, size_t predicateIndex);
  bool add_exprSempred(Add_exprContext *_localctx, size_t predicateIndex);
  bool mul_exprSempred(Mul_exprContext *_localctx, size_t predicateIndex);
  bool typeSempred(TypeContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

