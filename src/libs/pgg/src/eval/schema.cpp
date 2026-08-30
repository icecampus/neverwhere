#include "../../pch.h"

#include "schema.h"

namespace pgg {

GeoSchema sourceSchema(GeoKind kind, bool withNormals) {
    GeoSchema s;
    s.open = false;
    s.kind = kind;
    s.hasN = withNormals;
    return s;
}

}  // namespace pgg
