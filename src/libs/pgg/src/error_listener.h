#pragma once

// Maps ANTLR syntax errors to structured PGG diagnostics (spec §11). Code
// assignment: E101 for missing tokens (typically an unclosed brace/paren),
// E100 otherwise. Default ANTLR messages never reach the user unwrapped —
// they are normalized here (spec §13.1 item 3).

#include <antlr4-runtime.h>

#include "ast.h"

namespace pgg {

class PggErrorListener final : public antlr4::BaseErrorListener {
public:
    std::vector<Diagnostic> diagnostics;

    void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offendingSymbol,
                     size_t line, size_t charPositionInLine, const std::string& msg,
                     std::exception_ptr e) override;

private:
    static std::string normalize(const std::string& msg);
};

}  // namespace pgg
