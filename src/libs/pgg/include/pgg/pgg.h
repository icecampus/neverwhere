#pragma once

// PGG public API (stage E0): parse source text into AST + diagnostics.
// Spec: docs/pgg/geometry_generation_language.md

#include <memory>
#include <string>
#include <vector>

#include "../../src/ast.h"

namespace pgg {

struct Document {
    File* file = nullptr;  // owned by arena
    std::vector<std::unique_ptr<Node>> arena;
    std::vector<Diagnostic> diagnostics;  // sorted by (line, col)
    std::vector<Comment> comments;        // `#` comments, source order

    bool hasErrors() const;
};

// Parses source text (fileName is only used in diagnostics). Always returns a
// document; on syntax errors the AST may be partial and diagnostics non-empty.
Document parse(const std::string& text, const std::string& fileName = "<stdin>");

// Reads and parses a file. On IO failure returns a document with a single
// E100 diagnostic and null AST.
Document parseFile(const std::string& path);

// Canonical format (spec §6.4). Comments come from Document::comments.
std::string format(const File* file, const std::vector<Comment>& comments);

// Spec §11.1 text form: "E100 file.pgg:14: message" + optional hint line.
std::string formatDiagnostic(const Diagnostic& d, const std::string& fileName);

}  // namespace pgg
