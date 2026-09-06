#pragma once

// Static schema inference (stage E5, spec §7.6): tracks the attribute/group
// shape of geo bindings through the flat graph, so missing-attribute reads
// (E302), missing-group reads (E305) and attr-form contracts (E303) are
// caught before any execution — including through def boundaries, where the
// expansion has already inlined the fields into their consumption context.
//
// A schema is either closed (fully proven by the transfer table) or open
// (unprovable: non-literal names, deferred nodes, polymorphic inputs). Open
// schemas skip the static checks and fall back to the runtime ones
// unchanged. Reads are valid when the attribute/group exists on ANY domain
// (cross-domain reads interpolate, §4.3); promote is the exception — it
// reads one specific domain, mirrored here.

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "geometry.h"
#include "value.h"

namespace pgg {

// Static view of an attribute column: value type + typeinfo tag (v1.14).
// Implicitly convertible from/to ScalarType so the transfer table can keep
// writing `attrs[d][name] = ScalarType::Vec3` (tag None) and reading the type.
struct SchemaAttrType {
    ScalarType type = ScalarType::None;
    AttrTypeInfo info = AttrTypeInfo::None;
    SchemaAttrType() = default;
    SchemaAttrType(ScalarType t) : type(t) {}  // NOLINT(google-explicit-constructor)
    SchemaAttrType(ScalarType t, AttrTypeInfo i) : type(t), info(i) {}
    operator ScalarType() const { return type; }  // NOLINT(google-explicit-constructor)
};

struct GeoSchema {
    bool open = true;
    GeoKind kind = GeoKind::Any;
    bool hasN = false;  // dedicated normals column (@N, points)
    // Stored @N no longer matches the surface: set_position/smooth moved the
    // points after the column was written and no compute_normals followed.
    // Reading @N in that state is W006 (a mesh without a column derives fresh
    // normals from its faces and is never stale).
    bool nStale = false;
    std::array<std::unordered_map<std::string, SchemaAttrType>, 4> attrs;   // per domain
    std::array<std::unordered_set<std::string>, 4> groups;              // per domain
    std::shared_ptr<GeoSchema> instanceSource;  // geo<instances>: source schema when proven

    bool hasAttr(const std::string& name) const {
        for (const auto& perDomain : attrs)
            if (perDomain.count(name)) return true;
        return false;
    }

    bool hasGroup(const std::string& name) const {
        for (const auto& perDomain : groups)
            if (perDomain.count(name)) return true;
        return false;
    }
};

// Closed schema of the §8.1 sources: @P always, @N on surface sources.
// mesh_from_sdf reuses this with withNormals=false (attribute barrier §8.4).
GeoSchema sourceSchema(GeoKind kind, bool withNormals);

}  // namespace pgg
