#pragma once

// Structural fingerprints for the content-addressed cache (spec §5.3, N3/N4).
//
// fp(binding) = mix(profileId, expression AST structure, fp(dependencies by
// name)) — computed in O(AST) without evaluating any values. The expression
// walk covers operations, constants, keyword arguments in written order and
// attribute references; identifiers resolve to the fingerprint of the
// referenced top-level binding, recursively with a per-name memo (the graph
// is a DAG after the static checks). Launch params fingerprint by their bound
// value (or by the default literal when unbound). rng identities need no
// separate treatment: they derive from the same structure (seed literals /
// launch params and split keys are all covered by the AST walk).
//
// Every fingerprint chains from the numeric profile id as the seed, so a
// profile change invalidates the whole cache. Mixing is platform-independent
// integer hashing (splitmix64/fnv1a class, same family as rng.cpp).
//
// A fingerprint of 0 means "uncacheable — bypass the cache" (unknown or
// non-evaluable names, payloads without a structural hash). Real fingerprints
// are remixed to never collide with that sentinel; bypassing is always the
// safe direction.

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "../ast.h"
#include "value.h"

namespace pgg {

// Hashes a Value payload into a platform-independent 64-bit fingerprint
// (floats by bit pattern, geometry by content). Returns false for payloads
// without a structural hash (compiled fields): the enclosing binding must be
// treated as uncacheable.
bool fingerprintValue(const Value& v, uint64_t& out);

class BindingFingerprinter {
public:
    // boundParams: launch param bindings (name -> value), same as RunParams::values.
    BindingFingerprinter(const File& file,
                         const std::vector<std::pair<std::string, Value>>& boundParams,
                         uint64_t profileId);

    // Structural fingerprint of a top-level binding (0 = uncacheable/unknown).
    uint64_t fingerprint(const std::string& name);

private:
    uint64_t fingerprintNode(const std::string& name, const Node* node);
    uint64_t fingerprintExpr(const Expr* e, bool& ok);

    std::vector<std::pair<std::string, Value>> boundParams_;  // copied (tiny; outlives temporaries)
    uint64_t profileId_ = 0;
    std::unordered_map<std::string, const Node*> topLevel_;
    std::unordered_map<std::string, uint64_t> memo_;
    std::unordered_set<std::string> inProgress_;  // cycle guard (DAG after static checks)
};

}  // namespace pgg
