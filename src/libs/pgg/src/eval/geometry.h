#pragma once

// Immutable geometry (spec §4.1): topology + typed attribute columns and
// named groups per domain. Nodes produce new Geo values and share unchanged
// buffers through shared_ptr (structural sharing, §12.1): a node copies only
// the columns it changes. Built-in attributes: @P (vec3, points) and @N
// (vec3, points) are dedicated columns; @index (int, any domain) is implicit
// and never stored. Groups (§4.2) are named bool masks living per domain in
// their own storage (separate from attributes, so E305 "no group" is not
// confused with E302 "no attribute"). geo<instances> (§4.1) is a lightweight
// kind: anchor positions + stamp point attributes + a shared variant list;
// the source geometry is never copied until `realize`.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pgg {

enum class GeoKind { Any, Mesh, Points, Instances };  // Any only in signature positions
enum class Domain { Points = 0, Corners, Faces, Detail };

const char* geoKindName(GeoKind kind);
const char* domainName(Domain domain);
// "points"/"corners"/"faces"/"detail" -> Domain (enum parameters); Detail on
// anything else (static E206 should have caught it).
Domain domainFromName(const std::string& name);

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

// Geometric meaning of an attribute (spec §4.4, v1.14 — Houdini typeinfo).
// transform/realize move a column by its tag instead of by a name list:
//   None       — plain data (colors, sizes, ids): copied untouched.
//   Vector     — direction with magnitude: rotated and scaled, not translated.
//   Normal     — surface normal: inverse-transpose rotation, renormalized.
//   Point      — position: full affine (rotation, scale, translation).
//   Quaternion — orientation (x, y, z, w): composed, q' = rot * q.
enum class AttrTypeInfo : uint8_t { None = 0, Vector, Normal, Point, Quaternion };

const char* attrTypeInfoName(AttrTypeInfo info);
// Parses "none|vector|normal|point|quaternion"; nullopt otherwise.
std::optional<AttrTypeInfo> attrTypeInfoFromName(const std::string& name);
// Default tag for set(..., typeinfo = auto): reserved names (N -> normal,
// orient -> quaternion, tint/color/Cd -> none), scalars/vec2/int/bool/string
// -> none. A vec3/vec4 under any other name is ambiguous (direction? position?
// color?) and returns nullopt — the caller demands an explicit tag (E610).
std::optional<AttrTypeInfo> inferAttrTypeInfo(const std::string& name, const ColumnData& value);
// Tag/type compatibility: vector/normal/point need vec3, quaternion needs vec4,
// none accepts anything.
bool attrTypeInfoFits(AttrTypeInfo info, const ColumnData& value);
// Reserved instance-stamp names (§8.8) and their required value type; nullptr
// for a free name. Returned string is the type name for diagnostics.
const char* reservedAttrTypeName(const std::string& name);
bool reservedAttrTypeFits(const std::string& name, const ColumnData& value);

struct AttrColumn {
    ColumnData data;
    AttrTypeInfo typeInfo = AttrTypeInfo::None;
    size_t size() const;
};

struct AttrSet {
    std::unordered_map<std::string, AttrColumn> columns;
    const AttrColumn* find(const std::string& name) const;
};

// Group storage (§4.2): one shared bool (u8) column per group name.
using BoolColumn = std::vector<uint8_t>;
using ConstBoolColumnPtr = std::shared_ptr<const BoolColumn>;

struct GroupSet {
    std::unordered_map<std::string, ConstBoolColumnPtr> columns;
    ConstBoolColumnPtr find(const std::string& name) const;
};

struct Geo;
using GeoPtr = std::shared_ptr<const Geo>;

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
    std::shared_ptr<const GroupSet> pointGroups;
    std::shared_ptr<const GroupSet> cornerGroups;
    std::shared_ptr<const GroupSet> faceGroups;
    std::shared_ptr<const GroupSet> detailGroups;
    // geo<instances>: shared variant sources (never copied until realize).
    std::shared_ptr<const std::vector<GeoPtr>> instanceSources;

    size_t pointCount() const { return positions ? positions->size() : 0; }
    size_t cornerCount() const { return cornerVerts ? cornerVerts->size() : 0; }
    size_t faceCount() const {
        return faceOffsets && !faceOffsets->empty() ? faceOffsets->size() - 1 : 0;
    }
    size_t elementCount(Domain domain) const;
    const AttrSet* attrs(Domain domain) const;
    const GroupSet* groups(Domain domain) const;
};

