
#include "src/ast.h"
#include "src/compositor.h"


// Generated from /home/neuro/sources/neverwhere/src/libs/pgg/grammar/Pgg.g4 by ANTLR 4.13.2



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
      "attr_ref", "vec_literal", "list_literal", "literal", "type"
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
  	4,1,48,611,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,
  	0,5,0,90,8,0,10,0,12,0,93,9,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,3,1,102,8,1,
  	1,1,1,1,3,1,106,8,1,1,1,1,1,1,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,119,
  	8,2,1,2,1,2,1,2,1,3,1,3,1,3,1,3,1,3,1,4,1,4,1,4,1,4,1,4,1,4,3,4,135,8,
  	4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,145,8,4,10,4,12,4,148,9,4,1,4,1,
  	4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,163,8,4,10,4,12,4,166,
  	9,4,1,4,1,4,1,4,1,4,1,5,1,5,1,5,5,5,175,8,5,10,5,12,5,178,9,5,1,5,1,5,
  	1,6,1,6,1,6,1,6,1,6,1,6,1,6,3,6,189,8,6,1,6,1,6,1,7,1,7,1,7,5,7,196,8,
  	7,10,7,12,7,199,9,7,1,7,1,7,1,8,1,8,1,8,1,8,1,8,1,9,1,9,1,9,1,9,1,9,1,
  	9,1,9,1,9,1,9,3,9,217,8,9,1,9,1,9,1,9,3,9,222,8,9,1,9,1,9,1,9,1,10,1,
  	10,1,10,1,10,1,10,1,10,1,10,1,10,1,10,3,10,236,8,10,1,10,1,10,1,10,3,
  	10,241,8,10,1,10,1,10,1,10,1,11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,1,
  	11,1,11,1,11,1,11,1,11,1,11,3,11,260,8,11,1,12,1,12,1,12,1,12,1,12,1,
  	13,1,13,1,13,5,13,270,8,13,10,13,12,13,273,9,13,1,13,1,13,1,14,1,14,1,
  	14,1,14,3,14,281,8,14,1,14,1,14,1,14,1,15,1,15,1,15,1,15,1,15,1,15,1,
  	15,1,15,1,15,5,15,295,8,15,10,15,12,15,298,9,15,1,15,1,15,1,16,1,16,1,
  	16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,5,16,317,
  	8,16,10,16,12,16,320,9,16,3,16,322,8,16,1,16,1,16,1,16,1,16,5,16,328,
  	8,16,10,16,12,16,331,9,16,1,16,1,16,1,16,1,16,5,16,337,8,16,10,16,12,
  	16,340,9,16,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,17,1,17,1,17,1,
  	17,1,17,5,17,355,8,17,10,17,12,17,358,9,17,1,17,1,17,1,17,1,17,5,17,364,
  	8,17,10,17,12,17,367,9,17,1,17,1,17,1,17,1,17,1,18,1,18,1,18,1,19,1,19,
  	1,19,1,19,1,19,1,19,1,19,1,19,3,19,384,8,19,1,20,1,20,1,20,1,20,1,20,
  	1,20,1,20,1,20,1,20,5,20,395,8,20,10,20,12,20,398,9,20,1,21,1,21,1,21,
  	1,21,1,21,1,21,1,21,1,21,1,21,5,21,409,8,21,10,21,12,21,412,9,21,1,22,
  	1,22,1,22,1,22,1,22,1,22,3,22,420,8,22,1,23,1,23,1,23,1,23,1,23,1,23,
  	1,23,1,23,1,23,5,23,431,8,23,10,23,12,23,434,9,23,1,24,1,24,1,24,1,24,
  	1,24,1,24,1,24,1,24,1,24,5,24,445,8,24,10,24,12,24,448,9,24,1,25,1,25,
  	1,25,1,25,1,25,1,25,1,25,3,25,457,8,25,1,26,1,26,1,26,1,26,1,26,1,26,
  	3,26,465,8,26,1,27,1,27,1,27,1,27,1,27,5,27,472,8,27,10,27,12,27,475,
  	9,27,3,27,477,8,27,1,27,1,27,1,27,1,28,1,28,1,28,5,28,485,8,28,10,28,
  	12,28,488,9,28,1,28,1,28,1,29,1,29,1,29,3,29,495,8,29,1,29,1,29,1,29,
  	1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,
  	1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,3,30,524,8,30,1,31,
  	1,31,1,31,1,31,1,32,1,32,1,32,1,32,5,32,534,8,32,10,32,12,32,537,9,32,
  	1,32,1,32,1,32,1,33,1,33,1,33,1,33,5,33,546,8,33,10,33,12,33,549,9,33,
  	1,33,3,33,552,8,33,3,33,554,8,33,1,33,1,33,1,33,1,34,1,34,1,34,1,34,1,
  	34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,1,34,3,34,572,8,34,1,35,1,35,1,
  	35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,5,35,586,8,35,10,35,12,
  	35,589,9,35,3,35,591,8,35,1,35,1,35,1,35,1,35,3,35,597,8,35,1,35,1,35,
  	1,35,1,35,1,35,1,35,1,35,5,35,606,8,35,10,35,12,35,609,9,35,1,35,0,5,
  	40,42,46,48,70,36,0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,
  	38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,68,70,0,6,1,1,46,46,1,0,
  	19,24,1,0,25,26,1,0,27,29,2,0,26,26,30,30,1,0,13,14,647,0,72,1,0,0,0,
  	2,97,1,0,0,0,4,110,1,0,0,0,6,123,1,0,0,0,8,128,1,0,0,0,10,171,1,0,0,0,
  	12,181,1,0,0,0,14,192,1,0,0,0,16,202,1,0,0,0,18,207,1,0,0,0,20,226,1,
  	0,0,0,22,259,1,0,0,0,24,261,1,0,0,0,26,266,1,0,0,0,28,276,1,0,0,0,30,
  	285,1,0,0,0,32,301,1,0,0,0,34,345,1,0,0,0,36,372,1,0,0,0,38,375,1,0,0,
  	0,40,385,1,0,0,0,42,399,1,0,0,0,44,413,1,0,0,0,46,421,1,0,0,0,48,435,
  	1,0,0,0,50,456,1,0,0,0,52,464,1,0,0,0,54,466,1,0,0,0,56,481,1,0,0,0,58,
  	494,1,0,0,0,60,523,1,0,0,0,62,525,1,0,0,0,64,529,1,0,0,0,66,541,1,0,0,
  	0,68,571,1,0,0,0,70,596,1,0,0,0,72,91,6,0,-1,0,73,90,5,46,0,0,74,75,3,
  	2,1,0,75,76,6,0,-1,0,76,90,1,0,0,0,77,78,3,4,2,0,78,79,6,0,-1,0,79,90,
  	1,0,0,0,80,81,3,8,4,0,81,82,6,0,-1,0,82,90,1,0,0,0,83,84,3,6,3,0,84,85,
  	6,0,-1,0,85,90,1,0,0,0,86,87,3,22,11,0,87,88,6,0,-1,0,88,90,1,0,0,0,89,
  	73,1,0,0,0,89,74,1,0,0,0,89,77,1,0,0,0,89,80,1,0,0,0,89,83,1,0,0,0,89,
  	86,1,0,0,0,90,93,1,0,0,0,91,89,1,0,0,0,91,92,1,0,0,0,92,94,1,0,0,0,93,
  	91,1,0,0,0,94,95,5,0,0,1,95,96,6,0,-1,0,96,1,1,0,0,0,97,98,5,5,0,0,98,
  	101,3,56,28,0,99,100,5,6,0,0,100,102,5,45,0,0,101,99,1,0,0,0,101,102,
  	1,0,0,0,102,105,1,0,0,0,103,104,5,33,0,0,104,106,5,17,0,0,105,103,1,0,
  	0,0,105,106,1,0,0,0,106,107,1,0,0,0,107,108,5,46,0,0,108,109,6,1,-1,0,
  	109,3,1,0,0,0,110,111,5,10,0,0,111,112,5,45,0,0,112,113,5,35,0,0,113,
  	118,3,70,35,0,114,115,5,38,0,0,115,116,3,68,34,0,116,117,6,2,-1,0,117,
  	119,1,0,0,0,118,114,1,0,0,0,118,119,1,0,0,0,119,120,1,0,0,0,120,121,5,
  	46,0,0,121,122,6,2,-1,0,122,5,1,0,0,0,123,124,5,11,0,0,124,125,5,45,0,
  	0,125,126,5,46,0,0,126,127,6,3,-1,0,127,7,1,0,0,0,128,129,5,1,0,0,129,
  	130,5,45,0,0,130,134,5,39,0,0,131,132,3,10,5,0,132,133,6,4,-1,0,133,135,
  	1,0,0,0,134,131,1,0,0,0,134,135,1,0,0,0,135,136,1,0,0,0,136,137,5,40,
  	0,0,137,138,5,18,0,0,138,139,5,39,0,0,139,140,3,14,7,0,140,141,5,40,0,
  	0,141,142,6,4,-1,0,142,146,5,41,0,0,143,145,5,46,0,0,144,143,1,0,0,0,
  	145,148,1,0,0,0,146,144,1,0,0,0,146,147,1,0,0,0,147,164,1,0,0,0,148,146,
  	1,0,0,0,149,163,5,46,0,0,150,151,5,15,0,0,151,152,5,46,0,0,152,163,6,
  	4,-1,0,153,154,3,18,9,0,154,155,6,4,-1,0,155,163,1,0,0,0,156,157,3,20,
  	10,0,157,158,6,4,-1,0,158,163,1,0,0,0,159,160,3,22,11,0,160,161,6,4,-1,
  	0,161,163,1,0,0,0,162,149,1,0,0,0,162,150,1,0,0,0,162,153,1,0,0,0,162,
  	156,1,0,0,0,162,159,1,0,0,0,163,166,1,0,0,0,164,162,1,0,0,0,164,165,1,
  	0,0,0,165,167,1,0,0,0,166,164,1,0,0,0,167,168,5,42,0,0,168,169,7,0,0,
  	0,169,170,6,4,-1,0,170,9,1,0,0,0,171,176,3,12,6,0,172,173,5,37,0,0,173,
  	175,3,12,6,0,174,172,1,0,0,0,175,178,1,0,0,0,176,174,1,0,0,0,176,177,
  	1,0,0,0,177,179,1,0,0,0,178,176,1,0,0,0,179,180,6,5,-1,0,180,11,1,0,0,
  	0,181,182,5,45,0,0,182,183,5,35,0,0,183,188,3,70,35,0,184,185,5,38,0,
  	0,185,186,3,68,34,0,186,187,6,6,-1,0,187,189,1,0,0,0,188,184,1,0,0,0,
  	188,189,1,0,0,0,189,190,1,0,0,0,190,191,6,6,-1,0,191,13,1,0,0,0,192,197,
  	3,16,8,0,193,194,5,37,0,0,194,196,3,16,8,0,195,193,1,0,0,0,196,199,1,
  	0,0,0,197,195,1,0,0,0,197,198,1,0,0,0,198,200,1,0,0,0,199,197,1,0,0,0,
  	200,201,6,7,-1,0,201,15,1,0,0,0,202,203,5,45,0,0,203,204,5,35,0,0,204,
  	205,3,70,35,0,205,206,6,8,-1,0,206,17,1,0,0,0,207,216,5,2,0,0,208,209,
  	5,45,0,0,209,210,5,45,0,0,210,211,3,62,31,0,211,212,6,9,-1,0,212,217,
  	1,0,0,0,213,214,3,36,18,0,214,215,6,9,-1,0,215,217,1,0,0,0,216,208,1,
  	0,0,0,216,213,1,0,0,0,217,221,1,0,0,0,218,219,5,35,0,0,219,220,5,16,0,
  	0,220,222,6,9,-1,0,221,218,1,0,0,0,221,222,1,0,0,0,222,223,1,0,0,0,223,
  	224,5,46,0,0,224,225,6,9,-1,0,225,19,1,0,0,0,226,235,5,3,0,0,227,228,
  	5,45,0,0,228,229,5,45,0,0,229,230,3,62,31,0,230,231,6,10,-1,0,231,236,
  	1,0,0,0,232,233,3,36,18,0,233,234,6,10,-1,0,234,236,1,0,0,0,235,227,1,
  	0,0,0,235,232,1,0,0,0,236,240,1,0,0,0,237,238,5,35,0,0,238,239,5,16,0,
  	0,239,241,6,10,-1,0,240,237,1,0,0,0,240,241,1,0,0,0,241,242,1,0,0,0,242,
  	243,5,46,0,0,243,244,6,10,-1,0,244,21,1,0,0,0,245,246,3,24,12,0,246,247,
  	7,0,0,0,247,248,6,11,-1,0,248,260,1,0,0,0,249,250,3,28,14,0,250,251,7,
  	0,0,0,251,252,6,11,-1,0,252,260,1,0,0,0,253,254,3,32,16,0,254,255,6,11,
  	-1,0,255,260,1,0,0,0,256,257,3,34,17,0,257,258,6,11,-1,0,258,260,1,0,
  	0,0,259,245,1,0,0,0,259,249,1,0,0,0,259,253,1,0,0,0,259,256,1,0,0,0,260,
  	23,1,0,0,0,261,262,3,26,13,0,262,263,5,38,0,0,263,264,3,36,18,0,264,265,
  	6,12,-1,0,265,25,1,0,0,0,266,271,5,45,0,0,267,268,5,37,0,0,268,270,5,
  	45,0,0,269,267,1,0,0,0,270,273,1,0,0,0,271,269,1,0,0,0,271,272,1,0,0,
  	0,272,274,1,0,0,0,273,271,1,0,0,0,274,275,6,13,-1,0,275,27,1,0,0,0,276,
  	280,5,4,0,0,277,278,5,45,0,0,278,279,5,35,0,0,279,281,6,14,-1,0,280,277,
  	1,0,0,0,280,281,1,0,0,0,281,282,1,0,0,0,282,283,3,30,15,0,283,284,6,14,
  	-1,0,284,29,1,0,0,0,285,286,5,45,0,0,286,296,6,15,-1,0,287,288,5,36,0,
  	0,288,289,5,45,0,0,289,295,6,15,-1,0,290,291,5,43,0,0,291,292,5,17,0,
  	0,292,293,5,44,0,0,293,295,6,15,-1,0,294,287,1,0,0,0,294,290,1,0,0,0,
  	295,298,1,0,0,0,296,294,1,0,0,0,296,297,1,0,0,0,297,299,1,0,0,0,298,296,
  	1,0,0,0,299,300,6,15,-1,0,300,31,1,0,0,0,301,302,3,26,13,0,302,303,5,
  	38,0,0,303,304,5,7,0,0,304,305,5,39,0,0,305,306,3,36,18,0,306,307,5,37,
  	0,0,307,308,5,45,0,0,308,309,5,38,0,0,309,310,3,36,18,0,310,311,5,40,
  	0,0,311,312,6,16,-1,0,312,321,5,31,0,0,313,318,5,45,0,0,314,315,5,37,
  	0,0,315,317,5,45,0,0,316,314,1,0,0,0,317,320,1,0,0,0,318,316,1,0,0,0,
  	318,319,1,0,0,0,319,322,1,0,0,0,320,318,1,0,0,0,321,313,1,0,0,0,321,322,
  	1,0,0,0,322,323,1,0,0,0,323,324,5,31,0,0,324,325,6,16,-1,0,325,329,5,
  	41,0,0,326,328,5,46,0,0,327,326,1,0,0,0,328,331,1,0,0,0,329,327,1,0,0,
  	0,329,330,1,0,0,0,330,338,1,0,0,0,331,329,1,0,0,0,332,337,5,46,0,0,333,
  	334,3,22,11,0,334,335,6,16,-1,0,335,337,1,0,0,0,336,332,1,0,0,0,336,333,
  	1,0,0,0,337,340,1,0,0,0,338,336,1,0,0,0,338,339,1,0,0,0,339,341,1,0,0,
  	0,340,338,1,0,0,0,341,342,5,42,0,0,342,343,7,0,0,0,343,344,6,16,-1,0,
  	344,33,1,0,0,0,345,346,5,45,0,0,346,347,5,38,0,0,347,348,5,8,0,0,348,
  	349,5,45,0,0,349,350,5,9,0,0,350,351,3,36,18,0,351,352,6,17,-1,0,352,
  	356,5,41,0,0,353,355,5,46,0,0,354,353,1,0,0,0,355,358,1,0,0,0,356,354,
  	1,0,0,0,356,357,1,0,0,0,357,365,1,0,0,0,358,356,1,0,0,0,359,364,5,46,
  	0,0,360,361,3,22,11,0,361,362,6,17,-1,0,362,364,1,0,0,0,363,359,1,0,0,
  	0,363,360,1,0,0,0,364,367,1,0,0,0,365,363,1,0,0,0,365,366,1,0,0,0,366,
  	368,1,0,0,0,367,365,1,0,0,0,368,369,5,42,0,0,369,370,7,0,0,0,370,371,
  	6,17,-1,0,371,35,1,0,0,0,372,373,3,38,19,0,373,374,6,18,-1,0,374,37,1,
  	0,0,0,375,376,3,40,20,0,376,383,6,19,-1,0,377,378,5,34,0,0,378,379,3,
  	36,18,0,379,380,5,35,0,0,380,381,3,36,18,0,381,382,6,19,-1,0,382,384,
  	1,0,0,0,383,377,1,0,0,0,383,384,1,0,0,0,384,39,1,0,0,0,385,386,6,20,-1,
  	0,386,387,3,42,21,0,387,388,6,20,-1,0,388,396,1,0,0,0,389,390,10,2,0,
  	0,390,391,5,31,0,0,391,392,3,42,21,0,392,393,6,20,-1,0,393,395,1,0,0,
  	0,394,389,1,0,0,0,395,398,1,0,0,0,396,394,1,0,0,0,396,397,1,0,0,0,397,
  	41,1,0,0,0,398,396,1,0,0,0,399,400,6,21,-1,0,400,401,3,44,22,0,401,402,
  	6,21,-1,0,402,410,1,0,0,0,403,404,10,2,0,0,404,405,5,32,0,0,405,406,3,
  	44,22,0,406,407,6,21,-1,0,407,409,1,0,0,0,408,403,1,0,0,0,409,412,1,0,
  	0,0,410,408,1,0,0,0,410,411,1,0,0,0,411,43,1,0,0,0,412,410,1,0,0,0,413,
  	414,3,46,23,0,414,419,6,22,-1,0,415,416,7,1,0,0,416,417,3,46,23,0,417,
  	418,6,22,-1,0,418,420,1,0,0,0,419,415,1,0,0,0,419,420,1,0,0,0,420,45,
  	1,0,0,0,421,422,6,23,-1,0,422,423,3,48,24,0,423,424,6,23,-1,0,424,432,
  	1,0,0,0,425,426,10,2,0,0,426,427,7,2,0,0,427,428,3,48,24,0,428,429,6,
  	23,-1,0,429,431,1,0,0,0,430,425,1,0,0,0,431,434,1,0,0,0,432,430,1,0,0,
  	0,432,433,1,0,0,0,433,47,1,0,0,0,434,432,1,0,0,0,435,436,6,24,-1,0,436,
  	437,3,50,25,0,437,438,6,24,-1,0,438,446,1,0,0,0,439,440,10,2,0,0,440,
  	441,7,3,0,0,441,442,3,50,25,0,442,443,6,24,-1,0,443,445,1,0,0,0,444,439,
  	1,0,0,0,445,448,1,0,0,0,446,444,1,0,0,0,446,447,1,0,0,0,447,49,1,0,0,
  	0,448,446,1,0,0,0,449,450,7,4,0,0,450,451,3,50,25,0,451,452,6,25,-1,0,
  	452,457,1,0,0,0,453,454,3,52,26,0,454,455,6,25,-1,0,455,457,1,0,0,0,456,
  	449,1,0,0,0,456,453,1,0,0,0,457,51,1,0,0,0,458,459,3,54,27,0,459,460,
  	6,26,-1,0,460,465,1,0,0,0,461,462,3,60,30,0,462,463,6,26,-1,0,463,465,
  	1,0,0,0,464,458,1,0,0,0,464,461,1,0,0,0,465,53,1,0,0,0,466,467,3,56,28,
  	0,467,476,5,39,0,0,468,473,3,58,29,0,469,470,5,37,0,0,470,472,3,58,29,
  	0,471,469,1,0,0,0,472,475,1,0,0,0,473,471,1,0,0,0,473,474,1,0,0,0,474,
  	477,1,0,0,0,475,473,1,0,0,0,476,468,1,0,0,0,476,477,1,0,0,0,477,478,1,
  	0,0,0,478,479,5,40,0,0,479,480,6,27,-1,0,480,55,1,0,0,0,481,486,5,45,
  	0,0,482,483,5,36,0,0,483,485,5,45,0,0,484,482,1,0,0,0,485,488,1,0,0,0,
  	486,484,1,0,0,0,486,487,1,0,0,0,487,489,1,0,0,0,488,486,1,0,0,0,489,490,
  	6,28,-1,0,490,57,1,0,0,0,491,492,5,45,0,0,492,493,5,38,0,0,493,495,6,
  	29,-1,0,494,491,1,0,0,0,494,495,1,0,0,0,495,496,1,0,0,0,496,497,3,36,
  	18,0,497,498,6,29,-1,0,498,59,1,0,0,0,499,500,5,17,0,0,500,524,6,30,-1,
  	0,501,502,5,16,0,0,502,524,6,30,-1,0,503,504,7,5,0,0,504,524,6,30,-1,
  	0,505,506,3,64,32,0,506,507,6,30,-1,0,507,524,1,0,0,0,508,509,3,66,33,
  	0,509,510,6,30,-1,0,510,524,1,0,0,0,511,512,5,12,0,0,512,524,6,30,-1,
  	0,513,514,5,45,0,0,514,524,6,30,-1,0,515,516,3,62,31,0,516,517,6,30,-1,
  	0,517,524,1,0,0,0,518,519,5,39,0,0,519,520,3,36,18,0,520,521,5,40,0,0,
  	521,522,6,30,-1,0,522,524,1,0,0,0,523,499,1,0,0,0,523,501,1,0,0,0,523,
  	503,1,0,0,0,523,505,1,0,0,0,523,508,1,0,0,0,523,511,1,0,0,0,523,513,1,
  	0,0,0,523,515,1,0,0,0,523,518,1,0,0,0,524,61,1,0,0,0,525,526,5,33,0,0,
  	526,527,5,45,0,0,527,528,6,31,-1,0,528,63,1,0,0,0,529,530,5,39,0,0,530,
  	535,5,17,0,0,531,532,5,37,0,0,532,534,5,17,0,0,533,531,1,0,0,0,534,537,
  	1,0,0,0,535,533,1,0,0,0,535,536,1,0,0,0,536,538,1,0,0,0,537,535,1,0,0,
  	0,538,539,5,40,0,0,539,540,6,32,-1,0,540,65,1,0,0,0,541,553,5,43,0,0,
  	542,547,3,36,18,0,543,544,5,37,0,0,544,546,3,36,18,0,545,543,1,0,0,0,
  	546,549,1,0,0,0,547,545,1,0,0,0,547,548,1,0,0,0,548,551,1,0,0,0,549,547,
  	1,0,0,0,550,552,5,37,0,0,551,550,1,0,0,0,551,552,1,0,0,0,552,554,1,0,
  	0,0,553,542,1,0,0,0,553,554,1,0,0,0,554,555,1,0,0,0,555,556,5,44,0,0,
  	556,557,6,33,-1,0,557,67,1,0,0,0,558,559,5,17,0,0,559,572,6,34,-1,0,560,
  	561,5,16,0,0,561,572,6,34,-1,0,562,563,7,5,0,0,563,572,6,34,-1,0,564,
  	565,3,64,32,0,565,566,6,34,-1,0,566,572,1,0,0,0,567,568,5,12,0,0,568,
  	572,6,34,-1,0,569,570,5,45,0,0,570,572,6,34,-1,0,571,558,1,0,0,0,571,
  	560,1,0,0,0,571,562,1,0,0,0,571,564,1,0,0,0,571,567,1,0,0,0,571,569,1,
  	0,0,0,572,69,1,0,0,0,573,574,6,35,-1,0,574,575,5,45,0,0,575,576,5,23,
  	0,0,576,577,3,70,35,0,577,578,5,24,0,0,578,579,6,35,-1,0,579,597,1,0,
  	0,0,580,581,5,45,0,0,581,590,5,41,0,0,582,587,5,45,0,0,583,584,5,37,0,
  	0,584,586,5,45,0,0,585,583,1,0,0,0,586,589,1,0,0,0,587,585,1,0,0,0,587,
  	588,1,0,0,0,588,591,1,0,0,0,589,587,1,0,0,0,590,582,1,0,0,0,590,591,1,
  	0,0,0,591,592,1,0,0,0,592,593,5,42,0,0,593,597,6,35,-1,0,594,595,5,45,
  	0,0,595,597,6,35,-1,0,596,573,1,0,0,0,596,580,1,0,0,0,596,594,1,0,0,0,
  	597,607,1,0,0,0,598,599,10,5,0,0,599,600,5,34,0,0,600,606,6,35,-1,0,601,
  	602,10,4,0,0,602,603,5,43,0,0,603,604,5,44,0,0,604,606,6,35,-1,0,605,
  	598,1,0,0,0,605,601,1,0,0,0,606,609,1,0,0,0,607,605,1,0,0,0,607,608,1,
  	0,0,0,608,71,1,0,0,0,609,607,1,0,0,0,52,89,91,101,105,118,134,146,162,
  	164,176,188,197,216,221,235,240,259,271,280,294,296,318,321,329,336,338,
  	356,363,365,383,396,410,419,432,446,456,464,473,476,486,494,523,535,547,
  	551,553,571,587,590,596,605,607
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
    setState(91);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116269618) != 0)) {
      setState(89);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(73);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::IMPORT: {
          setState(74);
          antlrcpp::downCast<FileContext *>(_localctx)->i = import_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->i->result); 
          break;
        }

        case PggParser::PARAM: {
          setState(77);
          antlrcpp::downCast<FileContext *>(_localctx)->p = param_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->p->result); 
          break;
        }

        case PggParser::DEF: {
          setState(80);
          antlrcpp::downCast<FileContext *>(_localctx)->d = def_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->d->result); 
          break;
        }

        case PggParser::OUTPUT: {
          setState(83);
          antlrcpp::downCast<FileContext *>(_localctx)->o = output_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->o->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(86);
          antlrcpp::downCast<FileContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(93);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(94);
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
    setState(97);
    match(PggParser::IMPORT);
    setState(98);
    antlrcpp::downCast<Import_stmtContext *>(_localctx)->q = qualified_name();
    setState(101);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AS) {
      setState(99);
      match(PggParser::AS);
      setState(100);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->a = match(PggParser::IDENT);
    }
    setState(105);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AT) {
      setState(103);
      match(PggParser::AT);
      setState(104);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->v = match(PggParser::NUMBER);
    }
    setState(107);
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
    setState(110);
    match(PggParser::PARAM);
    setState(111);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(112);
    match(PggParser::COLON);
    setState(113);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->t = type(0);
    setState(118);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(114);
      match(PggParser::ASSIGN);
      setState(115);
      antlrcpp::downCast<Param_stmtContext *>(_localctx)->d = literal();
       antlrcpp::downCast<Param_stmtContext *>(_localctx)->def =  antlrcpp::downCast<Param_stmtContext *>(_localctx)->d->result; antlrcpp::downCast<Param_stmtContext *>(_localctx)->hasDef =  true; 
    }
    setState(120);
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
    setState(123);
    match(PggParser::OUTPUT);
    setState(124);
    antlrcpp::downCast<Output_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(125);
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
    setState(128);
    match(PggParser::DEF);
    setState(129);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(130);
    match(PggParser::LPAREN);
    setState(134);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(131);
      antlrcpp::downCast<Def_stmtContext *>(_localctx)->p = params();
       antlrcpp::downCast<Def_stmtContext *>(_localctx)->ps =  antlrcpp::downCast<Def_stmtContext *>(_localctx)->p->result; 
    }
    setState(136);
    match(PggParser::RPAREN);
    setState(137);
    match(PggParser::ARROW);
    setState(138);
    match(PggParser::LPAREN);
    setState(139);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->o = outputs();
    setState(140);
    match(PggParser::RPAREN);
     gc->beginDef((antlrcpp::downCast<Def_stmtContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->n), _localctx->ps, antlrcpp::downCast<Def_stmtContext *>(_localctx)->o->result, spanOf(_localctx)); 
    setState(142);
    match(PggParser::LBRACE);
    setState(146);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(143);
        match(PggParser::NEWLINE); 
      }
      setState(148);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    }
    setState(164);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116299292) != 0)) {
      setState(162);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(149);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TRIPLE_STRING: {
          setState(150);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc = match(PggParser::TRIPLE_STRING);
          setState(151);
          match(PggParser::NEWLINE);
           gc->defDoc((antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc)); 
          break;
        }

        case PggParser::EXPECT: {
          setState(153);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->e = expect_stmt();
           gc->addExpect(antlrcpp::downCast<Def_stmtContext *>(_localctx)->e->result); 
          break;
        }

        case PggParser::ENSURE: {
          setState(156);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->en = ensure_stmt();
           gc->addEnsure(antlrcpp::downCast<Def_stmtContext *>(_localctx)->en->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(159);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<Def_stmtContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(166);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(167);
    match(PggParser::RBRACE);
    setState(168);
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
    setState(171);
    antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
    antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
    setState(176);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(172);
      match(PggParser::COMMA);
      setState(173);
      antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
      antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
      setState(178);
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
    setState(181);
    antlrcpp::downCast<ParamContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(182);
    match(PggParser::COLON);
    setState(183);
    antlrcpp::downCast<ParamContext *>(_localctx)->t = type(0);
    setState(188);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(184);
      match(PggParser::ASSIGN);
      setState(185);
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
    setState(192);
    antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
    antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
    setState(197);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(193);
      match(PggParser::COMMA);
      setState(194);
      antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
      antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
      setState(199);
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
    setState(202);
    antlrcpp::downCast<Out_declContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(203);
    match(PggParser::COLON);
    setState(204);
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
    setState(207);
    match(PggParser::EXPECT);
    setState(216);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      setState(208);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(209);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(210);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Expect_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(213);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(221);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(218);
      match(PggParser::COLON);
      setState(219);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(223);
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
    setState(226);
    match(PggParser::ENSURE);
    setState(235);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      setState(227);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(228);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(229);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(232);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(240);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(237);
      match(PggParser::COLON);
      setState(238);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(242);
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
    setState(259);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(245);
      antlrcpp::downCast<StmtContext *>(_localctx)->b = binding();
      setState(246);
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
      setState(249);
      antlrcpp::downCast<StmtContext *>(_localctx)->t = tap_stmt();
      setState(250);
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
      setState(253);
      antlrcpp::downCast<StmtContext *>(_localctx)->r = repeat_zone();
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->r->result; 
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(256);
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
    setState(261);
    antlrcpp::downCast<BindingContext *>(_localctx)->t = targets();
    setState(262);
    match(PggParser::ASSIGN);
    setState(263);
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
    setState(266);
    antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
    setState(271);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(267);
      match(PggParser::COMMA);
      setState(268);
      antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
      setState(273);
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
    setState(276);
    match(PggParser::TAP);
    setState(280);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx)) {
    case 1: {
      setState(277);
      antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l = match(PggParser::IDENT);
      setState(278);
      match(PggParser::COLON);
       antlrcpp::downCast<Tap_stmtContext *>(_localctx)->label =  (antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l != nullptr ? antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l->getText() : ""); antlrcpp::downCast<Tap_stmtContext *>(_localctx)->hasLabel =  true; 
      break;
    }

    default:
      break;
    }
    setState(282);
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
    setState(285);
    antlrcpp::downCast<PathContext *>(_localctx)->i = match(PggParser::IDENT);
     _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->i->getText() : ""))); 
    setState(296);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT

    || _la == PggParser::LBRACKET) {
      setState(294);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::DOT: {
          setState(287);
          match(PggParser::DOT);
          setState(288);
          antlrcpp::downCast<PathContext *>(_localctx)->j = match(PggParser::IDENT);
           _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->j != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->j->getText() : ""))); 
          break;
        }

        case PggParser::LBRACKET: {
          setState(290);
          match(PggParser::LBRACKET);
          setState(291);
          antlrcpp::downCast<PathContext *>(_localctx)->n = match(PggParser::NUMBER);
          setState(292);
          match(PggParser::RBRACKET);
           _localctx->elems.push_back(gc->pathIndex((antlrcpp::downCast<PathContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->n->getText() : ""))); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(298);
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
    setState(301);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t = targets();
    setState(302);
    match(PggParser::ASSIGN);
    setState(303);
    match(PggParser::REPEAT);
    setState(304);
    match(PggParser::LPAREN);
    setState(305);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v = aexpr();
    setState(306);
    match(PggParser::COMMA);
    setState(307);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it = match(PggParser::IDENT);
    setState(308);
    match(PggParser::ASSIGN);
    setState(309);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n = aexpr();
    setState(310);
    match(PggParser::RPAREN);
     gc->checkKeyword(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it, "iterations"); 
    setState(312);
    match(PggParser::PIPE);
    setState(321);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(313);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
      setState(318);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(314);
        match(PggParser::COMMA);
        setState(315);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
        setState(320);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(323);
    match(PggParser::PIPE);
     gc->beginRepeat(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n->result, gc->nameListOf(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s), spanOf(_localctx)); 
    setState(325);
    match(PggParser::LBRACE);
    setState(329);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(326);
        match(PggParser::NEWLINE); 
      }
      setState(331);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    }
    setState(338);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(336);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(332);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(333);
          antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(340);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(341);
    match(PggParser::RBRACE);
    setState(342);
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
    setState(345);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt = match(PggParser::IDENT);
    setState(346);
    match(PggParser::ASSIGN);
    setState(347);
    match(PggParser::FOREACH);
    setState(348);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item = match(PggParser::IDENT);
    setState(349);
    match(PggParser::IN);
    setState(350);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c = aexpr();
     gc->beginForeach((antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt), (antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item), antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c->result,
                             spanOf(_localctx)); 
    setState(352);
    match(PggParser::LBRACE);
    setState(356);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(353);
        match(PggParser::NEWLINE); 
      }
      setState(358);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    }
    setState(365);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(363);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(359);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(360);
          antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(367);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(368);
    match(PggParser::RBRACE);
    setState(369);
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
    setState(372);
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
    setState(375);
    antlrcpp::downCast<TernaryContext *>(_localctx)->c = or_expr(0);
     antlrcpp::downCast<TernaryContext *>(_localctx)->result =  antlrcpp::downCast<TernaryContext *>(_localctx)->c->result; 
    setState(383);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::QUESTION) {
      setState(377);
      match(PggParser::QUESTION);
      setState(378);
      antlrcpp::downCast<TernaryContext *>(_localctx)->t = aexpr();
      setState(379);
      match(PggParser::COLON);
      setState(380);
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
    setState(386);
    antlrcpp::downCast<Or_exprContext *>(_localctx)->a = and_expr(0);
     antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  antlrcpp::downCast<Or_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(396);
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
        setState(389);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(390);
        match(PggParser::PIPE);
        setState(391);
        antlrcpp::downCast<Or_exprContext *>(_localctx)->r = and_expr(0);
         antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  gc->newBinary("|", antlrcpp::downCast<Or_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Or_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(398);
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
    setState(400);
    antlrcpp::downCast<And_exprContext *>(_localctx)->a = cmp_expr();
     antlrcpp::downCast<And_exprContext *>(_localctx)->result =  antlrcpp::downCast<And_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(410);
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
        setState(403);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(404);
        match(PggParser::AMP);
        setState(405);
        antlrcpp::downCast<And_exprContext *>(_localctx)->r = cmp_expr();
         antlrcpp::downCast<And_exprContext *>(_localctx)->result =  gc->newBinary("&", antlrcpp::downCast<And_exprContext *>(_localctx)->l->result, antlrcpp::downCast<And_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(412);
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
    setState(413);
    antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l = add_expr(0);
     antlrcpp::downCast<Cmp_exprContext *>(_localctx)->result =  antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l->result; 
    setState(419);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx)) {
    case 1: {
      setState(415);
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
      setState(416);
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
    setState(422);
    antlrcpp::downCast<Add_exprContext *>(_localctx)->a = mul_expr(0);
     antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  antlrcpp::downCast<Add_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(432);
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
        setState(425);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(426);
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
        setState(427);
        antlrcpp::downCast<Add_exprContext *>(_localctx)->r = mul_expr(0);
         antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Add_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Add_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Add_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Add_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(434);
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
    setState(436);
    antlrcpp::downCast<Mul_exprContext *>(_localctx)->a = unary();
     antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  antlrcpp::downCast<Mul_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(446);
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
        setState(439);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(440);
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
        setState(441);
        antlrcpp::downCast<Mul_exprContext *>(_localctx)->r = unary();
         antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Mul_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Mul_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Mul_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Mul_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(448);
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
    setState(456);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::MINUS:
      case PggParser::BANG: {
        enterOuterAlt(_localctx, 1);
        setState(449);
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
        setState(450);
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
      case PggParser::LBRACKET:
      case PggParser::IDENT: {
        enterOuterAlt(_localctx, 2);
        setState(453);
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
    setState(464);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(458);
      antlrcpp::downCast<PostfixContext *>(_localctx)->c = call();
       antlrcpp::downCast<PostfixContext *>(_localctx)->result =  antlrcpp::downCast<PostfixContext *>(_localctx)->c->result; 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(461);
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
    setState(466);
    antlrcpp::downCast<CallContext *>(_localctx)->q = qualified_name();
    setState(467);
    match(PggParser::LPAREN);
    setState(476);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 44539951935488) != 0)) {
      setState(468);
      antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
      antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
      setState(473);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(469);
        match(PggParser::COMMA);
        setState(470);
        antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
        antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
        setState(475);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(478);
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
    setState(481);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
    setState(486);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT) {
      setState(482);
      match(PggParser::DOT);
      setState(483);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
      setState(488);
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
    setState(494);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 40, _ctx)) {
    case 1: {
      setState(491);
      antlrcpp::downCast<ArgContext *>(_localctx)->n = match(PggParser::IDENT);
      setState(492);
      match(PggParser::ASSIGN);
       _localctx->a.name = (antlrcpp::downCast<ArgContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<ArgContext *>(_localctx)->n->getText() : ""); _localctx->a.hasName = true; 
      break;
    }

    default:
      break;
    }
    setState(496);
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

PggParser::List_literalContext* PggParser::PrimaryContext::list_literal() {
  return getRuleContext<PggParser::List_literalContext>(0);
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
    setState(523);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(499);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->n = match(PggParser::NUMBER);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<PrimaryContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->n)); 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(501);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->s = match(PggParser::STRING);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<PrimaryContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->s)); 
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(503);
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
      setState(505);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->v = vec_literal();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->v->result; 
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(508);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->l = list_literal();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->l->result; 
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(511);
      match(PggParser::NONE);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(513);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->i = match(PggParser::IDENT);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newIdent((antlrcpp::downCast<PrimaryContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->i->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->i)); 
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(515);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->a->result; 
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(518);
      match(PggParser::LPAREN);
      setState(519);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->e = aexpr();
      setState(520);
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
    setState(525);
    match(PggParser::AT);
    setState(526);
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
    setState(529);
    match(PggParser::LPAREN);
    setState(530);
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken = match(PggParser::NUMBER);
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->n.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken);
    setState(535);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(531);
      match(PggParser::COMMA);
      setState(532);
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken = match(PggParser::NUMBER);
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->n.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->numberToken);
      setState(537);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(538);
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

