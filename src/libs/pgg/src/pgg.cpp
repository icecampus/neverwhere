#include "pch.h"

#include "pgg/pgg.h"

#include <fstream>
#include <sstream>

#include "compositor.h"
#include "error_listener.h"
#include "formatter.h"
#include "parser_gen/PggLexer.h"
#include "parser_gen/PggParser.h"
#include "validate.h"

namespace pgg {

bool Document::hasErrors() const {
    for (const Diagnostic& d : diagnostics)
        if (!d.isWarning) return true;
    return false;
}

Document parse(const std::string& text, const std::string& fileName) {
    (void)fileName;  // spans carry positions; the name is attached at print time

    antlr4::ANTLRInputStream input(text);
    PggLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    PggParser parser(&tokens);

    PggErrorListener listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);

    GrammarCompositor gc;
    parser.gc = &gc;
    File* file = parser.file()->result;

    Document doc;
    doc.file = file;
    doc.arena = gc.takeArena();
    doc.diagnostics = std::move(listener.diagnostics);
    doc.diagnostics.insert(doc.diagnostics.end(), gc.diagnostics().begin(),
                           gc.diagnostics().end());
    validate(file, doc.diagnostics);
    std::stable_sort(doc.diagnostics.begin(), doc.diagnostics.end(),
                     [](const Diagnostic& a, const Diagnostic& b) {
                         if (a.span.line != b.span.line) return a.span.line < b.span.line;
                         return a.span.col < b.span.col;
                     });

    // Recover `#` comments from the hidden channel for the formatter.
    tokens.fill();
    for (antlr4::Token* t : tokens.getTokens()) {
        if (t->getType() != PggLexer::COMMENT) continue;
        std::string text = t->getText().substr(1);  // strip '#'
        const size_t first = text.find_first_not_of(" \t");
        const size_t last = text.find_last_not_of(" \t");
        text = first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        Comment c;
        c.text = std::move(text);
        c.span.line = static_cast<int32_t>(t->getLine());
        c.span.col = static_cast<int32_t>(t->getCharPositionInLine());
        c.span.endLine = c.span.line;
        c.span.endCol = c.span.col + static_cast<int32_t>(t->getText().size());
        doc.comments.push_back(std::move(c));
    }
    return doc;
}

Document parseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        Document doc;
        doc.diagnostics.push_back(
            Diagnostic{"E100", Span{}, "cannot open file '" + path + "'", {}, false});
        return doc;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), path);
}

std::string formatDiagnostic(const Diagnostic& d, const std::string& fileName) {
    std::string out = d.code + " " + fileName + ":" + std::to_string(d.span.line) + ": " +
                      (d.isWarning ? "warning: " : "") + d.message;
    if (!d.hint.empty()) out += "\n  hint: " + d.hint;
    return out;
}

}  // namespace pgg
