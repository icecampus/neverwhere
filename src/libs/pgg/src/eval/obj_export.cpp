#include "pch.h"

#include "obj_export.h"

#include <fstream>
#include <optional>
#include <variant>

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

}  // namespace

namespace pgg {

bool writeObj(const std::string& path, const Geo& geo, std::string* err) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write " + path;
        return false;
    }
    out << "# PggTool run export\n";
    const std::optional<Domain> cdDomain = colorDomain(geo);
    const bool unweld = geo.kind == GeoKind::Mesh && cdDomain && *cdDomain != Domain::Points;
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
            shaded[i] = (Cd ? (*Cd)[i] : glm::vec3(0.66f, 0.64f, 0.61f)) * std::clamp((*ao)[i], 0.0f, 1.0f);
        Cd = std::make_shared<const std::vector<glm::vec3>>(std::move(shaded));
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

}  // namespace pgg
