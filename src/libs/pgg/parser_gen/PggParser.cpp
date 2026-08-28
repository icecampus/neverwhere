
#include "src/ast.h"
#include "src/compositor.h"


// Generated from /Users/neuro/sources/neverwhere/src/libs/pgg/grammar/Pgg.g4 by ANTLR 4.13.2



#include "PggParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct PggParserStaticData final {
  PggParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  PggParserStaticData(const PggParserStaticData&) = delete;
  PggParserStaticData(PggParserStaticData&&) = delete;
  PggParserStaticData& operator=(const PggParserStaticData&) = delete;
  PggParserStaticData& operator=(PggParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag pggParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<PggParserStaticData> pggParserStaticData = nullptr;

void pggParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (pggParserStaticData != nullptr) {
    return;
  }
#else
  assert(pggParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<PggParserStaticData>(
    std::vector<std::string>{
      "file", "import_stmt", "param_stmt", "output_stmt", "def_stmt", "params", 
      "param", "outputs", "out_decl", "expect_stmt", "ensure_stmt", "stmt", 
      "binding", "targets", "tap_stmt", "path", "repeat_zone", "foreach_zone", 
      "aexpr", "ternary", "or_expr", "and_expr", "cmp_expr", "add_expr", 
      "mul_expr", "unary", "postfix", "call", "qualified_name", "arg", "primary", 
      "attr_ref", "vec_literal", "literal", "type"
    },
    std::vector<std::string>{
      "", "'def'", "'expect'", "'ensure'", "'tap'", "'import'", "'as'", 
      "'repeat'", "'foreach'", "'in'", "'param'", "'output'", "'none'", 
      "'true'", "'false'", "", "", "", "'->'", "'<='", "'>='", "'=='", "'!='", 
      "'<'", "'>'", "'+'", "'-'", "'*'", "'/'", "'%'", "'!'", "'|'", "'&'", 
      "'@'", "'\\u003F'", "':'", "'.'", "','", "'='", "'('", "')'", "'{'", 
      "'}'", "'['", "']'"
    },
    std::vector<std::string>{
      "", "DEF", "EXPECT", "ENSURE", "TAP", "IMPORT", "AS", "REPEAT", "FOREACH", 
      "IN", "PARAM", "OUTPUT", "NONE", "TRUE", "FALSE", "TRIPLE_STRING", 
      "STRING", "NUMBER", "ARROW", "LTE", "GTE", "EQ", "NEQ", "LT", "GT", 
      "PLUS", "MINUS", "STAR", "SLASH", "PERCENT", "BANG", "PIPE", "AMP", 
      "AT", "QUESTION", "COLON", "DOT", "COMMA", "ASSIGN", "LPAREN", "RPAREN", 
      "LBRACE", "RBRACE", "LBRACKET", "RBRACKET", "IDENT", "NEWLINE", "COMMENT", 
      "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,48,589,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,1,0,1,
  	0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,5,0,88,
  	8,0,10,0,12,0,91,9,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,3,1,100,8,1,1,1,1,1,
  	3,1,104,8,1,1,1,1,1,1,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,117,8,2,1,
  	2,1,2,1,2,1,3,1,3,1,3,1,3,1,3,1,4,1,4,1,4,1,4,1,4,1,4,3,4,133,8,4,1,4,
  	1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,143,8,4,10,4,12,4,146,9,4,1,4,1,4,1,4,
  	1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,161,8,4,10,4,12,4,164,9,4,
  	1,4,1,4,1,4,1,4,1,5,1,5,1,5,5,5,173,8,5,10,5,12,5,176,9,5,1,5,1,5,1,6,
  	1,6,1,6,1,6,1,6,1,6,1,6,3,6,187,8,6,1,6,1,6,1,7,1,7,1,7,5,7,194,8,7,10,
  	7,12,7,197,9,7,1,7,1,7,1,8,1,8,1,8,1,8,1,8,1,9,1,9,1,9,1,9,1,9,1,9,1,
  	9,1,9,1,9,3,9,215,8,9,1,9,1,9,1,9,3,9,220,8,9,1,9,1,9,1,9,1,10,1,10,1,
  	10,1,10,1,10,1,10,1,10,1,10,1,10,3,10,234,8,10,1,10,1,10,1,10,3,10,239,
  	8,10,1,10,1,10,1,10,1,11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,
  	1,11,1,11,1,11,1,11,3,11,258,8,11,1,12,1,12,1,12,1,12,1,12,1,13,1,13,
  	1,13,5,13,268,8,13,10,13,12,13,271,9,13,1,13,1,13,1,14,1,14,1,14,1,14,
  	3,14,279,8,14,1,14,1,14,1,14,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,
  	1,15,5,15,293,8,15,10,15,12,15,296,9,15,1,15,1,15,1,16,1,16,1,16,1,16,
  	1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,5,16,315,8,16,
  	10,16,12,16,318,9,16,3,16,320,8,16,1,16,1,16,1,16,1,16,5,16,326,8,16,
  	10,16,12,16,329,9,16,1,16,1,16,1,16,1,16,5,16,335,8,16,10,16,12,16,338,
  	9,16,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,17,
  	5,17,353,8,17,10,17,12,17,356,9,17,1,17,1,17,1,17,1,17,5,17,362,8,17,
  	10,17,12,17,365,9,17,1,17,1,17,1,17,1,17,1,18,1,18,1,18,1,19,1,19,1,19,
  	1,19,1,19,1,19,1,19,1,19,3,19,382,8,19,1,20,1,20,1,20,1,20,1,20,1,20,
  	1,20,1,20,1,20,5,20,393,8,20,10,20,12,20,396,9,20,1,21,1,21,1,21,1,21,
  	1,21,1,21,1,21,1,21,1,21,5,21,407,8,21,10,21,12,21,410,9,21,1,22,1,22,
  	1,22,1,22,1,22,1,22,3,22,418,8,22,1,23,1,23,1,23,1,23,1,23,1,23,1,23,
  	1,23,1,23,5,23,429,8,23,10,23,12,23,432,9,23,1,24,1,24,1,24,1,24,1,24,
  	1,24,1,24,1,24,1,24,5,24,443,8,24,10,24,12,24,446,9,24,1,25,1,25,1,25,
  	1,25,1,25,1,25,1,25,3,25,455,8,25,1,26,1,26,1,26,1,26,1,26,1,26,3,26,
  	463,8,26,1,27,1,27,1,27,1,27,1,27,5,27,470,8,27,10,27,12,27,473,9,27,
  	3,27,475,8,27,1,27,1,27,1,27,1,28,1,28,1,28,5,28,483,8,28,10,28,12,28,
  	486,9,28,1,28,1,28,1,29,1,29,1,29,3,29,493,8,29,1,29,1,29,1,29,1,30,1,
  	30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,
  	30,1,30,1,30,1,30,1,30,1,30,3,30,519,8,30,1,31,1,31,1,31,1,31,1,32,1,
  	32,1,32,1,32,5,32,529,8,32,10,32,12,32,532,9,32,1,32,1,32,1,32,1,33,1,
  	33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,1,33,3,33,550,8,
  	33,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,5,34,564,
  	8,34,10,34,12,34,567,9,34,3,34,569,8,34,1,34,1,34,1,34,1,34,3,34,575,
  	8,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,5,34,584,8,34,10,34,12,34,587,
  	9,34,1,34,0,5,40,42,46,48,68,35,0,2,4,6,8,10,12,14,16,18,20,22,24,26,
  	28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,0,6,1,
  	1,46,46,1,0,19,24,1,0,25,26,1,0,27,29,2,0,26,26,30,30,1,0,13,14,622,0,
  	70,1,0,0,0,2,95,1,0,0,0,4,108,1,0,0,0,6,121,1,0,0,0,8,126,1,0,0,0,10,
  	169,1,0,0,0,12,179,1,0,0,0,14,190,1,0,0,0,16,200,1,0,0,0,18,205,1,0,0,
  	0,20,224,1,0,0,0,22,257,1,0,0,0,24,259,1,0,0,0,26,264,1,0,0,0,28,274,
  	1,0,0,0,30,283,1,0,0,0,32,299,1,0,0,0,34,343,1,0,0,0,36,370,1,0,0,0,38,
  	373,1,0,0,0,40,383,1,0,0,0,42,397,1,0,0,0,44,411,1,0,0,0,46,419,1,0,0,
  	0,48,433,1,0,0,0,50,454,1,0,0,0,52,462,1,0,0,0,54,464,1,0,0,0,56,479,
  	1,0,0,0,58,492,1,0,0,0,60,518,1,0,0,0,62,520,1,0,0,0,64,524,1,0,0,0,66,
  	549,1,0,0,0,68,574,1,0,0,0,70,89,6,0,-1,0,71,88,5,46,0,0,72,73,3,2,1,
  	0,73,74,6,0,-1,0,74,88,1,0,0,0,75,76,3,4,2,0,76,77,6,0,-1,0,77,88,1,0,
  	0,0,78,79,3,8,4,0,79,80,6,0,-1,0,80,88,1,0,0,0,81,82,3,6,3,0,82,83,6,
  	0,-1,0,83,88,1,0,0,0,84,85,3,22,11,0,85,86,6,0,-1,0,86,88,1,0,0,0,87,
  	71,1,0,0,0,87,72,1,0,0,0,87,75,1,0,0,0,87,78,1,0,0,0,87,81,1,0,0,0,87,
  	84,1,0,0,0,88,91,1,0,0,0,89,87,1,0,0,0,89,90,1,0,0,0,90,92,1,0,0,0,91,
  	89,1,0,0,0,92,93,5,0,0,1,93,94,6,0,-1,0,94,1,1,0,0,0,95,96,5,5,0,0,96,
  	99,3,56,28,0,97,98,5,6,0,0,98,100,5,45,0,0,99,97,1,0,0,0,99,100,1,0,0,
  	0,100,103,1,0,0,0,101,102,5,33,0,0,102,104,5,17,0,0,103,101,1,0,0,0,103,
  	104,1,0,0,0,104,105,1,0,0,0,105,106,5,46,0,0,106,107,6,1,-1,0,107,3,1,
  	0,0,0,108,109,5,10,0,0,109,110,5,45,0,0,110,111,5,35,0,0,111,116,3,68,
  	34,0,112,113,5,38,0,0,113,114,3,66,33,0,114,115,6,2,-1,0,115,117,1,0,
  	0,0,116,112,1,0,0,0,116,117,1,0,0,0,117,118,1,0,0,0,118,119,5,46,0,0,
  	119,120,6,2,-1,0,120,5,1,0,0,0,121,122,5,11,0,0,122,123,5,45,0,0,123,
  	124,5,46,0,0,124,125,6,3,-1,0,125,7,1,0,0,0,126,127,5,1,0,0,127,128,5,
  	45,0,0,128,132,5,39,0,0,129,130,3,10,5,0,130,131,6,4,-1,0,131,133,1,0,
  	0,0,132,129,1,0,0,0,132,133,1,0,0,0,133,134,1,0,0,0,134,135,5,40,0,0,
  	135,136,5,18,0,0,136,137,5,39,0,0,137,138,3,14,7,0,138,139,5,40,0,0,139,
  	140,6,4,-1,0,140,144,5,41,0,0,141,143,5,46,0,0,142,141,1,0,0,0,143,146,
  	1,0,0,0,144,142,1,0,0,0,144,145,1,0,0,0,145,162,1,0,0,0,146,144,1,0,0,
  	0,147,161,5,46,0,0,148,149,5,15,0,0,149,150,5,46,0,0,150,161,6,4,-1,0,
  	151,152,3,18,9,0,152,153,6,4,-1,0,153,161,1,0,0,0,154,155,3,20,10,0,155,
  	156,6,4,-1,0,156,161,1,0,0,0,157,158,3,22,11,0,158,159,6,4,-1,0,159,161,
  	1,0,0,0,160,147,1,0,0,0,160,148,1,0,0,0,160,151,1,0,0,0,160,154,1,0,0,
  	0,160,157,1,0,0,0,161,164,1,0,0,0,162,160,1,0,0,0,162,163,1,0,0,0,163,
  	165,1,0,0,0,164,162,1,0,0,0,165,166,5,42,0,0,166,167,7,0,0,0,167,168,
  	6,4,-1,0,168,9,1,0,0,0,169,174,3,12,6,0,170,171,5,37,0,0,171,173,3,12,
  	6,0,172,170,1,0,0,0,173,176,1,0,0,0,174,172,1,0,0,0,174,175,1,0,0,0,175,
  	177,1,0,0,0,176,174,1,0,0,0,177,178,6,5,-1,0,178,11,1,0,0,0,179,180,5,
  	45,0,0,180,181,5,35,0,0,181,186,3,68,34,0,182,183,5,38,0,0,183,184,3,
  	66,33,0,184,185,6,6,-1,0,185,187,1,0,0,0,186,182,1,0,0,0,186,187,1,0,
  	0,0,187,188,1,0,0,0,188,189,6,6,-1,0,189,13,1,0,0,0,190,195,3,16,8,0,
  	191,192,5,37,0,0,192,194,3,16,8,0,193,191,1,0,0,0,194,197,1,0,0,0,195,
  	193,1,0,0,0,195,196,1,0,0,0,196,198,1,0,0,0,197,195,1,0,0,0,198,199,6,
  	7,-1,0,199,15,1,0,0,0,200,201,5,45,0,0,201,202,5,35,0,0,202,203,3,68,
  	34,0,203,204,6,8,-1,0,204,17,1,0,0,0,205,214,5,2,0,0,206,207,5,45,0,0,
  	207,208,5,45,0,0,208,209,3,62,31,0,209,210,6,9,-1,0,210,215,1,0,0,0,211,
  	212,3,36,18,0,212,213,6,9,-1,0,213,215,1,0,0,0,214,206,1,0,0,0,214,211,
  	1,0,0,0,215,219,1,0,0,0,216,217,5,35,0,0,217,218,5,16,0,0,218,220,6,9,
  	-1,0,219,216,1,0,0,0,219,220,1,0,0,0,220,221,1,0,0,0,221,222,5,46,0,0,
  	222,223,6,9,-1,0,223,19,1,0,0,0,224,233,5,3,0,0,225,226,5,45,0,0,226,
  	227,5,45,0,0,227,228,3,62,31,0,228,229,6,10,-1,0,229,234,1,0,0,0,230,
  	231,3,36,18,0,231,232,6,10,-1,0,232,234,1,0,0,0,233,225,1,0,0,0,233,230,
  	1,0,0,0,234,238,1,0,0,0,235,236,5,35,0,0,236,237,5,16,0,0,237,239,6,10,
  	-1,0,238,235,1,0,0,0,238,239,1,0,0,0,239,240,1,0,0,0,240,241,5,46,0,0,
  	241,242,6,10,-1,0,242,21,1,0,0,0,243,244,3,24,12,0,244,245,7,0,0,0,245,
  	246,6,11,-1,0,246,258,1,0,0,0,247,248,3,28,14,0,248,249,7,0,0,0,249,250,
  	6,11,-1,0,250,258,1,0,0,0,251,252,3,32,16,0,252,253,6,11,-1,0,253,258,
  	1,0,0,0,254,255,3,34,17,0,255,256,6,11,-1,0,256,258,1,0,0,0,257,243,1,
  	0,0,0,257,247,1,0,0,0,257,251,1,0,0,0,257,254,1,0,0,0,258,23,1,0,0,0,
  	259,260,3,26,13,0,260,261,5,38,0,0,261,262,3,36,18,0,262,263,6,12,-1,
  	0,263,25,1,0,0,0,264,269,5,45,0,0,265,266,5,37,0,0,266,268,5,45,0,0,267,
  	265,1,0,0,0,268,271,1,0,0,0,269,267,1,0,0,0,269,270,1,0,0,0,270,272,1,
  	0,0,0,271,269,1,0,0,0,272,273,6,13,-1,0,273,27,1,0,0,0,274,278,5,4,0,
  	0,275,276,5,45,0,0,276,277,5,35,0,0,277,279,6,14,-1,0,278,275,1,0,0,0,
  	278,279,1,0,0,0,279,280,1,0,0,0,280,281,3,30,15,0,281,282,6,14,-1,0,282,
  	29,1,0,0,0,283,284,5,45,0,0,284,294,6,15,-1,0,285,286,5,36,0,0,286,287,
  	5,45,0,0,287,293,6,15,-1,0,288,289,5,43,0,0,289,290,5,17,0,0,290,291,
  	5,44,0,0,291,293,6,15,-1,0,292,285,1,0,0,0,292,288,1,0,0,0,293,296,1,
  	0,0,0,294,292,1,0,0,0,294,295,1,0,0,0,295,297,1,0,0,0,296,294,1,0,0,0,
  	297,298,6,15,-1,0,298,31,1,0,0,0,299,300,3,26,13,0,300,301,5,38,0,0,301,
  	302,5,7,0,0,302,303,5,39,0,0,303,304,3,36,18,0,304,305,5,37,0,0,305,306,
  	5,45,0,0,306,307,5,38,0,0,307,308,3,36,18,0,308,309,5,40,0,0,309,310,
  	6,16,-1,0,310,319,5,31,0,0,311,316,5,45,0,0,312,313,5,37,0,0,313,315,
  	5,45,0,0,314,312,1,0,0,0,315,318,1,0,0,0,316,314,1,0,0,0,316,317,1,0,
  	0,0,317,320,1,0,0,0,318,316,1,0,0,0,319,311,1,0,0,0,319,320,1,0,0,0,320,
  	321,1,0,0,0,321,322,5,31,0,0,322,323,6,16,-1,0,323,327,5,41,0,0,324,326,
  	5,46,0,0,325,324,1,0,0,0,326,329,1,0,0,0,327,325,1,0,0,0,327,328,1,0,
  	0,0,328,336,1,0,0,0,329,327,1,0,0,0,330,335,5,46,0,0,331,332,3,22,11,
  	0,332,333,6,16,-1,0,333,335,1,0,0,0,334,330,1,0,0,0,334,331,1,0,0,0,335,
  	338,1,0,0,0,336,334,1,0,0,0,336,337,1,0,0,0,337,339,1,0,0,0,338,336,1,
  	0,0,0,339,340,5,42,0,0,340,341,7,0,0,0,341,342,6,16,-1,0,342,33,1,0,0,
  	0,343,344,5,45,0,0,344,345,5,38,0,0,345,346,5,8,0,0,346,347,5,45,0,0,
  	347,348,5,9,0,0,348,349,3,36,18,0,349,350,6,17,-1,0,350,354,5,41,0,0,
  	351,353,5,46,0,0,352,351,1,0,0,0,353,356,1,0,0,0,354,352,1,0,0,0,354,
  	355,1,0,0,0,355,363,1,0,0,0,356,354,1,0,0,0,357,362,5,46,0,0,358,359,
  	3,22,11,0,359,360,6,17,-1,0,360,362,1,0,0,0,361,357,1,0,0,0,361,358,1,
  	0,0,0,362,365,1,0,0,0,363,361,1,0,0,0,363,364,1,0,0,0,364,366,1,0,0,0,
  	365,363,1,0,0,0,366,367,5,42,0,0,367,368,7,0,0,0,368,369,6,17,-1,0,369,
  	35,1,0,0,0,370,371,3,38,19,0,371,372,6,18,-1,0,372,37,1,0,0,0,373,374,
  	3,40,20,0,374,381,6,19,-1,0,375,376,5,34,0,0,376,377,3,36,18,0,377,378,
  	5,35,0,0,378,379,3,36,18,0,379,380,6,19,-1,0,380,382,1,0,0,0,381,375,
  	1,0,0,0,381,382,1,0,0,0,382,39,1,0,0,0,383,384,6,20,-1,0,384,385,3,42,
  	21,0,385,386,6,20,-1,0,386,394,1,0,0,0,387,388,10,2,0,0,388,389,5,31,
  	0,0,389,390,3,42,21,0,390,391,6,20,-1,0,391,393,1,0,0,0,392,387,1,0,0,
  	0,393,396,1,0,0,0,394,392,1,0,0,0,394,395,1,0,0,0,395,41,1,0,0,0,396,
  	394,1,0,0,0,397,398,6,21,-1,0,398,399,3,44,22,0,399,400,6,21,-1,0,400,
  	408,1,0,0,0,401,402,10,2,0,0,402,403,5,32,0,0,403,404,3,44,22,0,404,405,
  	6,21,-1,0,405,407,1,0,0,0,406,401,1,0,0,0,407,410,1,0,0,0,408,406,1,0,
  	0,0,408,409,1,0,0,0,409,43,1,0,0,0,410,408,1,0,0,0,411,412,3,46,23,0,
  	412,417,6,22,-1,0,413,414,7,1,0,0,414,415,3,46,23,0,415,416,6,22,-1,0,
  	416,418,1,0,0,0,417,413,1,0,0,0,417,418,1,0,0,0,418,45,1,0,0,0,419,420,
  	6,23,-1,0,420,421,3,48,24,0,421,422,6,23,-1,0,422,430,1,0,0,0,423,424,
  	10,2,0,0,424,425,7,2,0,0,425,426,3,48,24,0,426,427,6,23,-1,0,427,429,
  	1,0,0,0,428,423,1,0,0,0,429,432,1,0,0,0,430,428,1,0,0,0,430,431,1,0,0,
  	0,431,47,1,0,0,0,432,430,1,0,0,0,433,434,6,24,-1,0,434,435,3,50,25,0,
  	435,436,6,24,-1,0,436,444,1,0,0,0,437,438,10,2,0,0,438,439,7,3,0,0,439,
  	440,3,50,25,0,440,441,6,24,-1,0,441,443,1,0,0,0,442,437,1,0,0,0,443,446,
  	1,0,0,0,444,442,1,0,0,0,444,445,1,0,0,0,445,49,1,0,0,0,446,444,1,0,0,
  	0,447,448,7,4,0,0,448,449,3,50,25,0,449,450,6,25,-1,0,450,455,1,0,0,0,
  	451,452,3,52,26,0,452,453,6,25,-1,0,453,455,1,0,0,0,454,447,1,0,0,0,454,
  	451,1,0,0,0,455,51,1,0,0,0,456,457,3,54,27,0,457,458,6,26,-1,0,458,463,
  	1,0,0,0,459,460,3,60,30,0,460,461,6,26,-1,0,461,463,1,0,0,0,462,456,1,
  	0,0,0,462,459,1,0,0,0,463,53,1,0,0,0,464,465,3,56,28,0,465,474,5,39,0,
  	0,466,471,3,58,29,0,467,468,5,37,0,0,468,470,3,58,29,0,469,467,1,0,0,
  	0,470,473,1,0,0,0,471,469,1,0,0,0,471,472,1,0,0,0,472,475,1,0,0,0,473,
  	471,1,0,0,0,474,466,1,0,0,0,474,475,1,0,0,0,475,476,1,0,0,0,476,477,5,
  	40,0,0,477,478,6,27,-1,0,478,55,1,0,0,0,479,484,5,45,0,0,480,481,5,36,
  	0,0,481,483,5,45,0,0,482,480,1,0,0,0,483,486,1,0,0,0,484,482,1,0,0,0,
  	484,485,1,0,0,0,485,487,1,0,0,0,486,484,1,0,0,0,487,488,6,28,-1,0,488,
  	57,1,0,0,0,489,490,5,45,0,0,490,491,5,38,0,0,491,493,6,29,-1,0,492,489,
  	1,0,0,0,492,493,1,0,0,0,493,494,1,0,0,0,494,495,3,36,18,0,495,496,6,29,
  	-1,0,496,59,1,0,0,0,497,498,5,17,0,0,498,519,6,30,-1,0,499,500,5,16,0,
  	0,500,519,6,30,-1,0,501,502,7,5,0,0,502,519,6,30,-1,0,503,504,3,64,32,
  	0,504,505,6,30,-1,0,505,519,1,0,0,0,506,507,5,12,0,0,507,519,6,30,-1,
  	0,508,509,5,45,0,0,509,519,6,30,-1,0,510,511,3,62,31,0,511,512,6,30,-1,
  	0,512,519,1,0,0,0,513,514,5,39,0,0,514,515,3,36,18,0,515,516,5,40,0,0,
  	516,517,6,30,-1,0,517,519,1,0,0,0,518,497,1,0,0,0,518,499,1,0,0,0,518,
  	501,1,0,0,0,518,503,1,0,0,0,518,506,1,0,0,0,518,508,1,0,0,0,518,510,1,
  	0,0,0,518,513,1,0,0,0,519,61,1,0,0,0,520,521,5,33,0,0,521,522,5,45,0,
  	0,522,523,6,31,-1,0,523,63,1,0,0,0,524,525,5,39,0,0,525,530,5,17,0,0,
  	526,527,5,37,0,0,527,529,5,17,0,0,528,526,1,0,0,0,529,532,1,0,0,0,530,
  	528,1,0,0,0,530,531,1,0,0,0,531,533,1,0,0,0,532,530,1,0,0,0,533,534,5,
  	40,0,0,534,535,6,32,-1,0,535,65,1,0,0,0,536,537,5,17,0,0,537,550,6,33,
  	-1,0,538,539,5,16,0,0,539,550,6,33,-1,0,540,541,7,5,0,0,541,550,6,33,
  	-1,0,542,543,3,64,32,0,543,544,6,33,-1,0,544,550,1,0,0,0,545,546,5,12,
  	0,0,546,550,6,33,-1,0,547,548,5,45,0,0,548,550,6,33,-1,0,549,536,1,0,
  	0,0,549,538,1,0,0,0,549,540,1,0,0,0,549,542,1,0,0,0,549,545,1,0,0,0,549,
  	547,1,0,0,0,550,67,1,0,0,0,551,552,6,34,-1,0,552,553,5,45,0,0,553,554,
  	5,23,0,0,554,555,3,68,34,0,555,556,5,24,0,0,556,557,6,34,-1,0,557,575,
  	1,0,0,0,558,559,5,45,0,0,559,568,5,41,0,0,560,565,5,45,0,0,561,562,5,
  	37,0,0,562,564,5,45,0,0,563,561,1,0,0,0,564,567,1,0,0,0,565,563,1,0,0,
  	0,565,566,1,0,0,0,566,569,1,0,0,0,567,565,1,0,0,0,568,560,1,0,0,0,568,
  	569,1,0,0,0,569,570,1,0,0,0,570,571,5,42,0,0,571,575,6,34,-1,0,572,573,
  	5,45,0,0,573,575,6,34,-1,0,574,551,1,0,0,0,574,558,1,0,0,0,574,572,1,
  	0,0,0,575,585,1,0,0,0,576,577,10,5,0,0,577,578,5,34,0,0,578,584,6,34,
  	-1,0,579,580,10,4,0,0,580,581,5,43,0,0,581,582,5,44,0,0,582,584,6,34,
  	-1,0,583,576,1,0,0,0,583,579,1,0,0,0,584,587,1,0,0,0,585,583,1,0,0,0,
  	585,586,1,0,0,0,586,69,1,0,0,0,587,585,1,0,0,0,49,87,89,99,103,116,132,
  	144,160,162,174,186,195,214,219,233,238,257,269,278,292,294,316,319,327,
  	334,336,354,361,363,381,394,408,417,430,444,454,462,471,474,484,492,518,
  	530,549,565,568,574,583,585
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  pggParserStaticData = std::move(staticData);
}

}

PggParser::PggParser(TokenStream *input) : PggParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

PggParser::PggParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  PggParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *pggParserStaticData->atn, pggParserStaticData->decisionToDFA, pggParserStaticData->sharedContextCache, options);
}

