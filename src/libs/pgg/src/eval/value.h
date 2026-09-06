#pragma once

// Runtime values and types for the execution core (spec §4).
//
// Type = scalar/vector base x isField x geoKind (§4.1: geo kinds and domains
// are separate type axes). Value = the runtime payload: numbers, vectors,
// strings, rng keys, geometry (shared, immutable) and compiled fields.
// Conversion rules are §4.5: the widening chain bool -> int -> f32, scalar ->
// vector broadcast, component-wise vector arithmetic; narrowing conversions
// exist only as explicit casts. A value implicitly becomes a constant field;
// a field never becomes a value without an aggregator (E205).

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include "../ast.h"
#include "geometry.h"
#include "rng.h"

namespace pgg {

struct FieldNode;
using FieldPtr = std::shared_ptr<const FieldNode>;

struct SdfNode;
using SdfPtr = std::shared_ptr<const SdfNode>;

enum class ScalarType {
    None,
    Bool,
    Int,
    F32,
    Vec2,
    Vec3,
    Vec4,
    String,
    Rng,
    Geo,
    Sdf,
    Any,  // signature positions only: any numeric scalar or vector
};

struct Type {
    ScalarType base = ScalarType::None;
    bool isField = false;
    GeoKind geoKind = GeoKind::Any;  // for base == Geo
    bool isList = false;             // T[] — value-level lists (never fields)

