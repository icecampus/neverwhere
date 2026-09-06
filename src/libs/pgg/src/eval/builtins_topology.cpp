#include "../../pch.h"

// §8.3 topology nodes beyond merge, value level: delete(geo, where, domain).
//
// delete removes the elements of `domain` where the mask is true and cascades
// to the incident elements of the higher domains (spec §8.3): a deleted point
// takes every face (and its corners) that touches it, a deleted corner takes
// its face, a deleted face takes its corners. Lower domains are never touched
// (deleting faces leaves orphan points — Houdini semantics; use a points mask
// when the points must go too). Columns on every surviving domain are gathered
// by the kept indices, so attributes/groups/@N stay aligned. detail is not a
// per-element domain and is rejected (E204).

#include "builtins.h"

namespace pgg {
namespace {

// Gathers every column of an AttrSet by `keep` (indices into the old domain).
std::shared_ptr<const AttrSet> gatherAttrs(const AttrSet* src, const std::vector<int32_t>& keep) {
    if (!src) return nullptr;
    AttrSet out;
    for (const auto& [name, col] : src->columns)
        out.columns[name] = AttrColumn{gatherColumn(col.data, keep), col.typeInfo};
    return std::make_shared<const AttrSet>(std::move(out));
}

std::shared_ptr<const GroupSet> gatherGroups(const GroupSet* src, const std::vector<int32_t>& keep) {
    if (!src) return nullptr;
    GroupSet out;
    for (const auto& [name, col] : src->columns) {
        BoolColumn g;
        g.reserve(keep.size());
        for (int32_t i : keep) g.push_back((*col)[static_cast<size_t>(i)]);
        out.columns[name] = std::make_shared<const BoolColumn>(std::move(g));
    }
    return std::make_shared<const GroupSet>(std::move(out));
}

std::vector<int32_t> keptIndices(const std::vector<uint8_t>& drop) {
    std::vector<int32_t> keep;
    keep.reserve(drop.size());
    for (size_t i = 0; i < drop.size(); ++i)
        if (!drop[i]) keep.push_back(static_cast<int32_t>(i));
    return keep;
}

// Rebuilds the mesh part of `out` from the face-drop mask: kept faces keep all
// their corners; corner indices are remapped through `pointRemap` (identity
// when null). Points are left to the caller.
void rebuildFaces(const Geo& in, Geo& out, const std::vector<uint8_t>& dropFace,
                  const std::vector<int32_t>* pointRemap) {
    const auto& offsets = *in.faceOffsets;
    const auto& verts = *in.cornerVerts;
    std::vector<int32_t> keepFace, keepCorner, newVerts, newOffsets;
    newOffsets.push_back(0);
    for (size_t f = 0; f + 1 < offsets.size(); ++f) {
        if (dropFace[f]) continue;
        keepFace.push_back(static_cast<int32_t>(f));
        for (int32_t c = offsets[f]; c < offsets[f + 1]; ++c) {
            keepCorner.push_back(c);
            const int32_t v = verts[static_cast<size_t>(c)];
            newVerts.push_back(pointRemap ? (*pointRemap)[static_cast<size_t>(v)] : v);
        }
        newOffsets.push_back(static_cast<int32_t>(newVerts.size()));
    }
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(newVerts));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(newOffsets));
    out.cornerAttrs = gatherAttrs(in.cornerAttrs.get(), keepCorner);
    out.cornerGroups = gatherGroups(in.cornerGroups.get(), keepCorner);
    out.faceAttrs = gatherAttrs(in.faceAttrs.get(), keepFace);
    out.faceGroups = gatherGroups(in.faceGroups.get(), keepFace);
}

Value opDelete(const BoundCall& bound, RunContext& run) {
    const GeoPtr inPtr = asGeo(bound.values[0]);
    const Geo& in = *inPtr;
    const Domain domain = domainFromName(asString(bound.values[2]));
    if (domain == Domain::Detail) {
        run.report("E204", bound.span, "delete: detail is not a per-element domain",
                   "mask points, corners or faces instead");
        return Value(inPtr);
    }
    const bool hasFaces = in.kind == GeoKind::Mesh && in.faceOffsets && in.cornerVerts;
    if (domain != Domain::Points && !hasFaces) {
        run.report("E204", bound.span,
                   std::string("delete on ") + domainName(domain) + " needs a geo<mesh>; " +
                       geoKindName(in.kind) + " has only points",
                   "use domain = points");
        return Value(inPtr);
    }

    ConstBufferPtr where = convertBuffer(evalField(bound.fields[1], in, domain, run), ScalarType::Bool);
    const std::vector<uint8_t>& drop = std::get<BoolBuf>(*where);
    if (drop.size() != in.elementCount(domain)) return Value(inPtr);  // field error already reported
    if (std::find(drop.begin(), drop.end(), uint8_t(1)) == drop.end()) return Value(inPtr);

    Geo out = in;
    if (domain == Domain::Points) {
        const std::vector<int32_t> keep = keptIndices(drop);
        std::vector<int32_t> remap(in.pointCount(), -1);
        for (size_t i = 0; i < keep.size(); ++i) remap[static_cast<size_t>(keep[i])] = static_cast<int32_t>(i);
        using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;
        out.positions = std::get<Vec3Col>(gatherColumn(ColumnData(in.positions), keep));
        if (in.normals) out.normals = std::get<Vec3Col>(gatherColumn(ColumnData(in.normals), keep));
        out.pointAttrs = gatherAttrs(in.pointAttrs.get(), keep);
        out.pointGroups = gatherGroups(in.pointGroups.get(), keep);
        if (hasFaces) {
            // A face dies with any of its points.
            const auto& offsets = *in.faceOffsets;
            const auto& verts = *in.cornerVerts;
            std::vector<uint8_t> dropFace(in.faceCount(), 0);
            for (size_t f = 0; f + 1 < offsets.size(); ++f)
                for (int32_t c = offsets[f]; c < offsets[f + 1] && !dropFace[f]; ++c)
                    if (drop[static_cast<size_t>(verts[static_cast<size_t>(c)])]) dropFace[f] = 1;
            rebuildFaces(in, out, dropFace, &remap);
        }
    } else if (domain == Domain::Faces) {
        rebuildFaces(in, out, drop, nullptr);
    } else {  // corners: a face dies with any of its corners
        const auto& offsets = *in.faceOffsets;
        std::vector<uint8_t> dropFace(in.faceCount(), 0);
        for (size_t f = 0; f + 1 < offsets.size(); ++f)
            for (int32_t c = offsets[f]; c < offsets[f + 1] && !dropFace[f]; ++c)
                if (drop[static_cast<size_t>(c)]) dropFace[f] = 1;
        rebuildFaces(in, out, dropFace, nullptr);
    }
    return Value(std::make_shared<const Geo>(std::move(out)));
}

}  // namespace

Value evalTopologyBuiltin(const BoundCall& bound, RunContext& run) {
    switch (bound.sig->id) {
        case BuiltinId::Delete:
            return opDelete(bound, run);
        default:
            return Value();
    }
}

}  // namespace pgg
