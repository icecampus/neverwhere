#pragma once

// Canonical formatter (spec §6.4): 4-space indent, K&R braces, ` = ` with
// spaces, blank lines between top-level items preserved (capped at one),
// comments reattached (own-line before their statement, trailing after code).
// Keyword arguments are reordered to the declared parameter order when the
// callee is a def in the same file and every argument is named.

#include "ast.h"

namespace pgg {

std::string format(const File* file, const std::vector<Comment>& comments);

}  // namespace pgg
