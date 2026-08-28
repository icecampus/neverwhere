#pragma once

// Validator-lite (stage E0 scope): SSA reassignment (E102, with the zone
// state-port exception of §5.4), forward references (E103, define-before-use)
// and the unused-name lint (§6.5), plus unknown type names deferred from the
// compositor. Type/RNG/contract checks are later stages (E1/E5).

#include "ast.h"

namespace pgg {

// Appends diagnostics in source order; callers usually sort afterwards.
void validate(const File* file, std::vector<Diagnostic>& diagnostics);

}  // namespace pgg
