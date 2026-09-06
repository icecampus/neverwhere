#include "../../pch.h"

// §8.6 groups (mark/unmark) and §8.7 attributes (set/remove_attr/rename_attr/
// promote). Groups live in their own per-domain storage on Geo (§4.2), so a
// missing group is E305 while a missing attribute is E302. `set` infers its
// domain from the field: a constant field lands on detail, anything else on
// points (the working domain, §4.1) — faces/corners are reachable by reading
// through the silent interpolation or by an explicit promote. The attribute
// name is global: `set` replaces any same-named column on the other domains —
// the read path takes the first domain hit in points/corners/faces/detail
// order (resampleAttr), so a leftover copy would shadow the new value.

#include "builtins.h"

namespace pgg {
namespace {

Domain domainArg(const Value& v) { return domainFromName(asString(v)); }

PromoteMode promoteModeArg(const Value& v) {
    const std::string& m = asString(v);
    if (m == "sum") return PromoteMode::Sum;
    if (m == "first") return PromoteMode::First;
    return PromoteMode::Average;
}

// Field evaluation buffer -> stored attribute column (types match 1:1).
ColumnData bufferToColumn(const Buffer& buf) {
    return std::visit(
        [](const auto& v) -> ColumnData {
            using VecT = std::decay_t<decltype(v)>;
            return std::make_shared<const VecT>(v);
        },
        buf);
}

// mark(geo, name, where, domain): create/overwrite a named bool mask on the
// given domain (§8.6).
Value opMark(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& name = asString(bound.values[1]);
    const Domain domain = domainArg(bound.values[3]);

    ConstBufferPtr where;
    if (bound.fields[2]) {
        where = convertBuffer(evalField(bound.fields[2], in, domain, run), ScalarType::Bool);
    } else {
        where = makeConstBuffer(Value(true), in.elementCount(domain));
    }
    const auto& w = std::get<BoolBuf>(*where);

    GroupSet groups = in.groups(domain) ? *in.groups(domain) : GroupSet{};
    groups.columns[name] = std::make_shared<const BoolColumn>(w);
    return Value(withGroups(in, domain, std::make_shared<const GroupSet>(std::move(groups))));
}

// unmark(geo, name): drop the group from every domain that has it.
Value opUnmark(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& name = asString(bound.values[1]);
    Geo out = in;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const GroupSet* gs = out.groups(d);
        if (!gs || !gs->find(name)) continue;
        GroupSet copy = *gs;
        copy.columns.erase(name);
        auto ptr = std::make_shared<const GroupSet>(std::move(copy));
        switch (d) {
            case Domain::Points: out.pointGroups = std::move(ptr); break;
            case Domain::Corners: out.cornerGroups = std::move(ptr); break;
            case Domain::Faces: out.faceGroups = std::move(ptr); break;
            case Domain::Detail: out.detailGroups = std::move(ptr); break;
        }
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// set(geo, name, value, domain = auto): materialize a field into a named
// attribute column. The target domain is the explicit `domain` argument when
// given, otherwise inferred: constant field -> detail, anything else -> points
// (§8.7, §19).
Value opSet(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& name = asString(bound.values[1]);
    if (name == "P" || name == "N" || name == "index") {
        run.report("E301", bound.span, "attribute '@" + name + "' is reserved and cannot be written by set",
                   "use set_position for @P and compute_normals for @N");
        return Value(asGeo(bound.values[0]));
    }
    if (!bound.fields[2]) return Value(asGeo(bound.values[0]));  // static E202 already reported

    const FieldNode* field = bound.fields[2];
    const std::string& domArg = asString(bound.values[3]);
    const Domain domain = domArg == "auto"
                              ? (field->kind == FKind::Const ? Domain::Detail : Domain::Points)
                              : domainFromName(domArg);
    ConstBufferPtr buf = evalField(field, in, domain, run);

    // The name is global: drop any same-named column from every domain (the
    // target domain included — it is rewritten below), so no stale copy can
    // shadow the new value on the read path.
    Geo out = in;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* as = out.attrs(d);
        if (!as || !as->find(name)) continue;
        AttrSet copy = *as;
        copy.columns.erase(name);
        auto ptr = std::make_shared<const AttrSet>(std::move(copy));
        switch (d) {
            case Domain::Points: out.pointAttrs = std::move(ptr); break;
            case Domain::Corners: out.cornerAttrs = std::move(ptr); break;
            case Domain::Faces: out.faceAttrs = std::move(ptr); break;
            case Domain::Detail: out.detailAttrs = std::move(ptr); break;
        }
    }
    AttrSet attrs = out.attrs(domain) ? *out.attrs(domain) : AttrSet{};
    attrs.columns[name] = AttrColumn{bufferToColumn(*buf)};
    return Value(withAttrs(out, domain, std::make_shared<const AttrSet>(std::move(attrs))));
}

// remove_attr(geo, name): drop the column from every domain that has it.
Value opRemoveAttr(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& name = asString(bound.values[1]);
    Geo out = in;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* as = out.attrs(d);
        if (!as || !as->find(name)) continue;
        AttrSet copy = *as;
        copy.columns.erase(name);
        auto ptr = std::make_shared<const AttrSet>(std::move(copy));
        switch (d) {
            case Domain::Points: out.pointAttrs = std::move(ptr); break;
            case Domain::Corners: out.cornerAttrs = std::move(ptr); break;
            case Domain::Faces: out.faceAttrs = std::move(ptr); break;
            case Domain::Detail: out.detailAttrs = std::move(ptr); break;
        }
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// rename_attr(geo, old, new): rename the column on every domain that has it.
Value opRenameAttr(const BoundCall& bound) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& oldName = asString(bound.values[1]);
    const std::string& newName = asString(bound.values[2]);
    Geo out = in;
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* as = out.attrs(d);
        const AttrColumn* col = as ? as->find(oldName) : nullptr;
        if (!col) continue;
        AttrSet copy = *as;
        copy.columns.erase(oldName);
        copy.columns[newName] = *col;
        auto ptr = std::make_shared<const AttrSet>(std::move(copy));
        switch (d) {
            case Domain::Points: out.pointAttrs = std::move(ptr); break;
            case Domain::Corners: out.cornerAttrs = std::move(ptr); break;
            case Domain::Faces: out.faceAttrs = std::move(ptr); break;
            case Domain::Detail: out.detailAttrs = std::move(ptr); break;
        }
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

// promote(geo, name, from, to, mode): explicit domain conversion (§8.7).
Value opPromote(const BoundCall& bound, RunContext& run) {
    const Geo& in = *asGeo(bound.values[0]);
    const std::string& name = asString(bound.values[1]);
    const Domain from = domainArg(bound.values[2]);
    const Domain to = domainArg(bound.values[3]);
    const PromoteMode mode = promoteModeArg(bound.values[4]);

    const AttrSet* as = in.attrs(from);
    const AttrColumn* existing = as ? as->find(name) : nullptr;
    if (!existing) {
        run.report("E302", bound.span,
                   "attribute '@" + name + "' is not present on the " + std::string(domainName(from)) +
                       " domain",
                   "write it with set() first, or promote from the domain that has it");
        return Value(asGeo(bound.values[0]));
    }
    std::optional<ColumnData> col = promoteAttrColumn(in, name, from, to, mode);
    if (!col) {
        run.report("E301", bound.span, "attribute '@" + name + "' cannot be promoted",
                   "string attributes do not convert across domains");
        return Value(asGeo(bound.values[0]));
    }
    AttrSet attrs = in.attrs(to) ? *in.attrs(to) : AttrSet{};
    attrs.columns[name] = AttrColumn{std::move(*col)};
    return Value(withAttrs(in, to, std::make_shared<const AttrSet>(std::move(attrs))));
}

}  // namespace

Value evalAttrBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Mark: return opMark(bound, run);
        case BuiltinId::Unmark: return opUnmark(bound);
        case BuiltinId::SetAttr: return opSet(bound, run);
        case BuiltinId::RemoveAttr: return opRemoveAttr(bound);
        case BuiltinId::RenameAttr: return opRenameAttr(bound);
        case BuiltinId::Promote: return opPromote(bound, run);
        default: return Value();
    }
}

}  // namespace pgg
