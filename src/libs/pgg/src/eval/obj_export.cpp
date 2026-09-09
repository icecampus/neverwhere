#include "pch.h"

#include "obj_export.h"

#include <fstream>
#include <optional>
#include <variant>

#include "probe.h"          // classifyMeshIssueFaces (the check-coloring categories)
#include "topology_util.h"  // topo::gatherAttrs/gatherGroups for the group split

namespace {

// Domain the vec3 @Cd column lives on (spec §4.3 read order); nullopt when
// absent or not vec3.
std::optional<pgg::Domain> colorDomain(const pgg::Geo& geo) {
    for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces, pgg::Domain::Detail}) {
        const pgg::AttrSet* attrs = geo.attrs(d);
        const pgg::AttrColumn* col = attrs ? attrs->find("Cd") : nullptr;
        if (!col) continue;
        return std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(col->data)
                   ? std::optional<pgg::Domain>(d)
                   : std::nullopt;
    }
    return std::nullopt;
}

std::shared_ptr<const std::vector<glm::vec3>> vec3Column(const std::optional<pgg::ColumnData>& col, size_t count) {
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&*col);
    if (!vec || !*vec || (*vec)->size() != count) return nullptr;
    return *vec;
}

std::shared_ptr<const std::vector<float>> f32Column(const std::optional<pgg::ColumnData>& col, size_t count) {
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<float>>>(&*col);
    if (!vec || !*vec || (*vec)->size() != count) return nullptr;
    return *vec;
}

bool hasAttr(const pgg::Geo& g, const char* name) {
    for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces})
        if (const pgg::AttrSet* a = g.attrs(d); a && a->find(name)) return true;
    return false;
}

// The neutral surface gray (also the @Cd stand-in when @ao is baked into a
// colorless mesh) — and the check-coloring base for intact faces.
constexpr glm::vec3 kNeutralColor(0.66f, 0.64f, 0.61f);
// --obj-color=check flag colors (priority degenerate > nonmanifold > boundary).
constexpr glm::vec3 kCheckDegenerate(1.0f, 0.0f, 0.0f);
constexpr glm::vec3 kCheckNonmanifold(1.0f, 1.0f, 0.0f);
constexpr glm::vec3 kCheckBoundary(0.0f, 0.4f, 1.0f);

// Sub-mesh of the faces listed in `faces` (ascending): corners re-based,
// points compacted in first-appearance (corner) order, every attr/group
// column gathered per domain (same surgery as fracture's splitMeshPieces).
// nullptr for a faceless/broken input (no positions/corners/offsets).
pgg::GeoPtr extractMeshFaces(const pgg::Geo& mesh, const std::vector<int32_t>& faces) {
    if (!mesh.positions || !mesh.cornerVerts || !mesh.faceOffsets) return nullptr;
    std::vector<int32_t> cornerIdx;
    std::vector<int32_t> faceOffsets(1, 0);
    for (const int32_t f : faces) {
        const int32_t begin = (*mesh.faceOffsets)[static_cast<size_t>(f)];
        const int32_t end = (*mesh.faceOffsets)[static_cast<size_t>(f) + 1];
        for (int32_t c = begin; c < end; ++c) cornerIdx.push_back(c);
        faceOffsets.push_back(faceOffsets.back() + (end - begin));
    }
    std::vector<int32_t> remap(mesh.pointCount(), -1);
    std::vector<int32_t> pointIdx;
    std::vector<int32_t> cornerVerts(cornerIdx.size());
    for (size_t i = 0; i < cornerIdx.size(); ++i) {
        const int32_t p = (*mesh.cornerVerts)[static_cast<size_t>(cornerIdx[i])];
        if (remap[static_cast<size_t>(p)] < 0) {
            remap[static_cast<size_t>(p)] = static_cast<int32_t>(pointIdx.size());
            pointIdx.push_back(p);
        }
        cornerVerts[i] = remap[static_cast<size_t>(p)];
    }
    pgg::Geo out;
    out.kind = pgg::GeoKind::Mesh;
    out.positions = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
        pgg::gatherColumn(pgg::ColumnData(mesh.positions), pointIdx));
    if (mesh.normals)
        out.normals = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
            pgg::gatherColumn(pgg::ColumnData(mesh.normals), pointIdx));
    out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(cornerVerts));
    out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(faceOffsets));
    out.pointAttrs = pgg::topo::gatherAttrs(mesh.attrs(pgg::Domain::Points), pointIdx);
    out.cornerAttrs = pgg::topo::gatherAttrs(mesh.attrs(pgg::Domain::Corners), cornerIdx);
    out.faceAttrs = pgg::topo::gatherAttrs(mesh.attrs(pgg::Domain::Faces), faces);
    out.detailAttrs = mesh.detailAttrs;  // shared by pointer (§5.4)
    out.pointGroups = pgg::topo::gatherGroups(mesh.groups(pgg::Domain::Points), pointIdx);
    out.cornerGroups = pgg::topo::gatherGroups(mesh.groups(pgg::Domain::Corners), cornerIdx);
    out.faceGroups = pgg::topo::gatherGroups(mesh.groups(pgg::Domain::Faces), faces);
    out.detailGroups = mesh.detailGroups;
    return std::make_shared<const pgg::Geo>(std::move(out));
}

}  // namespace

