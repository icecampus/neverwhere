#pragma once

// Built-in node catalog (spec §8): signature registry and dispatch for the
// execution core.
//
// Every supported operation has a BuiltinSig: parameter names, types
// (value vs field vs either), defaults, result type. The same registry drives
// the static type checker (E2xx), the expression compiler and the runtime
// dispatch — one source of truth for signatures.
//
// Operations deferred past the current stage (import_mesh, §8.3 topology
// beyond merge, raycast/transfer, zones, ...) are registered with a
// deferredStage note so calling them produces a precise "not supported at
// this stage" diagnostic instead of E201.

#include "field.h"
#include "value.h"

namespace pgg {

enum class BuiltinId {
    None,
    // §8.1 sources
    IcoSphere, Box, Grid, MeshLine, PointCloud,
    // §8.2 transforms
    Transform, SetPosition, Smooth, ComputeNormals,
    // §8.5 rng
    RngFromSeed, SplitRng, AliasRng,
    // §8.5 field generators
    Fbm, Vnoise, Random, RandomVec, RandomInt, DistanceTo, Position, Normal, Index,
    // §6.3 expression functions (field-polymorphic)
    Dot, Cross, Length, Normalize, Clamp, Smoothstep, Mix, Abs, Min, Max, Floor, Pow,
    Vec2, Vec3, Vec4, CastInt, CastF32, CastBool, OrientFromEuler, Ramp,
    // §8.6 groups
    Mark, Unmark, Ingroup,
    // §8.7 attributes
    SetAttr, RemoveAttr, RenameAttr, Promote,
    // §8.3 topology (E2 subset + delete + clip)
    Merge, Delete, Clip,
    // §8.8 scatter and instancing
    DistributePoints, InstanceOnPoints, Realize,
    // §8.10 aggregators
    Bbox, Extent, Centroid, Count, MinOf, MaxOf, AvgOf, SumOf, ValueOf,
    // §8.4 SDF
    SdfSphere, SdfBox, SdfUnion, SdfUnionSmooth, SdfSubtract, SdfSubtractSmooth,
    SdfIntersect, SdfDisplace, SdfInstanceOnPoints, SdfFromMesh, MeshFromSdf, SdfGrind,
    // §8.3 islands / §8.11 fracture (E7)
    Islands, Fracture,
    // Known but not supported at this stage.
    Deferred,
};

enum class ParamKind {
    Value,   // concrete value argument (field argument -> E205)
    Field,   // declared field<T>: a value argument becomes a constant field
    Either,  // expression-function argument: value or field (result follows)
};

struct ParamSig {
    std::string name;
    ScalarType base = ScalarType::None;  // Any = any numeric scalar or vector
    ParamKind kind = ParamKind::Value;
    bool optional = false;      // T? (accepts none)
    bool required = false;
    bool allowString = false;   // base Any also accepts a string (split_rng key)
    GeoKind geoKind = GeoKind::Any;
    bool isList = false;        // T[] parameter (value-level list)
    Value defValue;
    bool hasDefValue = false;
    bool defPosition = false;   // default = position()
    bool defIndex = false;      // default = index()
    std::vector<std::string> enumValues;  // non-empty -> enum parameter
};

struct BuiltinSig {
    BuiltinId id = BuiltinId::None;
    const char* name = "";
    std::vector<ParamSig> params;
    Type result;
    std::vector<Type> results;               // non-empty -> multi-output node (destructured)
    bool variadic = false;                   // trailing positional args: ramp (values), merge (geos)
    bool resultGeoKindOfFirstArg = false;    // geo<K> of the first geo argument is kept
    bool exprFunc = false;                   // §6.3 expression function (field-polymorphic)
    const char* deferredStage = nullptr;     // non-null -> known, but not supported at this stage
};

const BuiltinSig* findBuiltin(const std::string& name);

// §8.1 geometry sources as plain functions (testable without the engine;
// the Value-level dispatch wraps them). All write @P; surface sources
// (ico_sphere, box, grid) also write @N.
GeoPtr genIcoSphere(int subdiv, float radius);
GeoPtr genBox(glm::vec3 size, int res);
GeoPtr genGrid(glm::vec2 size, int res);
GeoPtr genMeshLine(int count, float length, glm::vec3 dir);
GeoPtr genPointCloud(int count, glm::vec3 bounds, Rng rng);

// One call with arguments bound to the signature (produced by the compiler
// after bindCallArgs; diagnostics come from the static typecheck pass).
struct BoundCall {
    const BuiltinSig* sig = nullptr;
    std::vector<Value> values;             // per param (value args / defaults)
    std::vector<const FieldNode*> fields;  // per param (nullptr when not a field arg)
    std::vector<bool> present;             // per param: argument explicitly supplied
    Span span;
};

// Positional-then-keyword argument binding with defaults; emits E202/E203.
// Returns per-param source args (nullptr = use the declared default) and, for
// variadic signatures (ramp), the trailing positional args.
bool bindCallArgs(const BuiltinSig& sig, const Call& call,
                  std::vector<const CallArg*>& outByParam,
                  std::vector<const CallArg*>& outVariadic,
                  std::vector<Diagnostic>& diags);

// Value-level dispatch (sources, transforms, rng ops, const-folded expr funcs).
Value evalBuiltinCall(const BoundCall& bound, RunContext& run);

// Field-level dispatch used by the evaluator for Call nodes: §6.3 expression
// functions and §8.5 field generators.
ConstBufferPtr evalFieldCall(int callId, const FieldNode& node,
                             const std::vector<ConstBufferPtr>& args, EvalContext& ctx);

// §6.3 expression functions on evaluated argument buffers (geo-free, so the
// same code constant-folds value calls); params are the variadic values.
// Element loops are chunked over the pool (disjoint writes, N7).
ConstBufferPtr evalExprFuncBuf(int callId, const std::vector<ConstBufferPtr>& args,
                               const std::vector<Value>& params, size_t count, unsigned threads);

// §8.5 field generators (need the evaluation context: positions, geo, domain).
ConstBufferPtr evalFieldGenBuf(int callId, const FieldNode& node,
                               const std::vector<ConstBufferPtr>& args, EvalContext& ctx);

// Result type of an expression function given its argument types (§6.3);
// isField follows the arguments (field-polymorphic). Emits E204.
Type inferExprFuncType(BuiltinId id, const std::vector<Type>& argTypes, Span span,
                       std::vector<Diagnostic>& diags);

// §8.2 transform nodes, value level (builtins_transform.cpp).
Value evalTransformBuiltin(const BoundCall& bound, RunContext& run);

// §8.6 group and §8.7 attribute nodes, value level (builtins_attrs.cpp).
Value evalAttrBuiltin(const BoundCall& bound, RunContext& run);

// §8.3 merge and §8.8 scatter/instance nodes, value level (builtins_scatter.cpp).
Value evalScatterBuiltin(const BoundCall& bound, RunContext& run);

// §8.10 aggregators, value level (builtins_aggregate.cpp).
Value evalAggregateBuiltin(const BoundCall& bound, RunContext& run);

// §8.4 SDF nodes, value level (builtins_sdf.cpp).
Value evalSdfBuiltin(const BoundCall& bound, RunContext& run);

// §8.3 islands and §8.11 fracture nodes, value level (builtins_fracture.cpp, E7).
Value evalFractureBuiltin(const BoundCall& bound, RunContext& run);

// §8.3 delete (element removal with cascade), value level (builtins_topology.cpp).
Value evalTopologyBuiltin(const BoundCall& bound, RunContext& run);

// Materializes geo<instances> into geo<mesh> (spec §8.8); host entry point
// for tools that export instances without running the graph. nullptr when the
// input is not an instances geometry. Per-anchor slices are precomputed, so
// the fill parallelizes over `threads` lanes bit-identically (N7).
GeoPtr realizeInstances(const Geo& inst, unsigned threads = 1);

}  // namespace pgg
