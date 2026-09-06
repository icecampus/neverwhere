#include "../../pch.h"

#include "geometry.h"

#include <cmath>
#include <map>
#include <utility>

namespace pgg {

const char* geoKindName(GeoKind kind) {
    switch (kind) {
        case GeoKind::Mesh: return "mesh";
        case GeoKind::Points: return "points";
        case GeoKind::Instances: return "instances";
        default: return "geo";
    }
}

const char* domainName(Domain domain) {
    switch (domain) {
        case Domain::Points: return "points";
        case Domain::Corners: return "corners";
        case Domain::Faces: return "faces";
        case Domain::Detail: return "detail";
    }
    return "?";
}

Domain domainFromName(const std::string& name) {
    if (name == "points") return Domain::Points;
    if (name == "corners") return Domain::Corners;
    if (name == "faces") return Domain::Faces;
    return Domain::Detail;
}

const char* attrTypeInfoName(AttrTypeInfo info) {
    switch (info) {
        case AttrTypeInfo::Vector: return "vector";
        case AttrTypeInfo::Normal: return "normal";
        case AttrTypeInfo::Point: return "point";
        case AttrTypeInfo::Quaternion: return "quaternion";
        default: return "none";
    }
}

std::optional<AttrTypeInfo> attrTypeInfoFromName(const std::string& name) {
    if (name == "none") return AttrTypeInfo::None;
    if (name == "vector") return AttrTypeInfo::Vector;
    if (name == "normal") return AttrTypeInfo::Normal;
    if (name == "point") return AttrTypeInfo::Point;
    if (name == "quaternion") return AttrTypeInfo::Quaternion;
    return std::nullopt;
}

namespace {
bool isVec3Col(const ColumnData& v) { return std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(v); }
bool isVec4Col(const ColumnData& v) { return std::holds_alternative<std::shared_ptr<const std::vector<glm::vec4>>>(v); }
bool isF32Col(const ColumnData& v) { return std::holds_alternative<std::shared_ptr<const std::vector<float>>>(v); }
bool isIntCol(const ColumnData& v) { return std::holds_alternative<std::shared_ptr<const std::vector<int64_t>>>(v); }
}  // namespace

std::optional<AttrTypeInfo> inferAttrTypeInfo(const std::string& name, const ColumnData& value) {
    if (name == "N") return AttrTypeInfo::Normal;
    if (name == "orient") return AttrTypeInfo::Quaternion;
    if (name == "tint" || name == "color" || name == "Cd") return AttrTypeInfo::None;
    if (isVec3Col(value) || isVec4Col(value)) return std::nullopt;
    return AttrTypeInfo::None;
}

bool attrTypeInfoFits(AttrTypeInfo info, const ColumnData& value) {
    switch (info) {
        case AttrTypeInfo::Vector:
        case AttrTypeInfo::Normal:
        case AttrTypeInfo::Point: return isVec3Col(value);
        case AttrTypeInfo::Quaternion: return isVec4Col(value);
        default: return true;
    }
}

const char* reservedAttrTypeName(const std::string& name) {
    if (name == "orient") return "vec4";
    if (name == "tint") return "vec3";
    if (name == "scale") return "f32";
    if (name == "variant") return "int";
    return nullptr;
}

bool reservedAttrTypeFits(const std::string& name, const ColumnData& value) {
    if (name == "orient") return isVec4Col(value);
    if (name == "tint") return isVec3Col(value);
    if (name == "scale") return isF32Col(value) || isIntCol(value);
    if (name == "variant") return isIntCol(value);
    return true;
}

size_t AttrColumn::size() const {
    return std::visit([](const auto& ptr) { return ptr ? ptr->size() : size_t(0); }, data);
}

const AttrColumn* AttrSet::find(const std::string& name) const {
    auto it = columns.find(name);
    return it == columns.end() ? nullptr : &it->second;
}

ConstBoolColumnPtr GroupSet::find(const std::string& name) const {
    auto it = columns.find(name);
    return it == columns.end() ? nullptr : it->second;
}

size_t Geo::elementCount(Domain domain) const {
    switch (domain) {
        case Domain::Points: return pointCount();
        case Domain::Corners: return cornerCount();
        case Domain::Faces: return faceCount();
        case Domain::Detail: return 1;
    }
    return 0;
}

const AttrSet* Geo::attrs(Domain domain) const {
    switch (domain) {
        case Domain::Points: return pointAttrs.get();
        case Domain::Corners: return cornerAttrs.get();
        case Domain::Faces: return faceAttrs.get();
        case Domain::Detail: return detailAttrs.get();
    }
    return nullptr;
}

const GroupSet* Geo::groups(Domain domain) const {
    switch (domain) {
        case Domain::Points: return pointGroups.get();
        case Domain::Corners: return cornerGroups.get();
        case Domain::Faces: return faceGroups.get();
        case Domain::Detail: return detailGroups.get();
    }
    return nullptr;
}

GeoPtr makeMesh(std::vector<glm::vec3> positions,
                std::vector<int32_t> cornerVerts,
                std::vector<int32_t> faceOffsets) {
    Geo geo;
    geo.kind = GeoKind::Mesh;
    geo.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(positions));
    geo.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(cornerVerts));
    geo.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(faceOffsets));
    return std::make_shared<const Geo>(std::move(geo));
}