    bool operator==(const Type&) const = default;
};

inline bool isNumericBase(ScalarType t) {
    return t == ScalarType::Bool || t == ScalarType::Int || t == ScalarType::F32;
}

inline bool isVectorBase(ScalarType t) {
    return t == ScalarType::Vec2 || t == ScalarType::Vec3 || t == ScalarType::Vec4;
}

inline int vecWidth(ScalarType t) {
    switch (t) {
        case ScalarType::Vec2: return 2;
        case ScalarType::Vec3: return 3;
        case ScalarType::Vec4: return 4;
        default: return 0;
    }
}

inline const char* scalarName(ScalarType t) {
    switch (t) {
        case ScalarType::None: return "none";
        case ScalarType::Bool: return "bool";
        case ScalarType::Int: return "int";
        case ScalarType::F32: return "f32";
        case ScalarType::Vec2: return "vec2";
        case ScalarType::Vec3: return "vec3";
        case ScalarType::Vec4: return "vec4";
        case ScalarType::String: return "string";
        case ScalarType::Rng: return "rng";
        case ScalarType::Geo: return "geo";
        case ScalarType::Sdf: return "sdf";
        case ScalarType::Any: return "any";
    }
    return "?";
}

inline std::string typeName(const Type& t) {
    std::string out;
    if (t.base == ScalarType::Geo && t.geoKind != GeoKind::Any) {
        out = std::string("geo<") + geoKindName(t.geoKind) + ">";
    } else {
        out = scalarName(t.base);
    }
    if (t.isList) out += "[]";
    if (t.isField) out = "field<" + out + ">";
    return out;
}

// Widening rank of the bool -> int -> f32 chain (§4.5); -1 for non-numeric.
inline int numericRank(ScalarType t) {
    switch (t) {
        case ScalarType::Bool: return 0;
        case ScalarType::Int: return 1;
        case ScalarType::F32: return 2;
        default: return -1;
    }
}

// Base-level conversion test (widening + broadcast only).
inline bool canConvertBase(ScalarType from, ScalarType to) {
    if (from == to) return true;
    // Any as a *source* = statically unknown (a user attribute read, v1.23):
    // accepted here, the runtime checks the actual column type.
    if (from == ScalarType::Any) return true;
    if (to == ScalarType::Any) return isNumericBase(from) || isVectorBase(from);
    if (isNumericBase(from) && isNumericBase(to)) return numericRank(from) <= numericRank(to);
    if (isNumericBase(from) && isVectorBase(to)) return true;  // broadcast
    return false;
}

// Static type of an attribute read `@name` (v1.23): the built-ins and the
// reserved / conventional names are known, everything else is Any (unknown
// until the runtime sees the column; expression typing stays permissive for
// Any and the runtime re-checks with the actual types).
inline ScalarType knownAttrType(const std::string& name) {
    if (name == "P" || name == "N" || name == "Cd" || name == "color" || name == "tint") return ScalarType::Vec3;
    if (name == "orient") return ScalarType::Vec4;
    if (name == "uv" || name == "profile_scale") return ScalarType::Vec2;
    if (name == "index" || name == "variant" || name == "island_id") return ScalarType::Int;
    if (name == "scale" || name == "twist" || name == "t" || name == "ao" || name == "roughness") return ScalarType::F32;
    return ScalarType::Any;
}

// Binary promotion (numpy-style): numeric rank, scalar broadcasts into
// vector; two vectors must share their width. Returns None when incompatible.
inline ScalarType promoteBase(ScalarType a, ScalarType b) {
    // Unknown (Any) absorbs: the result of an expression over a statically
    // untyped attribute is itself unknown until the runtime sees the column.
    if (a == ScalarType::Any || b == ScalarType::Any) return ScalarType::Any;
    if (isVectorBase(a) && isVectorBase(b)) return a == b ? a : ScalarType::None;
    if (isVectorBase(a) && isNumericBase(b)) return a;
    if (isNumericBase(a) && isVectorBase(b)) return b;
    if (isNumericBase(a) && isNumericBase(b))
        return numericRank(a) >= numericRank(b) ? a : b;
    return ScalarType::None;
}

// Full conversion test including field flags and geo kinds (§4.5).
inline bool canConvert(const Type& from, const Type& to) {
    if (from.isField && !to.isField) return false;  // field -> value needs an aggregator
    if (from.isList || to.isList) {
        if (!from.isList || !to.isList) return false;
        if (from.base == ScalarType::None) return true;  // empty list literal
        if (to.base == ScalarType::Geo || from.base == ScalarType::Geo) {
            if (from.base != ScalarType::Geo || to.base != ScalarType::Geo) return false;
            return to.geoKind == GeoKind::Any || from.geoKind == GeoKind::Any ||
                   from.geoKind == to.geoKind;
        }
        return canConvertBase(from.base, to.base);
    }
    if (to.base == ScalarType::Geo || from.base == ScalarType::Geo) {
        if (from.base != ScalarType::Geo || to.base != ScalarType::Geo) return false;
        return to.geoKind == GeoKind::Any || from.geoKind == GeoKind::Any ||
               from.geoKind == to.geoKind;
    }
    return canConvertBase(from.base, to.base);
}

// Static type of a parsed TypeRef (param/def signatures).
inline Type typeFromRef(const TypeRef& ref) {
    Type t;
    const std::string& b = ref.base;
    if (b == "bool") t.base = ScalarType::Bool;
    else if (b == "int") t.base = ScalarType::Int;
    else if (b == "f32") t.base = ScalarType::F32;
    else if (b == "vec2") t.base = ScalarType::Vec2;
    else if (b == "vec3") t.base = ScalarType::Vec3;
    else if (b == "vec4") t.base = ScalarType::Vec4;
    else if (b == "string") t.base = ScalarType::String;
    else if (b == "rng") t.base = ScalarType::Rng;
    else if (b == "sdf") t.base = ScalarType::Sdf;
    else if (b == "geo") {
        t.base = ScalarType::Geo;
        if (ref.geoKind == "mesh") t.geoKind = GeoKind::Mesh;
        else if (ref.geoKind == "points") t.geoKind = GeoKind::Points;
        else if (ref.geoKind == "instances") t.geoKind = GeoKind::Instances;
    } else if (b == "field" && ref.arg) {
        t = typeFromRef(*ref.arg);
        t.isField = true;
        return t;  // field<...> keeps the inner flags; field<T>[] is not legal
    }
    t.isList = ref.list;
    return t;
}

// --- Value ------------------------------------------------------------------

struct Value;
using ListValuePtr = std::shared_ptr<const std::vector<Value>>;

struct Value {
    // Index-stable variant: new alternatives are appended at the END (12 =
    // sdf, stage E4); existing indices 0..11 are part of the cache/switch
    // contract and must not move.
    using Data = std::variant<std::monostate,  // none
                              bool, int64_t, float, glm::vec2, glm::vec3, glm::vec4,
                              std::string, Rng, GeoPtr, FieldPtr, ListValuePtr, SdfPtr>;
    Data data;

