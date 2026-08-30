#pragma once

// Pull-based lazy driver (spec §5.1, §6.6-6.7): binds launch params, then
// evaluates only the bindings on the path to the requested outputs (N2).
// Static checks (expansion + typecheck) run first; the graph executes only
// when clean.
// E3: RunParams selects the lane count for per-element loops (N7) and carries
// the caller-owned cross-run cache (N3/N4); RunStats reports cache counters,
// the resolved thread count and the numeric profile id (§5.2).
// E5: RunParams carries the import roots (§7.6) and the engine checks the
// runtime half of the def contracts (E303/E304) at instance-output pulls.

#include <unordered_map>

#include "../ast.h"
#include "field.h"
#include "value.h"

namespace pgg {

struct Document;  // pgg/pgg.h
class MemoryCache;  // eval/cache.h

struct RunParams {
    std::vector<std::pair<std::string, Value>> values;  // param name -> bound value
    // Per-element loop parallelism: 0 = hardware concurrency, 1 = sequential.
    // Results are bit-identical at any lane count within one numeric profile.
    unsigned threads = 0;
    // Caller-owned cross-run cache (nullptr = disabled). Value bindings only;
    // inspector/debug sessions use their own instance by design (spec §5.3).
    MemoryCache* cache = nullptr;
    // Import roots (spec §7.6), searched in order for `<root>/<path>.pgg`.
    // runFile appends the importing file's own directory implicitly.
    std::vector<std::string> importRoots;
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
    // Cross-run cache (MemoryCache); both stay 0 when no cache is attached.
    uint64_t cacheHits = 0;
    uint64_t cacheMisses = 0;
    unsigned threadsUsed = 1;    // resolved lane count (RunParams::threads 0 -> hardware)
    uint64_t profileId = 0;      // numeric profile of this run (spec §5.2)
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