GeoPtr makePoints(std::vector<glm::vec3> positions) {
    Geo geo;
    geo.kind = GeoKind::Points;
    geo.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(positions));
    return std::make_shared<const Geo>(std::move(geo));
}

GeoPtr withPositions(const Geo& geo, std::vector<glm::vec3> positions) {
    Geo out = geo;
    out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(positions));
    return std::make_shared<const Geo>(std::move(out));
}

GeoPtr withNormals(const Geo& geo, std::vector<glm::vec3> normals) {
    Geo out = geo;
    out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(normals));
    return std::make_shared<const Geo>(std::move(out));
}

GeoPtr withAttrs(const Geo& geo, Domain domain, std::shared_ptr<const AttrSet> attrs) {
    Geo out = geo;
    switch (domain) {
        case Domain::Points: out.pointAttrs = std::move(attrs); break;
        case Domain::Corners: out.cornerAttrs = std::move(attrs); break;
        case Domain::Faces: out.faceAttrs = std::move(attrs); break;
        case Domain::Detail: out.detailAttrs = std::move(attrs); break;
    }
    return std::make_shared<const Geo>(std::move(out));
}

GeoPtr withGroups(const Geo& geo, Domain domain, std::shared_ptr<const GroupSet> groups) {
    Geo out = geo;
    switch (domain) {
        case Domain::Points: out.pointGroups = std::move(groups); break;
        case Domain::Corners: out.cornerGroups = std::move(groups); break;
        case Domain::Faces: out.faceGroups = std::move(groups); break;
        case Domain::Detail: out.detailGroups = std::move(groups); break;
    }
    return std::make_shared<const Geo>(std::move(out));
}

bool attrExistsOnAnyDomain(const Geo& geo, const std::string& name) {
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* attrs = geo.attrs(d);
        if (attrs && attrs->find(name)) return true;
    }
    return false;
}

bool groupExistsOnAnyDomain(const Geo& geo, const std::string& name) {
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const GroupSet* groups = geo.groups(d);
        if (groups && groups->find(name)) return true;
    }
    return false;
}

// --- domain interpolation matrix (spec §4.3) ----------------------------------

