#pragma once

// Full static expansion of def calls (stage E5, spec §7.6): recursion is
// forbidden, so def calls are inlined into a flat graph before typecheck,
// fingerprinting and the engine. One mechanism buys call semantics (argument
// binding, defaults, multi-outputs, nested defs), lazy field closures (§7.2:
// a field argument becomes an ordinary local binding of the instance),
// static schema inference through def boundaries (fields are checked in
// their consumption context), per-instance cache invalidation (flat binding
// fingerprints are structural) and instance paths (§7.7).
//
// Instance naming: the k-th call of def `foo` in expansion order (source
// order, outside-in) becomes instance `foo[k]`; locals are renamed to
// `foo[k].local`, parameters become bindings `foo[k].param = <caller arg
// expression>` (argument expressions are NOT renamed — they live in the
// caller's context, already renamed by then). Expression-internal def calls
// are lifted into generated `_lift<N>` bindings (DFS pre-order).
//
// Pass-through: a file without def/import items is returned unexpanded (the
// original File pointer, no rewriting) so flat graphs flow through the
// extended pipeline bit-for-bit as before.

#include <string>
#include <unordered_map>
#include <vector>

#include "../ast.h"
#include "modules.h"
#include "value.h"

namespace pgg {

struct FlatContract {
    bool isEnsure = false;
    bool attrForm = false;
    std::string ident;          // attr form: flat binding name
    std::string attrName;       // attr form, without '@'
    const Expr* cond = nullptr; // condition form (renamed, flat arena)
    std::string message;        // author text ("подсказка для нейронки", §7.4)
    bool hasMessage = false;
    Span span;
    size_t instance = 0;  // index into FlatProgram::instances
};

struct FlatInstance {
    std::string name;     // make_rock[1]
    std::string path;     // full chain: cliff_wall[0].make_rock[1]
    std::string defName;  // make_rock
    std::vector<std::string> outputs;  // flat names of the output bindings
};

// E6 (spec §9.3/§9.4): a tap inside a def body, recorded per instantiation
// (50 rocks -> 50 concrete taps). The path root is already renamed to the
// instance's flat local (`make_rock[1].raw`); top-level taps are not here —
// they are read from the flat file items as-is. Fires only in debug mode.
struct FlatTap {
    std::string label;    // inspector name when hasLabel (`tap stats: ...`)
    bool hasLabel = false;
    std::string path;     // concrete flat path string (PathElems, root renamed)
    size_t instance = 0;  // index into FlatProgram::instances
};

struct FlatProgram {
    std::vector<std::unique_ptr<Node>> arena;  // nodes created by expansion
    const File* file = nullptr;                // original file when !expanded
    bool expanded = false;
    // Declared interface types (def params/outputs) per flat binding — E204 input.
    std::unordered_map<std::string, Type> declaredTypes;
    std::unordered_map<std::string, size_t> instanceOfBinding;
    std::vector<FlatInstance> instances;
    std::vector<FlatContract> contracts;
    std::vector<FlatTap> taps;  // def-body taps, expansion order
};

// Expands the main file into a flat program. Reports E503 (def recursion) and
// E401 (stochastic def without an rng parameter, transitive over the def DAG)
// for the whole def table, E505 (unknown qualified symbol) and the call
// binding errors E202/E203/E204 during expansion.
FlatProgram expandProgram(const File& mainFile, const ModuleClosure* closure,
                          std::vector<Diagnostic>& diags);

}  // namespace pgg
