#pragma once

// Def doc-card lookup (spec §7.5), shared by `PggTool docs` and the viewer RPC
// `docs` command: a def's signature as written plus its docstring. Unqualified
// symbols resolve in the main file, qualified `ns.name` through the file's
// import closure (same roots as a run: importRoots + the file's own
// directory). The result carries STRINGS, not a Def*: a qualified lookup's
// Def lives in a temporary import closure and would dangle.

#include <string>
#include <vector>

#include "../ast.h"

namespace pgg {

struct DocsLookupResult {
    bool found = false;
    std::string signature;  // signatureText of the def (when found)
    bool hasDoc = false;
    std::string docstring;                  // dedented (when hasDoc)
    std::vector<Diagnostic> diagnostics;    // import-closure findings (qualified lookup only)
    std::string error;                      // human-readable reason when !found
};

// Resolves `symbol` (bare or `ns.name`) against the main file. `filePath` is
// used for messages and as the implicit import root's source (its directory).
DocsLookupResult findDef(const File& mainFile, const std::string& filePath,
                         const std::string& symbol, const std::vector<std::string>& importRoots);

// "def name(param: type = default, ...) -> (out: type, ...)" as written.
std::string signatureText(const Def* d);

// Docstrings are written as one indented block; this dedents line-by-line.
std::string dedentDocstring(const std::string& docstring);

}  // namespace pgg