namespace {

// Untyped double-wide working column: interpolation runs once for all element
// types; the typed load/store at the edges keeps the source type (bool
// thresholds at > 0.5, int rounds to nearest).
struct DColumn {
    int width = 1;
    std::vector<glm::dvec4> vals;
};

DColumn loadDColumn(const ColumnData& data) {
    return std::visit(
        [](const auto& ptr) -> DColumn {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            DColumn out;
            const size_t n = ptr ? ptr->size() : 0;
            out.vals.resize(n, glm::dvec4(0.0));
            if constexpr (std::is_same_v<ElemT, float>) {
                out.width = 1;
                for (size_t i = 0; i < n; ++i) out.vals[i].x = (*ptr)[i];
            } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                out.width = 1;
                for (size_t i = 0; i < n; ++i) out.vals[i].x = static_cast<double>((*ptr)[i]);
            } else if constexpr (std::is_same_v<ElemT, uint8_t>) {
                out.width = 1;
                for (size_t i = 0; i < n; ++i) out.vals[i].x = (*ptr)[i] ? 1.0 : 0.0;
            } else if constexpr (std::is_same_v<ElemT, glm::vec2>) {
                out.width = 2;
                for (size_t i = 0; i < n; ++i)
                    for (int k = 0; k < 2; ++k) out.vals[i][k] = (*ptr)[i][k];
            } else if constexpr (std::is_same_v<ElemT, glm::vec3>) {
                out.width = 3;
                for (size_t i = 0; i < n; ++i)
                    for (int k = 0; k < 3; ++k) out.vals[i][k] = (*ptr)[i][k];
            } else if constexpr (std::is_same_v<ElemT, glm::vec4>) {
                out.width = 4;
                for (size_t i = 0; i < n; ++i)
                    for (int k = 0; k < 4; ++k) out.vals[i][k] = (*ptr)[i][k];
            } else {  // string: never interpolated
                out.width = 0;
            }
            return out;
        },
        data);
}

ColumnData storeDColumn(const DColumn& col, const ColumnData& typeOf) {
    return std::visit(
        [&](const auto& ptr) -> ColumnData {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            const size_t n = col.vals.size();
            if constexpr (std::is_same_v<ElemT, float>) {
                std::vector<float> out(n);
                for (size_t i = 0; i < n; ++i) out[i] = static_cast<float>(col.vals[i].x);
                return std::make_shared<const std::vector<float>>(std::move(out));
            } else if constexpr (std::is_same_v<ElemT, int64_t>) {
                std::vector<int64_t> out(n);
                for (size_t i = 0; i < n; ++i) out[i] = static_cast<int64_t>(std::llround(col.vals[i].x));
                return std::make_shared<const std::vector<int64_t>>(std::move(out));
            } else if constexpr (std::is_same_v<ElemT, uint8_t>) {
                std::vector<uint8_t> out(n);
                for (size_t i = 0; i < n; ++i) out[i] = col.vals[i].x > 0.5 ? 1 : 0;
                return std::make_shared<const std::vector<uint8_t>>(std::move(out));
            } else if constexpr (std::is_same_v<ElemT, glm::vec2>) {
                std::vector<glm::vec2> out(n);
                for (size_t i = 0; i < n; ++i)
                    out[i] = glm::vec2(static_cast<float>(col.vals[i].x), static_cast<float>(col.vals[i].y));
                return std::make_shared<const std::vector<glm::vec2>>(std::move(out));
            } else if constexpr (std::is_same_v<ElemT, glm::vec3>) {
                std::vector<glm::vec3> out(n);
                for (size_t i = 0; i < n; ++i)
                    out[i] = glm::vec3(static_cast<float>(col.vals[i].x), static_cast<float>(col.vals[i].y),
                                       static_cast<float>(col.vals[i].z));
                return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
            } else if constexpr (std::is_same_v<ElemT, glm::vec4>) {
                std::vector<glm::vec4> out(n);
                for (size_t i = 0; i < n; ++i)
                    out[i] = glm::vec4(static_cast<float>(col.vals[i].x), static_cast<float>(col.vals[i].y),
                                       static_cast<float>(col.vals[i].z), static_cast<float>(col.vals[i].w));
                return std::make_shared<const std::vector<glm::vec4>>(std::move(out));
            } else {
                return typeOf;  // string: unreachable (callers short-circuit)
            }
        },
        typeOf);
}

// corner -> owning face lookup, built on demand.
std::vector<int32_t> cornerFaceMap(const Geo& geo) {
    std::vector<int32_t> map(geo.cornerCount(), 0);
    for (size_t f = 0; f < geo.faceCount(); ++f)
        for (int32_t c = (*geo.faceOffsets)[f]; c < (*geo.faceOffsets)[f + 1]; ++c)
            map[static_cast<size_t>(c)] = static_cast<int32_t>(f);
    return map;
}