//----------------- List_literalContext ------------------------------------------------------------------

PggParser::List_literalContext::List_literalContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::List_literalContext::LBRACKET() {
  return getToken(PggParser::LBRACKET, 0);
}

tree::TerminalNode* PggParser::List_literalContext::RBRACKET() {
  return getToken(PggParser::RBRACKET, 0);
}

std::vector<PggParser::AexprContext *> PggParser::List_literalContext::aexpr() {
  return getRuleContexts<PggParser::AexprContext>();
}

PggParser::AexprContext* PggParser::List_literalContext::aexpr(size_t i) {
  return getRuleContext<PggParser::AexprContext>(i);
}

std::vector<tree::TerminalNode *> PggParser::List_literalContext::COMMA() {
  return getTokens(PggParser::COMMA);
}

tree::TerminalNode* PggParser::List_literalContext::COMMA(size_t i) {
  return getToken(PggParser::COMMA, i);
}


size_t PggParser::List_literalContext::getRuleIndex() const {
  return PggParser::RuleList_literal;
}


PggParser::List_literalContext* PggParser::list_literal() {
  List_literalContext *_localctx = _tracker.createInstance<List_literalContext>(_ctx, getState());
  enterRule(_localctx, 66, PggParser::RuleList_literal);
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
    setState(541);
    match(PggParser::LBRACKET);
    setState(553);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 44539951935488) != 0)) {
      setState(542);
      antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext = aexpr();
      antlrcpp::downCast<List_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext);
      setState(547);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(543);
          match(PggParser::COMMA);
          setState(544);
          antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext = aexpr();
          antlrcpp::downCast<List_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext); 
        }
        setState(549);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 43, _ctx);
      }
      setState(551);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PggParser::COMMA) {
        setState(550);
        match(PggParser::COMMA);
      }
    }
    setState(555);
    match(PggParser::RBRACKET);
     antlrcpp::downCast<List_literalContext *>(_localctx)->result =  gc->newList(gc->resultsOf(antlrcpp::downCast<List_literalContext *>(_localctx)->e), spanOf(_localctx)); 
   
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
  enterRule(_localctx, 68, PggParser::RuleLiteral);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(571);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(558);
        antlrcpp::downCast<LiteralContext *>(_localctx)->n = match(PggParser::NUMBER);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<LiteralContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->n)); 
        break;
      }

      case PggParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(560);
        antlrcpp::downCast<LiteralContext *>(_localctx)->s = match(PggParser::STRING);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<LiteralContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->s)); 
        break;
      }

      case PggParser::TRUE:
      case PggParser::FALSE: {
        enterOuterAlt(_localctx, 3);
        setState(562);
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
        setState(564);
        antlrcpp::downCast<LiteralContext *>(_localctx)->v = vec_literal();
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  antlrcpp::downCast<LiteralContext *>(_localctx)->v->result; 
        break;
      }

      case PggParser::NONE: {
        enterOuterAlt(_localctx, 5);
        setState(567);
        match(PggParser::NONE);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
        break;
      }

      case PggParser::IDENT: {
        enterOuterAlt(_localctx, 6);
        setState(569);
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
  size_t startState = 70;
  enterRecursionRule(_localctx, 70, PggParser::RuleType, precedence);

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
    setState(596);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 49, _ctx)) {
    case 1: {
      setState(574);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
      setState(575);
      match(PggParser::LT);
      setState(576);
      antlrcpp::downCast<TypeContext *>(_localctx)->a = type(0);
      setState(577);
      match(PggParser::GT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeGeneric((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b), antlrcpp::downCast<TypeContext *>(_localctx)->a->result, spanOf(_localctx)); 
      break;
    }

    case 2: {
      setState(580);
      antlrcpp::downCast<TypeContext *>(_localctx)->e = match(PggParser::IDENT);
      setState(581);
      match(PggParser::LBRACE);
      setState(590);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PggParser::IDENT) {
        setState(582);
        antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
        setState(587);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == PggParser::COMMA) {
          setState(583);
          match(PggParser::COMMA);
          setState(584);
          antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
          antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
          setState(589);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
      }
      setState(592);
      match(PggParser::RBRACE);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeEnum(antlrcpp::downCast<TypeContext *>(_localctx)->e, gc->namesOf(antlrcpp::downCast<TypeContext *>(_localctx)->v), spanOf(_localctx)); 
      break;
    }

    case 3: {
      setState(594);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeName((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b)); 
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(607);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(605);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(598);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(599);
          match(PggParser::QUESTION);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeOptional(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(601);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(602);
          match(PggParser::LBRACKET);
          setState(603);
          match(PggParser::RBRACKET);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeList(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        default:
          break;
        } 
      }
      setState(609);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx);
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
    case 35: return typeSempred(antlrcpp::downCast<TypeContext *>(context), predicateIndex);

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
