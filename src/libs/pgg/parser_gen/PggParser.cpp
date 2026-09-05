
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
      "attr_ref", "vec_literal", "vec_elem", "list_literal", "literal", 
      "type"
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
  	4,1,48,618,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,2,34,7,34,2,35,7,
  	35,2,36,7,36,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,
  	1,0,1,0,1,0,5,0,92,8,0,10,0,12,0,95,9,0,1,0,1,0,1,0,1,1,1,1,1,1,1,1,3,
  	1,104,8,1,1,1,1,1,3,1,108,8,1,1,1,1,1,1,1,1,2,1,2,1,2,1,2,1,2,1,2,1,2,
  	1,2,3,2,121,8,2,1,2,1,2,1,2,1,3,1,3,1,3,1,3,1,3,1,4,1,4,1,4,1,4,1,4,1,
  	4,3,4,137,8,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,147,8,4,10,4,12,4,150,
  	9,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,5,4,165,8,4,10,
  	4,12,4,168,9,4,1,4,1,4,1,4,1,4,1,5,1,5,1,5,5,5,177,8,5,10,5,12,5,180,
  	9,5,1,5,1,5,1,6,1,6,1,6,1,6,1,6,1,6,1,6,3,6,191,8,6,1,6,1,6,1,7,1,7,1,
  	7,5,7,198,8,7,10,7,12,7,201,9,7,1,7,1,7,1,8,1,8,1,8,1,8,1,8,1,9,1,9,1,
  	9,1,9,1,9,1,9,1,9,1,9,1,9,3,9,219,8,9,1,9,1,9,1,9,3,9,224,8,9,1,9,1,9,
  	1,9,1,10,1,10,1,10,1,10,1,10,1,10,1,10,1,10,1,10,3,10,238,8,10,1,10,1,
  	10,1,10,3,10,243,8,10,1,10,1,10,1,10,1,11,1,11,1,11,1,11,1,11,1,11,1,
  	11,1,11,1,11,1,11,1,11,1,11,1,11,1,11,3,11,262,8,11,1,12,1,12,1,12,1,
  	12,1,12,1,13,1,13,1,13,5,13,272,8,13,10,13,12,13,275,9,13,1,13,1,13,1,
  	14,1,14,1,14,1,14,3,14,283,8,14,1,14,1,14,1,14,1,15,1,15,1,15,1,15,1,
  	15,1,15,1,15,1,15,1,15,5,15,297,8,15,10,15,12,15,300,9,15,1,15,1,15,1,
  	16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,16,1,
  	16,5,16,319,8,16,10,16,12,16,322,9,16,3,16,324,8,16,1,16,1,16,1,16,1,
  	16,5,16,330,8,16,10,16,12,16,333,9,16,1,16,1,16,1,16,1,16,5,16,339,8,
  	16,10,16,12,16,342,9,16,1,16,1,16,1,16,1,16,1,17,1,17,1,17,1,17,1,17,
  	1,17,1,17,1,17,1,17,5,17,357,8,17,10,17,12,17,360,9,17,1,17,1,17,1,17,
  	1,17,5,17,366,8,17,10,17,12,17,369,9,17,1,17,1,17,1,17,1,17,1,18,1,18,
  	1,18,1,19,1,19,1,19,1,19,1,19,1,19,1,19,1,19,3,19,386,8,19,1,20,1,20,
  	1,20,1,20,1,20,1,20,1,20,1,20,1,20,5,20,397,8,20,10,20,12,20,400,9,20,
  	1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,1,21,5,21,411,8,21,10,21,12,21,
  	414,9,21,1,22,1,22,1,22,1,22,1,22,1,22,3,22,422,8,22,1,23,1,23,1,23,1,
  	23,1,23,1,23,1,23,1,23,1,23,5,23,433,8,23,10,23,12,23,436,9,23,1,24,1,
  	24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,5,24,447,8,24,10,24,12,24,450,9,
  	24,1,25,1,25,1,25,1,25,1,25,1,25,1,25,3,25,459,8,25,1,26,1,26,1,26,1,
  	26,1,26,1,26,3,26,467,8,26,1,27,1,27,1,27,1,27,1,27,5,27,474,8,27,10,
  	27,12,27,477,9,27,3,27,479,8,27,1,27,1,27,1,27,1,28,1,28,1,28,5,28,487,
  	8,28,10,28,12,28,490,9,28,1,28,1,28,1,29,1,29,1,29,3,29,497,8,29,1,29,
  	1,29,1,29,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,
  	1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,1,30,3,30,526,
  	8,30,1,31,1,31,1,31,1,31,1,32,1,32,1,32,1,32,4,32,536,8,32,11,32,12,32,
  	537,1,32,1,32,1,32,1,33,3,33,544,8,33,1,33,1,33,1,33,1,34,1,34,1,34,1,
  	34,5,34,553,8,34,10,34,12,34,556,9,34,1,34,3,34,559,8,34,3,34,561,8,34,
  	1,34,1,34,1,34,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,1,35,
  	1,35,1,35,3,35,579,8,35,1,36,1,36,1,36,1,36,1,36,1,36,1,36,1,36,1,36,
  	1,36,1,36,1,36,5,36,593,8,36,10,36,12,36,596,9,36,3,36,598,8,36,1,36,
  	1,36,1,36,1,36,3,36,604,8,36,1,36,1,36,1,36,1,36,1,36,1,36,1,36,5,36,
  	613,8,36,10,36,12,36,616,9,36,1,36,0,5,40,42,46,48,72,37,0,2,4,6,8,10,
  	12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,
  	58,60,62,64,66,68,70,72,0,6,1,1,46,46,1,0,19,24,1,0,25,26,1,0,27,29,2,
  	0,26,26,30,30,1,0,13,14,654,0,74,1,0,0,0,2,99,1,0,0,0,4,112,1,0,0,0,6,
  	125,1,0,0,0,8,130,1,0,0,0,10,173,1,0,0,0,12,183,1,0,0,0,14,194,1,0,0,
  	0,16,204,1,0,0,0,18,209,1,0,0,0,20,228,1,0,0,0,22,261,1,0,0,0,24,263,
  	1,0,0,0,26,268,1,0,0,0,28,278,1,0,0,0,30,287,1,0,0,0,32,303,1,0,0,0,34,
  	347,1,0,0,0,36,374,1,0,0,0,38,377,1,0,0,0,40,387,1,0,0,0,42,401,1,0,0,
  	0,44,415,1,0,0,0,46,423,1,0,0,0,48,437,1,0,0,0,50,458,1,0,0,0,52,466,
  	1,0,0,0,54,468,1,0,0,0,56,483,1,0,0,0,58,496,1,0,0,0,60,525,1,0,0,0,62,
  	527,1,0,0,0,64,531,1,0,0,0,66,543,1,0,0,0,68,548,1,0,0,0,70,578,1,0,0,
  	0,72,603,1,0,0,0,74,93,6,0,-1,0,75,92,5,46,0,0,76,77,3,2,1,0,77,78,6,
  	0,-1,0,78,92,1,0,0,0,79,80,3,4,2,0,80,81,6,0,-1,0,81,92,1,0,0,0,82,83,
  	3,8,4,0,83,84,6,0,-1,0,84,92,1,0,0,0,85,86,3,6,3,0,86,87,6,0,-1,0,87,
  	92,1,0,0,0,88,89,3,22,11,0,89,90,6,0,-1,0,90,92,1,0,0,0,91,75,1,0,0,0,
  	91,76,1,0,0,0,91,79,1,0,0,0,91,82,1,0,0,0,91,85,1,0,0,0,91,88,1,0,0,0,
  	92,95,1,0,0,0,93,91,1,0,0,0,93,94,1,0,0,0,94,96,1,0,0,0,95,93,1,0,0,0,
  	96,97,5,0,0,1,97,98,6,0,-1,0,98,1,1,0,0,0,99,100,5,5,0,0,100,103,3,56,
  	28,0,101,102,5,6,0,0,102,104,5,45,0,0,103,101,1,0,0,0,103,104,1,0,0,0,
  	104,107,1,0,0,0,105,106,5,33,0,0,106,108,5,17,0,0,107,105,1,0,0,0,107,
  	108,1,0,0,0,108,109,1,0,0,0,109,110,5,46,0,0,110,111,6,1,-1,0,111,3,1,
  	0,0,0,112,113,5,10,0,0,113,114,5,45,0,0,114,115,5,35,0,0,115,120,3,72,
  	36,0,116,117,5,38,0,0,117,118,3,70,35,0,118,119,6,2,-1,0,119,121,1,0,
  	0,0,120,116,1,0,0,0,120,121,1,0,0,0,121,122,1,0,0,0,122,123,5,46,0,0,
  	123,124,6,2,-1,0,124,5,1,0,0,0,125,126,5,11,0,0,126,127,5,45,0,0,127,
  	128,5,46,0,0,128,129,6,3,-1,0,129,7,1,0,0,0,130,131,5,1,0,0,131,132,5,
  	45,0,0,132,136,5,39,0,0,133,134,3,10,5,0,134,135,6,4,-1,0,135,137,1,0,
  	0,0,136,133,1,0,0,0,136,137,1,0,0,0,137,138,1,0,0,0,138,139,5,40,0,0,
  	139,140,5,18,0,0,140,141,5,39,0,0,141,142,3,14,7,0,142,143,5,40,0,0,143,
  	144,6,4,-1,0,144,148,5,41,0,0,145,147,5,46,0,0,146,145,1,0,0,0,147,150,
  	1,0,0,0,148,146,1,0,0,0,148,149,1,0,0,0,149,166,1,0,0,0,150,148,1,0,0,
  	0,151,165,5,46,0,0,152,153,5,15,0,0,153,154,5,46,0,0,154,165,6,4,-1,0,
  	155,156,3,18,9,0,156,157,6,4,-1,0,157,165,1,0,0,0,158,159,3,20,10,0,159,
  	160,6,4,-1,0,160,165,1,0,0,0,161,162,3,22,11,0,162,163,6,4,-1,0,163,165,
  	1,0,0,0,164,151,1,0,0,0,164,152,1,0,0,0,164,155,1,0,0,0,164,158,1,0,0,
  	0,164,161,1,0,0,0,165,168,1,0,0,0,166,164,1,0,0,0,166,167,1,0,0,0,167,
  	169,1,0,0,0,168,166,1,0,0,0,169,170,5,42,0,0,170,171,7,0,0,0,171,172,
  	6,4,-1,0,172,9,1,0,0,0,173,178,3,12,6,0,174,175,5,37,0,0,175,177,3,12,
  	6,0,176,174,1,0,0,0,177,180,1,0,0,0,178,176,1,0,0,0,178,179,1,0,0,0,179,
  	181,1,0,0,0,180,178,1,0,0,0,181,182,6,5,-1,0,182,11,1,0,0,0,183,184,5,
  	45,0,0,184,185,5,35,0,0,185,190,3,72,36,0,186,187,5,38,0,0,187,188,3,
  	70,35,0,188,189,6,6,-1,0,189,191,1,0,0,0,190,186,1,0,0,0,190,191,1,0,
  	0,0,191,192,1,0,0,0,192,193,6,6,-1,0,193,13,1,0,0,0,194,199,3,16,8,0,
  	195,196,5,37,0,0,196,198,3,16,8,0,197,195,1,0,0,0,198,201,1,0,0,0,199,
  	197,1,0,0,0,199,200,1,0,0,0,200,202,1,0,0,0,201,199,1,0,0,0,202,203,6,
  	7,-1,0,203,15,1,0,0,0,204,205,5,45,0,0,205,206,5,35,0,0,206,207,3,72,
  	36,0,207,208,6,8,-1,0,208,17,1,0,0,0,209,218,5,2,0,0,210,211,5,45,0,0,
  	211,212,5,45,0,0,212,213,3,62,31,0,213,214,6,9,-1,0,214,219,1,0,0,0,215,
  	216,3,36,18,0,216,217,6,9,-1,0,217,219,1,0,0,0,218,210,1,0,0,0,218,215,
  	1,0,0,0,219,223,1,0,0,0,220,221,5,35,0,0,221,222,5,16,0,0,222,224,6,9,
  	-1,0,223,220,1,0,0,0,223,224,1,0,0,0,224,225,1,0,0,0,225,226,5,46,0,0,
  	226,227,6,9,-1,0,227,19,1,0,0,0,228,237,5,3,0,0,229,230,5,45,0,0,230,
  	231,5,45,0,0,231,232,3,62,31,0,232,233,6,10,-1,0,233,238,1,0,0,0,234,
  	235,3,36,18,0,235,236,6,10,-1,0,236,238,1,0,0,0,237,229,1,0,0,0,237,234,
  	1,0,0,0,238,242,1,0,0,0,239,240,5,35,0,0,240,241,5,16,0,0,241,243,6,10,
  	-1,0,242,239,1,0,0,0,242,243,1,0,0,0,243,244,1,0,0,0,244,245,5,46,0,0,
  	245,246,6,10,-1,0,246,21,1,0,0,0,247,248,3,24,12,0,248,249,7,0,0,0,249,
  	250,6,11,-1,0,250,262,1,0,0,0,251,252,3,28,14,0,252,253,7,0,0,0,253,254,
  	6,11,-1,0,254,262,1,0,0,0,255,256,3,32,16,0,256,257,6,11,-1,0,257,262,
  	1,0,0,0,258,259,3,34,17,0,259,260,6,11,-1,0,260,262,1,0,0,0,261,247,1,
  	0,0,0,261,251,1,0,0,0,261,255,1,0,0,0,261,258,1,0,0,0,262,23,1,0,0,0,
  	263,264,3,26,13,0,264,265,5,38,0,0,265,266,3,36,18,0,266,267,6,12,-1,
  	0,267,25,1,0,0,0,268,273,5,45,0,0,269,270,5,37,0,0,270,272,5,45,0,0,271,
  	269,1,0,0,0,272,275,1,0,0,0,273,271,1,0,0,0,273,274,1,0,0,0,274,276,1,
  	0,0,0,275,273,1,0,0,0,276,277,6,13,-1,0,277,27,1,0,0,0,278,282,5,4,0,
  	0,279,280,5,45,0,0,280,281,5,35,0,0,281,283,6,14,-1,0,282,279,1,0,0,0,
  	282,283,1,0,0,0,283,284,1,0,0,0,284,285,3,30,15,0,285,286,6,14,-1,0,286,
  	29,1,0,0,0,287,288,5,45,0,0,288,298,6,15,-1,0,289,290,5,36,0,0,290,291,
  	5,45,0,0,291,297,6,15,-1,0,292,293,5,43,0,0,293,294,5,17,0,0,294,295,
  	5,44,0,0,295,297,6,15,-1,0,296,289,1,0,0,0,296,292,1,0,0,0,297,300,1,
  	0,0,0,298,296,1,0,0,0,298,299,1,0,0,0,299,301,1,0,0,0,300,298,1,0,0,0,
  	301,302,6,15,-1,0,302,31,1,0,0,0,303,304,3,26,13,0,304,305,5,38,0,0,305,
  	306,5,7,0,0,306,307,5,39,0,0,307,308,3,36,18,0,308,309,5,37,0,0,309,310,
  	5,45,0,0,310,311,5,38,0,0,311,312,3,36,18,0,312,313,5,40,0,0,313,314,
  	6,16,-1,0,314,323,5,31,0,0,315,320,5,45,0,0,316,317,5,37,0,0,317,319,
  	5,45,0,0,318,316,1,0,0,0,319,322,1,0,0,0,320,318,1,0,0,0,320,321,1,0,
  	0,0,321,324,1,0,0,0,322,320,1,0,0,0,323,315,1,0,0,0,323,324,1,0,0,0,324,
  	325,1,0,0,0,325,326,5,31,0,0,326,327,6,16,-1,0,327,331,5,41,0,0,328,330,
  	5,46,0,0,329,328,1,0,0,0,330,333,1,0,0,0,331,329,1,0,0,0,331,332,1,0,
  	0,0,332,340,1,0,0,0,333,331,1,0,0,0,334,339,5,46,0,0,335,336,3,22,11,
  	0,336,337,6,16,-1,0,337,339,1,0,0,0,338,334,1,0,0,0,338,335,1,0,0,0,339,
  	342,1,0,0,0,340,338,1,0,0,0,340,341,1,0,0,0,341,343,1,0,0,0,342,340,1,
  	0,0,0,343,344,5,42,0,0,344,345,7,0,0,0,345,346,6,16,-1,0,346,33,1,0,0,
  	0,347,348,5,45,0,0,348,349,5,38,0,0,349,350,5,8,0,0,350,351,5,45,0,0,
  	351,352,5,9,0,0,352,353,3,36,18,0,353,354,6,17,-1,0,354,358,5,41,0,0,
  	355,357,5,46,0,0,356,355,1,0,0,0,357,360,1,0,0,0,358,356,1,0,0,0,358,
  	359,1,0,0,0,359,367,1,0,0,0,360,358,1,0,0,0,361,366,5,46,0,0,362,363,
  	3,22,11,0,363,364,6,17,-1,0,364,366,1,0,0,0,365,361,1,0,0,0,365,362,1,
  	0,0,0,366,369,1,0,0,0,367,365,1,0,0,0,367,368,1,0,0,0,368,370,1,0,0,0,
  	369,367,1,0,0,0,370,371,5,42,0,0,371,372,7,0,0,0,372,373,6,17,-1,0,373,
  	35,1,0,0,0,374,375,3,38,19,0,375,376,6,18,-1,0,376,37,1,0,0,0,377,378,
  	3,40,20,0,378,385,6,19,-1,0,379,380,5,34,0,0,380,381,3,36,18,0,381,382,
  	5,35,0,0,382,383,3,36,18,0,383,384,6,19,-1,0,384,386,1,0,0,0,385,379,
  	1,0,0,0,385,386,1,0,0,0,386,39,1,0,0,0,387,388,6,20,-1,0,388,389,3,42,
  	21,0,389,390,6,20,-1,0,390,398,1,0,0,0,391,392,10,2,0,0,392,393,5,31,
  	0,0,393,394,3,42,21,0,394,395,6,20,-1,0,395,397,1,0,0,0,396,391,1,0,0,
  	0,397,400,1,0,0,0,398,396,1,0,0,0,398,399,1,0,0,0,399,41,1,0,0,0,400,
  	398,1,0,0,0,401,402,6,21,-1,0,402,403,3,44,22,0,403,404,6,21,-1,0,404,
  	412,1,0,0,0,405,406,10,2,0,0,406,407,5,32,0,0,407,408,3,44,22,0,408,409,
  	6,21,-1,0,409,411,1,0,0,0,410,405,1,0,0,0,411,414,1,0,0,0,412,410,1,0,
  	0,0,412,413,1,0,0,0,413,43,1,0,0,0,414,412,1,0,0,0,415,416,3,46,23,0,
  	416,421,6,22,-1,0,417,418,7,1,0,0,418,419,3,46,23,0,419,420,6,22,-1,0,
  	420,422,1,0,0,0,421,417,1,0,0,0,421,422,1,0,0,0,422,45,1,0,0,0,423,424,
  	6,23,-1,0,424,425,3,48,24,0,425,426,6,23,-1,0,426,434,1,0,0,0,427,428,
  	10,2,0,0,428,429,7,2,0,0,429,430,3,48,24,0,430,431,6,23,-1,0,431,433,
  	1,0,0,0,432,427,1,0,0,0,433,436,1,0,0,0,434,432,1,0,0,0,434,435,1,0,0,
  	0,435,47,1,0,0,0,436,434,1,0,0,0,437,438,6,24,-1,0,438,439,3,50,25,0,
  	439,440,6,24,-1,0,440,448,1,0,0,0,441,442,10,2,0,0,442,443,7,3,0,0,443,
  	444,3,50,25,0,444,445,6,24,-1,0,445,447,1,0,0,0,446,441,1,0,0,0,447,450,
  	1,0,0,0,448,446,1,0,0,0,448,449,1,0,0,0,449,49,1,0,0,0,450,448,1,0,0,
  	0,451,452,7,4,0,0,452,453,3,50,25,0,453,454,6,25,-1,0,454,459,1,0,0,0,
  	455,456,3,52,26,0,456,457,6,25,-1,0,457,459,1,0,0,0,458,451,1,0,0,0,458,
  	455,1,0,0,0,459,51,1,0,0,0,460,461,3,54,27,0,461,462,6,26,-1,0,462,467,
  	1,0,0,0,463,464,3,60,30,0,464,465,6,26,-1,0,465,467,1,0,0,0,466,460,1,
  	0,0,0,466,463,1,0,0,0,467,53,1,0,0,0,468,469,3,56,28,0,469,478,5,39,0,
  	0,470,475,3,58,29,0,471,472,5,37,0,0,472,474,3,58,29,0,473,471,1,0,0,
  	0,474,477,1,0,0,0,475,473,1,0,0,0,475,476,1,0,0,0,476,479,1,0,0,0,477,
  	475,1,0,0,0,478,470,1,0,0,0,478,479,1,0,0,0,479,480,1,0,0,0,480,481,5,
  	40,0,0,481,482,6,27,-1,0,482,55,1,0,0,0,483,488,5,45,0,0,484,485,5,36,
  	0,0,485,487,5,45,0,0,486,484,1,0,0,0,487,490,1,0,0,0,488,486,1,0,0,0,
  	488,489,1,0,0,0,489,491,1,0,0,0,490,488,1,0,0,0,491,492,6,28,-1,0,492,
  	57,1,0,0,0,493,494,5,45,0,0,494,495,5,38,0,0,495,497,6,29,-1,0,496,493,
  	1,0,0,0,496,497,1,0,0,0,497,498,1,0,0,0,498,499,3,36,18,0,499,500,6,29,
  	-1,0,500,59,1,0,0,0,501,502,5,17,0,0,502,526,6,30,-1,0,503,504,5,16,0,
  	0,504,526,6,30,-1,0,505,506,7,5,0,0,506,526,6,30,-1,0,507,508,3,64,32,
  	0,508,509,6,30,-1,0,509,526,1,0,0,0,510,511,3,68,34,0,511,512,6,30,-1,
  	0,512,526,1,0,0,0,513,514,5,12,0,0,514,526,6,30,-1,0,515,516,5,45,0,0,
  	516,526,6,30,-1,0,517,518,3,62,31,0,518,519,6,30,-1,0,519,526,1,0,0,0,
  	520,521,5,39,0,0,521,522,3,36,18,0,522,523,5,40,0,0,523,524,6,30,-1,0,
  	524,526,1,0,0,0,525,501,1,0,0,0,525,503,1,0,0,0,525,505,1,0,0,0,525,507,
  	1,0,0,0,525,510,1,0,0,0,525,513,1,0,0,0,525,515,1,0,0,0,525,517,1,0,0,
  	0,525,520,1,0,0,0,526,61,1,0,0,0,527,528,5,33,0,0,528,529,5,45,0,0,529,
  	530,6,31,-1,0,530,63,1,0,0,0,531,532,5,39,0,0,532,535,3,66,33,0,533,534,
  	5,37,0,0,534,536,3,66,33,0,535,533,1,0,0,0,536,537,1,0,0,0,537,535,1,
  	0,0,0,537,538,1,0,0,0,538,539,1,0,0,0,539,540,5,40,0,0,540,541,6,32,-1,
  	0,541,65,1,0,0,0,542,544,5,26,0,0,543,542,1,0,0,0,543,544,1,0,0,0,544,
  	545,1,0,0,0,545,546,5,17,0,0,546,547,6,33,-1,0,547,67,1,0,0,0,548,560,
  	5,43,0,0,549,554,3,36,18,0,550,551,5,37,0,0,551,553,3,36,18,0,552,550,
  	1,0,0,0,553,556,1,0,0,0,554,552,1,0,0,0,554,555,1,0,0,0,555,558,1,0,0,
  	0,556,554,1,0,0,0,557,559,5,37,0,0,558,557,1,0,0,0,558,559,1,0,0,0,559,
  	561,1,0,0,0,560,549,1,0,0,0,560,561,1,0,0,0,561,562,1,0,0,0,562,563,5,
  	44,0,0,563,564,6,34,-1,0,564,69,1,0,0,0,565,566,5,17,0,0,566,579,6,35,
  	-1,0,567,568,5,16,0,0,568,579,6,35,-1,0,569,570,7,5,0,0,570,579,6,35,
  	-1,0,571,572,3,64,32,0,572,573,6,35,-1,0,573,579,1,0,0,0,574,575,5,12,
  	0,0,575,579,6,35,-1,0,576,577,5,45,0,0,577,579,6,35,-1,0,578,565,1,0,
  	0,0,578,567,1,0,0,0,578,569,1,0,0,0,578,571,1,0,0,0,578,574,1,0,0,0,578,
  	576,1,0,0,0,579,71,1,0,0,0,580,581,6,36,-1,0,581,582,5,45,0,0,582,583,
  	5,23,0,0,583,584,3,72,36,0,584,585,5,24,0,0,585,586,6,36,-1,0,586,604,
  	1,0,0,0,587,588,5,45,0,0,588,597,5,41,0,0,589,594,5,45,0,0,590,591,5,
  	37,0,0,591,593,5,45,0,0,592,590,1,0,0,0,593,596,1,0,0,0,594,592,1,0,0,
  	0,594,595,1,0,0,0,595,598,1,0,0,0,596,594,1,0,0,0,597,589,1,0,0,0,597,
  	598,1,0,0,0,598,599,1,0,0,0,599,600,5,42,0,0,600,604,6,36,-1,0,601,602,
  	5,45,0,0,602,604,6,36,-1,0,603,580,1,0,0,0,603,587,1,0,0,0,603,601,1,
  	0,0,0,604,614,1,0,0,0,605,606,10,5,0,0,606,607,5,34,0,0,607,613,6,36,
  	-1,0,608,609,10,4,0,0,609,610,5,43,0,0,610,611,5,44,0,0,611,613,6,36,
  	-1,0,612,605,1,0,0,0,612,608,1,0,0,0,613,616,1,0,0,0,614,612,1,0,0,0,
  	614,615,1,0,0,0,615,73,1,0,0,0,616,614,1,0,0,0,53,91,93,103,107,120,136,
  	148,164,166,178,190,199,218,223,237,242,261,273,282,296,298,320,323,331,
  	338,340,358,365,367,385,398,412,421,434,448,458,466,475,478,488,496,525,
  	537,543,554,558,560,578,594,597,603,612,614
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
    setState(93);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116269618) != 0)) {
      setState(91);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(75);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::IMPORT: {
          setState(76);
          antlrcpp::downCast<FileContext *>(_localctx)->i = import_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->i->result); 
          break;
        }

        case PggParser::PARAM: {
          setState(79);
          antlrcpp::downCast<FileContext *>(_localctx)->p = param_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->p->result); 
          break;
        }

        case PggParser::DEF: {
          setState(82);
          antlrcpp::downCast<FileContext *>(_localctx)->d = def_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->d->result); 
          break;
        }

        case PggParser::OUTPUT: {
          setState(85);
          antlrcpp::downCast<FileContext *>(_localctx)->o = output_stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->o->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(88);
          antlrcpp::downCast<FileContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<FileContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(95);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(96);
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
    setState(99);
    match(PggParser::IMPORT);
    setState(100);
    antlrcpp::downCast<Import_stmtContext *>(_localctx)->q = qualified_name();
    setState(103);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AS) {
      setState(101);
      match(PggParser::AS);
      setState(102);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->a = match(PggParser::IDENT);
    }
    setState(107);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::AT) {
      setState(105);
      match(PggParser::AT);
      setState(106);
      antlrcpp::downCast<Import_stmtContext *>(_localctx)->v = match(PggParser::NUMBER);
    }
    setState(109);
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
    setState(112);
    match(PggParser::PARAM);
    setState(113);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(114);
    match(PggParser::COLON);
    setState(115);
    antlrcpp::downCast<Param_stmtContext *>(_localctx)->t = type(0);
    setState(120);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(116);
      match(PggParser::ASSIGN);
      setState(117);
      antlrcpp::downCast<Param_stmtContext *>(_localctx)->d = literal();
       antlrcpp::downCast<Param_stmtContext *>(_localctx)->def =  antlrcpp::downCast<Param_stmtContext *>(_localctx)->d->result; antlrcpp::downCast<Param_stmtContext *>(_localctx)->hasDef =  true; 
    }
    setState(122);
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
    setState(125);
    match(PggParser::OUTPUT);
    setState(126);
    antlrcpp::downCast<Output_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(127);
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
    setState(130);
    match(PggParser::DEF);
    setState(131);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(132);
    match(PggParser::LPAREN);
    setState(136);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(133);
      antlrcpp::downCast<Def_stmtContext *>(_localctx)->p = params();
       antlrcpp::downCast<Def_stmtContext *>(_localctx)->ps =  antlrcpp::downCast<Def_stmtContext *>(_localctx)->p->result; 
    }
    setState(138);
    match(PggParser::RPAREN);
    setState(139);
    match(PggParser::ARROW);
    setState(140);
    match(PggParser::LPAREN);
    setState(141);
    antlrcpp::downCast<Def_stmtContext *>(_localctx)->o = outputs();
    setState(142);
    match(PggParser::RPAREN);
     gc->beginDef((antlrcpp::downCast<Def_stmtContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->n), _localctx->ps, antlrcpp::downCast<Def_stmtContext *>(_localctx)->o->result, spanOf(_localctx)); 
    setState(144);
    match(PggParser::LBRACE);
    setState(148);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(145);
        match(PggParser::NEWLINE); 
      }
      setState(150);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 6, _ctx);
    }
    setState(166);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116299292) != 0)) {
      setState(164);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(151);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TRIPLE_STRING: {
          setState(152);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc = match(PggParser::TRIPLE_STRING);
          setState(153);
          match(PggParser::NEWLINE);
           gc->defDoc((antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc != nullptr ? antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc->getText() : ""), spanTok(antlrcpp::downCast<Def_stmtContext *>(_localctx)->doc)); 
          break;
        }

        case PggParser::EXPECT: {
          setState(155);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->e = expect_stmt();
           gc->addExpect(antlrcpp::downCast<Def_stmtContext *>(_localctx)->e->result); 
          break;
        }

        case PggParser::ENSURE: {
          setState(158);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->en = ensure_stmt();
           gc->addEnsure(antlrcpp::downCast<Def_stmtContext *>(_localctx)->en->result); 
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(161);
          antlrcpp::downCast<Def_stmtContext *>(_localctx)->s = stmt();
           gc->addNode(antlrcpp::downCast<Def_stmtContext *>(_localctx)->s->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(168);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(169);
    match(PggParser::RBRACE);
    setState(170);
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
    setState(173);
    antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
    antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
    setState(178);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(174);
      match(PggParser::COMMA);
      setState(175);
      antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext = param();
      antlrcpp::downCast<ParamsContext *>(_localctx)->p.push_back(antlrcpp::downCast<ParamsContext *>(_localctx)->paramContext);
      setState(180);
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
    setState(183);
    antlrcpp::downCast<ParamContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(184);
    match(PggParser::COLON);
    setState(185);
    antlrcpp::downCast<ParamContext *>(_localctx)->t = type(0);
    setState(190);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::ASSIGN) {
      setState(186);
      match(PggParser::ASSIGN);
      setState(187);
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
    setState(194);
    antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
    antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
    setState(199);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(195);
      match(PggParser::COMMA);
      setState(196);
      antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext = out_decl();
      antlrcpp::downCast<OutputsContext *>(_localctx)->o.push_back(antlrcpp::downCast<OutputsContext *>(_localctx)->out_declContext);
      setState(201);
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
    setState(204);
    antlrcpp::downCast<Out_declContext *>(_localctx)->n = match(PggParser::IDENT);
    setState(205);
    match(PggParser::COLON);
    setState(206);
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
    setState(209);
    match(PggParser::EXPECT);
    setState(218);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx)) {
    case 1: {
      setState(210);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(211);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(212);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Expect_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(215);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Expect_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(223);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(220);
      match(PggParser::COLON);
      setState(221);
      antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Expect_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Expect_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Expect_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(225);
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
    setState(228);
    match(PggParser::ENSURE);
    setState(237);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      setState(229);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i = match(PggParser::IDENT);
      setState(230);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h = match(PggParser::IDENT);
      setState(231);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->formA =  true; antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->id =  (antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->i->getText() : ""); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->attr =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->a->result; gc->checkKeyword(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->h, "has"); 
      break;
    }

    case 2: {
      setState(234);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c = aexpr();
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->cond =  antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->c->result; 
      break;
    }

    default:
      break;
    }
    setState(242);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::COLON) {
      setState(239);
      match(PggParser::COLON);
      setState(240);
      antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m = match(PggParser::STRING);
       antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->msg =  gc->stringValue((antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m != nullptr ? antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m->getText() : ""), spanTok(antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->m)); antlrcpp::downCast<Ensure_stmtContext *>(_localctx)->hasMsg =  true; 
    }
    setState(244);
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
    setState(261);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(247);
      antlrcpp::downCast<StmtContext *>(_localctx)->b = binding();
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
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->b->result; 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(251);
      antlrcpp::downCast<StmtContext *>(_localctx)->t = tap_stmt();
      setState(252);
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
      setState(255);
      antlrcpp::downCast<StmtContext *>(_localctx)->r = repeat_zone();
       antlrcpp::downCast<StmtContext *>(_localctx)->result =  antlrcpp::downCast<StmtContext *>(_localctx)->r->result; 
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(258);
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
    setState(263);
    antlrcpp::downCast<BindingContext *>(_localctx)->t = targets();
    setState(264);
    match(PggParser::ASSIGN);
    setState(265);
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
    setState(268);
    antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
    setState(273);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::COMMA) {
      setState(269);
      match(PggParser::COMMA);
      setState(270);
      antlrcpp::downCast<TargetsContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<TargetsContext *>(_localctx)->i.push_back(antlrcpp::downCast<TargetsContext *>(_localctx)->identToken);
      setState(275);
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
    setState(278);
    match(PggParser::TAP);
    setState(282);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx)) {
    case 1: {
      setState(279);
      antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l = match(PggParser::IDENT);
      setState(280);
      match(PggParser::COLON);
       antlrcpp::downCast<Tap_stmtContext *>(_localctx)->label =  (antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l != nullptr ? antlrcpp::downCast<Tap_stmtContext *>(_localctx)->l->getText() : ""); antlrcpp::downCast<Tap_stmtContext *>(_localctx)->hasLabel =  true; 
      break;
    }

    default:
      break;
    }
    setState(284);
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
    setState(287);
    antlrcpp::downCast<PathContext *>(_localctx)->i = match(PggParser::IDENT);
     _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->i->getText() : ""))); 
    setState(298);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT

    || _la == PggParser::LBRACKET) {
      setState(296);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::DOT: {
          setState(289);
          match(PggParser::DOT);
          setState(290);
          antlrcpp::downCast<PathContext *>(_localctx)->j = match(PggParser::IDENT);
           _localctx->elems.push_back(gc->pathName((antlrcpp::downCast<PathContext *>(_localctx)->j != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->j->getText() : ""))); 
          break;
        }

        case PggParser::LBRACKET: {
          setState(292);
          match(PggParser::LBRACKET);
          setState(293);
          antlrcpp::downCast<PathContext *>(_localctx)->n = match(PggParser::NUMBER);
          setState(294);
          match(PggParser::RBRACKET);
           _localctx->elems.push_back(gc->pathIndex((antlrcpp::downCast<PathContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PathContext *>(_localctx)->n->getText() : ""))); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(300);
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
    setState(303);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t = targets();
    setState(304);
    match(PggParser::ASSIGN);
    setState(305);
    match(PggParser::REPEAT);
    setState(306);
    match(PggParser::LPAREN);
    setState(307);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v = aexpr();
    setState(308);
    match(PggParser::COMMA);
    setState(309);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it = match(PggParser::IDENT);
    setState(310);
    match(PggParser::ASSIGN);
    setState(311);
    antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n = aexpr();
    setState(312);
    match(PggParser::RPAREN);
     gc->checkKeyword(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->it, "iterations"); 
    setState(314);
    match(PggParser::PIPE);
    setState(323);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::IDENT) {
      setState(315);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
      setState(320);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(316);
        match(PggParser::COMMA);
        setState(317);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s.push_back(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->identToken);
        setState(322);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(325);
    match(PggParser::PIPE);
     gc->beginRepeat(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->t->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->v->result, antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->n->result, gc->nameListOf(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->s), spanOf(_localctx)); 
    setState(327);
    match(PggParser::LBRACE);
    setState(331);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(328);
        match(PggParser::NEWLINE); 
      }
      setState(333);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx);
    }
    setState(340);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(338);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(334);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(335);
          antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Repeat_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(342);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(343);
    match(PggParser::RBRACE);
    setState(344);
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
    setState(347);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt = match(PggParser::IDENT);
    setState(348);
    match(PggParser::ASSIGN);
    setState(349);
    match(PggParser::FOREACH);
    setState(350);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item = match(PggParser::IDENT);
    setState(351);
    match(PggParser::IN);
    setState(352);
    antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c = aexpr();
     gc->beginForeach((antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->tgt), (antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item != nullptr ? antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item->getText() : ""), spanTok(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->item), antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->c->result,
                             spanOf(_localctx)); 
    setState(354);
    match(PggParser::LBRACE);
    setState(358);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(355);
        match(PggParser::NEWLINE); 
      }
      setState(360);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    }
    setState(367);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 105553116266512) != 0)) {
      setState(365);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PggParser::NEWLINE: {
          setState(361);
          match(PggParser::NEWLINE);
          break;
        }

        case PggParser::TAP:
        case PggParser::IDENT: {
          setState(362);
          antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b = stmt();
           gc->addNode(antlrcpp::downCast<Foreach_zoneContext *>(_localctx)->b->result); 
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(369);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(370);
    match(PggParser::RBRACE);
    setState(371);
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
    setState(374);
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
    setState(377);
    antlrcpp::downCast<TernaryContext *>(_localctx)->c = or_expr(0);
     antlrcpp::downCast<TernaryContext *>(_localctx)->result =  antlrcpp::downCast<TernaryContext *>(_localctx)->c->result; 
    setState(385);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::QUESTION) {
      setState(379);
      match(PggParser::QUESTION);
      setState(380);
      antlrcpp::downCast<TernaryContext *>(_localctx)->t = aexpr();
      setState(381);
      match(PggParser::COLON);
      setState(382);
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
    setState(388);
    antlrcpp::downCast<Or_exprContext *>(_localctx)->a = and_expr(0);
     antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  antlrcpp::downCast<Or_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(398);
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
        setState(391);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(392);
        match(PggParser::PIPE);
        setState(393);
        antlrcpp::downCast<Or_exprContext *>(_localctx)->r = and_expr(0);
         antlrcpp::downCast<Or_exprContext *>(_localctx)->result =  gc->newBinary("|", antlrcpp::downCast<Or_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Or_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(400);
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
    setState(402);
    antlrcpp::downCast<And_exprContext *>(_localctx)->a = cmp_expr();
     antlrcpp::downCast<And_exprContext *>(_localctx)->result =  antlrcpp::downCast<And_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(412);
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
        setState(405);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(406);
        match(PggParser::AMP);
        setState(407);
        antlrcpp::downCast<And_exprContext *>(_localctx)->r = cmp_expr();
         antlrcpp::downCast<And_exprContext *>(_localctx)->result =  gc->newBinary("&", antlrcpp::downCast<And_exprContext *>(_localctx)->l->result, antlrcpp::downCast<And_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(414);
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
    setState(415);
    antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l = add_expr(0);
     antlrcpp::downCast<Cmp_exprContext *>(_localctx)->result =  antlrcpp::downCast<Cmp_exprContext *>(_localctx)->l->result; 
    setState(421);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 32, _ctx)) {
    case 1: {
      setState(417);
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
      setState(418);
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
    setState(424);
    antlrcpp::downCast<Add_exprContext *>(_localctx)->a = mul_expr(0);
     antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  antlrcpp::downCast<Add_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(434);
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
        setState(427);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(428);
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
        setState(429);
        antlrcpp::downCast<Add_exprContext *>(_localctx)->r = mul_expr(0);
         antlrcpp::downCast<Add_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Add_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Add_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Add_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Add_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(436);
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
    setState(438);
    antlrcpp::downCast<Mul_exprContext *>(_localctx)->a = unary();
     antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  antlrcpp::downCast<Mul_exprContext *>(_localctx)->a->result; 
    _ctx->stop = _input->LT(-1);
    setState(448);
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
        setState(441);

        if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
        setState(442);
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
        setState(443);
        antlrcpp::downCast<Mul_exprContext *>(_localctx)->r = unary();
         antlrcpp::downCast<Mul_exprContext *>(_localctx)->result =  gc->newBinary((antlrcpp::downCast<Mul_exprContext *>(_localctx)->op != nullptr ? antlrcpp::downCast<Mul_exprContext *>(_localctx)->op->getText() : ""), antlrcpp::downCast<Mul_exprContext *>(_localctx)->l->result, antlrcpp::downCast<Mul_exprContext *>(_localctx)->r->result, spanOf(_localctx));  
      }
      setState(450);
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
    setState(458);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::MINUS:
      case PggParser::BANG: {
        enterOuterAlt(_localctx, 1);
        setState(451);
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
        setState(452);
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
        setState(455);
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
    setState(466);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(460);
      antlrcpp::downCast<PostfixContext *>(_localctx)->c = call();
       antlrcpp::downCast<PostfixContext *>(_localctx)->result =  antlrcpp::downCast<PostfixContext *>(_localctx)->c->result; 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(463);
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
    setState(468);
    antlrcpp::downCast<CallContext *>(_localctx)->q = qualified_name();
    setState(469);
    match(PggParser::LPAREN);
    setState(478);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 44539951935488) != 0)) {
      setState(470);
      antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
      antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
      setState(475);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PggParser::COMMA) {
        setState(471);
        match(PggParser::COMMA);
        setState(472);
        antlrcpp::downCast<CallContext *>(_localctx)->argContext = arg();
        antlrcpp::downCast<CallContext *>(_localctx)->a.push_back(antlrcpp::downCast<CallContext *>(_localctx)->argContext);
        setState(477);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
    }
    setState(480);
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
    setState(483);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
    antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
    setState(488);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PggParser::DOT) {
      setState(484);
      match(PggParser::DOT);
      setState(485);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken = match(PggParser::IDENT);
      antlrcpp::downCast<Qualified_nameContext *>(_localctx)->i.push_back(antlrcpp::downCast<Qualified_nameContext *>(_localctx)->identToken);
      setState(490);
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
    setState(496);
    _errHandler->sync(this);

    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 40, _ctx)) {
    case 1: {
      setState(493);
      antlrcpp::downCast<ArgContext *>(_localctx)->n = match(PggParser::IDENT);
      setState(494);
      match(PggParser::ASSIGN);
       _localctx->a.name = (antlrcpp::downCast<ArgContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<ArgContext *>(_localctx)->n->getText() : ""); _localctx->a.hasName = true; 
      break;
    }

    default:
      break;
    }
    setState(498);
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
    setState(525);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 41, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(501);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->n = match(PggParser::NUMBER);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<PrimaryContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->n)); 
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(503);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->s = match(PggParser::STRING);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<PrimaryContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->s)); 
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(505);
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
      setState(507);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->v = vec_literal();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->v->result; 
      break;
    }

    case 5: {
      enterOuterAlt(_localctx, 5);
      setState(510);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->l = list_literal();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->l->result; 
      break;
    }

    case 6: {
      enterOuterAlt(_localctx, 6);
      setState(513);
      match(PggParser::NONE);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
      break;
    }

    case 7: {
      enterOuterAlt(_localctx, 7);
      setState(515);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->i = match(PggParser::IDENT);
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  gc->newIdent((antlrcpp::downCast<PrimaryContext *>(_localctx)->i != nullptr ? antlrcpp::downCast<PrimaryContext *>(_localctx)->i->getText() : ""), spanTok(antlrcpp::downCast<PrimaryContext *>(_localctx)->i)); 
      break;
    }

    case 8: {
      enterOuterAlt(_localctx, 8);
      setState(517);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->a = attr_ref();
       antlrcpp::downCast<PrimaryContext *>(_localctx)->result =  antlrcpp::downCast<PrimaryContext *>(_localctx)->a->result; 
      break;
    }

    case 9: {
      enterOuterAlt(_localctx, 9);
      setState(520);
      match(PggParser::LPAREN);
      setState(521);
      antlrcpp::downCast<PrimaryContext *>(_localctx)->e = aexpr();
      setState(522);
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
    setState(527);
    match(PggParser::AT);
    setState(528);
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

std::vector<PggParser::Vec_elemContext *> PggParser::Vec_literalContext::vec_elem() {
  return getRuleContexts<PggParser::Vec_elemContext>();
}

PggParser::Vec_elemContext* PggParser::Vec_literalContext::vec_elem(size_t i) {
  return getRuleContext<PggParser::Vec_elemContext>(i);
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
    setState(531);
    match(PggParser::LPAREN);
    setState(532);
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->vec_elemContext = vec_elem();
    antlrcpp::downCast<Vec_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->vec_elemContext);
    setState(535); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(533);
      match(PggParser::COMMA);
      setState(534);
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->vec_elemContext = vec_elem();
      antlrcpp::downCast<Vec_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<Vec_literalContext *>(_localctx)->vec_elemContext);
      setState(537); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == PggParser::COMMA);
    setState(539);
    match(PggParser::RPAREN);
     antlrcpp::downCast<Vec_literalContext *>(_localctx)->result =  gc->newVec(gc->resultsOf(antlrcpp::downCast<Vec_literalContext *>(_localctx)->e), spanOf(_localctx)); 
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- Vec_elemContext ------------------------------------------------------------------