GeoPtr makeMesh(std::vector<glm::vec3> positions,
                std::vector<int32_t> cornerVerts,
                std::vector<int32_t> faceOffsets);
GeoPtr makePoints(std::vector<glm::vec3> positions);

// Copy-modify helpers: the result shares every column except the replaced one.
GeoPtr withPositions(const Geo& geo, std::vector<glm::vec3> positions);
GeoPtr withNormals(const Geo& geo, std::vector<glm::vec3> normals);
GeoPtr withAttrs(const Geo& geo, Domain domain, std::shared_ptr<const AttrSet> attrs);
GeoPtr withGroups(const Geo& geo, Domain domain, std::shared_ptr<const GroupSet> groups);

// True when `name` exists as an attribute column on any domain.
bool attrExistsOnAnyDomain(const Geo& geo, const std::string& name);
// True when `name` exists as a group on any domain.
bool groupExistsOnAnyDomain(const Geo& geo, const std::string& name);

// Silent domain interpolation (spec §4.3): the named attribute column
// resampled onto `domain`. detail broadcasts; * -> detail averages;
// points<->faces and corners<->points/faces convert via cornerVerts/
// faceOffsets; bool columns interpolate as 0/1 and threshold back at > 0.5.
// nullopt when the attribute exists on no domain (E302) or cannot be
// interpolated (string across domains, E301).
std::optional<ColumnData> sampleAttrColumn(const Geo& geo, const std::string& name, Domain domain);

// Group read with cross-domain fallback (same interpolation, threshold
// > 0.5). nullptr when the group exists on no domain (E305).
ConstBoolColumnPtr sampleGroupColumn(const Geo& geo, const std::string& name, Domain domain);

// @P/@N resampled onto a domain via the §4.3 matrix (corner copies, face
// centroids, whole-geometry average on detail). Normals: nullptr when the
// geo has none.
std::shared_ptr<const std::vector<glm::vec3>> samplePositions(const Geo& geo, Domain domain);
std::shared_ptr<const std::vector<glm::vec3>> sampleNormals(const Geo& geo, Domain domain);

// Normals derived from the topology when no @N column is stored (v1.14
// read fallback, Blender-style): points — accumulated incident face normals,
// corners/faces — the face normal, detail — the average. nullptr for
// geometries without faces (points/instances) — there @N stays E302.
std::shared_ptr<const std::vector<glm::vec3>> derivedNormals(const Geo& geo, Domain domain);

// Neutral fill for a missing column in a union (merge/realize/fracture): the
// identity quaternion for Quaternion, derived normals for Normal on a mesh
// domain that has them, white for the reserved multiplicative `tint` (vec3;
// realize's own default, so a tinted realized mesh merges with plain geometry
// without going black), zeros otherwise.
ColumnData neutralColumnLike(const std::string& name, const AttrColumn& typeOf, const Geo& forGeo,
                             Domain domain, size_t count);

// Applies p' = rot * (p * scale) + translate to a column by its typeinfo
// (vector: no translation; normal: inverse-transpose + renormalize; point:
// full affine; quaternion: rot * q). None or a non-vec3/vec4 column returns
// the input unchanged.
ColumnData transformColumn(const AttrColumn& col, const glm::quat& rot, const glm::vec3& scale,
                           const glm::vec3& translate);

// Explicit domain conversion for `promote` (spec §8.7): mode sum/average/
// first; on expanding conversions (detail -> *, points -> corners, faces ->
// corners) all modes coincide (copy/broadcast). nullopt as above.
enum class PromoteMode { Sum, Average, First };
std::optional<ColumnData> promoteAttrColumn(const Geo& geo, const std::string& name, Domain from,
                                            Domain to, PromoteMode mode);

// Column utilities for the merge/realize union semantics (§8.3, §8.8): typed
// concat (a's type wins; b converts numerically), zero-fill of a type, and
// index gather (filtering by a kept-element list).
ColumnData concatColumns(const ColumnData& a, const ColumnData& b);
ColumnData zeroColumnLike(const ColumnData& typeOf, size_t count);
ColumnData convertColumn(const ColumnData& src, const ColumnData& typeOf);
ColumnData gatherColumn(const ColumnData& col, const std::vector<int32_t>& indices);

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
