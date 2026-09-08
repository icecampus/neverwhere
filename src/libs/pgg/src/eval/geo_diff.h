#pragma once

// Geometry diff for the run-compare tooling (`PggTool diff`, agent_tooling_plan
// C2). Comparison is by output name at the CLI level; this module compares two
// Geo payloads: kind/counts/bbox, attribute and group set deltas (+/-), and —
// when both sides are meshes with the same point count — the per-index position
// delta (points are paired by index; the point order is deterministic, N1).
// The fingerprint fast-path (identical runs) lives in the caller: equal
// fingerprints skip diffGeo entirely.
//
// Determinism rules (same family as the probe formats, §19): attribute/group
// names are sorted, floats print with %g, the mean accumulates in f64.

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "geometry.h"

namespace pgg {

struct GeoAttrDelta {
    std::string name;
    Domain domain = Domain::Points;
    // Element type ("f32"/"int"/"bool"/"vec2"/"vec3"/"vec4"/"string") with
    // "/<typeinfo>" appended when the tag is not none (e.g. "vec3/normal").
    std::string type;
    bool addedInB = false;  // true: only in b (+attr); false: only in a (-attr)
};

struct GeoGroupDelta {
    std::string name;
    Domain domain = Domain::Points;
    bool addedInB = false;  // true: only in b (+group); false: only in a (-group)
};

struct GeoDiffResult {
    GeoKind kindA = GeoKind::Mesh;
    GeoKind kindB = GeoKind::Mesh;
    size_t pointsA = 0, pointsB = 0;
    size_t facesA = 0, facesB = 0;
    bool hasBBoxA = false, hasBBoxB = false;  // that side has points
    glm::vec3 bboxMinA{0.0f}, bboxMaxA{0.0f}, bboxMinB{0.0f}, bboxMaxB{0.0f};
    // Attribute deltas across all four domains, sorted by (name, domain). A
    // stored @N column counts as a points attribute "N" (vec3/normal), so
    // adding/dropping compute_normals shows up in the table.
    std::vector<GeoAttrDelta> attrs;
    // Group deltas across all four domains, sorted by (name, domain).
    std::vector<GeoGroupDelta> groups;
    // Position delta by index pairing. Computed only when both sides are
    // meshes with the same non-zero point count.
    bool hasDeltaP = false;
    float deltaPMax = 0.0f;
    float deltaPMean = 0.0f;  // f64 accumulation in @index order
    size_t deltaPMaxIndex = 0;
    // First faces group (sorted by name) of geometry B whose mask covers the
    // max-delta point (a point belongs to a faces group when any incident face
    // is masked); "-" when the point is in no faces group. B is the "new"
    // side of the diff — its groups describe what moved.
    std::string deltaPMaxGroup;
};

GeoDiffResult diffGeo(const Geo& a, const Geo& b);

// Text lines of the PggTool diff contract (the caller prefixes the first line
// with the output name and indents the rest):
//   kind mesh, points N1→N2, faces M1→M2, bbox (…)→(…)
//   +attr name(type,domain) / -attr name(type,domain)
//   +group name(domain) / -group name(domain)
//   ΔP max X (point #i, group g) / ΔP mean Y
std::vector<std::string> formatGeoDiff(const GeoDiffResult& d);

}  // namespace pgg