// Reduce `vals` (indexed by selector, e.g. corners of a face) with the mode.
glm::dvec4 reduceVals(const std::vector<glm::dvec4>& vals, const std::vector<int32_t>& sel,
                      PromoteMode mode) {
    glm::dvec4 acc(0.0);
    if (sel.empty()) return acc;
    if (mode == PromoteMode::First) return vals[static_cast<size_t>(sel.front())];
    for (int32_t s : sel) acc += vals[static_cast<size_t>(s)];
    if (mode == PromoteMode::Average) acc /= static_cast<double>(sel.size());
    return acc;
}

// The conversion matrix. from == to is a copy; expanding conversions
// (detail -> *, points -> corners, faces -> corners) copy/broadcast for every
// mode; reducing conversions apply the mode.
std::vector<glm::dvec4> interpolateDColumn(const Geo& geo, Domain from,
                                           const std::vector<glm::dvec4>& src, Domain to,
                                           PromoteMode mode) {
    if (from == to) return src;
    const size_t nTo = geo.elementCount(to);
    if (from == Domain::Detail) {
        const glm::dvec4 v = src.empty() ? glm::dvec4(0.0) : src[0];
        return std::vector<glm::dvec4>(nTo, v);
    }
    if (to == Domain::Detail) {
        glm::dvec4 acc(0.0);
        if (!src.empty()) {
            if (mode == PromoteMode::First) {
                acc = src[0];
            } else {
                for (const glm::dvec4& v : src) acc += v;
                if (mode == PromoteMode::Average) acc /= static_cast<double>(src.size());
            }
        }
        return {acc};
    }

    const bool hasTopo = geo.cornerVerts && geo.faceOffsets;
    auto incidentCorners = [&](int32_t point) {
        std::vector<int32_t> sel;
        if (hasTopo)
            for (size_t c = 0; c < geo.cornerVerts->size(); ++c)
                if ((*geo.cornerVerts)[c] == point) sel.push_back(static_cast<int32_t>(c));
        return sel;
    };

    std::vector<glm::dvec4> out(nTo, glm::dvec4(0.0));
    if (from == Domain::Points && to == Domain::Corners) {
        for (size_t c = 0; c < nTo; ++c)
            out[c] = src[static_cast<size_t>((*geo.cornerVerts)[c])];
        return out;
    }
    if (from == Domain::Corners && to == Domain::Faces) {
        for (size_t f = 0; f < geo.faceCount(); ++f) {
            std::vector<int32_t> sel;
            for (int32_t c = (*geo.faceOffsets)[f]; c < (*geo.faceOffsets)[f + 1]; ++c) sel.push_back(c);
            out[f] = reduceVals(src, sel, mode);
        }
        return out;
    }
    if (from == Domain::Faces && to == Domain::Corners) {
        const std::vector<int32_t> faceOf = cornerFaceMap(geo);
        for (size_t c = 0; c < nTo; ++c) out[c] = src[static_cast<size_t>(faceOf[c])];
        return out;
    }
    if (from == Domain::Corners && to == Domain::Points) {
        for (size_t p = 0; p < nTo; ++p) out[p] = reduceVals(src, incidentCorners(static_cast<int32_t>(p)), mode);
        return out;
    }
    if (from == Domain::Points && to == Domain::Faces) {
        for (size_t f = 0; f < geo.faceCount(); ++f) {
            std::vector<int32_t> sel;
            for (int32_t c = (*geo.faceOffsets)[f]; c < (*geo.faceOffsets)[f + 1]; ++c)
                sel.push_back((*geo.cornerVerts)[c]);
            out[f] = reduceVals(src, sel, mode);
        }
        return out;
    }
    // Faces -> Points: corner-wise copy, then the corners -> points reduction.
    const std::vector<int32_t> faceOf = cornerFaceMap(geo);
    std::vector<glm::dvec4> viaCorners(geo.cornerCount());
    for (size_t c = 0; c < viaCorners.size(); ++c) viaCorners[c] = src[static_cast<size_t>(faceOf[c])];
    for (size_t p = 0; p < nTo; ++p)
        out[p] = reduceVals(viaCorners, incidentCorners(static_cast<int32_t>(p)), mode);
    return out;
}

