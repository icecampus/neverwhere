#pragma once

// PGG public execution API (stages E1–E5): run a .pgg graph to its declared
// outputs. Parsing (pgg/pgg.h) and E0 checks happen inside; imports resolve
// against RunParams::importRoots (runFile adds the file's own directory),
// def calls are statically expanded, and the result carries outputs,
// diagnostics (E0 + static + runtime) and evaluation stats (incl. cache
// counters, resolved thread count and the numeric profile id).

#include <string>
#include <vector>

#include "../../src/eval/engine.h"

namespace pgg {

// Parses and runs source text. `outputs` selects a subset of the declared
// outputs (empty = all). fileName is only used in diagnostics.
RunResult run(const std::string& text, const RunParams& params = {},
              const std::vector<std::string>& outputs = {},
              const std::string& fileName = "<run>");

// Reads, parses and runs a file.
RunResult runFile(const std::string& path, const RunParams& params = {},
                  const std::vector<std::string>& outputs = {});

}  // namespace pgg
