#include "../../pch.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "builtins.h"

namespace pgg {
namespace {

float f32At(const Buffer& b, size_t i) {
    switch (b.index()) {
        case 0: return std::get<F32Buf>(b)[i];
        case 1: return static_cast<float>(std::get<IntBuf>(b)[i]);
        case 2: return std::get<BoolBuf>(b)[i] ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

int64_t intAt(const Buffer& b, size_t i) {
    switch (b.index()) {
        case 1: return std::get<IntBuf>(b)[i];
        case 2: return std::get<BoolBuf>(b)[i] ? 1 : 0;
        default: return 0;
    }
}

ConstBufferPtr asF32Buf(ConstBufferPtr b) { return convertBuffer(std::move(b), ScalarType::F32); }

template <typename T>
constexpr bool isVectorBaseElem() {
    return std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>;
}

// promoteBase over 2..3 operands; None when incompatible.
ScalarType promoteN(std::initializer_list<ScalarType> ts) {
    ScalarType t = ScalarType::Bool;
    for (ScalarType x : ts) {
        t = promoteBase(t, x);
        if (t == ScalarType::None) return ScalarType::None;
    }
    return t;
}

// Piecewise-linear ramp evaluation, clamped at the ends. params hold
// (pos, value) pairs; ElemT is the value type (float or glm vector).
template <typename ElemT>
void rampFill(const Buffer& xbuf, const std::vector<Value>& params, size_t pairs, size_t count,
              std::vector<ElemT>& out) {
    auto posAt = [&](size_t k) { return numericValueF32(params[2 * k]); };
    auto valAt = [&](size_t k) -> ElemT {
        const Value& v = params[2 * k + 1];
        if constexpr (std::is_same_v<ElemT, float>) return numericValueF32(v);
        else if constexpr (std::is_same_v<ElemT, glm::vec2>) return asVec2(v);
        else if constexpr (std::is_same_v<ElemT, glm::vec3>) return asVec3(v);
        else return asVec4(v);
    };
    for (size_t i = 0; i < count; ++i) {
        const float x = f32At(xbuf, i);
        if (pairs == 0) {
            out[i] = ElemT(0);
            continue;
        }
        if (x <= posAt(0)) {
            out[i] = valAt(0);
            continue;
        }
        if (x >= posAt(pairs - 1)) {
            out[i] = valAt(pairs - 1);
            continue;
        }
        size_t k = 0;
        while (k + 2 < pairs && x >= posAt(k + 1)) ++k;
        const float p0 = posAt(k);
        const float p1 = posAt(k + 1);
        const float u = p1 > p0 ? (x - p0) / (p1 - p0) : 0.0f;
        out[i] = valAt(k) + (valAt(k + 1) - valAt(k)) * u;
    }
}

}  // namespace

Type inferExprFuncType(BuiltinId id, const std::vector<Type>& args, Span span,
                       std::vector<Diagnostic>& diags) {
    auto base = [&](size_t i) { return i < args.size() ? args[i].base : ScalarType::None; };
    auto err = [&](const std::string& code, std::string msg, std::string hint = {}) {
        diags.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
        return Type{ScalarType::F32, false, GeoKind::Any};
    };
    auto numericOrVec = [](ScalarType t) { return isNumericBase(t) || isVectorBase(t); };
    const ScalarType a = base(0), b = base(1), c = base(2);
    switch (id) {
        case BuiltinId::Dot:
            if (isNumericBase(a) && isNumericBase(b)) return Type{ScalarType::F32, false, GeoKind::Any};
            if (isVectorBase(a) && a == b) return Type{ScalarType::F32, false, GeoKind::Any};
            return err("E204", "dot expects two scalars or two vectors of the same width");
        case BuiltinId::Cross:
            if (a == ScalarType::Vec3 && b == ScalarType::Vec3) return Type{ScalarType::Vec3, false, GeoKind::Any};
            return err("E204", "cross expects two vec3");
        case BuiltinId::Length:
            if (numericOrVec(a)) return Type{ScalarType::F32, false, GeoKind::Any};
            return err("E204", "length expects a numeric scalar or a vector");
        case BuiltinId::Normalize:
            if (isVectorBase(a)) return Type{a, false, GeoKind::Any};
            return err("E204", "normalize expects a vector");
        case BuiltinId::Clamp: {
            const ScalarType t = promoteN({a, b, c});
            if (t != ScalarType::None) return Type{t, false, GeoKind::Any};
            return err("E204", "clamp arguments have incompatible types");
        }
        case BuiltinId::Smoothstep: {
            ScalarType t = promoteN({a, b, c});
            if (t == ScalarType::None) return err("E204", "smoothstep arguments have incompatible types");
            if (!isVectorBase(t)) t = ScalarType::F32;  // integer edges still interpolate in f32
            return Type{t, false, GeoKind::Any};
        }
        case BuiltinId::Mix: {
            const ScalarType t = promoteBase(a, b);
            if (t == ScalarType::None || t == ScalarType::Bool)
                return err("E204", "mix operands have incompatible types");
            if (!canConvertBase(c, ScalarType::F32) && !canConvertBase(c, t))
                return err("E204", "mix factor must be f32 or match the operand type");
            return Type{t, false, GeoKind::Any};
        }
        case BuiltinId::Abs:
        case BuiltinId::Floor:
            if (numericOrVec(a)) return Type{a, false, GeoKind::Any};
            return err("E204", std::string(id == BuiltinId::Abs ? "abs" : "floor") + " expects a numeric scalar or a vector");
        case BuiltinId::Min:
        case BuiltinId::Max: {
            const ScalarType t = promoteBase(a, b);
            if (t != ScalarType::None) return Type{t, false, GeoKind::Any};
            return err("E204", "min/max operands have incompatible types");
        }
        case BuiltinId::Pow: {
            ScalarType t = promoteBase(a, b);
            if (t == ScalarType::None) return err("E204", "pow operands have incompatible types");
            if (!isVectorBase(t)) t = ScalarType::F32;
            return Type{t, false, GeoKind::Any};
        }
        case BuiltinId::Vec2:
        case BuiltinId::Vec3:
        case BuiltinId::Vec4: {
            const int n = id == BuiltinId::Vec2 ? 2 : id == BuiltinId::Vec3 ? 3 : 4;
            const ScalarType vecT = id == BuiltinId::Vec2 ? ScalarType::Vec2
                                    : id == BuiltinId::Vec3 ? ScalarType::Vec3
                                                            : ScalarType::Vec4;
            int count = 0;
            ScalarType first = ScalarType::None;
            bool allNumeric = true;
            for (size_t i = 0; i < args.size(); ++i) {
                if (args[i].base == ScalarType::None) continue;
                if (count == 0) first = args[i].base;
                allNumeric = allNumeric && isNumericBase(args[i].base);
                ++count;
            }
            if (count == 1 && (isNumericBase(first) || first == vecT)) return Type{vecT, false, GeoKind::Any};
            if (count == n && allNumeric) return Type{vecT, false, GeoKind::Any};
            return err(count == n ? "E204" : "E202",
                       "vec" + std::to_string(n) + " takes 1 or " + std::to_string(n) + " numeric arguments");
        }
        case BuiltinId::CastInt:
            if (isNumericBase(a)) return Type{ScalarType::Int, false, GeoKind::Any};
            return err("E204", "int() cast takes a numeric scalar");
        case BuiltinId::CastF32:
            if (isNumericBase(a)) return Type{ScalarType::F32, false, GeoKind::Any};
            return err("E204", "f32() cast takes a numeric scalar");
        case BuiltinId::CastBool:
            if (isNumericBase(a)) return Type{ScalarType::Bool, false, GeoKind::Any};
            return err("E204", "bool() cast takes a numeric scalar");
        case BuiltinId::OrientFromEuler:
            if (a == ScalarType::Vec3) return Type{ScalarType::Vec4, false, GeoKind::Any};
            return err("E204", "orient_from_euler expects vec3 (Euler angles, degrees)");
        case BuiltinId::Ramp: {
            if (!isNumericBase(a)) return err("E204", "ramp x must be a numeric scalar");
            const size_t pairs = args.size() - 1;
            if (pairs < 2 || pairs % 2 != 0)
                return err("E202", "ramp needs (pos, value) pairs after x", "ramp(x, p0, v0, p1, v1, ...)");
            ScalarType vt = ScalarType::None;
            for (size_t i = 1; i < args.size(); ++i) {
                const ScalarType t = args[i].base;
                if (i % 2 == 1) {
                    if (!isNumericBase(t)) return err("E204", "ramp positions must be numeric scalars");
                } else {
                    if (!isNumericBase(t) && !isVectorBase(t))
                        return err("E204", "ramp values must be numeric scalars or vectors");
                    if (vt == ScalarType::None) vt = t;
                    if (t != vt) return err("E204", "ramp values must share one type");
                }
            }
            return Type{vt, false, GeoKind::Any};
        }
        default:
            return err("E204", "not an expression function");
    }
}

ConstBufferPtr evalExprFuncBuf(int callId, const std::vector<ConstBufferPtr>& args,
                               const std::vector<Value>& params, size_t count) {
    const BuiltinId id = static_cast<BuiltinId>(callId);
    auto out1 = [](Buffer b) { return std::make_shared<const Buffer>(std::move(b)); };
    switch (id) {
        case BuiltinId::Dot: {
            F32Buf out(count);
            if (isVectorBase(bufferType(*args[0]))) {
                auto [pa, pb] = promoteBuffers(args[0], args[1]);
                const ScalarType t = bufferType(*pa);
                if (t == ScalarType::Vec2) {
                    const auto& a = std::get<Vec2Buf>(*pa);
                    const auto& b = std::get<Vec2Buf>(*pb);
                    for (size_t i = 0; i < count; ++i) out[i] = glm::dot(a[i], b[i]);
                } else if (t == ScalarType::Vec3) {
                    const auto& a = std::get<Vec3Buf>(*pa);
                    const auto& b = std::get<Vec3Buf>(*pb);
                    for (size_t i = 0; i < count; ++i) out[i] = glm::dot(a[i], b[i]);
                } else {
                    const auto& a = std::get<Vec4Buf>(*pa);
                    const auto& b = std::get<Vec4Buf>(*pb);
                    for (size_t i = 0; i < count; ++i) out[i] = glm::dot(a[i], b[i]);
                }
            } else {
                ConstBufferPtr pa = asF32Buf(args[0]);
                ConstBufferPtr pb = asF32Buf(args[1]);
                for (size_t i = 0; i < count; ++i) out[i] = f32At(*pa, i) * f32At(*pb, i);
            }
            return out1(std::move(out));
        }
        case BuiltinId::Cross: {
            ConstBufferPtr pa = convertBuffer(args[0], ScalarType::Vec3);
            ConstBufferPtr pb = convertBuffer(args[1], ScalarType::Vec3);
            const auto& a = std::get<Vec3Buf>(*pa);
            const auto& b = std::get<Vec3Buf>(*pb);
            Vec3Buf out(count);
            for (size_t i = 0; i < count; ++i) out[i] = glm::cross(a[i], b[i]);
            return out1(std::move(out));
        }
        case BuiltinId::Length: {
            F32Buf out(count);
            const ScalarType t = bufferType(*args[0]);
            if (t == ScalarType::Vec2) {
                for (size_t i = 0; i < count; ++i) out[i] = glm::length(std::get<Vec2Buf>(*args[0])[i]);
            } else if (t == ScalarType::Vec3) {
                for (size_t i = 0; i < count; ++i) out[i] = glm::length(std::get<Vec3Buf>(*args[0])[i]);
            } else if (t == ScalarType::Vec4) {
                for (size_t i = 0; i < count; ++i) out[i] = glm::length(std::get<Vec4Buf>(*args[0])[i]);
            } else {
                ConstBufferPtr p = asF32Buf(args[0]);
                for (size_t i = 0; i < count; ++i) out[i] = std::fabs(f32At(*p, i));
            }
            return out1(std::move(out));
        }
        case BuiltinId::Normalize: {
            return out1(std::visit(
                [&](const auto& va) -> Buffer {
                    using VecT = std::decay_t<decltype(va)>;
                    using ElemT = typename VecT::value_type;
                    if constexpr (!isVectorBaseElem<ElemT>()) {
                        return va;  // unreachable post-typecheck (E204 static)
                    } else {
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i) {
                            const float len = glm::length(va[i]);
                            out[i] = len > 0.0f ? va[i] / len : va[i] * 0.0f;
                        }
                        return out;
                    }
                },
                *args[0]));
        }
        case BuiltinId::Clamp: {
            const ScalarType t = promoteN({bufferType(*args[0]), bufferType(*args[1]), bufferType(*args[2])});
            ConstBufferPtr px = convertBuffer(args[0], t);
            ConstBufferPtr pl = convertBuffer(args[1], t);
            ConstBufferPtr ph = convertBuffer(args[2], t);
            return out1(std::visit(
                [&](const auto& vx) -> Buffer {
                    using VecT = std::decay_t<decltype(vx)>;
                    using ElemT = typename VecT::value_type;
                    const auto& vl = std::get<VecT>(*pl);
                    const auto& vh = std::get<VecT>(*ph);
                    VecT out(vx.size());
                    if constexpr (std::is_same_v<ElemT, uint8_t>) {
                        IntBuf r(vx.size());
                        for (size_t i = 0; i < vx.size(); ++i) r[i] = std::clamp<int64_t>(vx[i], vl[i], vh[i]);
                        return r;
                    } else if constexpr (std::is_same_v<ElemT, int64_t> || std::is_same_v<ElemT, float>) {
                        for (size_t i = 0; i < vx.size(); ++i) out[i] = std::clamp(vx[i], vl[i], vh[i]);
                    } else {
                        for (size_t i = 0; i < vx.size(); ++i) out[i] = glm::clamp(vx[i], vl[i], vh[i]);
                    }
                    return out;
                },
                *px));
        }
        case BuiltinId::Smoothstep: {
            ScalarType t = promoteN({bufferType(*args[0]), bufferType(*args[1]), bufferType(*args[2])});
            if (!isVectorBase(t)) t = ScalarType::F32;
            ConstBufferPtr p0 = convertBuffer(args[0], t);
            ConstBufferPtr p1 = convertBuffer(args[1], t);
            ConstBufferPtr px = convertBuffer(args[2], t);
            return out1(std::visit(
                [&](const auto& v0) -> Buffer {
                    using VecT = std::decay_t<decltype(v0)>;
                    using ElemT = typename VecT::value_type;
                    if constexpr (!std::is_same_v<ElemT, float> && !isVectorBaseElem<ElemT>()) {
                        return v0;  // unreachable post-typecheck
                    } else {
                        const auto& v1 = std::get<VecT>(*p1);
                        const auto& vx = std::get<VecT>(*px);
                        VecT out(v0.size());
                        for (size_t i = 0; i < v0.size(); ++i) {
                            const ElemT u = glm::clamp((vx[i] - v0[i]) / (v1[i] - v0[i]), ElemT(0), ElemT(1));
                            out[i] = u * u * (ElemT(3) - ElemT(2) * u);
                        }
                        return out;
                    }
                },
                *p0));
        }
        case BuiltinId::Mix: {
            const ScalarType t = promoteBase(bufferType(*args[0]), bufferType(*args[1]));
            ConstBufferPtr pa = convertBuffer(args[0], t);
            ConstBufferPtr pb = convertBuffer(args[1], t);
            const bool vecFactor = isVectorBase(bufferType(*args[2])) && bufferType(*args[2]) == t;
            ConstBufferPtr pt = vecFactor ? convertBuffer(args[2], t) : asF32Buf(args[2]);
            return out1(std::visit(
                [&](const auto& va) -> Buffer {
                    using VecT = std::decay_t<decltype(va)>;
                    using ElemT = typename VecT::value_type;
                    const auto& vb = std::get<VecT>(*pb);
                    if constexpr (std::is_same_v<ElemT, uint8_t>) {
                        return va;  // unreachable post-typecheck (mix on bool is E204)
                    } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                        const auto& vu = std::get<F32Buf>(*pt);
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i)
                            out[i] = va[i] + (vb[i] - va[i]) * static_cast<int64_t>(vu[i]);
                        return out;
                    } else if constexpr (std::is_same_v<ElemT, float>) {
                        const auto& vu = std::get<F32Buf>(*pt);
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i) out[i] = va[i] + (vb[i] - va[i]) * vu[i];
                        return out;
                    } else if (vecFactor) {
                        const auto& vt = std::get<VecT>(*pt);
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i) out[i] = glm::mix(va[i], vb[i], vt[i]);
                        return out;
                    } else {
                        const auto& vu = std::get<F32Buf>(*pt);
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i) out[i] = glm::mix(va[i], vb[i], vu[i]);
                        return out;
                    }
                },
                *pa));
        }
        case BuiltinId::Abs:
        case BuiltinId::Floor: {
            return out1(std::visit(
                [&](const auto& va) -> Buffer {
                    using VecT = std::decay_t<decltype(va)>;
                    using ElemT = typename VecT::value_type;
                    VecT out(va.size());
                    for (size_t i = 0; i < va.size(); ++i) {
                        if constexpr (std::is_same_v<ElemT, uint8_t>) {
                            out[i] = va[i];
                        } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                            out[i] = id == BuiltinId::Abs ? std::abs(va[i]) : va[i];
                        } else if constexpr (std::is_same_v<ElemT, float>) {
                            out[i] = id == BuiltinId::Abs ? std::fabs(va[i]) : std::floor(va[i]);
                        } else {
                            out[i] = id == BuiltinId::Abs ? glm::abs(va[i]) : glm::floor(va[i]);
                        }
                    }
                    return out;
                },
                *args[0]));
        }
        case BuiltinId::Min:
        case BuiltinId::Max: {
            auto [pa, pb] = promoteBuffers(args[0], args[1]);
            return out1(std::visit(
                [&](const auto& va) -> Buffer {
                    using VecT = std::decay_t<decltype(va)>;
                    using ElemT = typename VecT::value_type;
                    const auto& vb = std::get<VecT>(*pb);
                    VecT out(va.size());
                    for (size_t i = 0; i < va.size(); ++i) {
                        if constexpr (std::is_same_v<ElemT, glm::vec2> || std::is_same_v<ElemT, glm::vec3> ||
                                      std::is_same_v<ElemT, glm::vec4>) {
                            out[i] = id == BuiltinId::Min ? glm::min(va[i], vb[i]) : glm::max(va[i], vb[i]);
                        } else {
                            out[i] = id == BuiltinId::Min ? std::min(va[i], vb[i]) : std::max(va[i], vb[i]);
                        }
                    }
                    return out;
                },
                *pa));
        }
        case BuiltinId::Pow: {
            ScalarType t = promoteBase(bufferType(*args[0]), bufferType(*args[1]));
            if (!isVectorBase(t)) t = ScalarType::F32;
            ConstBufferPtr pa = convertBuffer(args[0], t);
            ConstBufferPtr pb = convertBuffer(args[1], t);
            return out1(std::visit(
                [&](const auto& va) -> Buffer {
                    using VecT = std::decay_t<decltype(va)>;
                    using ElemT = typename VecT::value_type;
                    if constexpr (!std::is_same_v<ElemT, float> && !isVectorBaseElem<ElemT>()) {
                        return va;  // unreachable post-typecheck
                    } else {
                        const auto& vb = std::get<VecT>(*pb);
                        VecT out(va.size());
                        for (size_t i = 0; i < va.size(); ++i) {
                            if constexpr (std::is_same_v<ElemT, float>) {
                                out[i] = std::pow(va[i], vb[i]);
                            } else {
                                out[i] = glm::pow(va[i], vb[i]);
                            }
                        }
                        return out;
                    }
                },
                *pa));
        }
        case BuiltinId::Vec2:
        case BuiltinId::Vec3:
        case BuiltinId::Vec4: {
            const int n = id == BuiltinId::Vec2 ? 2 : id == BuiltinId::Vec3 ? 3 : 4;
            // Passthrough of a matching-width vector.
            if (args.size() == 1 && vecWidth(bufferType(*args[0])) == n) return args[0];
            std::vector<ConstBufferPtr> comps;
            if (args.size() == 1) {
                comps.assign(static_cast<size_t>(n), asF32Buf(args[0]));  // broadcast
            } else {
                for (const ConstBufferPtr& a : args) comps.push_back(asF32Buf(a));
            }
            if (n == 2) {
                Vec2Buf out(count);
                for (size_t i = 0; i < count; ++i) out[i] = glm::vec2(f32At(*comps[0], i), f32At(*comps[1], i));
                return out1(std::move(out));
            }
            if (n == 3) {
                Vec3Buf out(count);
                for (size_t i = 0; i < count; ++i)
                    out[i] = glm::vec3(f32At(*comps[0], i), f32At(*comps[1], i), f32At(*comps[2], i));
                return out1(std::move(out));
            }
            Vec4Buf out(count);
            for (size_t i = 0; i < count; ++i)
                out[i] = glm::vec4(f32At(*comps[0], i), f32At(*comps[1], i), f32At(*comps[2], i), f32At(*comps[3], i));
            return out1(std::move(out));
        }
        case BuiltinId::CastInt: {
            IntBuf out(count);
            const ScalarType t = bufferType(*args[0]);
            if (t == ScalarType::Int) return args[0];
            if (t == ScalarType::Bool) {
                for (size_t i = 0; i < count; ++i) out[i] = std::get<BoolBuf>(*args[0])[i] ? 1 : 0;
            } else {
                ConstBufferPtr p = asF32Buf(args[0]);
                for (size_t i = 0; i < count; ++i) out[i] = static_cast<int64_t>(f32At(*p, i));
            }
            return out1(std::move(out));
        }
        case BuiltinId::CastF32:
            return asF32Buf(args[0]);
        case BuiltinId::CastBool: {
            BoolBuf out(count);
            const ScalarType t = bufferType(*args[0]);
            if (t == ScalarType::Bool) return args[0];
            if (t == ScalarType::Int) {
                for (size_t i = 0; i < count; ++i) out[i] = std::get<IntBuf>(*args[0])[i] != 0;
            } else {
                ConstBufferPtr p = asF32Buf(args[0]);
                for (size_t i = 0; i < count; ++i) out[i] = f32At(*p, i) != 0.0f;
            }
            return out1(std::move(out));
        }
        case BuiltinId::OrientFromEuler: {
            ConstBufferPtr p = convertBuffer(args[0], ScalarType::Vec3);
            const auto& v = std::get<Vec3Buf>(*p);
            Vec4Buf out(count);
            for (size_t i = 0; i < count; ++i) {
                const glm::quat q(glm::radians(v[i]));
                out[i] = glm::vec4(q.x, q.y, q.z, q.w);
            }
            return out1(std::move(out));
        }
        case BuiltinId::Ramp: {
            ConstBufferPtr px = asF32Buf(args[0]);
            // params: (pos, value) pairs; values share one type (checked statically).
            const size_t pairs = params.size() / 2;
            const ScalarType vt = pairs > 0 ? valueBase(params[1]) : ScalarType::F32;
            switch (vt) {
                case ScalarType::Vec2: {
                    Vec2Buf out(count);
                    rampFill(*px, params, pairs, count, out);
                    return out1(std::move(out));
                }
                case ScalarType::Vec3: {
                    Vec3Buf out(count);
                    rampFill(*px, params, pairs, count, out);
                    return out1(std::move(out));
                }
                case ScalarType::Vec4: {
                    Vec4Buf out(count);
                    rampFill(*px, params, pairs, count, out);
                    return out1(std::move(out));
                }
                default: {
                    F32Buf out(count);
                    rampFill(*px, params, pairs, count, out);
                    return out1(std::move(out));
                }
            }
        }
        default:
            return std::make_shared<const Buffer>(F32Buf(count, 0.0f));
    }
}

