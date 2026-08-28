#include "pch.h"

#include "error_listener.h"

namespace pgg {

std::string PggErrorListener::normalize(const std::string& msg) {
    // "token recognition error at: 'X'" -> "unexpected character 'X'"
    const std::string tokenErr = "token recognition error at: '";
    if (msg.rfind(tokenErr, 0) == 0 && msg.size() > tokenErr.size()) {
        return "unexpected character " + msg.substr(tokenErr.size() - 1);
    }
    return msg;
}

void PggErrorListener::syntaxError(antlr4::Recognizer* /*recognizer*/,
                                   antlr4::Token* offendingSymbol, size_t line,
                                   size_t charPositionInLine, const std::string& msg,
                                   std::exception_ptr /*e*/) {
    Diagnostic d;
    d.code = "E100";
    d.span.line = static_cast<int32_t>(line);
    d.span.col = static_cast<int32_t>(charPositionInLine);
    d.span.endLine = d.span.line;
    d.span.endCol =
        d.span.col + (offendingSymbol ? static_cast<int32_t>(offendingSymbol->getText().size()) : 1);
    d.message = normalize(msg);
    if (msg.rfind("missing", 0) == 0) {
        d.code = "E101";
        d.hint = "check for an unclosed brace or paren above this line";
    }
    diagnostics.push_back(std::move(d));
}

}  // namespace pgg
