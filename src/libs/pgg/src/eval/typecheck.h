#pragma once

// Static type pass over the AST (spec §11.2, N5): E201-E206 call/type
// errors, E604 (unbound param), E605 (no output), the §6.7 output-type rule.
// Runs before any execution: every error it can find is found without
// evaluating the graph. At E2 named user attributes get a provisional f32
// type — attribute schemas are runtime data (static schema inference is E5).

#include "../ast.h"

namespace pgg {

// boundParams: names of params the launcher binds this run (E604 input).
void typecheck(const File* file, const std::vector<std::string>& boundParams,
               std::vector<Diagnostic>& diagnostics);

}  // namespace pgg
