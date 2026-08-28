#pragma once

// Field DAG and its memoized evaluator (spec §4.4).
//
// A field is a lazy per-element function, not data: expression ASTs compile
// into a FieldNode DAG (arena-owned by RunContext), and evaluation happens at
// the consumer against a (geometry, domain) context. The evaluator memoizes
// by (FieldNode*, Geo*, domain) -> shared element buffer, so one field with
// many consumers on the same geometry computes once (§4.4 identity rule).
// Evaluation counters live in RunContext for the E1 acceptance tests.

#include <functional>
#include <unordered_map>

#include "value.h"

namespace pgg {

// Per-element buffers: f32 / int64 / bool(u8) / vec2 / vec3 / vec4.
using F32Buf = std::vector<float>;
using IntBuf = std::vector<int64_t>;
using BoolBuf = std::vector<uint8_t>;
using Vec2Buf = std::vector<glm::vec2>;
using Vec3Buf = std::vector<glm::vec3>;
using Vec4Buf = std::vector<glm::vec4>;
using Buffer = std::variant<F32Buf, IntBuf, BoolBuf, Vec2Buf, Vec3Buf, Vec4Buf>;
using ConstBufferPtr = std::shared_ptr<const Buffer>;

enum class FKind {
    Const,      // constant value broadcast to every element
    AttrP,      // @P (points domain)
    AttrN,      // @N (points domain)
    AttrIndex,  // @index (any domain)
    AttrNamed,  // @name — user attribute (none exist before E2; kept for robustness)
    Unary,
    Binary,
    Ternary,
    Call,       // builtin field function (expression functions and §8.5 generators)
};

struct FieldNode {
    int id = -1;                         // arena index (stable)
    FKind kind = FKind::Const;
    ScalarType type = ScalarType::None;  // element type of the result
    Span span;
    Value constValue;                    // Const
    std::string name;                    // AttrNamed
    std::string op;                      // Unary / Binary
    int callId = -1;                     // Call: BuiltinId (as int, avoids header cycle)
    std::vector<const FieldNode*> args;  // Unary(1) / Binary(2) / Ternary(3) / Call(field args)
    std::vector<Value> params;           // Call: captured value args (rng, knobs, geo, ramp points)
};

// Shared per-run state: node arena, memoization cache, counters, diagnostics.
struct RunContext {
    struct FieldKey {
        const FieldNode* node = nullptr;
        const Geo* geo = nullptr;
        int domain = 0;
        bool operator==(const FieldKey&) const = default;
    };
    struct FieldKeyHash {
        size_t operator()(const FieldKey& k) const {
            size_t h = std::hash<const void*>()(k.node);
            h = h * 1000003 + std::hash<const void*>()(k.geo);
            h = h * 1000003 + std::hash<int>()(k.domain);
            return h;
        }
    };

    std::vector<std::unique_ptr<FieldNode>> fieldArena;
    std::unordered_map<FieldKey, ConstBufferPtr, FieldKeyHash> fieldCache;
    std::unordered_map<int, int> nodeEvals;  // FieldNode id -> evaluation count (cache misses)
    uint64_t fieldsEvaluated = 0;
    std::vector<Diagnostic>* diagnostics = nullptr;

    FieldNode* newNode();
    void report(const std::string& code, Span span, std::string message, std::string hint = {});
};

struct EvalContext {
    const Geo& geo;
    Domain domain;
    size_t elementCount;
    RunContext& run;
};

// Evaluates a field on (geo, domain), memoized by (node, geo, domain).
ConstBufferPtr evalField(const FieldNode* node, const Geo& geo, Domain domain, RunContext& run);

// --- buffer helpers (shared by the evaluator and builtin implementations) ---

ScalarType bufferType(const Buffer& buf);
size_t bufferSize(const Buffer& buf);

// Broadcast a constant value to `count` elements.
ConstBufferPtr makeConstBuffer(const Value& v, size_t count);

// Zero-filled buffer of a base type (error recovery path).
ConstBufferPtr makeZeroBuffer(ScalarType base, size_t count);

// Widening conversion + scalar->vector broadcast (§4.5); nullptr when the
// conversion is not in the chain (a static E204 should have caught it).
ConstBufferPtr convertBuffer(ConstBufferPtr buf, ScalarType target);

// Promote two buffers to their common §4.5 type.
std::pair<ConstBufferPtr, ConstBufferPtr> promoteBuffers(ConstBufferPtr a, ConstBufferPtr b);

// Extract element 0 as a Value (constant folding of expression functions).
Value bufferValueAt(const Buffer& buf, size_t i);

// --- expression compiler (AST -> TypedValue: value or field DAG) ------------

struct TypedValue {
    Type type;
    const FieldNode* field = nullptr;  // set when type.isField
    Value value;                       // set when !type.isField
    explicit operator bool() const { return type.base != ScalarType::None || field != nullptr; }
};

// Resolves an identifier in an expression; may trigger lazy binding
// evaluation in the engine. Returns a null TypedValue on failure (the
// resolver reports its own diagnostic).
using IdentResolver = std::function<TypedValue(const std::string& name, Span span)>;

TypedValue compileExpr(const Expr* expr, RunContext& run, const IdentResolver& resolve);

// Value-only evaluation: reports E205 when the expression is a field.
Value compileExprToValue(const Expr* expr, RunContext& run, const IdentResolver& resolve);

// Wraps a TypedValue into a field node (values become constant nodes).
const FieldNode* asFieldNode(const TypedValue& tv, RunContext& run);

}  // namespace pgg
