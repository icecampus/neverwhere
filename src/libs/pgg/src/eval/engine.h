#pragma once

// Pull-based lazy driver (spec §5.1, §6.6-6.7): binds launch params, then
// evaluates only the bindings on the path to the requested outputs (N2).
// Static checks (typecheck) run first; the graph executes only when clean.

#include <unordered_map>

#include "../ast.h"
#include "field.h"
#include "value.h"

namespace pgg {

struct Document;  // pgg/pgg.h

struct RunParams {
    std::vector<std::pair<std::string, Value>> values;  // param name -> bound value
};

struct RunOutput {
    std::string name;
    Value value;
};

struct RunStats {
    uint64_t fieldsEvaluated = 0;  // field-node evaluations (memoization misses) this run
    // Per evaluated field-binding: how often its root field was computed.
    // The memoization rule (§4.4) pins this at 1 no matter the consumer count;
    // an unused binding is absent (== 0 via map default).
    std::unordered_map<std::string, uint64_t> bindingFieldEvals;
};

struct RunResult {
    std::vector<RunOutput> outputs;
    std::vector<Diagnostic> diagnostics;  // E0 findings + static + runtime, in that order
    RunStats stats;
    bool hasErrors() const;
};

// Runs a parsed document: static typecheck, then lazy pull from the requested
// outputs (empty = all declared outputs).
RunResult run(const Document& doc, const RunParams& params, const std::vector<std::string>& outputs);

}  // namespace pgg
