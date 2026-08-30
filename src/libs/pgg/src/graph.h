#pragma once

// Node projection of a .pgg file (stage E8, spec §10): the graph is derived
// from names in the author's AST — a name is a node, a use of a name is a
// wire; a def call is a collapsed node that dives into the def's body along
// its instance path (§7.7); repeat/foreach zones are subgraphs with
// iteration-input/iteration-output ports and a state loop wire (§5.4). There
// is no separate storage format: the projection is rebuilt from text on every
// load, and layout hints live in trailing `# @pos X Y` comments (§10).
//
// Instance numbering matches the expansion bit-for-bit: the k-th call of def
// `foo` in expansion order (source order, outside-in — see eval/expand.h) is
// `foo[k]`, and tests cross-check the derived paths against
// FlatProgram::instances. Because instance counters are global to the whole
// expansion (a def called inside another instance continues the same counter
// — tower.pgg numbers the make_rock_sdf call inside make_rock[0] as
// make_rock_sdf[1]), one global walk derives the top-level scope and every
// instance body scope in a single pass; a standalone per-def builder could
// not reproduce those labels.
//
// Pure AST + Document::comments: the evaluator, caches and thread pools play
// no role here.

#include <string>
#include <vector>

#include "ast.h"

namespace pgg {

struct Document;       // pgg/pgg.h
struct ModuleClosure;  // eval/modules.h

struct GraphNode {
    enum class Kind {
        Binding,     // x = <expr>; op = outer call name or "expr"
        DefCall,     // collapsed def call, diveable; op = (qualified) def name
        Param,       // param chip (source)
        Output,      // output chip (sink)
        Import,      // import chip (namespace marker)
        Tap,         // tap chip (debug mark)
        ZoneHeader,  // repeat/foreach node of the parent scope
        ZoneInput,   // synthetic iteration-input port (inside a zone frame)
        ZoneOutput,  // synthetic iteration-output port (inside a zone frame)
    };

    int id = -1;
    Kind kind = Kind::Binding;
    std::string op;                    // call name / "expr" / "repeat" / "param" / ...
    std::string name;                  // display name (chips, ports; bindings use outputs)
    std::vector<std::string> outputs;  // out pins = bound names (targets)
    std::vector<std::string> inputs;   // in pins: arg names in discovery order ("" = positional/expr)
    Span span;
    int zone = -1;  // owning zone frame id (-1 = scope root)

    // Layout hint from a trailing `# @pos X Y` comment of the binding's line;
    // the final position/layer are filled by layoutScope (layout.h).
    bool hasHint = false;
    float hintX = 0.0f;
    float hintY = 0.0f;
    float x = 0.0f;  // center, scope coordinates
    float y = 0.0f;
    int layer = 0;

    // DefCall only: the dive target.
    const Def* def = nullptr;
    std::string instanceName;  // make_rock[1]
    std::string instancePath;  // cliff_wall[0].make_rock[1]
};

struct GraphEdge {
    int fromNode = -1;
    int fromPin = 0;  // index into GraphNode::outputs
    int toNode = -1;
    int toPin = 0;    // index into GraphNode::inputs
    bool loop = false;  // zone state feedback (drawn as a back edge)
};

struct GraphZone {
    int id = -1;
    int header = -1;  // ZoneHeader node of the parent scope
    int parent = -1;  // enclosing zone id (-1 = scope root)
    std::vector<int> inputPorts;   // ZoneInput node ids (iteration inputs)
    std::vector<int> outputPorts;  // ZoneOutput node ids (iteration outputs)
    std::vector<int> members;      // every node inside the frame (ports included)
    std::vector<int> children;     // nested zone ids
    // Frame rectangle in scope coordinates, filled by layoutScope.
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct GraphScope {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::vector<GraphZone> zones;
    // Source file of this scope: empty for the main file (its top level and
    // the bodies of its own defs), the module's canonical path for imported
    // def bodies — hint write-back applies to the main file only.
    std::string originFile;
};

struct GraphProject {
    GraphScope top;
    // One body scope per def instance, in expansion order — parallel to
    // FlatProgram::instances (the tests cross-check the paths element-wise).
    std::vector<std::string> instancePaths;
    std::vector<GraphScope> instanceScopes;

    // "" resolves to the top-level scope; otherwise the body scope of the
    // given instance path (nullptr when unknown).
    const GraphScope* scopeOf(const std::string& instancePath) const;
    GraphScope* scopeOf(const std::string& instancePath);
};

// Derives the projection of the whole file: the top-level scope plus one body
// scope per def instance (the dive targets). `closure` (optional) resolves
// qualified calls of imported defs; without it a qualified call is an ordinary
// op node (the same files the expansion rejects with E505). Layout hints are
// read from Document::comments (a trailing `# @pos X Y` on the binding's
// line, parsed by layout.h). Broken files still project — unresolved reads
// simply produce no wire (the diagnostics panel reports the errors).
GraphProject buildGraph(const Document& doc, const ModuleClosure* closure = nullptr);

}  // namespace pgg
