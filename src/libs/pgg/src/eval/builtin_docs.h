#pragma once

// Builtin reference docs (spec §8 / §6.3), surfaced by
// `PggTool docs builtin <name>` / `docs builtins [--group ...]` and the viewer
// RPC docs command (`symbol = "builtin:<name>"`): a one-paragraph summary and
// one example per builtin; the signature is rendered from the live registry
// (builtins.h), so it cannot drift from the typechecker.
//
// The table must cover every registry entry — the completeness test fails
// otherwise. Groups follow the registry's section order.

#include <string>
#include <vector>

#include "builtins.h"

namespace pgg {

struct BuiltinDoc {
    std::string name;
    std::string group;    // sources|transforms|rng|fields|expr|groups|attributes|
                          // topology|scatter|aggregators|sdf|fracture|deferred
    std::string summary;  // one paragraph, condensed from spec §8 / §6.3
    std::string example;  // one-line usage
};

// nullptr when the name is not a documented builtin.
const BuiltinDoc* findBuiltinDoc(const std::string& name);

// Every documented builtin, in registry order.
const std::vector<BuiltinDoc>& allBuiltinDocs();

// "clip(geo: geo, origin: vec3, normal: vec3, cap_group: string = "") -> geo"
// rendered from the registry signature (defaults, enums, field<T>, geo<K>).
std::string builtinSignatureText(const BuiltinSig& sig);

}  // namespace pgg