namespace pgg {

bool writeObj(const std::string& path, const Geo& geo, std::string* err, const ObjExportOptions& opts) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    out << "# PggTool run export\n";
    // --obj-color=check needs face-level colors -> the corner domain (unweld).
    const bool checkColors = opts.checkColors && geo.kind == GeoKind::Mesh;
    const std::optional<Domain> cdDomain = colorDomain(geo);
    const bool unweld = geo.kind == GeoKind::Mesh && ((cdDomain && *cdDomain != Domain::Points) || checkColors);
    const Domain vdom = unweld ? Domain::Corners : Domain::Points;
    const size_t vcount = geo.elementCount(vdom);
    const std::shared_ptr<const std::vector<glm::vec3>> P = samplePositions(geo, vdom);
    std::shared_ptr<const std::vector<glm::vec3>> Cd =
        cdDomain ? vec3Column(sampleAttrColumn(geo, "Cd", vdom), vcount) : nullptr;
    // Baked occlusion (@ao, v1.24) is multiplied into the exported color: OBJ
    // has no separate AO channel, and the viewer applies it the same way.
    if (const auto ao = hasAttr(geo, "ao") ? f32Column(sampleAttrColumn(geo, "ao", vdom), vcount) : nullptr) {
        std::vector<glm::vec3> shaded(vcount);
        for (size_t i = 0; i < vcount; ++i)
            shaded[i] = (Cd ? (*Cd)[i] : kNeutralColor) * std::clamp((*ao)[i], 0.0f, 1.0f);
        Cd = std::make_shared<const std::vector<glm::vec3>>(std::move(shaded));
    }
    if (checkColors) {
        // Repaint the problem faces (classifyMeshIssueFaces — the same
        // categories the check inspector reports) over the @Cd/@ao base;
        // face corner ranges index the unwelded vertices directly.
        std::vector<glm::vec3> painted(vcount);
        for (size_t i = 0; i < vcount; ++i) painted[i] = Cd ? (*Cd)[i] : kNeutralColor;
        MeshIssueFaces issues;
        classifyMeshIssueFaces(geo, issues);
        const auto paint = [&](const std::vector<int32_t>& faces, const glm::vec3& c) {
            for (const int32_t f : faces)
                for (int32_t corner = (*geo.faceOffsets)[static_cast<size_t>(f)];
                     corner < (*geo.faceOffsets)[static_cast<size_t>(f) + 1]; ++corner)
                    painted[static_cast<size_t>(corner)] = c;
        };
        paint(issues.boundary, kCheckBoundary);        // lowest priority first
        paint(issues.nonmanifold, kCheckNonmanifold);
        paint(issues.degenerate, kCheckDegenerate);
        Cd = std::make_shared<const std::vector<glm::vec3>>(std::move(painted));
        out << "# check colors: degenerate red (1 0 0), nonmanifold yellow (1 1 0), boundary blue (0 0.4 1)"
            << " (degenerate " << issues.degenerate.size() << ", nonmanifold " << issues.nonmanifoldEdges
            << " edge(s), boundary " << issues.boundaryEdges << " edge(s))\n";
    }
    std::shared_ptr<const std::vector<glm::vec3>> N;
    if (geo.kind == GeoKind::Mesh) {
        const AttrSet* cattrs = geo.attrs(Domain::Corners);
        const AttrColumn* cornerN = cattrs ? cattrs->find("N") : nullptr;
        if (cornerN && unweld) N = vec3Column(cornerN->data, vcount);
        if (!N && geo.normals) N = sampleNormals(geo, vdom);
    }
    if (Cd) out << "# vertex colors: @Cd" << (cdDomain ? std::string(" on ") + domainName(*cdDomain) : std::string(" (neutral)"))
                << (hasAttr(geo, "ao") ? " x @ao" : "") << (unweld ? " (unwelded)" : "") << "\n";
    for (size_t i = 0; i < vcount; ++i) {
        const glm::vec3& p = (*P)[i];
        out << "v " << p.x << " " << p.y << " " << p.z;
        if (Cd) {
            const glm::vec3 c = glm::clamp((*Cd)[i], glm::vec3(0.0f), glm::vec3(1.0f));
            out << " " << c.x << " " << c.y << " " << c.z;
        }
        out << "\n";
    }
    if (N && N->size() == vcount)
        for (const glm::vec3& n : *N) out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
    else
        N = nullptr;
    if (geo.kind == GeoKind::Mesh) {
        // Fan triangulation of polygon faces, 1-based indices.
        auto vertex = [&](int32_t c) { return unweld ? c + 1 : (*geo.cornerVerts)[c] + 1; };
        auto emit = [&](int32_t c) {
            const int idx = vertex(c);
            out << " " << idx;
            if (N) out << "//" << idx;
        };
        for (size_t f = 0; f < geo.faceCount(); ++f) {
            const int32_t begin = (*geo.faceOffsets)[f];
            const int32_t end = (*geo.faceOffsets)[f + 1];
            for (int32_t c = begin + 1; c + 1 < end; ++c) {
                out << "f";
                emit(begin);
                emit(c);
                emit(c + 1);
                out << "\n";
            }
        }
    }
    if (!out) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    return true;
}

