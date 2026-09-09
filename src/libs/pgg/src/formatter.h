#pragma once

// Canonical formatter (spec §6.4): 4-space indent, K&R braces, ` = ` with
// spaces, blank lines between top-level items preserved (capped at one),
// comments reattached (own-line before their statement, trailing after code).
// Keyword arguments are reordered to the declared parameter order when the
// callee is a def in the same file and every argument is named.

#include "ast.h"

namespace pgg {

std::string format(const File* file, const std::vector<Comment>& comments);

// Canonical one-line text of a single expression (`value(@wid, on = p1)`),
// in source argument order (no def-parameter reordering). Used by the
// diagnostic inline chains (§9.5) to name the origin expression of a value.
std::string formatExpr(const Expr* e);

}  // namespace pgg
