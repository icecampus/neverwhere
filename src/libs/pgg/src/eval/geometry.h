#pragma once

// Immutable geometry (spec §4.1): topology + typed attribute columns per
// domain. Nodes produce new Geo values and share unchanged buffers through
// shared_ptr (structural sharing, §12.1): a node copies only the columns it
// changes. Built-in attributes: @P (vec3, points) and @N (vec3, points) are
// dedicated columns; @index (int, any domain) is implicit and never stored.
// Named attribute columns (points/corners/faces/detail) exist for the E2
// attribute machinery; the E1 sources write only @P/@N.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace pgg {

enum class GeoKind { Any, Mesh, Points };  // Any only in signature positions
enum class Domain { Points = 0, Corners, Faces, Detail };

const char* geoKindName(GeoKind kind);
const char* domainName(Domain domain);

// Typed attribute storage: one shared flat buffer per column. uint8_t stands
// in for bool (std::vector<bool> is a bitset specialization).
using ColumnData = std::variant<
    std::shared_ptr<const std::vector<float>>,
    std::shared_ptr<const std::vector<int64_t>>,
    std::shared_ptr<const std::vector<uint8_t>>,
    std::shared_ptr<const std::vector<glm::vec2>>,
    std::shared_ptr<const std::vector<glm::vec3>>,
    std::shared_ptr<const std::vector<glm::vec4>>,
    std::shared_ptr<const std::vector<std::string>>>;

struct AttrColumn {
    ColumnData data;
    size_t size() const;
};

struct AttrSet {
    std::unordered_map<std::string, AttrColumn> columns;
    const AttrColumn* find(const std::string& name) const;
};

struct Geo {
    GeoKind kind = GeoKind::Mesh;
    std::shared_ptr<const std::vector<glm::vec3>> positions;  // @P
    std::shared_ptr<const std::vector<glm::vec3>> normals;    // @N (may be null)
    std::shared_ptr<const std::vector<int32_t>> cornerVerts;  // corner -> point index
    std::shared_ptr<const std::vector<int32_t>> faceOffsets;  // face -> first corner (faces+1 entries)
    std::shared_ptr<const AttrSet> pointAttrs;
    std::shared_ptr<const AttrSet> cornerAttrs;
    std::shared_ptr<const AttrSet> faceAttrs;
    std::shared_ptr<const AttrSet> detailAttrs;

    size_t pointCount() const { return positions ? positions->size() : 0; }
    size_t cornerCount() const { return cornerVerts ? cornerVerts->size() : 0; }
    size_t faceCount() const {
        return faceOffsets && !faceOffsets->empty() ? faceOffsets->size() - 1 : 0;
    }
    size_t elementCount(Domain domain) const;
    const AttrSet* attrs(Domain domain) const;
};

using GeoPtr = std::shared_ptr<const Geo>;

GeoPtr makeMesh(std::vector<glm::vec3> positions,
                std::vector<int32_t> cornerVerts,
                std::vector<int32_t> faceOffsets);
GeoPtr makePoints(std::vector<glm::vec3> positions);

// Copy-modify helpers: the result shares every column except the replaced one.
GeoPtr withPositions(const Geo& geo, std::vector<glm::vec3> positions);
GeoPtr withNormals(const Geo& geo, std::vector<glm::vec3> normals);

void geoBBox(const Geo& geo, glm::vec3& outMin, glm::vec3& outMax);

// Face normal via Newell's method (robust for non-planar polygons); the
// magnitude is twice the face area.
glm::vec3 faceNormal(const Geo& geo, size_t face);

// Number of undirected edges whose incidence count is not exactly 2
// (0 == watertight closed surface).
size_t nonManifoldEdgeCount(const Geo& geo);

// Distance from point to triangle (Ericson, closest point on triangle).
float pointTriangleDistance(const glm::vec3& p, const glm::vec3& a,
                            const glm::vec3& b, const glm::vec3& c);

}  // namespace pgg