// Finds the attribute on any domain (the first in points/corners/faces/detail
// order wins) and resamples it. String columns are same-domain only.
std::optional<ColumnData> resampleAttr(const Geo& geo, const std::string& name, Domain domain,
                                       Domain* foundDomain, PromoteMode mode) {
    for (Domain from : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* attrs = geo.attrs(from);
        const AttrColumn* col = attrs ? attrs->find(name) : nullptr;
        if (!col) continue;
        if (foundDomain) *foundDomain = from;
        if (std::holds_alternative<std::shared_ptr<const std::vector<std::string>>>(col->data)) {
            if (from == domain) return col->data;
            return std::nullopt;  // strings do not interpolate
        }
        if (from == domain) return col->data;
        DColumn dc = loadDColumn(col->data);
        dc.vals = interpolateDColumn(geo, from, dc.vals, domain, mode);
        return storeDColumn(dc, col->data);
    }
    return std::nullopt;
}

}  // namespace

std::optional<ColumnData> sampleAttrColumn(const Geo& geo, const std::string& name, Domain domain) {
    return resampleAttr(geo, name, domain, nullptr, PromoteMode::Average);
}

std::optional<ColumnData> promoteAttrColumn(const Geo& geo, const std::string& name, Domain from,
                                            Domain to, PromoteMode mode) {
    const AttrSet* attrs = geo.attrs(from);
    const AttrColumn* col = attrs ? attrs->find(name) : nullptr;
    if (!col) return std::nullopt;
    if (std::holds_alternative<std::shared_ptr<const std::vector<std::string>>>(col->data)) {
        if (from == to) return col->data;
        return std::nullopt;  // strings do not interpolate
    }
    if (from == to) return col->data;
    DColumn dc = loadDColumn(col->data);
    dc.vals = interpolateDColumn(geo, from, dc.vals, to, mode);
    return storeDColumn(dc, col->data);
}

ConstBoolColumnPtr sampleGroupColumn(const Geo& geo, const std::string& name, Domain domain) {
    for (Domain from : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const GroupSet* groups = geo.groups(from);
        ConstBoolColumnPtr col = groups ? groups->find(name) : nullptr;
        if (!col) continue;
        if (from == domain) return col;
        DColumn dc;
        dc.width = 1;
        dc.vals.resize(col->size(), glm::dvec4(0.0));
        for (size_t i = 0; i < col->size(); ++i) dc.vals[i].x = (*col)[i] ? 1.0 : 0.0;
        dc.vals = interpolateDColumn(geo, from, dc.vals, domain, PromoteMode::Average);
        BoolColumn out(dc.vals.size());
        for (size_t i = 0; i < dc.vals.size(); ++i) out[i] = dc.vals[i].x > 0.5 ? 1 : 0;
        return std::make_shared<const BoolColumn>(std::move(out));
    }
    return nullptr;
}

std::shared_ptr<const std::vector<glm::vec3>> samplePositions(const Geo& geo, Domain domain) {
    if (!geo.positions) return nullptr;
    if (domain == Domain::Points) return geo.positions;
    DColumn dc;
    dc.width = 3;
    dc.vals.resize(geo.positions->size(), glm::dvec4(0.0));
    for (size_t i = 0; i < geo.positions->size(); ++i) {
        const glm::vec3& p = (*geo.positions)[i];
        dc.vals[i] = glm::dvec4(p.x, p.y, p.z, 0.0);
    }
    dc.vals = interpolateDColumn(geo, Domain::Points, dc.vals, domain, PromoteMode::Average);
    std::vector<glm::vec3> out(dc.vals.size());
    for (size_t i = 0; i < dc.vals.size(); ++i)
        out[i] = glm::vec3(static_cast<float>(dc.vals[i].x), static_cast<float>(dc.vals[i].y),
                           static_cast<float>(dc.vals[i].z));
    return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
}

