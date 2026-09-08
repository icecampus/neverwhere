#include "../../pch.h"

#include "geo_diff.h"

#include <cstdio>
#include <map>

namespace pgg {
namespace {

const char* columnBaseName(const ColumnData& col) {
    switch (col.index()) {
        case 0: return "f32";
        case 1: return "int";
        case 2: return "bool";
        case 3: return "vec2";
        case 4: return "vec3";
        case 5: return "vec4";
        case 6: return "string";
        default: return "?";
    }
}

std::string columnTypeName(const ColumnData& col, AttrTypeInfo info) {
    std::string t = columnBaseName(col);
    if (info != AttrTypeInfo::None) {
        t += "/";
        t += attrTypeInfoName(info);
    }
    return t;
}

// (name, domain) -> type for every attribute column; a stored @N counts as a
// points attribute "N".
std::map<std::pair<std::string, Domain>, std::string> attrMap(const Geo& g) {
    std::map<std::pair<std::string, Domain>, std::string> out;
    for (Domain domain : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const AttrSet* set = g.attrs(domain);
        if (!set) continue;
        for (const auto& [name, col] : set->columns)
            out[{name, domain}] = columnTypeName(col.data, col.typeInfo);
    }
    if (g.normals) out[{"N", Domain::Points}] = "vec3/normal";
    return out;
}

std::map<std::pair<std::string, Domain>, bool> groupMap(const Geo& g) {
    std::map<std::pair<std::string, Domain>, bool> out;
    for (Domain domain : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        const GroupSet* set = g.groups(domain);
        if (!set) continue;
        for (const auto& [name, col] : set->columns) out[{name, domain}] = true;
    }
    return out;
}

// First faces group (sorted by name) whose mask covers any face incident to
// `point`; "-" when the point is in no faces group (or the mesh has none).
std::string facesGroupOfPoint(const Geo& g, size_t point) {
    if (!g.faceGroups || !g.cornerVerts || !g.faceOffsets) return "-";
    std::vector<size_t> incident;
    for (size_t f = 0; f < g.faceCount(); ++f) {
        const int32_t begin = (*g.faceOffsets)[f];
        const int32_t end = (*g.faceOffsets)[f + 1];
        for (int32_t c = begin; c < end; ++c) {
            if (static_cast<size_t>((*g.cornerVerts)[c]) == point) {
                incident.push_back(f);
                break;
            }
        }
    }
    if (incident.empty()) return "-";
    std::vector<const std::string*> names;
    for (const auto& [n, c] : g.faceGroups->columns) names.push_back(&n);
    std::sort(names.begin(), names.end(),
              [](const std::string* a, const std::string* b) { return *a < *b; });
    for (const std::string* n : names) {
        const BoolColumn& mask = *g.faceGroups->find(*n);
        for (size_t f : incident)
            if (f < mask.size() && mask[f]) return *n;
    }
    return "-";
}

std::string vec3Text(const glm::vec3& v) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "(%g, %g, %g)", v.x, v.y, v.z);
    return buf;
}

std::string bboxText(bool has, const glm::vec3& mn, const glm::vec3& mx) {
    if (!has) return "(empty)";
    return vec3Text(mn) + ".." + vec3Text(mx);
}

}  // namespace

