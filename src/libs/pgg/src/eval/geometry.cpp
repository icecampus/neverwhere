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

size_t AttrColumn::size() const {
    return std::visit([](const auto& ptr) { return ptr ? ptr->size() : size_t(0); }, data);
}

const AttrColumn* AttrSet::find(const std::string& name) const {
    auto it = columns.find(name);
    return it == columns.end() ? nullptr : &it->second;
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