ConstBufferPtr evalFieldGenBuf(int callId, const FieldNode& node,
                               const std::vector<ConstBufferPtr>& args, EvalContext& ctx) {
    const BuiltinId id = static_cast<BuiltinId>(callId);
    const size_t count = ctx.elementCount;
    switch (id) {
        case BuiltinId::Ingroup: {
            // ingroup("name"): read the named mask on the evaluation domain;
            // cross-domain falls back to the §4.3 interpolation with a > 0.5
            // threshold; fully missing is a runtime E305 (§8.6).
            const std::string& name = asString(node.params[0]);
            ConstBoolColumnPtr col = sampleGroupColumn(ctx.geo, name, ctx.domain);
            if (!col) {
                ctx.run.report("E305", node.span, "group \"" + name + "\" does not exist on this geometry",
                               "create it with mark(geo, \"" + name + "\", where = ...) first");
                return std::make_shared<const Buffer>(BoolBuf(count, 0));
            }
            return std::make_shared<const Buffer>(BoolBuf(*col));
        }
        case BuiltinId::Fbm: {
            ConstBufferPtr at = convertBuffer(args[0], ScalarType::Vec3);
            const auto& p = std::get<Vec3Buf>(*at);
            const float scale = asF32(node.params[0]);
            const int octaves = std::max(1, static_cast<int>(asInt(node.params[1])));
            const Rng rng = asRng(node.params[2]);
            const float lacunarity = asF32(node.params[3]);
            const float gain = asF32(node.params[4]);
            F32Buf out(count);
            for (size_t i = 0; i < count; ++i) {
                const glm::vec3 q = p[i] * scale;
                out[i] = fbmNoise(rng, q.x, q.y, q.z, octaves, lacunarity, gain);
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        case BuiltinId::Vnoise: {
            ConstBufferPtr at = convertBuffer(args[0], ScalarType::Vec3);
            const auto& p = std::get<Vec3Buf>(*at);
            const float scale = asF32(node.params[0]);
            const Rng rng = asRng(node.params[1]);
            Vec3Buf out(count);
            for (size_t i = 0; i < count; ++i) {
                const glm::vec3 q = p[i] * scale;
                out[i] = glm::vec3(2.0f * valueNoise(rng, q.x, q.y, q.z, 0) - 1.0f,
                                   2.0f * valueNoise(rng, q.x, q.y, q.z, 1) - 1.0f,
                                   2.0f * valueNoise(rng, q.x, q.y, q.z, 2) - 1.0f);
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        case BuiltinId::Random: {
            ConstBufferPtr counter = convertBuffer(args[0], ScalarType::Int);
            const auto& c = std::get<IntBuf>(*counter);
            const float lo = asF32(node.params[0]);
            const float hi = asF32(node.params[1]);
            const Rng rng = asRng(node.params[2]);
            F32Buf out(count);
            for (size_t i = 0; i < count; ++i)
                out[i] = lo + (hi - lo) * rngF32(rng, static_cast<uint64_t>(c[i]), 0);
            return std::make_shared<const Buffer>(std::move(out));
        }
        case BuiltinId::RandomVec: {
            ConstBufferPtr counter = convertBuffer(args[0], ScalarType::Int);
            const auto& c = std::get<IntBuf>(*counter);
            const glm::vec3 lo = asVec3(node.params[0]);
            const glm::vec3 hi = asVec3(node.params[1]);
            const Rng rng = asRng(node.params[2]);
            Vec3Buf out(count);
            for (size_t i = 0; i < count; ++i) {
                const uint64_t ctr = static_cast<uint64_t>(c[i]);
                out[i] = glm::vec3(lo.x + (hi.x - lo.x) * rngF32(rng, ctr, 0),
                                   lo.y + (hi.y - lo.y) * rngF32(rng, ctr, 1),
                                   lo.z + (hi.z - lo.z) * rngF32(rng, ctr, 2));
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        case BuiltinId::RandomInt: {
            ConstBufferPtr counter = convertBuffer(args[0], ScalarType::Int);
            const auto& c = std::get<IntBuf>(*counter);
            const uint64_t n = static_cast<uint64_t>(std::max<int64_t>(1, asInt(node.params[0])));
            const Rng rng = asRng(node.params[1]);
            IntBuf out(count);
            for (size_t i = 0; i < count; ++i) {
                const uint32_t w = rngWord(rng, static_cast<uint64_t>(c[i]), 0);
                out[i] = static_cast<int64_t>((static_cast<uint64_t>(w) * n) >> 32);
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        case BuiltinId::DistanceTo: {
            // Brute-force point-to-surface distance (perf: later stages).
            const GeoPtr target = asGeo(node.params[0]);
            if (ctx.domain != Domain::Points) {
                ctx.run.report("E301", node.span, "distance_to is only evaluable on the points domain at stage E2");
                return std::make_shared<const Buffer>(F32Buf(count, 0.0f));
            }
            const auto& pos = *ctx.geo.positions;
            F32Buf out(count, std::numeric_limits<float>::infinity());
            const bool targetFaces = target->kind == GeoKind::Mesh && target->faceCount() > 0;
            if (targetFaces) {
                for (size_t i = 0; i < count; ++i) {
                    float best = std::numeric_limits<float>::infinity();
                    for (size_t f = 0; f < target->faceCount(); ++f) {
                        const int32_t begin = (*target->faceOffsets)[f];
                        const int32_t end = (*target->faceOffsets)[f + 1];
                        const glm::vec3& a = (*target->positions)[(*target->cornerVerts)[begin]];
                        for (int32_t ci = begin + 1; ci + 1 < end; ++ci) {
                            const glm::vec3& b = (*target->positions)[(*target->cornerVerts)[ci]];
                            const glm::vec3& cc = (*target->positions)[(*target->cornerVerts)[ci + 1]];
                            best = std::min(best, pointTriangleDistance(pos[i], a, b, cc));
                        }
                    }
                    out[i] = best;
                }
            } else if (target->pointCount() > 0) {
                for (size_t i = 0; i < count; ++i) {
                    float best = std::numeric_limits<float>::infinity();
                    for (const glm::vec3& q : *target->positions)
                        best = std::min(best, glm::length(pos[i] - q));
                    out[i] = best;
                }
            }
            return std::make_shared<const Buffer>(std::move(out));
        }
        default:
            return std::make_shared<const Buffer>(F32Buf(count, 0.0f));
    }
}

ConstBufferPtr evalFieldCall(int callId, const FieldNode& node,
                             const std::vector<ConstBufferPtr>& args, EvalContext& ctx) {
    const BuiltinId id = static_cast<BuiltinId>(callId);
    switch (id) {
        case BuiltinId::Dot:
        case BuiltinId::Cross:
        case BuiltinId::Length:
        case BuiltinId::Normalize:
        case BuiltinId::Clamp:
        case BuiltinId::Smoothstep:
        case BuiltinId::Mix:
        case BuiltinId::Abs:
        case BuiltinId::Min:
        case BuiltinId::Max:
        case BuiltinId::Floor:
        case BuiltinId::Pow:
        case BuiltinId::Vec2:
        case BuiltinId::Vec3:
        case BuiltinId::Vec4:
        case BuiltinId::CastInt:
        case BuiltinId::CastF32:
        case BuiltinId::CastBool:
        case BuiltinId::OrientFromEuler:
        case BuiltinId::Ramp:
            return evalExprFuncBuf(callId, args, node.params, ctx.elementCount);
        default:
            return evalFieldGenBuf(callId, node, args, ctx);
    }
}

}  // namespace pgg