std::shared_ptr<const std::vector<glm::vec3>> sampleNormals(const Geo& geo, Domain domain) {
    // A corner "N" column (compute_normals(mode = flat)) is the nearest source
    // for corner and face reads: faceted @N on corners, the exact face
    // direction on faces. Point reads keep the smooth point normals.
    if (domain != Domain::Points && geo.cornerAttrs) {
        if (const AttrColumn* cn = geo.cornerAttrs->find("N")) {
            const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&cn->data);
            if (vec && *vec) {
                if (domain == Domain::Corners) return *vec;
                DColumn dc;
                dc.width = 3;
                dc.vals.resize((*vec)->size(), glm::dvec4(0.0));
                for (size_t i = 0; i < (*vec)->size(); ++i) {
                    const glm::vec3& n = (**vec)[i];
                    dc.vals[i] = glm::dvec4(n.x, n.y, n.z, 0.0);
                }
                dc.vals = interpolateDColumn(geo, Domain::Corners, dc.vals, domain, PromoteMode::Average);
                std::vector<glm::vec3> out(dc.vals.size());
                for (size_t i = 0; i < dc.vals.size(); ++i)
                    out[i] = glm::vec3(static_cast<float>(dc.vals[i].x), static_cast<float>(dc.vals[i].y),
                                       static_cast<float>(dc.vals[i].z));
                return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
            }
        }
    }
    if (!geo.normals) return derivedNormals(geo, domain);  // v1.14 read fallback
    if (domain == Domain::Points) return geo.normals;
    DColumn dc;
    dc.width = 3;
    dc.vals.resize(geo.normals->size(), glm::dvec4(0.0));
    for (size_t i = 0; i < geo.normals->size(); ++i) {
        const glm::vec3& n = (*geo.normals)[i];
        dc.vals[i] = glm::dvec4(n.x, n.y, n.z, 0.0);
    }
    dc.vals = interpolateDColumn(geo, Domain::Points, dc.vals, domain, PromoteMode::Average);
    std::vector<glm::vec3> out(dc.vals.size());
    for (size_t i = 0; i < dc.vals.size(); ++i)
        out[i] = glm::vec3(static_cast<float>(dc.vals[i].x), static_cast<float>(dc.vals[i].y),
                           static_cast<float>(dc.vals[i].z));
    return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
}

// --- column utilities (merge/realize union semantics) -------------------------

std::shared_ptr<const std::vector<glm::vec3>> derivedNormals(const Geo& geo, Domain domain) {
    if (geo.kind != GeoKind::Mesh || !geo.cornerVerts || !geo.faceOffsets || geo.faceCount() == 0)
        return nullptr;
    const size_t nf = geo.faceCount();
    const std::vector<int32_t>& CV = *geo.cornerVerts;
    const std::vector<int32_t>& FO = *geo.faceOffsets;
    std::vector<glm::vec3> faceN(nf);
    for (size_t f = 0; f < nf; ++f) {
        const glm::vec3 n = faceNormal(geo, f);
        const float len = glm::length(n);
        faceN[f] = len > 1e-20f ? n / len : glm::vec3(0.0f);
    }
    std::vector<glm::vec3> out;
    switch (domain) {
        case Domain::Faces: out = std::move(faceN); break;
        case Domain::Corners:
            out.resize(geo.cornerCount());
            for (size_t f = 0; f < nf; ++f)
                for (int32_t c = FO[f]; c < FO[f + 1]; ++c) out[static_cast<size_t>(c)] = faceN[f];
            break;
        case Domain::Points:
        case Domain::Detail: {
            std::vector<glm::vec3> acc(geo.pointCount(), glm::vec3(0.0f));
            for (size_t f = 0; f < nf; ++f)
                for (int32_t c = FO[f]; c < FO[f + 1]; ++c) acc[static_cast<size_t>(CV[c])] += faceN[f];
            for (glm::vec3& n : acc) {
                const float len = glm::length(n);
                n = len > 1e-20f ? n / len : glm::vec3(0.0f);
            }
            if (domain == Domain::Points) {
                out = std::move(acc);
            } else {
                glm::vec3 sum(0.0f);
                for (const glm::vec3& n : acc) sum += n;
                out.assign(1, acc.empty() ? glm::vec3(0.0f) : sum / static_cast<float>(acc.size()));
            }
            break;
        }
    }
    return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
}