PggParser::Vec_elemContext::Vec_elemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PggParser::Vec_elemContext::NUMBER() {
  return getToken(PggParser::NUMBER, 0);
}

tree::TerminalNode* PggParser::Vec_elemContext::MINUS() {
  return getToken(PggParser::MINUS, 0);
}


size_t PggParser::Vec_elemContext::getRuleIndex() const {
  return PggParser::RuleVec_elem;
}


PggParser::Vec_elemContext* PggParser::vec_elem() {
  Vec_elemContext *_localctx = _tracker.createInstance<Vec_elemContext>(_ctx, getState());
  enterRule(_localctx, 66, PggParser::RuleVec_elem);
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
    setState(543);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PggParser::MINUS) {
      setState(542);
      antlrcpp::downCast<Vec_elemContext *>(_localctx)->m = match(PggParser::MINUS);
    }
    setState(545);
    antlrcpp::downCast<Vec_elemContext *>(_localctx)->n = match(PggParser::NUMBER);
     antlrcpp::downCast<Vec_elemContext *>(_localctx)->result =  gc->newSignedNumber(antlrcpp::downCast<Vec_elemContext *>(_localctx)->m, antlrcpp::downCast<Vec_elemContext *>(_localctx)->n, spanOf(_localctx)); 
   
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
  enterRule(_localctx, 68, PggParser::RuleList_literal);
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
    setState(548);
    match(PggParser::LBRACKET);
    setState(560);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 44539951935488) != 0)) {
      setState(549);
      antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext = aexpr();
      antlrcpp::downCast<List_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext);
      setState(554);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(550);
          match(PggParser::COMMA);
          setState(551);
          antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext = aexpr();
          antlrcpp::downCast<List_literalContext *>(_localctx)->e.push_back(antlrcpp::downCast<List_literalContext *>(_localctx)->aexprContext); 
        }
        setState(556);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 44, _ctx);
      }
      setState(558);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PggParser::COMMA) {
        setState(557);
        match(PggParser::COMMA);
      }
    }
    setState(562);
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
  enterRule(_localctx, 70, PggParser::RuleLiteral);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(578);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PggParser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(565);
        antlrcpp::downCast<LiteralContext *>(_localctx)->n = match(PggParser::NUMBER);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNumber((antlrcpp::downCast<LiteralContext *>(_localctx)->n != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->n->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->n)); 
        break;
      }

      case PggParser::STRING: {
        enterOuterAlt(_localctx, 2);
        setState(567);
        antlrcpp::downCast<LiteralContext *>(_localctx)->s = match(PggParser::STRING);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newString((antlrcpp::downCast<LiteralContext *>(_localctx)->s != nullptr ? antlrcpp::downCast<LiteralContext *>(_localctx)->s->getText() : ""), spanTok(antlrcpp::downCast<LiteralContext *>(_localctx)->s)); 
        break;
      }

      case PggParser::TRUE:
      case PggParser::FALSE: {
        enterOuterAlt(_localctx, 3);
        setState(569);
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
        setState(571);
        antlrcpp::downCast<LiteralContext *>(_localctx)->v = vec_literal();
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  antlrcpp::downCast<LiteralContext *>(_localctx)->v->result; 
        break;
      }

      case PggParser::NONE: {
        enterOuterAlt(_localctx, 5);
        setState(574);
        match(PggParser::NONE);
         antlrcpp::downCast<LiteralContext *>(_localctx)->result =  gc->newNone(spanOf(_localctx)); 
        break;
      }

      case PggParser::IDENT: {
        enterOuterAlt(_localctx, 6);
        setState(576);
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
  size_t startState = 72;
  enterRecursionRule(_localctx, 72, PggParser::RuleType, precedence);

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
    setState(603);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 50, _ctx)) {
    case 1: {
      setState(581);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
      setState(582);
      match(PggParser::LT);
      setState(583);
      antlrcpp::downCast<TypeContext *>(_localctx)->a = type(0);
      setState(584);
      match(PggParser::GT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeGeneric((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b), antlrcpp::downCast<TypeContext *>(_localctx)->a->result, spanOf(_localctx)); 
      break;
    }

    case 2: {
      setState(587);
      antlrcpp::downCast<TypeContext *>(_localctx)->e = match(PggParser::IDENT);
      setState(588);
      match(PggParser::LBRACE);
      setState(597);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PggParser::IDENT) {
        setState(589);
        antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
        antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
        setState(594);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == PggParser::COMMA) {
          setState(590);
          match(PggParser::COMMA);
          setState(591);
          antlrcpp::downCast<TypeContext *>(_localctx)->identToken = match(PggParser::IDENT);
          antlrcpp::downCast<TypeContext *>(_localctx)->v.push_back(antlrcpp::downCast<TypeContext *>(_localctx)->identToken);
          setState(596);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
      }
      setState(599);
      match(PggParser::RBRACE);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeEnum(antlrcpp::downCast<TypeContext *>(_localctx)->e, gc->namesOf(antlrcpp::downCast<TypeContext *>(_localctx)->v), spanOf(_localctx)); 
      break;
    }

    case 3: {
      setState(601);
      antlrcpp::downCast<TypeContext *>(_localctx)->b = match(PggParser::IDENT);
       antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->newTypeName((antlrcpp::downCast<TypeContext *>(_localctx)->b != nullptr ? antlrcpp::downCast<TypeContext *>(_localctx)->b->getText() : ""), spanTok(antlrcpp::downCast<TypeContext *>(_localctx)->b)); 
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(614);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(612);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 51, _ctx)) {
        case 1: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(605);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(606);
          match(PggParser::QUESTION);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeOptional(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        case 2: {
          _localctx = _tracker.createInstance<TypeContext>(parentContext, parentState);
          _localctx->t = previousContext;
          pushNewRecursionContext(_localctx, startState, RuleType);
          setState(608);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(609);
          match(PggParser::LBRACKET);
          setState(610);
          match(PggParser::RBRACKET);
           antlrcpp::downCast<TypeContext *>(_localctx)->result =  gc->typeList(antlrcpp::downCast<TypeContext *>(_localctx)->t->result, spanOf(_localctx)); 
          break;
        }

        default:
          break;
        } 
      }
      setState(616);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx);
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
    case 36: return typeSempred(antlrcpp::downCast<TypeContext *>(context), predicateIndex);

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
