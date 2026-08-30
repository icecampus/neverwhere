#pragma once

// Import closure (stage E5, spec §7.6): resolves the `import` directives of
// the main file against the configured roots, parses every module once
// (cache by canonical path), reports missing modules (E501), import cycles
// (E502), unknown namespace bindings vs built-ins (E102), library defs
// without a docstring (E506) and the not-yet-implemented version pinning.
//
// Only top-level defs of a module are visible through its namespace;
// params/bindings/outputs belong to the executable file and are never
// exported (spec §7.6). Modules are not executed, so E604/E605 do not apply
// to them.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../ast.h"
#include "pgg/pgg.h"

namespace pgg {

struct ModuleInfo {
    std::string canonicalPath;  // cache key
    std::string displayPath;    // as resolved, prefixes its diagnostics
    const Document* doc = nullptr;  // owned by ModuleClosure::owned
    // All that is visible through the namespace: top-level defs by name.
    std::unordered_map<std::string, const Def*> defs;
    // Namespace bindings of this module's own imports (nested resolution).
    std::unordered_map<std::string, const ModuleInfo*> namespaces;
};

struct ModuleClosure {
    std::vector<std::unique_ptr<Document>> owned;
    std::vector<std::unique_ptr<ModuleInfo>> ownedModules;  // pointers elsewhere point here
    // Namespaces as bound by the main file's imports.
    std::unordered_map<std::string, const ModuleInfo*> mainNamespaces;
    // Every loaded module (main file's closure), for def-graph checks.
    std::vector<const ModuleInfo*> modules;
};

// True when the file has any import directive (fast path: no closure work).
bool hasImports(const File& file);

// Loads the transitive import closure of mainFile. `import lib.noise` resolves
// to `<root>/lib/noise.pgg`, roots searched in order. A module's own parse /
// lint findings are copied into diags with the module path prefixed to the
// message (spans are left pointing into the module source).
ModuleClosure loadModuleClosure(const File& mainFile, const std::vector<std::string>& roots,
                                std::vector<Diagnostic>& diags);

}  // namespace pgg