ColumnData neutralColumnLike(const std::string& name, const AttrColumn& typeOf, const Geo& forGeo,
                             Domain domain, size_t count) {
    if (typeOf.typeInfo == AttrTypeInfo::Quaternion &&
        std::holds_alternative<std::shared_ptr<const std::vector<glm::vec4>>>(typeOf.data))
        return std::make_shared<const std::vector<glm::vec4>>(count, glm::vec4(0, 0, 0, 1));
    if (name == "tint" && std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(typeOf.data))
        return std::make_shared<const std::vector<glm::vec3>>(count, glm::vec3(1.0f));
    if (typeOf.typeInfo == AttrTypeInfo::Normal &&
        std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(typeOf.data)) {
        if (auto n = derivedNormals(forGeo, domain); n && n->size() == count) return n;
    }
    return zeroColumnLike(typeOf.data, count);
}

ColumnData transformColumn(const AttrColumn& col, const glm::quat& rot, const glm::vec3& scale,
                           const glm::vec3& translate) {
    using Vec3Col = std::shared_ptr<const std::vector<glm::vec3>>;
    using Vec4Col = std::shared_ptr<const std::vector<glm::vec4>>;
    switch (col.typeInfo) {
        case AttrTypeInfo::Vector:
        case AttrTypeInfo::Normal:
        case AttrTypeInfo::Point: {
            if (!std::holds_alternative<Vec3Col>(col.data)) return col.data;
            const std::vector<glm::vec3>& src = *std::get<Vec3Col>(col.data);
            std::vector<glm::vec3> out(src.size());
            if (col.typeInfo == AttrTypeInfo::Normal) {
                const bool safeScale = glm::abs(scale.x) > 1e-12f && glm::abs(scale.y) > 1e-12f &&
                                       glm::abs(scale.z) > 1e-12f;
                for (size_t i = 0; i < out.size(); ++i) {
                    const glm::vec3 rn = rot * (safeScale ? src[i] / scale : src[i]);
                    const float len = glm::length(rn);
                    out[i] = len > 0.0f ? rn / len : glm::vec3(0.0f);
                }
            } else if (col.typeInfo == AttrTypeInfo::Vector) {
                for (size_t i = 0; i < out.size(); ++i) out[i] = rot * (src[i] * scale);
            } else {
                for (size_t i = 0; i < out.size(); ++i) out[i] = rot * (src[i] * scale) + translate;
            }
            return std::make_shared<const std::vector<glm::vec3>>(std::move(out));
        }
        case AttrTypeInfo::Quaternion: {
            if (!std::holds_alternative<Vec4Col>(col.data)) return col.data;
            const std::vector<glm::vec4>& src = *std::get<Vec4Col>(col.data);
            std::vector<glm::vec4> out(src.size());
            for (size_t i = 0; i < out.size(); ++i) {
                const glm::quat q = rot * glm::quat(src[i].w, src[i].x, src[i].y, src[i].z);
                out[i] = glm::vec4(q.x, q.y, q.z, q.w);
            }
            return std::make_shared<const std::vector<glm::vec4>>(std::move(out));
        }
        default: return col.data;
    }
}

ColumnData zeroColumnLike(const ColumnData& typeOf, size_t count) {
    return std::visit(
        [count](const auto& ptr) -> ColumnData {
            using VecT = std::decay_t<decltype(*ptr)>;
            using ElemT = typename VecT::value_type;
            return std::make_shared<const VecT>(count, ElemT{});
        },
        typeOf);
}

ColumnData convertColumn(const ColumnData& src, const ColumnData& typeOf) {
    if (src.index() == typeOf.index()) return src;
    const bool srcStr = std::holds_alternative<std::shared_ptr<const std::vector<std::string>>>(src);
    const bool dstStr =
        std::holds_alternative<std::shared_ptr<const std::vector<std::string>>>(typeOf);
    if (srcStr || dstStr) return zeroColumnLike(typeOf, AttrColumn{src}.size());
    DColumn dc = loadDColumn(src);
    return storeDColumn(dc, typeOf);
}

