#include "../../pch.h"

// §8.10 aggregators — the field -> value bridge (§4.5). bbox is the first
// multi-output node (destructured as `min, max = bbox(g)`). Reductions
// (min_of/max_of/avg_of/sum_of) run strictly in @index order (§8.10, N1);
// an empty selection is E601 for avg/min/max and 0 for count/sum.

#include "builtins.h"

namespace pgg {
namespace {

Value opBbox(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    glm::vec3 mn, mx;
    geoBBox(in, mn, mx);
    auto elems = std::make_shared<std::vector<Value>>();
    elems->push_back(Value(mn));
    elems->push_back(Value(mx));
    return Value(ListValuePtr(elems));
}

Value opExtent(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    glm::vec3 mn, mx;
    geoBBox(in, mn, mx);
    return Value(mx - mn);
}

Value opCentroid(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    glm::dvec3 acc(0.0);
    const size_t n = in.pointCount();
    for (const glm::vec3& p : *in.positions) acc += glm::dvec3(p);
    const glm::dvec3 c = n > 0 ? acc / static_cast<double>(n) : acc;
    return Value(glm::vec3(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)));
}

Value opCount(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const Domain domain = domainFromName(asString(bound.values[1]));
    ConstBufferPtr where;
    if (bound.fields[2]) {
        where = convertBuffer(evalField(bound.fields[2], in, domain, run), ScalarType::Bool);
    } else {
        where = makeConstBuffer(Value(true), in.elementCount(domain));
    }
    const auto& mask = std::get<BoolBuf>(*where);
    int64_t n = 0;
    for (uint8_t b : mask) n += b ? 1 : 0;
    return Value(n);
}

// min_of/max_of/avg_of/sum_of(field, on, where): reduce the field over the
// points of `on` under the mask, in @index order.
Value opReduce(const BoundCall& bound, RunContext& run) {
    const Geo& on = *asGeo(bound.values[1]);
    ConstBufferPtr vals = convertBuffer(evalField(bound.fields[0], on, Domain::Points, run), ScalarType::F32);
    ConstBufferPtr where;
    if (bound.fields[2]) {
        where = convertBuffer(evalField(bound.fields[2], on, Domain::Points, run), ScalarType::Bool);
    } else {
        where = makeConstBuffer(Value(true), on.pointCount());
    }
    const auto& v = std::get<F32Buf>(*vals);
    const auto& mask = std::get<BoolBuf>(*where);

    double sum = 0.0;
    float best = 0.0f;
    size_t n = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!mask[i]) continue;
        if (n == 0) best = v[i];
        sum += static_cast<double>(v[i]);
        switch (bound.sig->id) {
            case BuiltinId::MinOf: best = std::min(best, v[i]); break;
            case BuiltinId::MaxOf: best = std::max(best, v[i]); break;
            default: break;
        }
        ++n;
    }
    switch (bound.sig->id) {
        case BuiltinId::SumOf:
            return Value(static_cast<float>(sum));
        case BuiltinId::AvgOf:
            if (n == 0) break;
            return Value(static_cast<float>(sum / static_cast<double>(n)));
        case BuiltinId::MinOf:
        case BuiltinId::MaxOf:
            if (n == 0) break;
            return Value(best);
        default:
            return Value();
    }
    run.report("E601", bound.span,
               std::string(bound.sig->name) + " over an empty selection",
               "check the where mask — it selects 0 elements");
    return Value(0.0f);
}

}  // namespace

Value evalAggregateBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Bbox: return opBbox(bound);
        case BuiltinId::Extent: return opExtent(bound);
        case BuiltinId::Centroid: return opCentroid(bound);
        case BuiltinId::Count: return opCount(bound, run);
        case BuiltinId::MinOf:
        case BuiltinId::MaxOf:
        case BuiltinId::AvgOf:
        case BuiltinId::SumOf: return opReduce(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
