#pragma once

// Static type pass over the flat program (spec §11.2, N5): E201-E206
// call/type errors, E604 (unbound param), E605 (no output), the §6.7
// output-type rule. Runs before any execution: every error it can find is
// found without evaluating the graph.
//
// Stage E5 merges the static schema inference (§7.6) into this same
// topological pass: geo bindings get a proven attribute/group shape where
// the transfer table allows, field-consuming nodes check their @attr/ingroup
// reads against it (static E302/E305 — open schemas fall back to runtime),
// def-interface types are checked on the parameter/output bindings (E204),
// and the contracts are split into statically decided (E303/E304 now) and
// runtime-checked (reported back through runtimeContracts).

#include "../ast.h"
#include "expand.h"

namespace pgg {

// boundParams: names of params the launcher binds this run (E604 input).
// runtimeContracts: output — indices into flat.contracts that could not be
// decided statically (open schema / non-constant condition); the engine
// checks them at run time.
void typecheckFlat(const FlatProgram& flat, const std::vector<std::string>& boundParams,
                   std::vector<Diagnostic>& diagnostics, std::vector<size_t>& runtimeContracts);

}  // namespace pgg