ColumnData concatColumns(const ColumnData& a, const ColumnData& b) {
    const ColumnData cb = b.index() == a.index() ? b : convertColumn(b, a);
    return std::visit(
        [&](const auto& pa) -> ColumnData {
            using VecT = std::decay_t<decltype(*pa)>;
            const auto& pb = std::get<std::shared_ptr<const VecT>>(cb);
            VecT out;
            out.reserve((pa ? pa->size() : 0) + (pb ? pb->size() : 0));
            if (pa) out.insert(out.end(), pa->begin(), pa->end());
            if (pb) out.insert(out.end(), pb->begin(), pb->end());
            return std::make_shared<const VecT>(std::move(out));
        },
        a);
}

ColumnData gatherColumn(const ColumnData& col, const std::vector<int32_t>& indices) {
    return std::visit(
        [&](const auto& ptr) -> ColumnData {
            using VecT = std::decay_t<decltype(*ptr)>;
            VecT out;
            out.reserve(indices.size());
            for (int32_t i : indices) out.push_back((*ptr)[static_cast<size_t>(i)]);
            return std::make_shared<const VecT>(std::move(out));
        },
        col);
}

void geoBBox(const Geo& geo, glm::vec3& outMin, glm::vec3& outMax) {
    outMin = glm::vec3(0.0f);
    outMax = glm::vec3(0.0f);
    if (!geo.positions || geo.positions->empty()) return;
    outMin = outMax = (*geo.positions)[0];
    for (const glm::vec3& p : *geo.positions) {
        outMin = glm::min(outMin, p);
        outMax = glm::max(outMax, p);
    }
}

glm::vec3 faceNormal(const Geo& geo, size_t face) {
    glm::vec3 n(0.0f);
    if (!geo.cornerVerts || !geo.faceOffsets || face + 1 >= geo.faceOffsets->size()) return n;
    const int32_t begin = (*geo.faceOffsets)[face];
    const int32_t end = (*geo.faceOffsets)[face + 1];
    const std::vector<glm::vec3>& pos = *geo.positions;
    for (int32_t c = begin; c < end; ++c) {
        const glm::vec3& a = pos[(*geo.cornerVerts)[c]];
        const glm::vec3& b = pos[(*geo.cornerVerts)[c + 1 < end ? c + 1 : begin]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}

size_t nonManifoldEdgeCount(const Geo& geo) {
    std::map<std::pair<int32_t, int32_t>, int> incidence;
    if (!geo.cornerVerts || !geo.faceOffsets) return 0;
    for (size_t f = 0; f < geo.faceCount(); ++f) {
        const int32_t begin = (*geo.faceOffsets)[f];
        const int32_t end = (*geo.faceOffsets)[f + 1];
        for (int32_t c = begin; c < end; ++c) {
            int32_t a = (*geo.cornerVerts)[c];
            int32_t b = (*geo.cornerVerts)[c + 1 < end ? c + 1 : begin];
            if (a > b) std::swap(a, b);
            incidence[{a, b}] += 1;
        }
    }
    size_t bad = 0;
    for (const auto& [edge, count] : incidence)
        if (count != 2) bad += 1;
    return bad;
}

float pointTriangleDistance(const glm::vec3& p, const glm::vec3& a,
                            const glm::vec3& b, const glm::vec3& c) {
    // Closest point on triangle (Ericson, Real-Time Collision Detection §5.1.5).
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return glm::length(ap);

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return glm::length(bp);

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return glm::length(p - (a + ab * v));
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return glm::length(cp);

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return glm::length(p - (a + ac * w));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return glm::length(p - (b + (c - b) * w));
    }

    const float denom = 1.0f / (va + vb + vc);
    const glm::vec3 q = a + ab * (vb * denom) + ac * (vc * denom);
    return glm::length(p - q);
}

}  // namespace pgg