    Value() = default;
    Value(bool v) : data(v) {}
    Value(int64_t v) : data(v) {}
    Value(int v) : data(static_cast<int64_t>(v)) {}
    Value(float v) : data(v) {}
    Value(glm::vec2 v) : data(v) {}
    Value(glm::vec3 v) : data(v) {}
    Value(glm::vec4 v) : data(v) {}
    Value(std::string v) : data(std::move(v)) {}
    Value(Rng v) : data(v) {}
    Value(GeoPtr v) : data(std::move(v)) {}
    Value(FieldPtr v) : data(std::move(v)) {}
    Value(ListValuePtr v) : data(std::move(v)) {}
    Value(SdfPtr v) : data(std::move(v)) {}
};

inline bool isNone(const Value& v) { return std::holds_alternative<std::monostate>(v.data); }

inline ScalarType valueBase(const Value& v) {
    switch (v.data.index()) {
        case 1: return ScalarType::Bool;
        case 2: return ScalarType::Int;
        case 3: return ScalarType::F32;
        case 4: return ScalarType::Vec2;
        case 5: return ScalarType::Vec3;
        case 6: return ScalarType::Vec4;
        case 7: return ScalarType::String;
        case 8: return ScalarType::Rng;
        case 9: return ScalarType::Geo;
        case 10: return ScalarType::Any;  // field: element type lives in the node
        case 11: return ScalarType::Any;  // list: element type is not in the payload
        case 12: return ScalarType::Sdf;
        default: return ScalarType::None;
    }
}

inline bool asBool(const Value& v) { return std::get<bool>(v.data); }
inline int64_t asInt(const Value& v) { return std::get<int64_t>(v.data); }
inline float asF32(const Value& v) { return std::get<float>(v.data); }
inline glm::vec2 asVec2(const Value& v) { return std::get<glm::vec2>(v.data); }
inline glm::vec3 asVec3(const Value& v) { return std::get<glm::vec3>(v.data); }
inline glm::vec4 asVec4(const Value& v) { return std::get<glm::vec4>(v.data); }
inline const std::string& asString(const Value& v) { return std::get<std::string>(v.data); }
inline Rng asRng(const Value& v) { return std::get<Rng>(v.data); }
inline GeoPtr asGeo(const Value& v) { return std::get<GeoPtr>(v.data); }
inline FieldPtr asField(const Value& v) { return std::get<FieldPtr>(v.data); }
inline SdfPtr asSdf(const Value& v) { return std::get<SdfPtr>(v.data); }
inline bool isFieldValue(const Value& v) { return std::holds_alternative<FieldPtr>(v.data); }
inline bool isSdfValue(const Value& v) { return std::holds_alternative<SdfPtr>(v.data); }
inline bool isListValue(const Value& v) { return std::holds_alternative<ListValuePtr>(v.data); }
inline const std::vector<Value>& asList(const Value& v) { return *std::get<ListValuePtr>(v.data); }

// Widening conversion of a value to a target base (broadcast included).
// Returns none when the conversion is not in the §4.5 chain.
inline Value convertValue(const Value& v, ScalarType to) {
    const ScalarType from = valueBase(v);
    if (from == to) return v;
    if (isNumericBase(from) && isNumericBase(to)) {
        if (to == ScalarType::F32) {
            const float f = from == ScalarType::Bool ? (asBool(v) ? 1.0f : 0.0f)
                            : from == ScalarType::Int ? static_cast<float>(asInt(v))
                                                      : asF32(v);
            return Value(f);
        }
        if (to == ScalarType::Int && from == ScalarType::Bool)
            return Value(static_cast<int64_t>(asBool(v) ? 1 : 0));
        return Value();  // narrowing (f32 -> int, anything -> bool) needs a cast
    }
    if (isNumericBase(from) && isVectorBase(to)) {
        const float f = from == ScalarType::Bool ? (asBool(v) ? 1.0f : 0.0f)
                        : from == ScalarType::Int ? static_cast<float>(asInt(v))
                                                  : asF32(v);
        switch (to) {
            case ScalarType::Vec2: return Value(glm::vec2(f));
            case ScalarType::Vec3: return Value(glm::vec3(f));
            case ScalarType::Vec4: return Value(glm::vec4(f));
            default: return Value();
        }
    }
    return Value();
}

// Numeric scalar value read as f32 (bool/int/f32); 0 for anything else.
inline float numericValueF32(const Value& v) {
    switch (valueBase(v)) {
        case ScalarType::Bool: return asBool(v) ? 1.0f : 0.0f;
        case ScalarType::Int: return static_cast<float>(asInt(v));
        case ScalarType::F32: return asF32(v);
        default: return 0.0f;
    }
}

// Numeric scalar value read as int (bool/int); f32 truncates (explicit-cast
// semantics). 0 for anything else.
inline int64_t numericValueInt(const Value& v) {
    switch (valueBase(v)) {
        case ScalarType::Bool: return asBool(v) ? 1 : 0;
        case ScalarType::Int: return asInt(v);
        case ScalarType::F32: return static_cast<int64_t>(asF32(v));
        default: return 0;
    }
}

// --- value-level math (constant expressions, param arithmetic) --------------

namespace detail {

template <typename T>
Value elementBinary(const std::string& op, const T& a, const T& b) {
    if constexpr (std::is_same_v<T, bool>) {
        if (op == "&") return Value(a && b);
        if (op == "|") return Value(a || b);
        if (op == "==") return Value(a == b);
        if (op == "!=") return Value(a != b);
    } else if constexpr (std::is_same_v<T, int64_t>) {
        if (op == "+") return Value(a + b);
        if (op == "-") return Value(a - b);
        if (op == "*") return Value(a * b);
        // v0: integer division/modulo by zero yields 0 (kept deterministic).
        if (op == "/") return Value(b == 0 ? int64_t(0) : a / b);
        if (op == "%") return Value(b == 0 ? int64_t(0) : a % b);
        if (op == "<") return Value(a < b);
        if (op == "<=") return Value(a <= b);
        if (op == ">") return Value(a > b);
        if (op == ">=") return Value(a >= b);
        if (op == "==") return Value(a == b);
        if (op == "!=") return Value(a != b);
    } else if constexpr (std::is_same_v<T, float>) {
        if (op == "+") return Value(a + b);
        if (op == "-") return Value(a - b);
        if (op == "*") return Value(a * b);
        if (op == "/") return Value(a / b);  // IEEE semantics for f32
        if (op == "<") return Value(a < b);
        if (op == "<=") return Value(a <= b);
        if (op == ">") return Value(a > b);
        if (op == ">=") return Value(a >= b);
        if (op == "==") return Value(a == b);
        if (op == "!=") return Value(a != b);
    } else {  // glm vectors: component-wise arithmetic (§4.5)
        if (op == "+") return Value(a + b);
        if (op == "-") return Value(a - b);
        if (op == "*") return Value(a * b);
        if (op == "/") return Value(a / b);
        if (op == "==") return Value(a == b);
        if (op == "!=") return Value(a != b);
    }
    return Value();
}

}  // namespace detail

// Binary op on two values with §4.5 promotion; none on type mismatch.
inline Value valueBinary(const std::string& op, const Value& a, const Value& b) {
    if (a.data.index() == 7 && b.data.index() == 7) {  // strings: equality only
        if (op == "==") return Value(asString(a) == asString(b));
        if (op == "!=") return Value(asString(a) != asString(b));
        return Value();
    }
    ScalarType t = promoteBase(valueBase(a), valueBase(b));
    if (t == ScalarType::None) return Value();
    // Arithmetic on booleans happens as int (bool is rank 0 of the chain).
    const bool arith = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
    if (arith && t == ScalarType::Bool) t = ScalarType::Int;
    const Value ca = convertValue(a, t);
    const Value cb = convertValue(b, t);
    switch (t) {
        case ScalarType::Bool: return detail::elementBinary(op, asBool(ca), asBool(cb));
        case ScalarType::Int: return detail::elementBinary(op, asInt(ca), asInt(cb));
        case ScalarType::F32: return detail::elementBinary(op, asF32(ca), asF32(cb));
        case ScalarType::Vec2: return detail::elementBinary(op, asVec2(ca), asVec2(cb));
        case ScalarType::Vec3: return detail::elementBinary(op, asVec3(ca), asVec3(cb));
        case ScalarType::Vec4: return detail::elementBinary(op, asVec4(ca), asVec4(cb));
        default: return Value();
    }
}

inline Value valueUnary(const std::string& op, const Value& v) {
    const ScalarType t = valueBase(v);
    if (op == "-") {
        switch (t) {
            case ScalarType::Bool: return Value(-static_cast<int64_t>(asBool(v)));
            case ScalarType::Int: return Value(-asInt(v));
            case ScalarType::F32: return Value(-asF32(v));
            case ScalarType::Vec2: return Value(-asVec2(v));
            case ScalarType::Vec3: return Value(-asVec3(v));
            case ScalarType::Vec4: return Value(-asVec4(v));
            default: return Value();
        }
    }
    if (op == "!") {
        if (t == ScalarType::Bool) return Value(!asBool(v));
    }
    return Value();
}

inline std::string valueToString(const Value& v) {
    char buf[64];
    switch (v.data.index()) {
        case 0: return "none";
        case 1: return asBool(v) ? "true" : "false";
        case 2: return std::to_string(asInt(v));
        case 3:
            std::snprintf(buf, sizeof(buf), "%g", asF32(v));
            return buf;
        case 4:
        case 5:
        case 6: {
            const int w = vecWidth(valueBase(v));
            std::string out = "(";
            for (int i = 0; i < w; ++i) {
                const float f = w == 2 ? asVec2(v)[i] : w == 3 ? asVec3(v)[i] : asVec4(v)[i];
                std::snprintf(buf, sizeof(buf), "%g", f);
                if (i) out += ", ";
                out += buf;
            }
            return out + ")";
        }
        case 7: return "\"" + asString(v) + "\"";
        case 8: return "<rng>";
        case 9: return std::string("geo<") + geoKindName(asGeo(v)->kind) + ">";
        case 10: return "<field>";
        case 11: {
            std::string out = "[";
            const auto& elems = asList(v);
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i) out += ", ";
                out += valueToString(elems[i]);
            }
            return out + "]";
        }
        case 12: return "<sdf>";
        default: return "?";
    }
}

}  // namespace pgg