GeoDiffResult diffGeo(const Geo& a, const Geo& b) {
    GeoDiffResult d;
    d.kindA = a.kind;
    d.kindB = b.kind;
    d.pointsA = a.pointCount();
    d.pointsB = b.pointCount();
    d.facesA = a.faceCount();
    d.facesB = b.faceCount();
    d.hasBBoxA = d.pointsA > 0;
    d.hasBBoxB = d.pointsB > 0;
    if (d.hasBBoxA) geoBBox(a, d.bboxMinA, d.bboxMaxA);
    if (d.hasBBoxB) geoBBox(b, d.bboxMinB, d.bboxMaxB);

    const auto attrsA = attrMap(a);
    const auto attrsB = attrMap(b);
    for (const auto& [key, type] : attrsA)
        if (!attrsB.count(key))
            d.attrs.push_back(GeoAttrDelta{key.first, key.second, type, false});
    for (const auto& [key, type] : attrsB)
        if (!attrsA.count(key))
            d.attrs.push_back(GeoAttrDelta{key.first, key.second, type, true});
    std::sort(d.attrs.begin(), d.attrs.end(), [](const GeoAttrDelta& x, const GeoAttrDelta& y) {
        return x.name != y.name ? x.name < y.name : x.domain < y.domain;
    });

    const auto groupsA = groupMap(a);
    const auto groupsB = groupMap(b);
    for (const auto& [key, _] : groupsA)
        if (!groupsB.count(key)) d.groups.push_back(GeoGroupDelta{key.first, key.second, false});
    for (const auto& [key, _] : groupsB)
        if (!groupsA.count(key)) d.groups.push_back(GeoGroupDelta{key.first, key.second, true});
    std::sort(d.groups.begin(), d.groups.end(), [](const GeoGroupDelta& x, const GeoGroupDelta& y) {
        return x.name != y.name ? x.name < y.name : x.domain < y.domain;
    });

    if (a.kind == GeoKind::Mesh && b.kind == GeoKind::Mesh && d.pointsA == d.pointsB &&
        d.pointsA > 0) {
        d.hasDeltaP = true;
        const std::vector<glm::vec3>& pa = *a.positions;
        const std::vector<glm::vec3>& pb = *b.positions;
        double sum = 0.0;
        float maxDelta = -1.0f;
        for (size_t i = 0; i < pa.size(); ++i) {
            const float delta = glm::length(pa[i] - pb[i]);
            sum += static_cast<double>(delta);
            if (delta > maxDelta) {
                maxDelta = delta;
                d.deltaPMaxIndex = i;
            }
        }
        d.deltaPMax = maxDelta;
        d.deltaPMean = static_cast<float>(sum / static_cast<double>(pa.size()));
        d.deltaPMaxGroup = facesGroupOfPoint(b, d.deltaPMaxIndex);
    }
    return d;
}

std::vector<std::string> formatGeoDiff(const GeoDiffResult& d) {
    std::vector<std::string> lines;
    char buf[256];
    std::string head = "kind ";
    head += geoKindName(d.kindA);
    if (d.kindA != d.kindB) {
        head += "\xe2\x86\x92";  // →
        head += geoKindName(d.kindB);
    }
    std::snprintf(buf, sizeof(buf), ", points %zu\xe2\x86\x92%zu", d.pointsA, d.pointsB);
    head += buf;
    if (d.kindA == GeoKind::Mesh || d.kindB == GeoKind::Mesh) {
        std::snprintf(buf, sizeof(buf), ", faces %zu\xe2\x86\x92%zu", d.facesA, d.facesB);
        head += buf;
    }
    head += ", bbox " + bboxText(d.hasBBoxA, d.bboxMinA, d.bboxMaxA) + "\xe2\x86\x92" +
            bboxText(d.hasBBoxB, d.bboxMinB, d.bboxMaxB);
    lines.push_back(head);

    for (const GeoAttrDelta& a : d.attrs) {
        std::snprintf(buf, sizeof(buf), "%sattr %s(%s,%s)", a.addedInB ? "+" : "-", a.name.c_str(),
                      a.type.c_str(), domainName(a.domain));
        lines.push_back(buf);
    }
    for (const GeoGroupDelta& g : d.groups) {
        std::snprintf(buf, sizeof(buf), "%sgroup %s(%s)", g.addedInB ? "+" : "-", g.name.c_str(),
                      domainName(g.domain));
        lines.push_back(buf);
    }
    if (d.hasDeltaP) {
        std::snprintf(buf, sizeof(buf), "\xce\x94P max %g (point #%zu, group %s)", d.deltaPMax,
                      d.deltaPMaxIndex, d.deltaPMaxGroup.c_str());
        lines.push_back(buf);
        std::snprintf(buf, sizeof(buf), "\xce\x94P mean %g", d.deltaPMean);
        lines.push_back(buf);
    }
    return lines;
}

}  // namespace pgg
