#pragma once

#include "math/lib.h"

// Occupancy of a building3d object: an odd-sized footprint is centered on
// the origin cell (3x3 → origin ± 1). Even sizes hang extra cells in +x/+y.
inline math::ivec2 buildingFootprintMin(const math::ivec2& origin, int width, int height)
{
    const int w = width > 0 ? width : 1;
    const int h = height > 0 ? height : 1;
    return math::ivec2(origin.x - (w - 1) / 2, origin.y - (h - 1) / 2);
}

inline bool buildingFootprintContains(const math::ivec2& origin, int width, int height, const math::ivec2& cell)
{
    const int w = width > 0 ? width : 1;
    const int h = height > 0 ? height : 1;
    const math::ivec2 min = buildingFootprintMin(origin, w, h);
    return cell.x >= min.x && cell.x < min.x + w && cell.y >= min.y && cell.y < min.y + h;
}

inline bool buildingFootprintOverlaps(const math::ivec2& aOrigin, int aW, int aH,
    const math::ivec2& bOrigin, int bW, int bH)
{
    const math::ivec2 aMin = buildingFootprintMin(aOrigin, aW, aH);
    const math::ivec2 bMin = buildingFootprintMin(bOrigin, bW, bH);
    const int aWcl = aW > 0 ? aW : 1;
    const int aHcl = aH > 0 ? aH : 1;
    const int bWcl = bW > 0 ? bW : 1;
    const int bHcl = bH > 0 ? bH : 1;
    return aMin.x < bMin.x + bWcl && aMin.x + aWcl > bMin.x
        && aMin.y < bMin.y + bHcl && aMin.y + aHcl > bMin.y;
}