bool writeObjSplitGroups(const std::string& dir, const std::string& name, const Geo& geo,
                         std::vector<std::string>& written, std::string* err,
                         const ObjExportOptions& opts) {
    written.clear();
    const GroupSet* faceGroups = geo.groups(Domain::Faces);
    const size_t nf = geo.faceCount();
    if (geo.kind != GeoKind::Mesh || !faceGroups || faceGroups->columns.empty() || nf == 0 ||
        !geo.positions || !geo.cornerVerts || !geo.faceOffsets) {
        // No faces-groups: the split degenerates to the single-file export.
        const std::string path = dir + "/" + name + ".obj";
        if (!writeObj(path, geo, err, opts)) return false;
        written.push_back(path);
        return true;
    }
    std::vector<std::string> names;
    for (const auto& [n, col] : faceGroups->columns) names.push_back(n);
    std::sort(names.begin(), names.end());
    std::vector<uint8_t> covered(nf, 0);
    for (const std::string& group : names) {
        const ConstBoolColumnPtr col = faceGroups->find(group);
        if (!col || col->size() != nf) continue;
        std::vector<int32_t> faces;
        for (size_t f = 0; f < nf; ++f)
            if ((*col)[f]) {
                faces.push_back(static_cast<int32_t>(f));
                covered[f] = 1;
            }
        if (faces.empty()) continue;  // an empty group gets no file
        const GeoPtr sub = extractMeshFaces(geo, faces);
        const std::string path = dir + "/" + name + "." + group + ".obj";
        if (!sub || !writeObj(path, *sub, err, opts)) return false;
        written.push_back(path);
    }
    std::vector<int32_t> rest;
    for (size_t f = 0; f < nf; ++f)
        if (!covered[f]) rest.push_back(static_cast<int32_t>(f));
    if (!rest.empty()) {
        const GeoPtr sub = extractMeshFaces(geo, rest);
        const std::string path = dir + "/" + name + "._nogroup.obj";
        if (!sub || !writeObj(path, *sub, err, opts)) return false;
        written.push_back(path);
    }
    return true;
}

}  // namespace pgg