PggParser::~PggParser() {
  delete _interpreter;
}

const atn::ATN& PggParser::getATN() const {
  return *pggParserStaticData->atn;
}

std::string PggParser::getGrammarFileName() const {
  return "Pgg.g4";
}

const std::vector<std::string>& PggParser::getRuleNames() const {
  return pggParserStaticData->ruleNames;
}

const dfa::Vocabulary& PggParser::getVocabulary() const {
  return pggParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView PggParser::getSerializedATN() const {
  return pggParserStaticData->serializedATN;
}


//----------------- FileContext ------------------------------------------------------------------

PggParser::FileContext::FileContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::FileContext::EOF() {
  return getToken(PggParser::EOF, 0);
}

std::vector<tree::TerminalNode *> PggParser::FileContext::NEWLINE() {
  return getTokens(PggParser::NEWLINE);
}

tree::TerminalNode* PggParser::FileContext::NEWLINE(size_t i) {
  return getToken(PggParser::NEWLINE, i);
}

std::vector<PggParser::Import_stmtContext *> PggParser::FileContext::import_stmt() {
  return getRuleContexts<PggParser::Import_stmtContext>();
}

PggParser::Import_stmtContext* PggParser::FileContext::import_stmt(size_t i) {
  return getRuleContext<PggParser::Import_stmtContext>(i);
}

std::vector<PggParser::Param_stmtContext *> PggParser::FileContext::param_stmt() {
  return getRuleContexts<PggParser::Param_stmtContext>();
}

PggParser::Param_stmtContext* PggParser::FileContext::param_stmt(size_t i) {
  return getRuleContext<PggParser::Param_stmtContext>(i);
}

std::vector<PggParser::Def_stmtContext *> PggParser::FileContext::def_stmt() {
  return getRuleContexts<PggParser::Def_stmtContext>();
}

PggParser::Def_stmtContext* PggParser::FileContext::def_stmt(size_t i) {
  return getRuleContext<PggParser::Def_stmtContext>(i);
}

std::vector<PggParser::Output_stmtContext *> PggParser::FileContext::output_stmt() {
  return getRuleContexts<PggParser::Output_stmtContext>();
}

PggParser::Output_stmtContext* PggParser::FileContext::output_stmt(size_t i) {
  return getRuleContext<PggParser::Output_stmtContext>(i);
}

std::vector<PggParser::StmtContext *> PggParser::FileContext::stmt() {
  return getRuleContexts<PggParser::StmtContext>();
}

PggParser::StmtContext* PggParser::FileContext::stmt(size_t i) {
  return getRuleContext<PggParser::StmtContext>(i);
}


size_t PggParser::FileContext::getRuleIndex() const {
  return PggParser::RuleFile;
}


PggParser::FileContext* PggParser::file() {
  FileContext *_localctx = _tracker.createInstance<FileContext>(_ctx, getState());
  enterRule(_localctx, 0, PggParser::RuleFile);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
     gc->beginFile(); 
    setState(89);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116269618) != 0)) {
      setState(87);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(71);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::IMPORT: {
          setState(72);
          antlrcpp::downCast<FileContext *>(_localctx)->i = import_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->i->result); 
          break;
        }

        case PggParser::PARAM: {
          setState(75);
          antlrcpp::downCast<FileContext *>(_localctx)->p = param_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->p->result); 
          break;
        }

        case PggParser::DEF: {
          setState(78);
          antlrcpp::downCast<FileContext *>(_localctx)->d = def_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->d->result); 
          break;
        }

        case PggParser::OUTPUT: {
          setState(81);
          antlrcpp::downCast<FileContext *>(_localctx)->o = output_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->o->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(84);
          antlrcpp::downCast<FileContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(91);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(92);
    match(PggParser::EOF);
     antlrcpp::downCast<FileContext *>(_localctx)->result =  gc->endFile(spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Import_stmtContext ------------------------------------------------------------------

PggParser::Import_stmtContext::Import_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Import_stmtContext::IMPORT() {
  return getToken(PggParser::IMPORT, 0);
}

tree::TerminalNode* PggParser::Import_stmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

PggParser::Qualified_nameContext* PggParser::Import_stmtContext::qualified_name() {
  return getRuleContext<PggParser::Qualified_nameContext>(0);
}

tree::TerminalNode* PggParser::Import_stmtContext::AS() {
  return getToken(PggParser::AS, 0);
}

tree::TerminalNode* PggParser::Import_stmtContext::AT() {
  return getToken(PggParser::AT, 0);
}

tree::TerminalNode* PggParser::Import_stmtContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

tree::TerminalNode* PggParser::Import_stmtContext::NUMBER() {
  return getToken(PggParser::NUMBER, 0);
}


size_t PggParser::Import_stmtContext::getRuleIndex() const {
  return PggParser::RuleImport_stmt;
}


PggParser::Import_stmtContext* PggParser::import_stmt() {
  Import_stmtContext *_localctx = _tracker.createInstance<Import_stmtContext>(_ctx, getState());
  enterRule(_localctx, 2, PggParser::RuleImport_stmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(95);
    match(PggParser::IMPORT);
    setState(96);
    antlrcpp::downCast<Import_stmtContext *>(_localctx)->q = qualified_name();
    setState(99);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AS) {
      setState(97);
      match(PggParser::AS);
      setState(98);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->a = match(PggParser::IDENT);
    }
    setState(103);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AT) {
      setState(101);
      match(PggParser::AT);
      setState(102);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->v = match(PggParser::NUMBER);
    }
    setState(105);
    match(PggParser::NEWLINE);
     antlrcpp::downCast<Import_stmtContext *>(_localctx)->result =  gc->newImport(antlrcpp::downCast<Import_stmtContext *>(_localctx)->q->result, antlrcpp::downCast<Import_stmtContext *>(_localctx)->a, antlrcpp::downCast<Import_stmtContext *>(_localctx)->v, spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Param_stmtContext ------------------------------------------------------------------

PggParser::Param_stmtContext::Param_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Param_stmtContext::PARAM() {
  return getToken(PggParser::PARAM, 0);
}

tree::TerminalNode* PggParser::Param_stmtContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::Param_stmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

tree::TerminalNode* PggParser::Param_stmtContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

PggParser::TypeContext* PggParser::Param_stmtContext::type() {
  return getRuleContext<PggParser::TypeContext>(0);
}

tree::TerminalNode* PggParser::Param_stmtContext::ASSIGN() {
  return getToken(PggParser::ASSIGN, 0);
}

PggParser::LiteralContext* PggParser::Param_stmtContext::literal() {
  return getRuleContext<PggParser::LiteralContext>(0);
}


size_t PggParser::Param_stmtContext::getRuleIndex() const {
  return PggParser::RuleParam_stmt;
}


PggParser::Param_stmtContext* PggParser::param_stmt() {
  Param_stmtContext *_localctx = _tracker.createInstance<Param_stmtContext>(_ctx, getState());
  enterRule(_localctx, 4, PggParser::RuleParam_stmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(108);
    match(PggParser::PARAM);
    setState(109);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(110);
    match(PggParser::COLON);
    setState(111);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->t = type(0);
    setState(116);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(112);
      match(PggParser::ASSIGN);
      setState(113);
      antlrcpp::downCast<Param_stmtContext *>(_localctx)->d = literal();
       antlrcpp::downCast<Param_stmtContext *>(_localctx)->def =  antlrcpp::downCast<Param_stmtContext *>(_localctx)->d->result; antlrcpp::downCast<Param_stmtContext *>(_localctx)->hasDef =  true; 
    }
    setState(118);
    match(PggParser::NEWLINE);
     antlrcpp::downCast<Param_stmtContext *>(_localctx)->result =  gc->newParam((antlrcpp::downCast<Param_stmtContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Param_stmtContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<Param_stmtContext *>(_localctx)->n), antlrcpp::downCast<Param_stmtContext *>(_localctx)->t->result, _localctx->def, _localctx->hasDef, spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Output_stmtContext ------------------------------------------------------------------

PggParser::Output_stmtContext::Output_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Output_stmtContext::OUTPUT() {
  return getToken(PggParser::OUTPUT, 0);
}

tree::TerminalNode* PggParser::Output_stmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

tree::TerminalNode* PggParser::Output_stmtContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}


size_t PggParser::Output_stmtContext::getRuleIndex() const {
  return PggParser::RuleOutput_stmt;
}


PggParser::Output_stmtContext* PggParser::output_stmt() {
  Output_stmtContext *_localctx = _tracker.createInstance<Output_stmtContext>(_ctx, getState());
  enterRule(_localctx, 6, PggParser::RuleOutput_stmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(121);
    match(PggParser::OUTPUT);
    setState(122);
    antlrcpp::downCast<Output_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(123);
    match(PggParser::NEWLINE);
     antlrcpp::downCast<Output_stmtContext *>(_localctx)->result =  gc->newOutput((antlrcpp::downCast<Output_stmtContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Output_stmtContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<Output_stmtContext *>(_localctx)->n), spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Def_stmtContext ------------------------------------------------------------------

PggParser::Def_stmtContext::Def_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Def_stmtContext::DEF() {
  return getToken(PggParser::DEF, 0);
}

std::vector<tree::TerminalNode *> PggParser::Def_stmtContext::LPAREN() {
  return getTokens(PggParser::LPAREN);
}

tree::TerminalNode* PggParser::Def_stmtContext::LPAREN(size_t i) {
  return getToken(PggParser::LPAREN, i);
}

std::vector<tree::TerminalNode *> PggParser::Def_stmtContext::RPAREN() {
  return getTokens(PggParser::RPAREN);
}

tree::TerminalNode* PggParser::Def_stmtContext::RPAREN(size_t i) {
  return getToken(PggParser::RPAREN, i);
}

tree::TerminalNode* PggParser::Def_stmtContext::ARROW() {
  return getToken(PggParser::ARROW, 0);
}

tree::TerminalNode* PggParser::Def_stmtContext::LBRACE() {
  return getToken(PggParser::LBRACE, 0);
}

tree::TerminalNode* PggParser::Def_stmtContext::RBRACE() {
  return getToken(PggParser::RBRACE, 0);
}

tree::TerminalNode* PggParser::Def_stmtContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

PggParser::OutputsContext* PggParser::Def_stmtContext::outputs() {
  return getRuleContext<PggParser::OutputsContext>(0);
}

std::vector<tree::TerminalNode *> PggParser::Def_stmtContext::NEWLINE() {
  return getTokens(PggParser::NEWLINE);
}

tree::TerminalNode* PggParser::Def_stmtContext::NEWLINE(size_t i) {
  return getToken(PggParser::NEWLINE, i);
}

tree::TerminalNode* PggParser::Def_stmtContext::EOF() {
  return getToken(PggParser::EOF, 0);
}

PggParser::ParamsContext* PggParser::Def_stmtContext::params() {
  return getRuleContext<PggParser::ParamsContext>(0);
}

std::vector<tree::TerminalNode *> PggParser::Def_stmtContext::TRIPLE_STRING() {
  return getTokens(PggParser::TRIPLE_STRING);
}

tree::TerminalNode* PggParser::Def_stmtContext::TRIPLE_STRING(size_t i) {
  return getToken(PggParser::TRIPLE_STRING, i);
}

std::vector<PggParser::Expect_stmtContext *> PggParser::Def_stmtContext::expect_stmt() {
  return getRuleContexts<PggParser::Expect_stmtContext>();
}

PggParser::Expect_stmtContext* PggParser::Def_stmtContext::expect_stmt(size_t i) {
  return getRuleContext<PggParser::Expect_stmtContext>(i);
}

std::vector<PggParser::Ensure_stmtContext *> PggParser::Def_stmtContext::ensure_stmt() {
  return getRuleContexts<PggParser::Ensure_stmtContext>();
}

PggParser::Ensure_stmtContext* PggParser::Def_stmtContext::ensure_stmt(size_t i) {
  return getRuleContext<PggParser::Ensure_stmtContext>(i);
}

std::vector<PggParser::StmtContext *> PggParser::Def_stmtContext::stmt() {
  return getRuleContexts<PggParser::StmtContext>();
}

PggParser::StmtContext* PggParser::Def_stmtContext::stmt(size_t i) {
  return getRuleContext<PggParser::StmtContext>(i);
}


size_t PggParser::Def_stmtContext::getRuleIndex() const {
  return PggParser::RuleDef_stmt;
}


PggParser::Def_stmtContext* PggParser::def_stmt() {
  Def_stmtContext *_localctx = _tracker.createInstance<Def_stmtContext>(_ctx, getState());
  enterRule(_localctx, 8, PggParser::RuleDef_stmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(126);
    match(PggParser::DEF);
    setState(127);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(128);
    match(PggParser::LPAREN);
    setState(132);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(129);
      antlrcpp::downCast<Def_stmtContext *>(_localctx)->p = params();
       antlrcpp::downCast<Def_stmtContext *>(_localctx)->ps =  antlrcpp::downCast<Def_stmtContext *>(_localctx)->p->result; 
    }
    setState(134);
    match(PggParser::RPAREN);
    setState(135);
    match(PggParser::ARROW);
    setState(136);
    match(PggParser::LPAREN);
    setState(137);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->o = outputs();
    setState(138);
    match(PggParser::RPAREN);
     gc->beginDef((antlrcpp::downCast<Def_stmtContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->n), _localctx->ps, antlrcpp::downCast<Def_stmtContext *>(_localctx)->o->result, spanOf(_localctx)); 
    setState(140);
    match(PggParser::LBRACE);
    setState(144);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(141);
        match(PggParser::NEWLINE); 
      }
      setState(146);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    }
    setState(162);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116299292) != 0)) {
      setState(160);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(147);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TRIPLE_STRING: {
          setState(148);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc = match(PggParser::TRIPLE_STRING);
          setState(149);
          match(PggParser::NEWLINE);
           gc->defDoc((antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc)); 
          break;
        }

        case PggParser::EXPECT: {
          setState(151);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->e = expect_stmt();
           gc->addExpect(antlrcpp::downCast<Def_stmtContext *>(_localctx)->e->result); 
          break;
        }

        case PggParser::ENSURE: {
          setState(154);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->en = ensure_stmt();
           gc->addEnsure(antlrcpp::downCast<Def_stmtContext *>(_localctx)->en->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(157);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<Def_stmtContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(164);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(165);
    match(PggParser::RBRACE);
    setState(166);
    _la = _input->LA(1);
    if (!(_la == PggParser::EOF

    || _la == PggParser::NEWLINE)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
     antlrcpp::downCast<Def_stmtContext *>(_localctx)->result =  gc->endDef(spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParamsContext ------------------------------------------------------------------

PggParser::ParamsContext::ParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PggParser::ParamContext *> PggParser::ParamsContext::param() {
  return getRuleContexts<PggParser::ParamContext>();
}

PggParser::ParamContext* PggParser::ParamsContext::param(size_t i) {
  return getRuleContext<PggParser::ParamContext>(i);
}

std::vector<tree::TerminalNode *> PggParser::ParamsContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::ParamsContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::ParamsContext::getRuleIndex() const {
  return PggParser::RuleParams;
}


PggParser::ParamsContext* PggParser::params() {
  ParamsContext *_localctx = _tracker.createInstance<ParamsContext>(_ctx, getState());
  enterRule(_localctx, 10, PggParser::RuleParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(169);
    antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
    antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
    setState(174);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(170);
      match(PggParser::COMMA);
      setState(171);
      antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
      antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
      setState(176);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
     antlrcpp::downCast<ParamsContext *>(_localctx)->result =  gc->resultsOf(antlrcpp::downCast<ParamsContext *>(_localctx)->p); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ParamContext ------------------------------------------------------------------

PggParser::ParamContext::ParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::ParamContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::ParamContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

PggParser::TypeContext* PggParser::ParamContext::type() {
  return getRuleContext<PggParser::TypeContext>(0);
}

tree::TerminalNode* PggParser::ParamContext::ASSIGN() {
  return getToken(PggParser::ASSIGN, 0);
}

PggParser::LiteralContext* PggParser::ParamContext::literal() {
  return getRuleContext<PggParser::LiteralContext>(0);
}


size_t PggParser::ParamContext::getRuleIndex() const {
  return PggParser::RuleParam;
}


PggParser::ParamContext* PggParser::param() {
  ParamContext *_localctx = _tracker.createInstance<ParamContext>(_ctx, getState());
  enterRule(_localctx, 12, PggParser::RuleParam);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(179);
    antlrcpp::downCast<ParamContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(180);
    match(PggParser::COLON);
    setState(181);
    antlrcpp::downCast<ParamContext *>(_localctx)->t = type(0);
    setState(186);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(182);
      match(PggParser::ASSIGN);
      setState(183);
      antlrcpp::downCast<ParamContext *>(_localctx)->d = literal();
       antlrcpp::downCast<ParamContext *>(_localctx)->def =  antlrcpp::downCast<ParamContext *>(_localctx)->d->result; antlrcpp::downCast<ParamContext *>(_localctx)->hasDef =  true; 
    }
     antlrcpp::downCast<ParamContext *>(_localctx)->result =  gc->newDefParam((antlrcpp::downCast<ParamContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<ParamContext *>(_localctx)->n->getText() : ""), antlrcpp::downCast<ParamContext *>(_localctx)->t->result, _localctx->def, _localctx->hasDef); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- OutputsContext ------------------------------------------------------------------

PggParser::OutputsContext::OutputsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PggParser::Out_declContext *> PggParser::OutputsContext::out_decl() {
  return getRuleContexts<PggParser::Out_declContext>();
}

PggParser::Out_declContext* PggParser::OutputsContext::out_decl(size_t i) {
  return getRuleContext<PggParser::Out_declContext>(i);
}

std::vector<tree::TerminalNode *> PggParser::OutputsContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::OutputsContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::OutputsContext::getRuleIndex() const {
  return PggParser::RuleOutputs;
}


PggParser::OutputsContext* PggParser::outputs() {
  OutputsContext *_localctx = _tracker.createInstance<OutputsContext>(_ctx, getState());
  enterRule(_localctx, 14, PggParser::RuleOutputs);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(190);
    antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
    antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
    setState(195);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(191);
      match(PggParser::COMMA);
      setState(192);
      antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
      antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
      setState(197);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
     antlrcpp::downCast<OutputsContext *>(_localctx)->result =  gc->resultsOf(antlrcpp::downCast<OutputsContext *>(_localctx)->o); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Out_declContext ------------------------------------------------------------------

PggParser::Out_declContext::Out_declContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Out_declContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::Out_declContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

PggParser::TypeContext* PggParser::Out_declContext::type() {
  return getRuleContext<PggParser::TypeContext>(0);
}


size_t PggParser::Out_declContext::getRuleIndex() const {
  return PggParser::RuleOut_decl;
}


PggParser::Out_declContext* PggParser::out_decl() {
  Out_declContext *_localctx = _tracker.createInstance<Out_declContext>(_ctx, getState());
  enterRule(_localctx, 16, PggParser::RuleOut_decl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(200);
    antlrcpp::downCast<Out_declContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(201);
    match(PggParser::COLON);
    setState(202);
    antlrcpp::downCast<Out_declContext *>(_localctx)->t = type(0);
     antlrcpp::downCast<Out_declContext *>(_localctx)->result =  gc->newOutDecl((antlrcpp::downCast<Out_declContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Out_declContext *>(_localctx)->n->getText() : ""), antlrcpp::downCast<Out_declContext *>(_localctx)->t->result); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Expect_stmtContext ------------------------------------------------------------------

PggParser::Expect_stmtContext::Expect_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Expect_stmtContext::EXPECT() {
  return getToken(PggParser::EXPECT, 0);
}

tree::TerminalNode* PggParser::Expect_stmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

std::vector<tree::TerminalNode *> PggParser::Expect_stmtContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::Expect_stmtContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

PggParser::Attr_refContext* PggParser::Expect_stmtContext::attr_ref() {
  return getRuleContext<PggParser::Attr_refContext>(0);
}

PggParser::AexprContext* PggParser::Expect_stmtContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}

tree::TerminalNode* PggParser::Expect_stmtContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::Expect_stmtContext::STRING() {
  return getToken(PggParser::STRING, 0);
}


size_t PggParser::Expect_stmtContext::getRuleIndex() const {
  return PggParser::RuleExpect_stmt;
}


PggParser::Expect_stmtContext* PggParser::expect_stmt() {
  Expect_stmtContext *_localctx = _tracker.createInstance<Expect_stmtContext>(_ctx, getState());
  enterRule(_localctx, 18, PggParser::RuleExpect_stmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(205);
    match(PggParser::EXPECT);
    setState(214);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      setState(206);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(207);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(208);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Expect_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(211);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(219);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(216);
      match(PggParser::COLON);
      setState(217);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(221);
    match(PggParser::NEWLINE);
     antlrcpp::downCast<Expect_stmtContext *>(_localctx)->result =  gc->newContract(pgg::NodeKind::Expect, _localctx->formA, _localctx->id, _localctx->attr, _localctx->cond, _localctx->msg, _localctx->hasMsg,
                                      spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Ensure_stmtContext ------------------------------------------------------------------

PggParser::Ensure_stmtContext::Ensure_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Ensure_stmtContext::ENSURE() {
  return getToken(PggParser::ENSURE, 0);
}

tree::TerminalNode* PggParser::Ensure_stmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

std::vector<tree::TerminalNode *> PggParser::Ensure_stmtContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::Ensure_stmtContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

PggParser::Attr_refContext* PggParser::Ensure_stmtContext::attr_ref() {
  return getRuleContext<PggParser::Attr_refContext>(0);
}

PggParser::AexprContext* PggParser::Ensure_stmtContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}

tree::TerminalNode* PggParser::Ensure_stmtContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::Ensure_stmtContext::STRING() {
  return getToken(PggParser::STRING, 0);
}


size_t PggParser::Ensure_stmtContext::getRuleIndex() const {
  return PggParser::RuleEnsure_stmt;
}


PggParser::Ensure_stmtContext* PggParser::ensure_stmt() {
  Ensure_stmtContext *_localctx = _tracker.createInstance<Ensure_stmtContext>(_ctx, getState());
  enterRule(_localctx, 20, PggParser::RuleEnsure_stmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(224);
    match(PggParser::ENSURE);
    setState(233);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      setState(225);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(226);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(227);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(230);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(238);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(235);
      match(PggParser::COLON);
      setState(236);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(240);
    match(PggParser::NEWLINE);
     antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->result =  gc->newContract(pgg::NodeKind::Ensure, _localctx->formA, _localctx->id, _localctx->attr, _localctx->cond, _localctx->msg, _localctx->hasMsg,
                                      spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtContext ------------------------------------------------------------------

PggParser::StmtContext::StmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::BindingContext* PggParser::StmtContext::binding() {
  return getRuleContext<PggParser::BindingContext>(0);
}

tree::TerminalNode* PggParser::StmtContext::NEWLINE() {
  return getToken(PggParser::NEWLINE, 0);
}

tree::TerminalNode* PggParser::StmtContext::EOF() {
  return getToken(PggParser::EOF, 0);
}

PggParser::Tap_stmtContext* PggParser::StmtContext::tap_stmt() {
  return getRuleContext<PggParser::Tap_stmtContext>(0);
}

PggParser::Repeat_zoneContext* PggParser::StmtContext::repeat_zone() {
  return getRuleContext<PggParser::Repeat_zoneContext>(0);
}

PggParser::Foreach_zoneContext* PggParser::StmtContext::foreach_zone() {
  return getRuleContext<PggParser::Foreach_zoneContext>(0);
}


size_t PggParser::StmtContext::getRuleIndex() const {
  return PggParser::RuleStmt;
}


PggParser::StmtContext* PggParser::stmt() {
  StmtContext *_localctx = _tracker.createInstance<StmtContext>(_ctx, getState());
  enterRule(_localctx, 22, PggParser::RuleStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(257);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(243);
      antlrcpp::downCast<StmtContext *>(_localctx)->b = binding();
      setState(244);
      _la = _input->LA(1);
      if (!(_la == PggParser::EOF

      || _la == PggParser::NEWLINE)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->b->result; 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(247);
      antlrcpp::downCast<StmtContext *>(_localctx)->t = tap_stmt();
      setState(248);
      _la = _input->LA(1);
      if (!(_la == PggParser::EOF

      || _la == PggParser::NEWLINE)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->t->result; 
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(251);
      antlrcpp::downCast<StmtContext *>(_localctx)->r = repeat_zone();
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->r->result; 
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(254);
      antlrcpp::downCast<StmtContext *>(_localctx)->f = foreach_zone();
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->f->result; 
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BindingContext ------------------------------------------------------------------

PggParser::BindingContext::BindingContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::BindingContext::ASSIGN() {
  return getToken(PggParser::ASSIGN, 0);
}

PggParser::TargetsContext* PggParser::BindingContext::targets() {
  return getRuleContext<PggParser::TargetsContext>(0);
}

PggParser::AexprContext* PggParser::BindingContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}


size_t PggParser::BindingContext::getRuleIndex() const {
  return PggParser::RuleBinding;
}


PggParser::BindingContext* PggParser::binding() {
  BindingContext *_localctx = _tracker.createInstance<BindingContext>(_ctx, getState());
  enterRule(_localctx, 24, PggParser::RuleBinding);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(259);
    antlrcpp::downCast<BindingContext *>(_localctx)->t = targets();
    setState(260);
    match(PggParser::ASSIGN);
    setState(261);
    antlrcpp::downCast<BindingContext *>(_localctx)->v = aexpr();
     antlrcpp::downCast<BindingContext *>(_localctx)->result =  gc->newBinding(antlrcpp::downCast<BindingContext *>(_localctx)->t->result, antlrcpp::downCast<BindingContext *>(_localctx)->v->result, spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TargetsContext ------------------------------------------------------------------

PggParser::TargetsContext::TargetsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PggParser::TargetsContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::TargetsContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PggParser::TargetsContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::TargetsContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::TargetsContext::getRuleIndex() const {
  return PggParser::RuleTargets;
}


PggParser::TargetsContext* PggParser::targets() {
  TargetsContext *_localctx = _tracker.createInstance<TargetsContext>(_ctx, getState());
  enterRule(_localctx, 26, PggParser::RuleTargets);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(264);
    antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
    setState(269);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(265);
      match(PggParser::COMMA);
      setState(266);
      antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
      setState(271);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
     antlrcpp::downCast<TargetsContext *>(_localctx)->result =  gc->nameListOf(antlrcpp::downCast<TargetsContext *>(_localctx)->i); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Tap_stmtContext ------------------------------------------------------------------

PggParser::Tap_stmtContext::Tap_stmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Tap_stmtContext::TAP() {
  return getToken(PggParser::TAP, 0);
}

PggParser::PathContext* PggParser::Tap_stmtContext::path() {
  return getRuleContext<PggParser::PathContext>(0);
}

tree::TerminalNode* PggParser::Tap_stmtContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

tree::TerminalNode* PggParser::Tap_stmtContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}


size_t PggParser::Tap_stmtContext::getRuleIndex() const {
  return PggParser::RuleTap_stmt;
}


PggParser::Tap_stmtContext* PggParser::tap_stmt() {
  Tap_stmtContext *_localctx = _tracker.createInstance<Tap_stmtContext>(_ctx, getState());
  enterRule(_localctx, 28, PggParser::RuleTap_stmt);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(274);
    match(PggParser::TAP);
    setState(278);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx)) {
    case 1: {
      setState(275);
      antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l = match(PggParser::IDENT);
      setState(276);
      match(PggParser::COLON);
       antlrcpp::downCast<Tap_stmtContext *>(_localctx)->label =  (antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l != nullptr ? antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l->getText() : ""); antlrcpp::downCast<Tap_stmtContext *>(_localctx)->hasLabel =  true; 
      break;
    }

    default:
      break;
    }
    setState(280);
    antlrcpp::downCast<Tap_stmtContext *>(_localctx)->p = path();
     antlrcpp::downCast<Tap_stmtContext *>(_localctx)->result =  gc->newTap(_localctx->label, _localctx->hasLabel, antlrcpp::downCast<Tap_stmtContext *>(_localctx)->p->result, spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PathContext ------------------------------------------------------------------

PggParser::PathContext::PathContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PggParser::PathContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::PathContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PggParser::PathContext::DOT() {
  return getTokens(PggParser::DOT);
}

tree::TerminalNode* PggParser::PathContext::DOT(size_t i) {
  return getToken(PggParser::DOT, i);
}

std::vector<tree::TerminalNode *> PggParser::PathContext::LBRACKET() {
  return getTokens(PggParser::LBRACKET);
}

tree::TerminalNode* PggParser::PathContext::LBRACKET(size_t i) {
  return getToken(PggParser::LBRACKET, i);
}

std::vector<tree::TerminalNode *> PggParser::PathContext::RBRACKET() {
  return getTokens(PggParser::RBRACKET);
}

tree::TerminalNode* PggParser::PathContext::RBRACKET(size_t i) {
  return getToken(PggParser::RBRACKET, i);
}

std::vector<tree::TerminalNode *> PggParser::PathContext::NUMBER() {
  return getTokens(PggParser::NUMBER);
}

tree::TerminalNode* PggParser::PathContext::NUMBER(size_t i) {
  return getToken(PggParser::NUMBER, i);
}


size_t PggParser::PathContext::getRuleIndex() const {
  return PggParser::RulePath;
}


PggParser::PathContext* PggParser::path() {
  PathContext *_localctx = _tracker.createInstance<PathContext>(_ctx, getState());
  enterRule(_localctx, 30, PggParser::RulePath);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(283);
    antlrcpp::downCast<PathContext *>(_localctx)->i = match(PggParser::IDENT);
     _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->i->getText() : ""))); 
    setState(294);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT

    || _la == PggParser::LBRACKET) {
      setState(292);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::DOT: {
          setState(285);
          match(PggParser::DOT);
          setState(286);
          antlrcpp::downCast<PathContext *>(_localctx)->j = match(PggParser::IDENT);
           _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->j != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->j->getText() : ""))); 
          break;
        }

        case PggParser::LBRACKET: {
          setState(288);
          match(PggParser::LBRACKET);
          setState(289);
          antlrcpp::downCast<PathContext *>(_localctx)->n = match(PggParser::NUMBER);
          setState(290);
          match(PggParser::RBRACKET);
           _localctx->elems.push_back(gc->pathIndex((antlrcpp::downCast<PathContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->n->getText() : ""))); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(296);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
     antlrcpp::downCast<PathContext *>(_localctx)->result =  _localctx->elems; 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Repeat_zoneContext ------------------------------------------------------------------

PggParser::Repeat_zoneContext::Repeat_zoneContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PggParser::Repeat_zoneContext::ASSIGN() {
  return getTokens(PggParser::ASSIGN);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::ASSIGN(size_t i) {
  return getToken(PggParser::ASSIGN, i);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::REPEAT() {
  return getToken(PggParser::REPEAT, 0);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::LPAREN() {
  return getToken(PggParser::LPAREN, 0);
}

std::vector<tree::TerminalNode *> PggParser::Repeat_zoneContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::RPAREN() {
  return getToken(PggParser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> PggParser::Repeat_zoneContext::PIPE() {
  return getTokens(PggParser::PIPE);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::PIPE(size_t i) {
  return getToken(PggParser::PIPE, i);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::LBRACE() {
  return getToken(PggParser::LBRACE, 0);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::RBRACE() {
  return getToken(PggParser::RBRACE, 0);
}

PggParser::TargetsContext* PggParser::Repeat_zoneContext::targets() {
  return getRuleContext<PggParser::TargetsContext>(0);
}

std::vector<PggParser::AexprContext *> PggParser::Repeat_zoneContext::aexpr() {
  return getRuleContexts<PggParser::AexprContext>();
}

PggParser::AexprContext* PggParser::Repeat_zoneContext::aexpr(size_t i) {
  return getRuleContext<PggParser::AexprContext>(i);
}

std::vector<tree::TerminalNode *> PggParser::Repeat_zoneContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PggParser::Repeat_zoneContext::NEWLINE() {
  return getTokens(PggParser::NEWLINE);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::NEWLINE(size_t i) {
  return getToken(PggParser::NEWLINE, i);
}

tree::TerminalNode* PggParser::Repeat_zoneContext::EOF() {
  return getToken(PggParser::EOF, 0);
}

std::vector<PggParser::StmtContext *> PggParser::Repeat_zoneContext::stmt() {
  return getRuleContexts<PggParser::StmtContext>();
}

PggParser::StmtContext* PggParser::Repeat_zoneContext::stmt(size_t i) {
  return getRuleContext<PggParser::StmtContext>(i);
}


size_t PggParser::Repeat_zoneContext::getRuleIndex() const {
  return PggParser::RuleRepeat_zone;
}


PggParser::Repeat_zoneContext* PggParser::repeat_zone() {
  Repeat_zoneContext *_localctx = _tracker.createInstance<Repeat_zoneContext>(_ctx, getState());
  enterRule(_localctx, 32, PggParser::RuleRepeat_zone);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(299);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t = targets();
    setState(300);
    match(PggParser::ASSIGN);
    setState(301);
    match(PggParser::REPEAT);
    setState(302);
    match(PggParser::LPAREN);
    setState(303);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v = aexpr();
    setState(304);
    match(PggParser::COMMA);
    setState(305);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it = match(PggParser::IDENT);
    setState(306);
    match(PggParser::ASSIGN);
    setState(307);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n = aexpr();
    setState(308);
    match(PggParser::RPAREN);
     gc->checkKeyword(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it, "iterations"); 
    setState(310);
    match(PggParser::PIPE);
    setState(319);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(311);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
      setState(316);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(312);
        match(PggParser::COMMA);
        setState(313);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
        setState(318);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(321);
    match(PggParser::PIPE);
     gc->beginRepeat(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n->result, gc->nameListOf(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s), spanOf(_localctx)); 
    setState(323);
    match(PggParser::LBRACE);
    setState(327);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(324);
        match(PggParser::NEWLINE); 
      }
      setState(329);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    }
    setState(336);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(334);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(330);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(331);
          antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(338);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(339);
    match(PggParser::RBRACE);
    setState(340);
    _la = _input->LA(1);
    if (!(_la == PggParser::EOF

    || _la == PggParser::NEWLINE)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
     antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->result =  gc->endRepeat(spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Foreach_zoneContext ------------------------------------------------------------------

PggParser::Foreach_zoneContext::Foreach_zoneContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Foreach_zoneContext::ASSIGN() {
  return getToken(PggParser::ASSIGN, 0);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::FOREACH() {
  return getToken(PggParser::FOREACH, 0);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::IN() {
  return getToken(PggParser::IN, 0);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::LBRACE() {
  return getToken(PggParser::LBRACE, 0);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::RBRACE() {
  return getToken(PggParser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> PggParser::Foreach_zoneContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

PggParser::AexprContext* PggParser::Foreach_zoneContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}

std::vector<tree::TerminalNode *> PggParser::Foreach_zoneContext::NEWLINE() {
  return getTokens(PggParser::NEWLINE);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::NEWLINE(size_t i) {
  return getToken(PggParser::NEWLINE, i);
}

tree::TerminalNode* PggParser::Foreach_zoneContext::EOF() {
  return getToken(PggParser::EOF, 0);
}

std::vector<PggParser::StmtContext *> PggParser::Foreach_zoneContext::stmt() {
  return getRuleContexts<PggParser::StmtContext>();
}

PggParser::StmtContext* PggParser::Foreach_zoneContext::stmt(size_t i) {
  return getRuleContext<PggParser::StmtContext>(i);
}


size_t PggParser::Foreach_zoneContext::getRuleIndex() const {
  return PggParser::RuleForeach_zone;
}


PggParser::Foreach_zoneContext* PggParser::foreach_zone() {
  Foreach_zoneContext *_localctx = _tracker.createInstance<Foreach_zoneContext>(_ctx, getState());
  enterRule(_localctx, 34, PggParser::RuleForeach_zone);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(343);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt = match(PggParser::IDENT);
    setState(344);
    match(PggParser::ASSIGN);
    setState(345);
    match(PggParser::FOREACH);
    setState(346);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item = match(PggParser::IDENT);
    setState(347);
    match(PggParser::IN);
    setState(348);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c = aexpr();
     gc->beginForeach((antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt), (antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item), antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c->result,
                             spanOf(_localctx)); 
    setState(350);
    match(PggParser::LBRACE);
    setState(354);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(351);
        match(PggParser::NEWLINE); 
      }
      setState(356);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    }
    setState(363);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(361);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(357);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(358);
          antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(365);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(366);
    match(PggParser::RBRACE);
    setState(367);
    _la = _input->LA(1);
    if (!(_la == PggParser::EOF

    || _la == PggParser::NEWLINE)) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
     antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->result =  gc->endForeach(spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- AexprContext ------------------------------------------------------------------

PggParser::AexprContext::AexprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::TernaryContext* PggParser::AexprContext::ternary() {
  return getRuleContext<PggParser::TernaryContext>(0);
}


size_t PggParser::AexprContext::getRuleIndex() const {
  return PggParser::RuleAexpr;
}


PggParser::AexprContext* PggParser::aexpr() {
  AexprContext *_localctx = _tracker.createInstance<AexprContext>(_ctx, getState());
  enterRule(_localctx, 36, PggParser::RuleAexpr);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(370);
    antlrcpp::downCast<AexprContext *>(_localctx)->t = ternary();
     antlrcpp::downCast<AexprContext *>(_localctx)->result =  antlrcpp::downCast<AexprContext *>(_localctx)->t->result; 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TernaryContext ------------------------------------------------------------------

PggParser::TernaryContext::TernaryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::Or_exprContext* PggParser::TernaryContext::or_expr() {
  return getRuleContext<PggParser::Or_exprContext>(0);
}

tree::TerminalNode* PggParser::TernaryContext::QUESTION() {
  return getToken(PggParser::QUESTION, 0);
}

tree::TerminalNode* PggParser::TernaryContext::COLON() {
  return getToken(PggParser::COLON, 0);
}

std::vector<PggParser::AexprContext *> PggParser::TernaryContext::aexpr() {
  return getRuleContexts<PggParser::AexprContext>();
}

PggParser::AexprContext* PggParser::TernaryContext::aexpr(size_t i) {
  return getRuleContext<PggParser::AexprContext>(i);
}


size_t PggParser::TernaryContext::getRuleIndex() const {
  return PggParser::RuleTernary;
}


PggParser::TernaryContext* PggParser::ternary() {
  TernaryContext *_localctx = _tracker.createInstance<TernaryContext>(_ctx, getState());
  enterRule(_localctx, 38, PggParser::RuleTernary);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(373);
    antlrcpp::downCast<TernaryContext *>(_localctx)->c = or_expr(0);
     antlrcpp::downCast<TernaryContext *>(_localctx)->result =  antlrcpp::downCast<TernaryContext *>(_localctx)->c->result; 
    setState(381);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::QUESTION) {
      setState(375);
      match(PggParser::QUESTION);
      setState(376);
      antlrcpp::downCast<TernaryContext *>(_localctx)->t = aexpr();
      setState(377);
      match(PggParser::COLON);
      setState(378);
      antlrcpp::downCast<TernaryContext *>(_localctx)->e = aexpr();
       antlrcpp::downCast<TernaryContext *>(_localctx)->result =  gc->newTernary(antlrcpp::downCast<TernaryContext *>(_localctx)->c->result, antlrcpp::downCast<TernaryContext *>(_localctx)->t->result, antlrcpp::downCast<TernaryContext *>(_localctx)->e->result, spanOf(_localctx)); 
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Or_exprContext ------------------------------------------------------------------

PggParser::Or_exprContext::Or_exprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::And_exprContext* PggParser::Or_exprContext::and_expr() {
  return getRuleContext<PggParser::And_exprContext>(0);
}

tree::TerminalNode* PggParser::Or_exprContext::PIPE() {
  return getToken(PggParser::PIPE, 0);
}

PggParser::Or_exprContext* PggParser::Or_exprContext::or_expr() {
  return getRuleContext<PggParser::Or_exprContext>(0);
}


size_t PggParser::Or_exprContext::getRuleIndex() const {
  return PggParser::RuleOr_expr;
}



PggParser::Or_exprContext* PggParser::or_expr() {
   return or_expr(0);
}

PggParser::Or_exprContext* PggParser::or_expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  PggParser::Or_exprContext *_localctx = _tracker.createInstance<Or_exprContext>(_ctx, parentState);
  PggParser::Or_exprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 40;
  enterRecursionRule(_localctx, 40, PggParser::RuleOr_expr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(384);
    antlrcpp::downCast<Or_exprContext *>(_localctx)->a = and_expr(0);
     antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  antlrcpp::downCast<Or_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(394);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<Or_exprContext>(parentContext, parentState);
        _localctx->l = previousContext;
        pushNewRecursionContext(_localctx, startState, RuleOr_expr);
        setState(387);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(388);
        match(PggParser::PIPE);
        setState(389);
        antlrcpp::downCast<Or_exprContext *>(_localctx)->r = and_expr(0);
         antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  gc->newBinary("|", antlrcpp::downCast<Or_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Or_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(396);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- And_exprContext ------------------------------------------------------------------

PggParser::And_exprContext::And_exprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::Cmp_exprContext* PggParser::And_exprContext::cmp_expr() {
  return getRuleContext<PggParser::Cmp_exprContext>(0);
}

tree::TerminalNode* PggParser::And_exprContext::AMP() {
  return getToken(PggParser::AMP, 0);
}

PggParser::And_exprContext* PggParser::And_exprContext::and_expr() {
  return getRuleContext<PggParser::And_exprContext>(0);
}


size_t PggParser::And_exprContext::getRuleIndex() const {
  return PggParser::RuleAnd_expr;
}



PggParser::And_exprContext* PggParser::and_expr() {
   return and_expr(0);
}

PggParser::And_exprContext* PggParser::and_expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  PggParser::And_exprContext *_localctx = _tracker.createInstance<And_exprContext>(_ctx, parentState);
  PggParser::And_exprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 42;
  enterRecursionRule(_localctx, 42, PggParser::RuleAnd_expr, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(398);
    antlrcpp::downCast<And_exprContext *>(_localctx)->a = cmp_expr();
     antlrcpp::downCast<And_exprContext *>(_localctx)->result =  antlrcpp::downCast<And_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(408);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<And_exprContext>(parentContext, parentState);
        _localctx->l = previousContext;
        pushNewRecursionContext(_localctx, startState, RuleAnd_expr);
        setState(401);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(402);
        match(PggParser::AMP);
        setState(403);
        antlrcpp::downCast<And_exprContext *>(_localctx)->r = cmp_expr();
         antlrcpp::downCast<And_exprContext *>(_localctx)->result =  gc->newBinary("&", antlrcpp::downCast<And_exprContext *>(_localctx)->l->result, antlrcpp::downCast<And_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(410);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- Cmp_exprContext ------------------------------------------------------------------

PggParser::Cmp_exprContext::Cmp_exprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PggParser::Add_exprContext *> PggParser::Cmp_exprContext::add_expr() {
  return getRuleContexts<PggParser::Add_exprContext>();
}

PggParser::Add_exprContext* PggParser::Cmp_exprContext::add_expr(size_t i) {
  return getRuleContext<PggParser::Add_exprContext>(i);
}

tree::TerminalNode* PggParser::Cmp_exprContext::LT() {
  return getToken(PggParser::LT, 0);
}

tree::TerminalNode* PggParser::Cmp_exprContext::GT() {
  return getToken(PggParser::GT, 0);
}

tree::TerminalNode* PggParser::Cmp_exprContext::LTE() {
  return getToken(PggParser::LTE, 0);
}

tree::TerminalNode* PggParser::Cmp_exprContext::GTE() {
  return getToken(PggParser::GTE, 0);
}

tree::TerminalNode* PggParser::Cmp_exprContext::EQ() {
  return getToken(PggParser::EQ, 0);
}

tree::TerminalNode* PggParser::Cmp_exprContext::NEQ() {
  return getToken(PggParser::NEQ, 0);
}


size_t PggParser::Cmp_exprContext::getRuleIndex() const {
  return PggParser::RuleCmp_expr;
}


PggParser::Cmp_exprContext* PggParser::cmp_expr() {
  Cmp_exprContext *_localctx = _tracker.createInstance<Cmp_exprContext>(_ctx, getState());
  enterRule(_localctx, 44, PggParser::RuleCmp_expr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(411);
    antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l = add_expr(0);
     antlrcpp::downCast<Cmp_exprContext *>(_localctx)->result =  antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l->result; 
    setState(417);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx)) {
    case 1: {
      setState(413);
      antlrcpp::downCast<Cmp_exprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 33030144) != 0))) {
        antlrcpp::downCast<Cmp_exprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(414);
      antlrcpp::downCast<Cmp_exprContext *>(_localctx)->r = add_expr(0);
       antlrcpp::downCast<Cmp_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Cmp_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Cmp_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Cmp_exprContext *>(_localctx)->r->result, spanOf(_localctx)); 
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Add_exprContext ------------------------------------------------------------------

PggParser::Add_exprContext::Add_exprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::Mul_exprContext* PggParser::Add_exprContext::mul_expr() {
  return getRuleContext<PggParser::Mul_exprContext>(0);
}

PggParser::Add_exprContext* PggParser::Add_exprContext::add_expr() {
  return getRuleContext<PggParser::Add_exprContext>(0);
}

tree::TerminalNode* PggParser::Add_exprContext::PLUS() {
  return getToken(PggParser::PLUS, 0);
}

tree::TerminalNode* PggParser::Add_exprContext::MINUS() {
  return getToken(PggParser::MINUS, 0);
}


size_t PggParser::Add_exprContext::getRuleIndex() const {
  return PggParser::RuleAdd_expr;
}



PggParser::Add_exprContext* PggParser::add_expr() {
   return add_expr(0);
}

PggParser::Add_exprContext* PggParser::add_expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  PggParser::Add_exprContext *_localctx = _tracker.createInstance<Add_exprContext>(_ctx, parentState);
  PggParser::Add_exprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 46;
  enterRecursionRule(_localctx, 46, PggParser::RuleAdd_expr, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(420);
    antlrcpp::downCast<Add_exprContext *>(_localctx)->a = mul_expr(0);
     antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  antlrcpp::downCast<Add_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(430);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<Add_exprContext>(parentContext, parentState);
        _localctx->l = previousContext;
        pushNewRecursionContext(_localctx, startState, RuleAdd_expr);
        setState(423);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(424);
        antlrcpp::downCast<Add_exprContext *>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == PggParser::PLUS

        || _la == PggParser::MINUS)) {
          antlrcpp::downCast<Add_exprContext *>(_localctx)->op = _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(425);
        antlrcpp::downCast<Add_exprContext *>(_localctx)->r = mul_expr(0);
         antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Add_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Add_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Add_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Add_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(432);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- Mul_exprContext ------------------------------------------------------------------

PggParser::Mul_exprContext::Mul_exprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::UnaryContext* PggParser::Mul_exprContext::unary() {
  return getRuleContext<PggParser::UnaryContext>(0);
}

PggParser::Mul_exprContext* PggParser::Mul_exprContext::mul_expr() {
  return getRuleContext<PggParser::Mul_exprContext>(0);
}

tree::TerminalNode* PggParser::Mul_exprContext::STAR() {
  return getToken(PggParser::STAR, 0);
}

tree::TerminalNode* PggParser::Mul_exprContext::SLASH() {
  return getToken(PggParser::SLASH, 0);
}

tree::TerminalNode* PggParser::Mul_exprContext::PERCENT() {
  return getToken(PggParser::PERCENT, 0);
}


size_t PggParser::Mul_exprContext::getRuleIndex() const {
  return PggParser::RuleMul_expr;
}



PggParser::Mul_exprContext* PggParser::mul_expr() {
   return mul_expr(0);
}

PggParser::Mul_exprContext* PggParser::mul_expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  PggParser::Mul_exprContext *_localctx = _tracker.createInstance<Mul_exprContext>(_ctx, parentState);
  PggParser::Mul_exprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 48;
  enterRecursionRule(_localctx, 48, PggParser::RuleMul_expr, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(434);
    antlrcpp::downCast<Mul_exprContext *>(_localctx)->a = unary();
     antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  antlrcpp::downCast<Mul_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(444);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        _localctx = _tracker.createInstance<Mul_exprContext>(parentContext, parentState);
        _localctx->l = previousContext;
        pushNewRecursionContext(_localctx, startState, RuleMul_expr);
        setState(437);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(438);
        antlrcpp::downCast<Mul_exprContext *>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 939524096) != 0))) {
          antlrcpp::downCast<Mul_exprContext *>(_localctx)->op = _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(439);
        antlrcpp::downCast<Mul_exprContext *>(_localctx)->r = unary();
         antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Mul_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Mul_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Mul_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Mul_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(446);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- UnaryContext ------------------------------------------------------------------

PggParser::UnaryContext::UnaryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::UnaryContext* PggParser::UnaryContext::unary() {
  return getRuleContext<PggParser::UnaryContext>(0);
}

tree::TerminalNode* PggParser::UnaryContext::MINUS() {
  return getToken(PggParser::MINUS, 0);
}

tree::TerminalNode* PggParser::UnaryContext::BANG() {
  return getToken(PggParser::BANG, 0);
}

PggParser::PostfixContext* PggParser::UnaryContext::postfix() {
  return getRuleContext<PggParser::PostfixContext>(0);
}


size_t PggParser::UnaryContext::getRuleIndex() const {
  return PggParser::RuleUnary;
}


PggParser::UnaryContext* PggParser::unary() {
  UnaryContext *_localctx = _tracker.createInstance<UnaryContext>(_ctx, getState());
  enterRule(_localctx, 50, PggParser::RuleUnary);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(454);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::MINUS:
      case PggParser::BANG: {
        enterOuterAlt(_localctx, 1);
        setState(447);
        antlrcpp::downCast<UnaryContext *>(_localctx)->op = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == PggParser::MINUS

        || _la == PggParser::BANG)) {
          antlrcpp::downCast<UnaryContext *>(_localctx)->op = _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(448);
        antlrcpp::downCast<UnaryContext *>(_localctx)->u = unary();
         antlrcpp::downCast<UnaryContext *>(_localctx)->result =  gc->newUnary((antlrcpp::downCast<UnaryContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<UnaryContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<UnaryContext *>(_localctx)->u->result, spanOf(_localctx)); 
        break;
      }

      case PggParser::NONE:
      case PggParser::TRUE:
      case PggParser::FALSE:
      case PggParser::STRING:
      case PggParser::NUMBER:
      case PggParser::AT:
      case PggParser::LPAREN:
      case PggParser::IDENT: {
        enterOuterAlt(_localctx, 2);
        setState(451);
        antlrcpp::downCast<UnaryContext *>(_localctx)->p = postfix();
         antlrcpp::downCast<UnaryContext *>(_localctx)->result =  antlrcpp::downCast<UnaryContext *>(_localctx)->p->result; 
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PostfixContext ------------------------------------------------------------------

PggParser::PostfixContext::PostfixContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::CallContext* PggParser::PostfixContext::call() {
  return getRuleContext<PggParser::CallContext>(0);
}

PggParser::PrimaryContext* PggParser::PostfixContext::primary() {
  return getRuleContext<PggParser::PrimaryContext>(0);
}


size_t PggParser::PostfixContext::getRuleIndex() const {
  return PggParser::RulePostfix;
}


PggParser::PostfixContext* PggParser::postfix() {
  PostfixContext *_localctx = _tracker.createInstance<PostfixContext>(_ctx, getState());
  enterRule(_localctx, 52, PggParser::RulePostfix);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(462);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(456);
      antlrcpp::downCast<PostfixContext *>(_localctx)->c = call();
       antlrcpp::downCast<PostfixContext *>(_localctx)->result =  antlrcpp::downCast<PostfixContext *>(_localctx)->c->result; 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(459);
      antlrcpp::downCast<PostfixContext *>(_localctx)->p = primary();
       antlrcpp::downCast<PostfixContext *>(_localctx)->result =  antlrcpp::downCast<PostfixContext *>(_localctx)->p->result; 
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CallContext ------------------------------------------------------------------

PggParser::CallContext::CallContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::CallContext::LPAREN() {
  return getToken(PggParser::LPAREN, 0);
}

tree::TerminalNode* PggParser::CallContext::RPAREN() {
  return getToken(PggParser::RPAREN, 0);
}

PggParser::Qualified_nameContext* PggParser::CallContext::qualified_name() {
  return getRuleContext<PggParser::Qualified_nameContext>(0);
}

std::vector<PggParser::ArgContext *> PggParser::CallContext::arg() {
  return getRuleContexts<PggParser::ArgContext>();
}

PggParser::ArgContext* PggParser::CallContext::arg(size_t i) {
  return getRuleContext<PggParser::ArgContext>(i);
}

std::vector<tree::TerminalNode *> PggParser::CallContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::CallContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::CallContext::getRuleIndex() const {
  return PggParser::RuleCall;
}


PggParser::CallContext* PggParser::call() {
  CallContext *_localctx = _tracker.createInstance<CallContext>(_ctx, getState());
  enterRule(_localctx, 54, PggParser::RuleCall);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(464);
    antlrcpp::downCast<CallContext *>(_localctx)->q = qualified_name();
    setState(465);
    match(PggParser::LPAREN);
    setState(474);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 35743858913280) != 0)) {
      setState(466);
      antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
      antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
      setState(471);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(467);
        match(PggParser::COMMA);
        setState(468);
        antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
        antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
        setState(473);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(476);
    match(PggParser::RPAREN);
     antlrcpp::downCast<CallContext *>(_localctx)->result =  gc->newCall(antlrcpp::downCast<CallContext *>(_localctx)->q->result, gc->resultsOf(antlrcpp::downCast<CallContext *>(_localctx)->a), spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Qualified_nameContext ------------------------------------------------------------------

PggParser::Qualified_nameContext::Qualified_nameContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PggParser::Qualified_nameContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::Qualified_nameContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PggParser::Qualified_nameContext::DOT() {
  return getTokens(PggParser::DOT);
}

tree::TerminalNode* PggParser::Qualified_nameContext::DOT(size_t i) {
  return getToken(PggParser::DOT, i);
}


size_t PggParser::Qualified_nameContext::getRuleIndex() const {
  return PggParser::RuleQualified_name;
}


PggParser::Qualified_nameContext* PggParser::qualified_name() {
  Qualified_nameContext *_localctx = _tracker.createInstance<Qualified_nameContext>(_ctx, getState());
  enterRule(_localctx, 56, PggParser::RuleQualified_name);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(479);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
    setState(484);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT) {
      setState(480);
      match(PggParser::DOT);
      setState(481);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
      setState(486);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
     antlrcpp::downCast<Qualified_nameContext *>(_localctx)->result =  gc->namesOf(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArgContext ------------------------------------------------------------------

PggParser::ArgContext::ArgContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PggParser::AexprContext* PggParser::ArgContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}

tree::TerminalNode* PggParser::ArgContext::ASSIGN() {
  return getToken(PggParser::ASSIGN, 0);
}

tree::TerminalNode* PggParser::ArgContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}


size_t PggParser::ArgContext::getRuleIndex() const {
  return PggParser::RuleArg;
}


PggParser::ArgContext* PggParser::arg() {
  ArgContext *_localctx = _tracker.createInstance<ArgContext>(_ctx, getState());
  enterRule(_localctx, 58, PggParser::RuleArg);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(492);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 40, _ctx)) {
    case 1: {
      setState(489);
      antlrcpp::downCast<ArgContext *>(_localctx)->n = match(PggParser::IDENT);
      setState(490);
      match(PggParser::ASSIGN);
       _localctx->a.name = (antlrcpp::downCast<ArgContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<ArgContext *>(_localctx)->n->getText() : ""); _localctx->a.hasName = true; 
      break;
    }

    default:
      break;
    }
    setState(494);
    antlrcpp::downCast<ArgContext *>(_localctx)->v = aexpr();
     _localctx->a.value = antlrcpp::downCast<ArgContext *>(_localctx)->v->result; antlrcpp::downCast<ArgContext *>(_localctx)->result =  _localctx->a; 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryContext ------------------------------------------------------------------

PggParser::PrimaryContext::PrimaryContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::PrimaryContext::NUMBER() {
  return getToken(PggParser::NUMBER, 0);
}

tree::TerminalNode* PggParser::PrimaryContext::STRING() {
  return getToken(PggParser::STRING, 0);
}

tree::TerminalNode* PggParser::PrimaryContext::TRUE() {
  return getToken(PggParser::TRUE, 0);
}

tree::TerminalNode* PggParser::PrimaryContext::FALSE() {
  return getToken(PggParser::FALSE, 0);
}

PggParser::Vec_literalContext* PggParser::PrimaryContext::vec_literal() {
  return getRuleContext<PggParser::Vec_literalContext>(0);
}

tree::TerminalNode* PggParser::PrimaryContext::NONE() {
  return getToken(PggParser::NONE, 0);
}

tree::TerminalNode* PggParser::PrimaryContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}

PggParser::Attr_refContext* PggParser::PrimaryContext::attr_ref() {
  return getRuleContext<PggParser::Attr_refContext>(0);
}

tree::TerminalNode* PggParser::PrimaryContext::LPAREN() {
  return getToken(PggParser::LPAREN, 0);
}

tree::TerminalNode* PggParser::PrimaryContext::RPAREN() {
  return getToken(PggParser::RPAREN, 0);
}

PggParser::AexprContext* PggParser::PrimaryContext::aexpr() {
  return getRuleContext<PggParser::AexprContext>(0);
}


size_t PggParser::PrimaryContext::getRuleIndex() const {
  return PggParser::RulePrimary;
}


PggParser::PrimaryContext* PggParser::primary() {
  PrimaryContext *_localctx = _tracker.createInstance<PrimaryContext>(_ctx, getState());
  enterRule(_localctx, 60, PggParser::RulePrimary);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(518);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(497);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->n = match(PggParser::NUMBER);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<PrimaryContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->n)); 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(499);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->s = match(PggParser::STRING);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<PrimaryContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->s)); 
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(501);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->b = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == PggParser::TRUE

      || _la == PggParser::FALSE)) {
        antlrcpp::downCast<PrimaryContext *>(_localctx)->b = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newBool((antlrcpp::downCast<PrimaryContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->b)); 
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(503);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->v = vec_literal();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->v->result; 
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(506);
      match(PggParser::NONE);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(508);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->i = match(PggParser::IDENT);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newIdent((antlrcpp::downCast<PrimaryContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->i->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->i)); 
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(510);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->a->result; 
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(513);
      match(PggParser::LPAREN);
      setState(514);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->e = aexpr();
      setState(515);
      match(PggParser::RPAREN);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newParen(antlrcpp::downCast<PrimaryContext *>(_localctx)->e->result, spanOf(_localctx)); 
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Attr_refContext ------------------------------------------------------------------

PggParser::Attr_refContext::Attr_refContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Attr_refContext::AT() {
  return getToken(PggParser::AT, 0);
}

tree::TerminalNode* PggParser::Attr_refContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}


size_t PggParser::Attr_refContext::getRuleIndex() const {
  return PggParser::RuleAttr_ref;
}


PggParser::Attr_refContext* PggParser::attr_ref() {
  Attr_refContext *_localctx = _tracker.createInstance<Attr_refContext>(_ctx, getState());
  enterRule(_localctx, 62, PggParser::RuleAttr_ref);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(520);
    match(PggParser::AT);
    setState(521);
    antlrcpp::downCast<Attr_refContext *>(_localctx)->n = match(PggParser::IDENT);
     antlrcpp::downCast<Attr_refContext *>(_localctx)->result =  gc->newAttr((antlrcpp::downCast<Attr_refContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Attr_refContext *>(_localctx)->n->getText() : ""), spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Vec_literalContext ------------------------------------------------------------------

PggParser::Vec_literalContext::Vec_literalContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Vec_literalContext::LPAREN() {
  return getToken(PggParser::LPAREN, 0);
}

tree::TerminalNode* PggParser::Vec_literalContext::RPAREN() {
  return getToken(PggParser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> PggParser::Vec_literalContext::NUMBER() {
  return getTokens(PggParser::NUMBER);
}

tree::TerminalNode* PggParser::Vec_literalContext::NUMBER(size_t i) {
  return getToken(PggParser::NUMBER, i);
}

std::vector<tree::TerminalNode *> PggParser::Vec_literalContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::Vec_literalContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::Vec_literalContext::getRuleIndex() const {
  return PggParser::RuleVec_literal;
}


PggParser::Vec_literalContext* PggParser::vec_literal() {
  Vec_literalContext *_localctx = _tracker.createInstance<Vec_literalContext>(_ctx, getState());
  enterRule(_localctx, 64, PggParser::RuleVec_literal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(524);
    match(PggParser::LPAREN);
    setState(525);
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken = match(PggParser::NUMBER);
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->n.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken);
    setState(530);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(526);
      match(PggParser::COMMA);
      setState(527);
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken = match(PggParser::NUMBER);
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->n.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken);
      setState(532);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(533);
    match(PggParser::RPAREN);
     antlrcpp::downCast<Vec_literalContext *>(_localctx)->result =  gc->newNumberVec(antlrcpp::downCast<Vec_literalContext *>(_localctx)->n, spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LiteralContext ------------------------------------------------------------------

PggParser::LiteralContext::LiteralContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::LiteralContext::NUMBER() {
  return getToken(PggParser::NUMBER, 0);
}

tree::TerminalNode* PggParser::LiteralContext::STRING() {
  return getToken(PggParser::STRING, 0);
}

tree::TerminalNode* PggParser::LiteralContext::TRUE() {
  return getToken(PggParser::TRUE, 0);
}

tree::TerminalNode* PggParser::LiteralContext::FALSE() {
  return getToken(PggParser::FALSE, 0);
}

PggParser::Vec_literalContext* PggParser::LiteralContext::vec_literal() {
  return getRuleContext<PggParser::Vec_literalContext>(0);
}

tree::TerminalNode* PggParser::LiteralContext::NONE() {
  return getToken(PggParser::NONE, 0);
}

tree::TerminalNode* PggParser::LiteralContext::IDENT() {
  return getToken(PggParser::IDENT, 0);
}


size_t PggParser::LiteralContext::getRuleIndex() const {
  return PggParser::RuleLiteral;
}


PggParser::LiteralContext* PggParser::literal() {
  LiteralContext *_localctx = _tracker.createInstance<LiteralContext>(_ctx, getState());
  enterRule(_localctx, 66, PggParser::RuleLiteral);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(549);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(536);
        antlrcpp::downCast<LiteralContext *>(_localctx)->n = match(PggParser::NUMBER);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<LiteralContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->n)); 
        break;
      }

      case PggParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(538);
        antlrcpp::downCast<LiteralContext *>(_localctx)->s = match(PggParser::STRING);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<LiteralContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->s)); 
        break;
      }

      case PggParser::TRUE:
      case PggParser::FALSE: {
        enterOuterAlt(_localctx, 3);
        setState(540);
        antlrcpp::downCast<LiteralContext *>(_localctx)->b = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == PggParser::TRUE

        || _la == PggParser::FALSE)) {
          antlrcpp::downCast<LiteralContext *>(_localctx)->b = _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newBool((antlrcpp::downCast<LiteralContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->b)); 
        break;
      }

      case PggParser::LPAREN: {
        enterOuterAlt(_localctx, 4);
        setState(542);
        antlrcpp::downCast<LiteralContext *>(_localctx)->v = vec_literal();
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  antlrcpp::downCast<LiteralContext *>(_localctx)->v->result; 
        break;
      }

      case PggParser::NONE: {
        enterOuterAlt(_localctx, 5);
        setState(545);
        match(PggParser::NONE);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
        break;
      }

      case PggParser::IDENT: {
        enterOuterAlt(_localctx, 6);
        setState(547);
        antlrcpp::downCast<LiteralContext *>(_localctx)->e = match(PggParser::IDENT);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newEnumLit((antlrcpp::downCast<LiteralContext *>(_localctx)->e != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->e->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->e)); 
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeContext ------------------------------------------------------------------

PggParser::TypeContext::TypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::TypeContext::LT() {
  return getToken(PggParser::LT, 0);
}

tree::TerminalNode* PggParser::TypeContext::GT() {
  return getToken(PggParser::GT, 0);
}

std::vector<tree::TerminalNode *> PggParser::TypeContext::IDENT() {
  return getTokens(PggParser::IDENT);
}

tree::TerminalNode* PggParser::TypeContext::IDENT(size_t i) {
  return getToken(PggParser::IDENT, i);
}

PggParser::TypeContext* PggParser::TypeContext::type() {
  return getRuleContext<PggParser::TypeContext>(0);
}

tree::TerminalNode* PggParser::TypeContext::LBRACE() {
  return getToken(PggParser::LBRACE, 0);
}

tree::TerminalNode* PggParser::TypeContext::RBRACE() {
  return getToken(PggParser::RBRACE, 0);
}

std::vector<tree::TerminalNode *> PggParser::TypeContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::TypeContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}

tree::TerminalNode* PggParser::TypeContext::QUESTION() {
  return getToken(PggParser::QUESTION, 0);
}

tree::TerminalNode* PggParser::TypeContext::LBRACKET() {
  return getToken(PggParser::LBRACKET, 0);
}

tree::TerminalNode* PggParser::TypeContext::RBRACKET() {
  return getToken(PggParser::RBRACKET, 0);
}


size_t PggParser::TypeContext::getRuleIndex() const {
  return PggParser::RuleType;
}



PggParser::TypeContext* PggParser::type() {
   return type(0);
}

PggParser::TypeContext* PggParser::type(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  PggParser::TypeContext *_localctx = _tracker.createInstance<TypeContext>(_ctx, parentState);
  PggParser::TypeContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 68;
  enterRecursionRule(_localctx, 68, PggParser::RuleType, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(574);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 46, _ctx)) {
    case 1: {
      setState(552);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
      setState(553);
      match(PggParser::LT);
      setState(554);
      antlrcpp::downCast<TypeContext *>(_localctx)->a = type(0);
      setState(555);
      match(PggParser::GT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeGeneric((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b), antlrcpp::downCast<TypeContext *>(_localctx)->a->result, spanOf(_localctx)); 
      break;
    }

    case 2: {
      setState(558);
      antlrcpp::downCast<TypeContext *>(_localctx)->e = match(PggParser::IDENT);
      setState(559);
      match(PggParser::LBRACE);
      setState(568);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PggParser::IDENT) {
        setState(560);
        antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
        setState(565);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == PggParser::COMMA) {
          setState(561);
          match(PggParser::COMMA);
          setState(562);
          antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
          antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
          setState(567);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
      }
      setState(570);
      match(PggParser::RBRACE);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeEnum(antlrcpp::downCast<TypeContext *>(_localctx)->e, gc->namesOf(antlrcpp::downCast<TypeContext *>(_localctx)->v), spanOf(_localctx)); 
      break;
    }

    case 3: {
      setState(572);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeName((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b)); 
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(585);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(583);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(576);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(577);
          match(PggParser::QUESTION);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeOptional(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(579);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(580);
          match(PggParser::LBRACKET);
          setState(581);
          match(PggParser::RBRACKET);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeList(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        default:
          break;
        } 
      }
      setState(587);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 48, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

bool PggParser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 20: return or_exprSempred(antlrcpp::downCast<Or_exprContext *>(context), predicateIndex);
    case 21: return and_exprSempred(antlrcpp::downCast<And_exprContext *>(context), predicateIndex);
    case 23: return add_exprSempred(antlrcpp::downCast<Add_exprContext *>(context), predicateIndex);
    case 24: return mul_exprSempred(antlrcpp::downCast<Mul_exprContext *>(context), predicateIndex);
    case 34: return typeSempred(antlrcpp::downCast<TypeContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool PggParser::or_exprSempred(Or_exprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 2);

  default:
    break;
  }
  return true;
}

bool PggParser::and_exprSempred(And_exprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 2);

  default:
    break;
  }
  return true;
}

bool PggParser::add_exprSempred(Add_exprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 2);

  default:
    break;
  }
  return true;
}

bool PggParser::mul_exprSempred(Mul_exprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 3: return precpred(_ctx, 2);

  default:
    break;
  }
  return true;
}

bool PggParser::typeSempred(TypeContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 4: return precpred(_ctx, 5);
    case 5: return precpred(_ctx, 4);

  default:
    break;
  }
  return true;
}

void PggParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  pggParserInitialize();
#else
  ::antlr4::internal::call_once(pggParserOnceFlag, pggParserInitialize);
#endif
}
