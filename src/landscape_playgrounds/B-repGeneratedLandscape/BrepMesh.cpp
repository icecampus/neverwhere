// Playground fork of src/libs/landscape_mesh/src/landscape_mesh.cpp — see
// BrepMesh.h for the fork contract.
#include "BrepMesh.h"
#include "BrepWallStyle.h"

#include <FastNoise/FastNoise.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace brepmesh {
namespace {

using landscape_core::EdgeSide;
using landscape_core::LandscapeTileKey;
using landscape_core::LandscapeTileType;
using landscape_core::LandscapeZone;
using landscape_core::TileBuildKind;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float horizontalLength(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 scale(const Vec3& value, float amount) {
    return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

Vec3 normalizeHorizontal(const Vec3& value, const Vec3& fallback) {
    const float length = horizontalLength(value);
    if (length < 0.0001f) {
        return fallback;
    }
    return {value.x / length, 0.0f, value.z / length};
}

float dot3(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross3(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 quadAreaNormal(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    // Area-weighted sum of the two triangles the renderer actually draws (a,b,c)+(a,c,d),
    // so the shaded facet and its normal always agree. Averaged-edge cross products
    // misbehave on the twisted, non-planar quads that displacement produces.
    return add(
        cross3(subtract(b, a), subtract(c, a)),
        cross3(subtract(c, a), subtract(d, a)));
}

// Surface normal of a displaced wall quad, oriented outward. Orientation is taken from
// the UNDISPLACED base quad, whose normal is exactly +/-outwardHint, so the sign is
// unambiguous and identical for every quad of a segment. The displaced quad shares the
// same winding, so applying that sign orients it outward without the threshold-splitting
// a per-quad hint dot causes on facets that are near-perpendicular to the hint.
Vec3 displacedFaceNormal(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& baseA,
    const Vec3& baseB,
    const Vec3& baseC,
    const Vec3& baseD,
    const Vec3& outwardHint) {
    Vec3 normal = quadAreaNormal(a, b, c, d);
    const float length = std::sqrt(dot3(normal, normal));
    if (length < 0.0001f) {
        return outwardHint;
    }
    normal = scale(normal, 1.0f / length);

    const Vec3 baseNormal = quadAreaNormal(baseA, baseB, baseC, baseD);
    if (dot3(baseNormal, outwardHint) < 0.0f) {
        normal = scale(normal, -1.0f);
    }
    return normal;
}

bool samePoint(const Int2& a, const Int2& b) {
    return a.x == b.x && a.y == b.y;
}

Vec3 gridPointToVec3(const Int2& point, float y) {
    return {(float)point.x, y, (float)point.y};
}

Vec3 outwardNormalForSide(BoundarySide side) {
    switch (side) {
    case BoundarySide::Right:
        return {1.0f, 0.0f, 0.0f};
    case BoundarySide::Bottom:
        return {0.0f, 0.0f, 1.0f};
    case BoundarySide::Left:
        return {-1.0f, 0.0f, 0.0f};
    case BoundarySide::Top:
    default:
        return {0.0f, 0.0f, -1.0f};
    }
}

ColorRgba surfaceColor(LandscapeZone zone, std::uint8_t level, std::uint8_t maxLevel) {
    const float t = maxLevel == 0 ? 0.0f : (float)level / (float)maxLevel;
    switch (zone) {
    case LandscapeZone::Clearing:
        return {105, 172, 93, 255};
    case LandscapeZone::Slope:
        return {(std::uint8_t)(118 + 36 * t), (std::uint8_t)(144 + 28 * t), (std::uint8_t)(78 + 26 * t), 255};
    case LandscapeZone::HighGround:
        return {154, 143, 105, 255};
    case LandscapeZone::Hill:
        return {136, 126, 104, 255};
    case LandscapeZone::Lowland:
    default:
        return {86, 132, 88, 255};
    }
}

ColorRgba wallColor(BoundarySide side, float heightT, float noiseValue) {
    const int sideBias = side == BoundarySide::Bottom ? 16 : (side == BoundarySide::Right ? -8 : 0);
    const int shade = (int)(noiseValue * 24.0f) + (int)((1.0f - heightT) * 12.0f) + sideBias;
    return {
        (std::uint8_t)std::clamp(92 + shade, 46, 170),
        (std::uint8_t)std::clamp(86 + shade, 44, 160),
        (std::uint8_t)std::clamp(78 + shade, 40, 150),
        255,
    };
}

float deterministicRock(const MeshBuildSettings& settings, float x, float y, float z) {
    const float scaleValue = std::max(0.001f, settings.rockScale);
    const float seedOffset = (float)settings.rockSeed * 0.017f;
    const float sx = x * scaleValue + seedOffset;
    const float sy = y * scaleValue - seedOffset * 0.37f;
    const float sz = z * scaleValue + seedOffset * 0.61f;
    const float a = std::sin(sx * 19.17f + sy * 37.31f + sz * 11.73f) * 0.55f;
    const float b = std::sin(sx * 43.11f - sy * 17.27f + sz * 29.41f) * 0.30f;
    const float c = std::sin(sx * 83.63f + sy * 13.37f - sz * 47.09f) * 0.15f;
    return std::clamp(a + b + c, -1.0f, 1.0f);
}

float terraceValue(float value, int steps) {
    if (steps <= 1) {
        return value;
    }
    const float normalized = clamp01((value + 1.0f) * 0.5f);
    const float terraced = std::floor(normalized * (float)steps) / (float)steps;
    return terraced * 2.0f - 1.0f;
}

Vec3 displaceWallPoint(
    const MeshBuildSettings& settings,
    const Vec3& point,
    const Vec3& normal,
    float heightT,
    float rawNoise,
    bool fadeAtBottom,
    float maxOffset = std::numeric_limits<float>::max()) {

    if (!settings.rockEnabled || settings.rockAmplitude <= 0.0f) {
        return point;
    }

    const float steppedNoise = terraceValue(rawNoise, settings.terraceSteps);
    float fade = 1.0f - clamp01(heightT);
    fade = fade * (0.35f + fade * 0.65f);
    if (fadeAtBottom) {
        fade = std::sin(clamp01(heightT) * 3.14159265f);
    }

    // Clamp the lateral push to a fraction of the local vertex spacing. When the rock amplitude
    // exceeds the distance between neighbouring wall vertices, adjacent samples cross over and the
    // quad folds back on itself (its two triangles end up facing opposite ways). Capping the offset
    // keeps the displaced surface a valid, non-overhanging height field while preserving relief
    // wherever the subdivision is fine enough to carry it.
    const float offset = std::clamp(steppedNoise * settings.rockAmplitude * fade, -maxOffset, maxOffset);
    return {
        point.x + normal.x * offset,
        point.y,
        point.z + normal.z * offset,
    };
}

// Apply a style offset along the (horizontal) normal, keeping Y fixed, with the
// uniform anti-fold clamp. The clamp MUST be identical for every vertex of a
// level so shared edges close exactly.
Vec3 applyWallShade(const Vec3& base, const Vec3& normal, float offset, float maxOffset) {
    const float o = std::clamp(offset, -maxOffset, maxOffset);
    return {base.x + normal.x * o, base.y, base.z + normal.z * o};
}

// Wall macro-profile (playground addition): lean-back batter, foot flare and
// strata ledges. Applied to the base wall rows (before the style displacement
// and outside its anti-fold clamp) as an offset along the row's outward
// normal. Every term pins to 0 at heightT == 1, so the crest row stays on the
// original boundary line and stitches to the top surface; the foot (heightT
// == 0) is free on the plateau. The ledge phase is a function of world
// position only, so vertices shared by adjacent segments offset identically.
float wallProfileOffset(const MeshBuildSettings& settings, float heightT, const Vec3& worldPos) {
    const float t = std::clamp(heightT, 0.0f, 1.0f);
    float offset = 0.0f;
    // Batter: the wall leans back linearly as it rises (foot out, crest in).
    offset += settings.wallBatter * (1.0f - t);
    // Foot flare: extra swell concentrated near the ground (talus-ish base).
    offset += settings.wallFootFlare * (1.0f - t) * (1.0f - t);
    // Strata ledges: smooth horizontal benches wandering gently along the wall.
    if (settings.wallLedgeAmp > 0.0f && settings.wallLedgeCount > 0) {
        const float phase = std::sin(worldPos.x * 0.9f + worldPos.z * 1.7f) * 0.9f;
        offset += settings.wallLedgeAmp *
            std::sin(t * (float)settings.wallLedgeCount * 6.2831853f + phase) * (1.0f - t);
    }
    return offset;
}

bool tryBoundaryVertexNormal(const SolidMaskGrid& mask, const std::vector<BoundarySegment>& segments, const Int2& vertex, Vec3& outNormal) {
    Vec3 sum{};
    bool found = false;
    for (const BoundarySegment& segment : segments) {
        if (!samePoint(segment.a, vertex) && !samePoint(segment.b, vertex)) {
            continue;
        }

        const Vec3 normal = outwardNormalForSide(segment.side);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }

    if (!found) {
        return false;
    }

    outNormal = normalizeHorizontal(sum, {0.0f, 0.0f, 1.0f});
    return true;
}

Vec3 boundaryVertexNormal(const SolidMaskGrid& mask, const std::vector<BoundarySegment>& segments, const Int2& vertex, const Vec3& fallback) {
    Vec3 normal;
    if (tryBoundaryVertexNormal(mask, segments, vertex, normal)) {
        return normal;
    }
    return fallback;
}

bool isBoundaryCorner(const std::vector<BoundarySegment>& segments, const Int2& vertex) {
    int firstSide = -1;
    for (const BoundarySegment& segment : segments) {
        if (!samePoint(segment.a, vertex) && !samePoint(segment.b, vertex)) {
            continue;
        }

        const int side = (int)segment.side;
        if (firstSide < 0) {
            firstSide = side;
        } else if (firstSide != side) {
            return true;
        }
    }
    return false;
}

bool applyTopCornerBevel(
    const SolidMaskGrid& mask,
    const std::vector<BoundarySegment>& boundarySegments,
    const MeshBuildSettings& settings,
    int cellX,
    int cellY,
    Vec3& point,
    Vec3& outNormal) {

    struct CornerRule {
        Int2 vertex;
        bool enabled = false;
        float axisAX = 0.0f;
        float axisAZ = 0.0f;
        float axisBX = 0.0f;
        float axisBZ = 0.0f;
    };

    const float bevel = settings.cornerBevel;
    if (bevel <= 0.0f) {
        return false;
    }

    const CornerRule rules[] = {
        {{cellX, cellY},
            !mask.isSolid(cellX, cellY - 1) && !mask.isSolid(cellX - 1, cellY),
            1.0f, 0.0f, 0.0f, 1.0f},
        {{cellX + 1, cellY},
            !mask.isSolid(cellX, cellY - 1) && !mask.isSolid(cellX + 1, cellY),
            -1.0f, 0.0f, 0.0f, 1.0f},
        {{cellX + 1, cellY + 1},
            !mask.isSolid(cellX + 1, cellY) && !mask.isSolid(cellX, cellY + 1),
            -1.0f, 0.0f, 0.0f, -1.0f},
        {{cellX, cellY + 1},
            !mask.isSolid(cellX - 1, cellY) && !mask.isSolid(cellX, cellY + 1),
            1.0f, 0.0f, 0.0f, -1.0f},
    };

    for (const CornerRule& rule : rules) {
        if (!rule.enabled) {
            continue;
        }

        const float fromCornerX = point.x - (float)rule.vertex.x;
        const float fromCornerZ = point.z - (float)rule.vertex.y;
        float distanceA = fromCornerX * rule.axisAX + fromCornerZ * rule.axisAZ;
        float distanceB = fromCornerX * rule.axisBX + fromCornerZ * rule.axisBZ;
        if (distanceA < -0.0001f || distanceB < -0.0001f || distanceA > bevel || distanceB > bevel) {
            continue;
        }

        const float sum = distanceA + distanceB;
        if (sum >= bevel) {
            continue;
        }

        if (sum < 0.0001f) {
            distanceA = bevel * 0.5f;
            distanceB = bevel * 0.5f;
        } else {
            const float factor = bevel / sum;
            distanceA *= factor;
            distanceB *= factor;
        }

        point.x = (float)rule.vertex.x + rule.axisAX * distanceA + rule.axisBX * distanceB;
        point.z = (float)rule.vertex.y + rule.axisAZ * distanceA + rule.axisBZ * distanceB;
        outNormal = boundaryVertexNormal(mask, boundarySegments, rule.vertex, {0.0f, 0.0f, 1.0f});
        return true;
    }

    return false;
}

bool topBoundaryNormalForCellPoint(
    const SolidMaskGrid& mask,
    const std::vector<BoundarySegment>& boundarySegments,
    int cellX,
    int cellY,
    int subX,
    int subY,
    int subdivisions,
    Vec3& outNormal) {

    const bool onLeft = subX == 0;
    const bool onRight = subX == subdivisions;
    const bool onTop = subY == 0;
    const bool onBottom = subY == subdivisions;

    if ((onLeft || onRight) && (onTop || onBottom)) {
        const Int2 vertex{
            cellX + (onRight ? 1 : 0),
            cellY + (onBottom ? 1 : 0),
        };
        if (tryBoundaryVertexNormal(mask, boundarySegments, vertex, outNormal)) {
            return true;
        }
    }

    Vec3 sum{};
    bool found = false;
    if (onTop && !mask.isSolid(cellX, cellY - 1)) {
        const Vec3 normal = outwardNormalForSide(BoundarySide::Top);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onRight && !mask.isSolid(cellX + 1, cellY)) {
        const Vec3 normal = outwardNormalForSide(BoundarySide::Right);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onBottom && !mask.isSolid(cellX, cellY + 1)) {
        const Vec3 normal = outwardNormalForSide(BoundarySide::Bottom);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onLeft && !mask.isSolid(cellX - 1, cellY)) {
        const Vec3 normal = outwardNormalForSide(BoundarySide::Left);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }

    if (!found) {
        return false;
    }

    outNormal = normalizeHorizontal(sum, {0.0f, 0.0f, 1.0f});
    return true;
}

constexpr float kOutwardWarnDotThreshold = 0.25f;

void assignTopQuadMetadata(MeshQuad& quad) {
    quad.boundarySide = BoundarySide::Top;
    quad.outwardHint = {0.0f, 1.0f, 0.0f};
}

void assignWallQuadMetadata(MeshQuad& quad, BoundarySide side, const Vec3& outwardHint) {
    quad.boundarySide = side;
    quad.outwardHint = outwardHint;
}

Vec3 normalizeVec3(const Vec3& value, const Vec3& fallback) {
    const float length = std::sqrt(dot3(value, value));
    if (length < 0.0001f) {
        return fallback;
    }
    return scale(value, 1.0f / length);
}

void mergeNormalOrientation(NormalOrientationStats& dst, const NormalOrientationStats& src) {
    dst.wallQuadsChecked += src.wallQuadsChecked;
    dst.topQuadsChecked += src.topQuadsChecked;
    dst.outwardFailCount += src.outwardFailCount;
    dst.outwardWarnCount += src.outwardWarnCount;
    dst.minWallOutwardDot = std::min(dst.minWallOutwardDot, src.minWallOutwardDot);
}

void logNormalOrientation(const char* context, const NormalOrientationStats& stats) {
    const char* marker = stats.outwardFailCount > 0
        ? "TEST FAIL"
        : stats.outwardWarnCount > 0
            ? "TEST WARN"
            : "TEST PASS";
    spdlog::info(
        "{} {} normal orientation: wallQuads={}, topQuads={}, fail={}, warn={}, minWallDot={:.4f}",
        marker,
        context,
        stats.wallQuadsChecked,
        stats.topQuadsChecked,
        stats.outwardFailCount,
        stats.outwardWarnCount,
        stats.minWallOutwardDot);
}

void addQuad(CompositionResult& result, MeshQuad quad) {
    result.quads.push_back(quad);
    if (quad.cliffWall) {
        result.stats.cliffWallQuadCount++;
    } else {
        result.stats.topQuadCount++;
    }
}

void appendTranslated(std::vector<MeshQuad>& out, const TileMesh& mesh, const Vec3& offset) {
    for (MeshQuad quad : mesh.quads) {
        quad.a = add(quad.a, offset);
        quad.b = add(quad.b, offset);
        quad.c = add(quad.c, offset);
        quad.d = add(quad.d, offset);
        out.push_back(quad);
    }
}

void addTopCell(
    CompositionResult& result,
    const SolidMaskGrid& mask,
    const std::vector<BoundarySegment>& boundarySegments,
    const MeshBuildSettings& settings,
    const IWallStyle& style,
    int cellX,
    int cellY,
    float height,
    std::uint8_t level,
    std::uint8_t maxLevel) {

    if (!settings.rockEnabled) {
        MeshQuad top;
        top.a = {(float)cellX, height, (float)cellY};
        top.b = {(float)(cellX + 1), height, (float)cellY};
        top.c = {(float)(cellX + 1), height, (float)(cellY + 1)};
        top.d = {(float)cellX, height, (float)(cellY + 1)};
        top.color = surfaceColor(mask.zoneAt(cellX, cellY), level, maxLevel);
        top.cliffWall = false;
        assignTopQuadMetadata(top);
        addQuad(result, top);
        return;
    }

    const int subdivisions = settings.wallHorizontalSubdivisions;
    const int vertexColumns = subdivisions + 1;
    std::vector<Vec3> points((std::size_t)vertexColumns * (std::size_t)vertexColumns);
    std::vector<Vec3> normals(points.size());
    std::vector<std::uint8_t> boundaryFlags(points.size(), 0);
    std::vector<float> noise;

    for (int y = 0; y <= subdivisions; y++) {
        const float v = (float)y / (float)subdivisions;
        for (int x = 0; x <= subdivisions; x++) {
            const float u = (float)x / (float)subdivisions;
            const std::size_t index = (std::size_t)y * (std::size_t)vertexColumns + (std::size_t)x;
            points[index] = {(float)cellX + u, height, (float)cellY + v};
            if (applyTopCornerBevel(mask, boundarySegments, settings, cellX, cellY, points[index], normals[index])) {
                boundaryFlags[index] = 1;
            } else {
                boundaryFlags[index] = topBoundaryNormalForCellPoint(
                    mask,
                    boundarySegments,
                    cellX,
                    cellY,
                    x,
                    y,
                    subdivisions,
                    normals[index]) ? 1 : 0;
            }
        }
    }

    // The crest band is owned by the style: it shades the rim vertices (edgeWeight
    // from the boundary flag) and colours the top panels. With heightT == 1 and no
    // fadeAtBottom the seam envelope yields a flat lip, so the crest meets the
    // pinned flat interior and the wall top without a gap.
    std::vector<WallStyleSample> samples(points.size());
    for (std::size_t i = 0; i < points.size(); i++) {
        samples[i].worldPos = points[i];
        samples[i].normal = normals[i];
        samples[i].heightT = 1.0f;
        samples[i].edgeWeight = boundaryFlags[i] != 0 ? 1.0f : 0.0f;
        samples[i].fadeAtBottom = false;
        samples[i].part = WallPart::Crest;
    }
    std::vector<WallShade> shades;
    style.shade(samples, shades);

    std::vector<Vec3> displaced(points.size());
    for (std::size_t i = 0; i < points.size(); i++) {
        displaced[i] = boundaryFlags[i] != 0
            ? applyWallShade(points[i], normals[i], shades[i].offset, std::numeric_limits<float>::max())
            : points[i];
    }

    for (int y = 0; y < subdivisions; y++) {
        for (int x = 0; x < subdivisions; x++) {
            const std::size_t i00 = (std::size_t)y * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = (std::size_t)(y + 1) * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i11 = i01 + 1;

            MeshQuad top;
            top.a = displaced[i00];
            top.b = displaced[i10];
            top.c = displaced[i11];
            top.d = displaced[i01];
            const float panelField = (shades[i00].field + shades[i10].field + shades[i11].field + shades[i01].field) * 0.25f;
            top.color = style.color(WallPart::Crest, BoundarySide::Top, 1.0f, panelField);
            top.cliffWall = false;
            assignTopQuadMetadata(top);
            addQuad(result, top);
        }
    }
}

LandscapeZone cornerSolidZone(const SolidMaskGrid& mask, const Int2& corner);
LandscapeZone cornerNonSolidZone(const SolidMaskGrid& mask, const Int2& corner);

void addInnerCornerBevelCaps(
    CompositionResult& result,
    const SolidMaskGrid& mask,
    const std::vector<BoundarySegment>& boundarySegments,
    const MeshBuildSettings& settings,
    float height,
    std::uint8_t level,
    std::uint8_t maxLevel) {

    struct CornerEndpoint {
        Int2 corner;
        Vec3 point;
        BoundarySide side = BoundarySide::Top;
    };

    const float bevel = std::clamp(settings.cornerBevel, 0.0f, 0.45f);
    if (bevel <= 0.0f) {
        return;
    }

    std::vector<CornerEndpoint> endpoints;
    endpoints.reserve(boundarySegments.size() * 2);
    for (const BoundarySegment& segment : boundarySegments) {
        const Vec3 a = gridPointToVec3(segment.a, height);
        const Vec3 b = gridPointToVec3(segment.b, height);
        const Vec3 delta = subtract(b, a);
        const float length = horizontalLength(delta);
        if (length < 0.0001f) {
            continue;
        }

        const Vec3 direction = scale(delta, 1.0f / length);
        const float trim = std::min(bevel, length * 0.45f);
        if (isBoundaryCorner(boundarySegments, segment.a)) {
            endpoints.push_back({segment.a, add(a, scale(direction, trim)), segment.side});
        }
        if (isBoundaryCorner(boundarySegments, segment.b)) {
            endpoints.push_back({segment.b, add(b, scale(direction, -trim)), segment.side});
        }
    }

    std::vector<std::uint8_t> consumed(endpoints.size(), 0);
    for (std::size_t i = 0; i < endpoints.size(); i++) {
        if (consumed[i]) {
            continue;
        }

        std::vector<std::size_t> group;
        for (std::size_t j = i; j < endpoints.size(); j++) {
            if (!consumed[j] && samePoint(endpoints[i].corner, endpoints[j].corner)) {
                group.push_back(j);
                consumed[j] = 1;
            }
        }

        if (group.size() != 2) {
            continue;
        }

        const VertexKind kind = classifyVertex(mask, endpoints[i].corner.x, endpoints[i].corner.y);
        if (kind != VertexKind::InnerCorner) {
            continue;
        }

        const CornerEndpoint& a = endpoints[group[0]];
        const CornerEndpoint& b = endpoints[group[1]];
        if (a.side == b.side) {
            continue;
        }

        const Vec3 corner = gridPointToVec3(a.corner, height);
        const Vec3 mid = scale(add(a.point, b.point), 0.5f);

        MeshQuad cap;
        cap.a = corner;
        cap.b = a.point;
        cap.c = mid;
        cap.d = b.point;
        cap.color = surfaceColor(cornerSolidZone(mask, a.corner), level, maxLevel);
        cap.cliffWall = false;
        assignTopQuadMetadata(cap);
        addQuad(result, cap);
        result.stats.cornerCapCount++;
    }
}

LandscapeZone cornerSolidZone(const SolidMaskGrid& mask, const Int2& corner) {
    for (int y = corner.y - 1; y <= corner.y; y++) {
        for (int x = corner.x - 1; x <= corner.x; x++) {
            if (mask.isSolid(x, y)) {
                return mask.zoneAt(x, y);
            }
        }
    }

    return LandscapeZone::Lowland;
}

LandscapeZone cornerNonSolidZone(const SolidMaskGrid& mask, const Int2& corner) {
    for (int y = corner.y - 1; y <= corner.y; y++) {
        for (int x = corner.x - 1; x <= corner.x; x++) {
            if (x < 0 || y < 0 || x >= mask.width || y >= mask.height) {
                continue;
            }
            if (!mask.isSolid(x, y)) {
                return mask.zoneAt(x, y);
            }
        }
    }

    return cornerSolidZone(mask, corner);
}

void addOuterCornerFootCaps(
    CompositionResult& result,
    const SolidMaskGrid& mask,
    const std::vector<BoundarySegment>& boundarySegments,
    const MeshBuildSettings& settings,
    float height,
    std::uint8_t lowerLevel,
    std::uint8_t maxLevel) {

    struct CornerEndpoint {
        Int2 corner;
        Vec3 point;
        BoundarySide side = BoundarySide::Top;
    };

    const float bevel = std::clamp(settings.cornerBevel, 0.0f, 0.45f);
    if (bevel <= 0.0f) {
        return;
    }

    std::vector<CornerEndpoint> endpoints;
    endpoints.reserve(boundarySegments.size() * 2);
    for (const BoundarySegment& segment : boundarySegments) {
        const Vec3 a = gridPointToVec3(segment.a, height);
        const Vec3 b = gridPointToVec3(segment.b, height);
        const Vec3 delta = subtract(b, a);
        const float length = horizontalLength(delta);
        if (length < 0.0001f) {
            continue;
        }

        const Vec3 direction = scale(delta, 1.0f / length);
        const float trim = std::min(bevel, length * 0.45f);
        if (isBoundaryCorner(boundarySegments, segment.a)) {
            endpoints.push_back({segment.a, add(a, scale(direction, trim)), segment.side});
        }
        if (isBoundaryCorner(boundarySegments, segment.b)) {
            endpoints.push_back({segment.b, add(b, scale(direction, -trim)), segment.side});
        }
    }

    std::vector<std::uint8_t> consumed(endpoints.size(), 0);
    for (std::size_t i = 0; i < endpoints.size(); i++) {
        if (consumed[i]) {
            continue;
        }

        std::vector<std::size_t> group;
        for (std::size_t j = i; j < endpoints.size(); j++) {
            if (!consumed[j] && samePoint(endpoints[i].corner, endpoints[j].corner)) {
                group.push_back(j);
                consumed[j] = 1;
            }
        }

        if (group.size() != 2) {
            continue;
        }

        const VertexKind kind = classifyVertex(mask, endpoints[i].corner.x, endpoints[i].corner.y);
        if (kind != VertexKind::OuterCorner) {
            continue;
        }

        const CornerEndpoint& a = endpoints[group[0]];
        const CornerEndpoint& b = endpoints[group[1]];
        if (a.side == b.side) {
            continue;
        }

        const Vec3 corner = gridPointToVec3(a.corner, height);
        const Vec3 mid = scale(add(a.point, b.point), 0.5f);

        MeshQuad cap;
        cap.a = corner;
        cap.b = a.point;
        cap.c = mid;
        cap.d = b.point;
        cap.color = surfaceColor(cornerNonSolidZone(mask, a.corner), lowerLevel, maxLevel);
        cap.cliffWall = false;
        assignTopQuadMetadata(cap);
        addQuad(result, cap);
        result.stats.cornerCapCount++;
    }
}

NormalOrientationStats validateNormalOrientationInternal(const std::vector<MeshQuad>& quads) {
    NormalOrientationStats stats;
    for (const MeshQuad& quad : quads) {
        if (quad.cliffWall) {
            stats.wallQuadsChecked++;
            const Vec3 faceNormal = normalizeVec3(quad.normal, quad.outwardHint);
            const Vec3 outward = normalizeVec3(quad.outwardHint, faceNormal);
            const float dot = dot3(faceNormal, outward);
            stats.minWallOutwardDot = std::min(stats.minWallOutwardDot, dot);
            if (dot < 0.0f) {
                stats.outwardFailCount++;
            } else if (dot < kOutwardWarnDotThreshold) {
                stats.outwardWarnCount++;
            }
            continue;
        }

        stats.topQuadsChecked++;
        if (quad.normal.y < 0.0f) {
            stats.outwardFailCount++;
        }
    }
    return stats;
}

Vec3 litWallNormalInternal(const MeshQuad& quad) {
    const Vec3 sideFallback = outwardNormalForSide(quad.boundarySide);
    const Vec3 normalFallback = normalizeHorizontal(quad.normal, {0.0f, 0.0f, 1.0f});
    const Vec3 primaryFallback = normalizeHorizontal(sideFallback, normalFallback);
    return normalizeHorizontal(quad.outwardHint, primaryFallback);
}

} // namespace

NormalOrientationStats validateNormalOrientation(const std::vector<MeshQuad>& quads) {
    return validateNormalOrientationInternal(quads);
}

Vec3 litWallNormal(const MeshQuad& quad) {
    return litWallNormalInternal(quad);
}

bool SolidMaskGrid::empty() const {
    return width <= 0 || height <= 0 || solidCells.empty();
}

int SolidMaskGrid::cellIndex(int x, int y) const {
    return y * width + x;
}

bool SolidMaskGrid::isSolid(int x, int y) const {
    if (empty() || x < 0 || y < 0 || x >= width || y >= height) {
        return false;
    }
    return solidCells[(std::size_t)cellIndex(x, y)] != 0;
}

bool SolidMaskGrid::hasTop(int x, int y) const {
    if (!isSolid(x, y)) {
        return false;
    }
    if (topCells.empty()) {
        return true;
    }
    return topCells[(std::size_t)cellIndex(x, y)] != 0;
}

LandscapeZone SolidMaskGrid::zoneAt(int x, int y) const {
    if (empty() || zones.empty() || x < 0 || y < 0 || x >= width || y >= height) {
        return LandscapeZone::Lowland;
    }
    return zones[(std::size_t)cellIndex(x, y)];
}

SolidMaskGrid solidMaskFromNodes(const std::uint8_t* nodes, int nodesX, int nodesY) {
    SolidMaskGrid mask;
    if (!nodes || nodesX < 2 || nodesY < 2) {
        return mask;
    }

    mask.width = nodesX - 1;
    mask.height = nodesY - 1;
    const std::size_t cellCount = static_cast<std::size_t>(mask.width) * mask.height;
    mask.solidCells.assign(cellCount, 0);
    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const bool on =
                nodes[static_cast<std::size_t>(y) * nodesX + x] != 0 ||
                nodes[static_cast<std::size_t>(y) * nodesX + x + 1] != 0 ||
                nodes[static_cast<std::size_t>(y + 1) * nodesX + x] != 0 ||
                nodes[static_cast<std::size_t>(y + 1) * nodesX + x + 1] != 0;
            mask.solidCells[static_cast<std::size_t>(y) * mask.width + x] = on ? 1 : 0;
        }
    }
    mask.topCells = mask.solidCells;
    mask.zones.assign(cellCount, landscape_core::LandscapeZone::Lowland);
    return mask;
}

VertexKind classifyVertex(const SolidMaskGrid& mask, int x, int y) {
    const bool topLeft = mask.isSolid(x - 1, y - 1);
    const bool topRight = mask.isSolid(x, y - 1);
    const bool bottomLeft = mask.isSolid(x - 1, y);
    const bool bottomRight = mask.isSolid(x, y);
    const int solidCount = (topLeft ? 1 : 0) + (topRight ? 1 : 0) + (bottomLeft ? 1 : 0) + (bottomRight ? 1 : 0);

    if (solidCount == 0) return VertexKind::Empty;
    if (solidCount == 1) return VertexKind::OuterCorner;
    if (solidCount == 3) return VertexKind::InnerCorner;
    if (solidCount == 4) return VertexKind::SolidInterior;

    const bool diagonal = (topLeft && bottomRight) || (topRight && bottomLeft);
    return diagonal ? VertexKind::DiagonalJoin : VertexKind::Edge;
}

std::vector<BoundarySegment> buildBoundarySegments(const SolidMaskGrid& mask) {
    std::vector<BoundarySegment> segments;
    if (mask.empty()) {
        return segments;
    }

    auto addSegment = [&](int x, int y, BoundarySide side) {
        BoundarySegment segment;
        segment.side = side;
        switch (side) {
        case BoundarySide::Top:
            segment.a = {x, y};
            segment.b = {x + 1, y};
            break;
        case BoundarySide::Right:
            segment.a = {x + 1, y};
            segment.b = {x + 1, y + 1};
            break;
        case BoundarySide::Bottom:
            segment.a = {x + 1, y + 1};
            segment.b = {x, y + 1};
            break;
        case BoundarySide::Left:
            segment.a = {x, y + 1};
            segment.b = {x, y};
            break;
        }
        segments.push_back(segment);
    };

    for (int y = 0; y < mask.height; y++) {
        for (int x = 0; x < mask.width; x++) {
            if (!mask.isSolid(x, y)) {
                continue;
            }
            if (!mask.isSolid(x, y - 1)) addSegment(x, y, BoundarySide::Top);
            if (!mask.isSolid(x + 1, y)) addSegment(x, y, BoundarySide::Right);
            if (!mask.isSolid(x, y + 1)) addSegment(x, y, BoundarySide::Bottom);
            if (!mask.isSolid(x - 1, y)) addSegment(x, y, BoundarySide::Left);
        }
    }

    return segments;
}

BeveledBoundaryResult buildBeveledBoundary(const SolidMaskGrid& mask, const MeshBuildSettings& settings) {
    struct TrimmedEndpoint {
        Int2 corner;
        Vec3 point;
        BoundarySide side = BoundarySide::Top;
        Vec3 sideNormal;
    };

    BeveledBoundaryResult result;
    result.boundarySegments = buildBoundarySegments(mask);
    std::vector<TrimmedEndpoint> endpoints;
    const float bevel = std::clamp(settings.cornerBevel, 0.0f, 0.45f);

    for (const BoundarySegment& segment : result.boundarySegments) {
        const Vec3 a = gridPointToVec3(segment.a, 0.0f);
        const Vec3 b = gridPointToVec3(segment.b, 0.0f);
        const Vec3 delta = subtract(b, a);
        const float length = horizontalLength(delta);
        if (length < 0.0001f) {
            continue;
        }

        const Vec3 direction = scale(delta, 1.0f / length);
        // Only trim where a bevel facet will actually be generated (a clean 2-segment corner).
        // Diagonal pinch points have four boundary segments meeting at one vertex and never receive
        // a bevel; trimming them anyway pulled the walls back and left an open gap at the seam.
        const bool canBevelA = isBoundaryCorner(result.boundarySegments, segment.a) &&
            classifyVertex(mask, segment.a.x, segment.a.y) != VertexKind::DiagonalJoin;
        const bool canBevelB = isBoundaryCorner(result.boundarySegments, segment.b) &&
            classifyVertex(mask, segment.b.x, segment.b.y) != VertexKind::DiagonalJoin;
        const bool trimStart = bevel > 0.0f && canBevelA;
        const bool trimEnd = bevel > 0.0f && canBevelB;
        const float trim = std::min(bevel, length * 0.45f);
        const Vec3 start = trimStart ? add(a, scale(direction, trim)) : a;
        const Vec3 end = trimEnd ? add(b, scale(direction, -trim)) : b;
        const Vec3 sideNormal = outwardNormalForSide(segment.side);

        // Straight wall panels keep a single, uniform side normal across every column. Blending the
        // corner columns toward the bevel diagonal used to tilt only the edge column, which displaced
        // that one column in a different direction than its neighbour and produced a twisted,
        // non-planar quad that flat shading rendered as a stray "plane" at the seam. The corner
        // curvature is instead carried entirely by the small bevel facet below.
        if (horizontalLength(subtract(end, start)) > 0.0001f) {
            result.beveledSegments.push_back({start, end, sideNormal, sideNormal, sideNormal, segment.side});
        }

        if (trimStart) {
            endpoints.push_back({segment.a, start, segment.side, sideNormal});
        }
        if (trimEnd) {
            endpoints.push_back({segment.b, end, segment.side, sideNormal});
        }
    }

    std::vector<std::uint8_t> consumed(endpoints.size(), 0);
    for (std::size_t i = 0; i < endpoints.size(); i++) {
        if (consumed[i]) {
            continue;
        }

        std::vector<std::size_t> group;
        for (std::size_t j = i; j < endpoints.size(); j++) {
            if (!consumed[j] && samePoint(endpoints[i].corner, endpoints[j].corner)) {
                group.push_back(j);
                consumed[j] = 1;
            }
        }

        if (group.size() != 2) {
            continue;
        }

        const TrimmedEndpoint& a = endpoints[group[0]];
        const TrimmedEndpoint& b = endpoints[group[1]];
        if (a.side == b.side) {
            continue;
        }

        const Vec3 normal = normalizeHorizontal(
            add(outwardNormalForSide(a.side), outwardNormalForSide(b.side)),
            outwardNormalForSide(a.side));
        // The bevel facet absorbs the corner curvature: its interior columns face along the diagonal,
        // while the two edge columns face along the adjacent straight panels' side normals. Because
        // those edge columns share their vertical seam (a.point / b.point) and side normal with the
        // neighbouring straight wall, both panels displace the shared edge to the exact same place,
        // so the chamfer stays watertight without tilting the flat wall faces.
        if (horizontalLength(subtract(b.point, a.point)) > 0.0001f) {
            result.beveledSegments.push_back({a.point, b.point, normal, a.sideNormal, b.sideNormal, a.side});
        }
    }

    return result;
}

TileMeshCatalog::TileMeshCatalog(MeshBuildSettings settings)
    : m_settings(settings) {
    m_settings.cellSize = std::max(0.01f, m_settings.cellSize);
    m_settings.levelHeight = std::max(0.01f, m_settings.levelHeight);
    m_settings.cornerBevel = std::clamp(m_settings.cornerBevel, 0.0f, 0.45f);
    m_settings.wallHorizontalSubdivisions = std::clamp(m_settings.wallHorizontalSubdivisions, 1, 16);
    m_settings.wallVerticalSubdivisions = std::clamp(m_settings.wallVerticalSubdivisions, 1, 16);
    m_settings.rockAmplitude = std::max(0.0f, m_settings.rockAmplitude);
    m_settings.rockScale = std::max(0.001f, m_settings.rockScale);
    m_settings.terraceSteps = std::clamp(m_settings.terraceSteps, 0, 12);
}

const TileMesh& TileMeshCatalog::meshFor(const LandscapeTileKey& key) {
    auto it = m_meshes.find(key);
    if (it != m_meshes.end()) {
        m_reusedCount++;
        return it->second;
    }

    auto [insertedIt, _] = m_meshes.emplace(key, buildMesh(key));
    m_generatedCount++;
    return insertedIt->second;
}

TileMesh TileMeshCatalog::buildMesh(const LandscapeTileKey& key) const {
    switch (key.kind) {
    case TileBuildKind::Wall:
        return buildWallMesh(key);
    case TileBuildKind::CornerCap:
    case TileBuildKind::Surface:
    default:
        return buildSurfaceMesh(key);
    }
}

TileMesh TileMeshCatalog::buildSurfaceMesh(const LandscapeTileKey& key) const {
    TileMesh mesh;
    if (!landscape_core::tileTypeHasSurface(key.tileType)) {
        return mesh;
    }

    const float s = m_settings.cellSize;
    const float h = (float)key.level * m_settings.levelHeight;
    MeshQuad quad;
    quad.a = {0.0f, h, 0.0f};
    quad.b = {s, h, 0.0f};
    quad.c = {s, h, s};
    quad.d = {0.0f, h, s};
    quad.color = surfaceColor(key.zone, key.level, std::max<std::uint8_t>(1, key.upperLevel));
    quad.cliffWall = false;
    assignTopQuadMetadata(quad);
    mesh.quads.push_back(quad);
    return mesh;
}

TileMesh TileMeshCatalog::buildWallMesh(const LandscapeTileKey& key) const {
    TileMesh mesh;
    const float s = m_settings.cellSize;
    const float low = (float)key.lowerLevel * m_settings.levelHeight;
    const float high = (float)key.upperLevel * m_settings.levelHeight;
    if (high <= low) {
        return mesh;
    }

    const int hSub = std::max(1, m_settings.wallHorizontalSubdivisions);
    const int vSub = std::max(1, m_settings.wallVerticalSubdivisions);
    const Vec3 normal = outwardNormalForSide(key.side);

    auto basePoint = [&](float alongT, float heightT) {
        const float along = alongT * s;
        const float y = low + (high - low) * heightT;
        switch (key.side) {
        case EdgeSide::Right:
            return Vec3{s, y, along};
        case EdgeSide::Bottom:
            return Vec3{along, y, s};
        case EdgeSide::Left:
            return Vec3{0.0f, y, along};
        case EdgeSide::Top:
        default:
            return Vec3{along, y, 0.0f};
        }
    };

    for (int y = 0; y < vSub; y++) {
        const float y0 = (float)y / (float)vSub;
        const float y1 = (float)(y + 1) / (float)vSub;
        for (int x = 0; x < hSub; x++) {
            const float x0 = (float)x / (float)hSub;
            const float x1 = (float)(x + 1) / (float)hSub;
            const Vec3 p00 = basePoint(x0, y0);
            const Vec3 p10 = basePoint(x1, y0);
            const Vec3 p01 = basePoint(x0, y1);
            const Vec3 p11 = basePoint(x1, y1);
            const float n00 = deterministicRock(m_settings, p00.x, p00.y, p00.z);
            const float n10 = deterministicRock(m_settings, p10.x, p10.y, p10.z);
            const float n01 = deterministicRock(m_settings, p01.x, p01.y, p01.z);
            const float n11 = deterministicRock(m_settings, p11.x, p11.y, p11.z);

            MeshQuad quad;
            quad.a = displaceWallPoint(m_settings, p00, normal, y0, n00, true);
            quad.b = displaceWallPoint(m_settings, p10, normal, y0, n10, true);
            quad.c = displaceWallPoint(m_settings, p11, normal, y1, n11, true);
            quad.d = displaceWallPoint(m_settings, p01, normal, y1, n01, true);
            quad.normal = displacedFaceNormal(quad.a, quad.b, quad.c, quad.d, p00, p10, p11, p01, normal);
            quad.relief = std::clamp((n00 + n10 + n11 + n01) * 0.25f, -1.0f, 1.0f);
            quad.heightFraction = std::clamp((y0 + y1) * 0.5f, 0.0f, 1.0f);
            quad.color = wallColor(key.side, (y0 + y1) * 0.5f, (n00 + n10 + n11 + n01) * 0.25f);
            quad.cliffWall = true;
            assignWallQuadMetadata(quad, key.side, normal);
            mesh.quads.push_back(quad);
        }
    }

    return mesh;
}

CompositionResult composeSolidMaskMesh(const SolidMeshBuildRequest& request, const MeshBuildSettings& inputSettings) {
    CompositionResult result;
    if (request.mask.empty()) {
        return result;
    }

    MeshBuildSettings settings = inputSettings;
    settings.cornerBevel = std::clamp(settings.cornerBevel, 0.0f, 0.45f);
    settings.wallHorizontalSubdivisions = std::clamp(settings.wallHorizontalSubdivisions, 1, 16);
    settings.wallVerticalSubdivisions = std::clamp(settings.wallVerticalSubdivisions, 1, 16);
    settings.rockScale = std::max(0.001f, settings.rockScale);
    settings.rockAmplitude = std::max(0.0f, settings.rockAmplitude);
    settings.terraceSteps = std::clamp(settings.terraceSteps, 0, 12);

    const BeveledBoundaryResult boundary = buildBeveledBoundary(request.mask, settings);
    result.stats.boundarySegmentCount += (int)boundary.boundarySegments.size();
    result.stats.beveledSegmentCount += (int)boundary.beveledSegments.size();

    // One style instance per level band, shared by the crest and every wall segment
    // so its noise nodes are built once and the whole transition reads coherently.
    std::unique_ptr<IWallStyle> style = makeWallStyle(settings.wallStyle);
    WallStyleContext styleContext;
    styleContext.settings = settings;
    styleContext.level = request.level;
    styleContext.lowerLevel = request.level > 0 ? (std::uint8_t)(request.level - 1) : 0;
    styleContext.maxLevel = request.maxLevel;
    styleContext.seed = settings.rockSeed;
    style->prepare(styleContext);

    for (int y = 0; y < request.mask.height; y++) {
        for (int x = 0; x < request.mask.width; x++) {
            if (!request.mask.hasTop(x, y)) {
                continue;
            }
            addTopCell(result, request.mask, boundary.boundarySegments, settings, *style, x, y, request.topHeight, request.level, request.maxLevel);
        }
    }

    addInnerCornerBevelCaps(
        result,
        request.mask,
        boundary.boundarySegments,
        settings,
        request.topHeight,
        request.level,
        request.maxLevel);

    if (!request.includeWalls) {
        return result;
    }

    if (request.fadeWallDisplacementAtBottom) {
        const std::uint8_t lowerLevel = request.level > 0 ? (std::uint8_t)(request.level - 1) : 0;
        addOuterCornerFootCaps(
            result,
            request.mask,
            boundary.boundarySegments,
            settings,
            request.baseHeight,
            lowerLevel,
            request.maxLevel);
    }

    // Cap rock displacement to a fraction of the vertex spacing so neighbouring samples cannot cross
    // and fold a panel. The cap MUST be identical for every segment: adjacent panels share vertical
    // edges, and if two panels clamped a shared vertex by different amounts the edge would split open.
    // The tightest bound (shortest bevel facet, plus the row height) is therefore applied uniformly.
    const float vSpacing = (request.topHeight - request.baseHeight) / (float)settings.wallVerticalSubdivisions;
    float minHSpacing = vSpacing;
    for (const MeshBoundarySegment& segment : boundary.beveledSegments) {
        const float length = horizontalLength(subtract(segment.b, segment.a));
        if (length > 0.0001f) {
            minHSpacing = std::min(minHSpacing, length / (float)settings.wallHorizontalSubdivisions);
        }
    }
    const float wallMaxOffset = 0.7f * std::max(0.0001f, std::min(minHSpacing, vSpacing));

    for (const MeshBoundarySegment& segment : boundary.beveledSegments) {
        std::vector<MeshQuad> segmentQuads = buildWallQuadsFromBoundarySegment(
            segment,
            request.baseHeight,
            request.topHeight,
            request.fadeWallDisplacementAtBottom,
            wallMaxOffset,
            settings,
            nullptr,
            style.get());
        for (const MeshQuad& wall : segmentQuads) {
            addQuad(result, wall);
        }
    }

    result.normalOrientation = validateNormalOrientationInternal(result.quads);
    logNormalOrientation("composeSolidMaskMesh", result.normalOrientation);

    return result;
}

std::vector<MeshQuad> buildWallQuadsFromBoundarySegment(
    const MeshBoundarySegment& segment,
    float baseHeight,
    float topHeight,
    bool fadeWallDisplacementAtBottom,
    float wallMaxOffset,
    const MeshBuildSettings& inputSettings,
    std::vector<MeshQuad>* outBaseQuads,
    const IWallStyle* style) {

    std::vector<MeshQuad> quads;
    MeshBuildSettings settings = inputSettings;
    settings.wallHorizontalSubdivisions = std::clamp(settings.wallHorizontalSubdivisions, 1, 32);
    settings.wallVerticalSubdivisions = std::clamp(settings.wallVerticalSubdivisions, 1, 32);
    settings.rockScale = std::max(0.001f, settings.rockScale);
    settings.rockAmplitude = std::max(0.0f, settings.rockAmplitude);
    settings.terraceSteps = std::clamp(settings.terraceSteps, 0, 12);

    // When called standalone (e.g. Wall Lab) build a style from the settings so the
    // single path stays self-contained; composeSolidMaskMesh passes a prepared one.
    std::unique_ptr<IWallStyle> localStyle;
    const IWallStyle* useStyle = style;
    if (useStyle == nullptr) {
        localStyle = makeWallStyle(settings.wallStyle);
        WallStyleContext ctx;
        ctx.settings = settings;
        ctx.seed = settings.rockSeed;
        localStyle->prepare(ctx);
        useStyle = localStyle.get();
    }

    const Vec3 topA{segment.a.x, topHeight, segment.a.z};
    const Vec3 topB{segment.b.x, topHeight, segment.b.z};
    const Vec3 bottomA{segment.a.x, baseHeight, segment.a.z};
    const Vec3 bottomB{segment.b.x, baseHeight, segment.b.z};
    const int vertexColumns = settings.wallHorizontalSubdivisions + 1;
    const int vertexRows = settings.wallVerticalSubdivisions + 1;
    std::vector<Vec3> wallPoints((std::size_t)vertexColumns * (std::size_t)vertexRows);
    std::vector<Vec3> wallNormals((std::size_t)vertexColumns * (std::size_t)vertexRows);
    std::vector<float> wallNoise;

    for (int row = 0; row < vertexRows; row++) {
        const float v = (float)row / (float)settings.wallVerticalSubdivisions;
        const float topT = 1.0f - v;
        const Vec3 rowA = lerp(bottomA, topA, topT);
        const Vec3 rowB = lerp(bottomB, topB, topT);
        const float heightT = 1.0f - v;
        for (int column = 0; column < vertexColumns; column++) {
            const float u = (float)column / (float)settings.wallHorizontalSubdivisions;
            const std::size_t index = (std::size_t)row * (std::size_t)vertexColumns + (std::size_t)column;
            wallPoints[index] = lerp(rowA, rowB, u);
            if (column == 0) {
                wallNormals[index] = segment.startNormal;
            } else if (column == vertexColumns - 1) {
                wallNormals[index] = segment.endNormal;
            } else {
                wallNormals[index] = segment.normal;
            }
            // Macro-profile (batter/flare/ledges) bends the base rows before
            // the style displacement; pinned at the crest, world-pos phased.
            const float profile = wallProfileOffset(settings, heightT, wallPoints[index]);
            if (profile != 0.0f) {
                wallPoints[index] = add(wallPoints[index], scale(wallNormals[index], profile));
            }
        }
    }

    // Shade every wall vertex through the style. Each vertex carries its heightT
    // (1 at the top row, 0 at the base), so the style's seam envelope pins the top
    // and bottom rows to 0 displacement and the panel stitches to crest and ground.
    std::vector<WallStyleSample> samples(wallPoints.size());
    for (int row = 0; row < vertexRows; row++) {
        const float heightT = 1.0f - (float)row / (float)settings.wallVerticalSubdivisions;
        for (int column = 0; column < vertexColumns; column++) {
            const std::size_t index = (std::size_t)row * (std::size_t)vertexColumns + (std::size_t)column;
            samples[index].worldPos = wallPoints[index];
            samples[index].normal = wallNormals[index];
            samples[index].heightT = heightT;
            samples[index].edgeWeight = 1.0f;
            samples[index].fadeAtBottom = fadeWallDisplacementAtBottom;
            samples[index].part = WallPart::Face;
        }
    }
    std::vector<WallShade> shades;
    useStyle->shade(samples, shades);
    wallNoise.resize(wallPoints.size());
    for (std::size_t i = 0; i < shades.size(); i++) {
        wallNoise[i] = shades[i].field;
    }

    float resolvedWallMaxOffset = wallMaxOffset;
    if (resolvedWallMaxOffset <= 0.0f) {
        const float vSpacing = (topHeight - baseHeight) / (float)settings.wallVerticalSubdivisions;
        const float segmentLength = horizontalLength(subtract(segment.b, segment.a));
        const float hSpacing = segmentLength > 0.0001f
            ? segmentLength / (float)settings.wallHorizontalSubdivisions
            : vSpacing;
        resolvedWallMaxOffset = 0.7f * std::max(0.0001f, std::min(hSpacing, vSpacing));
    }

    std::vector<Vec3> displaced(wallPoints.size());
    for (std::size_t i = 0; i < wallPoints.size(); i++) {
        displaced[i] = applyWallShade(wallPoints[i], wallNormals[i], shades[i].offset, resolvedWallMaxOffset);
    }

    for (int sy = 0; sy < settings.wallVerticalSubdivisions; sy++) {
        const float v0 = (float)sy / (float)settings.wallVerticalSubdivisions;
        const float v1 = (float)(sy + 1) / (float)settings.wallVerticalSubdivisions;
        const float topT0 = 1.0f - v0;
        const float topT1 = 1.0f - v1;
        for (int sx = 0; sx < settings.wallHorizontalSubdivisions; sx++) {
            const std::size_t i00 = (std::size_t)sy * (std::size_t)vertexColumns + (std::size_t)sx;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = (std::size_t)(sy + 1) * (std::size_t)vertexColumns + (std::size_t)sx;
            const std::size_t i11 = i01 + 1;

            const Vec3 quadOutward = normalizeHorizontal(
                add(
                    add(wallNormals[i00], wallNormals[i10]),
                    add(wallNormals[i01], wallNormals[i11])),
                segment.normal);
            const float panelNoise = (wallNoise[i00] + wallNoise[i10] + wallNoise[i11] + wallNoise[i01]) * 0.25f;
            const float panelHeightFraction = std::clamp((topT0 + topT1) * 0.5f, 0.0f, 1.0f);
            const ColorRgba panelColor = useStyle->color(WallPart::Face, segment.side, (topT0 + topT1) * 0.5f, panelNoise);

            if (outBaseQuads != nullptr) {
                MeshQuad base;
                base.a = wallPoints[i00];
                base.b = wallPoints[i10];
                base.c = wallPoints[i11];
                base.d = wallPoints[i01];
                base.normal = quadOutward;
                base.relief = std::clamp(panelNoise, -1.0f, 1.0f);
                base.heightFraction = panelHeightFraction;
                base.color = panelColor;
                base.cliffWall = true;
                assignWallQuadMetadata(base, segment.side, quadOutward);
                outBaseQuads->push_back(base);
            }

            MeshQuad wall;
            wall.a = displaced[i00];
            wall.b = displaced[i10];
            wall.c = displaced[i11];
            wall.d = displaced[i01];
            wall.normal = displacedFaceNormal(
                wall.a, wall.b, wall.c, wall.d,
                wallPoints[i00], wallPoints[i10], wallPoints[i11], wallPoints[i01],
                quadOutward);
            wall.relief = std::clamp(panelNoise, -1.0f, 1.0f);
            wall.heightFraction = panelHeightFraction;
            wall.color = panelColor;
            wall.cliffWall = true;
            assignWallQuadMetadata(wall, segment.side, quadOutward);
            quads.push_back(wall);
        }
    }

    // Talus apron (playground addition): a straight debris slope leaning
    // against the wall base. The top edge reuses the wall's displaced row at
    // talusHeightFrac, so the apron stitches to the band exactly; the bottom
    // edge is the same contour pushed outward at ground level (shared corner
    // vertices get identical positions, so neighbouring segments stay
    // continuous). Plateau-only: a faded foot has no ground to rest on.
    if (settings.talusWidth > 0.0f && !fadeWallDisplacementAtBottom) {
        const float talusT = std::clamp(settings.talusHeightFrac, 0.05f, 0.6f);
        const int talusRow = std::clamp(
            (int)std::lround((1.0f - talusT) * (float)settings.wallVerticalSubdivisions),
            0,
            settings.wallVerticalSubdivisions);
        for (int column = 0; column + 1 < vertexColumns; column++) {
            const std::size_t i0 = (std::size_t)talusRow * (std::size_t)vertexColumns + (std::size_t)column;
            const std::size_t i1 = i0 + 1;
            const Vec3& n0 = wallNormals[i0];
            const Vec3& n1 = wallNormals[i1];
            MeshQuad talus;
            talus.a = displaced[i0];
            talus.b = displaced[i1];
            talus.c = {
                displaced[i1].x + n1.x * settings.talusWidth,
                baseHeight,
                displaced[i1].z + n1.z * settings.talusWidth};
            talus.d = {
                displaced[i0].x + n0.x * settings.talusWidth,
                baseHeight,
                displaced[i0].z + n0.z * settings.talusWidth};
            talus.normal = displacedFaceNormal(
                talus.a, talus.b, talus.c, talus.d,
                talus.a, talus.b, talus.c, talus.d,
                {0.0f, 1.0f, 0.0f});
            talus.relief = 0.0f;
            talus.heightFraction = 0.0f;
            talus.color = useStyle->color(WallPart::Face, segment.side, 0.0f, 0.0f);
            // Not a wall: the renderer shades it by the facet normal (a sloped
            // apron must catch sky light, not the wall's horizontal hint).
            talus.cliffWall = false;
            talus.talus = true;
            talus.boundarySide = segment.side;
            talus.outwardHint = segment.normal;
            quads.push_back(talus);
        }
    }

    return quads;
}

WallPanelMesh buildWallPanelMesh(const WallPanelBuildRequest& request, const MeshBuildSettings& inputSettings) {
    const float width = std::max(0.1f, request.width);
    const float height = std::max(0.1f, request.height);
    const float baseHeight = -height * 0.5f;
    const float topHeight = height * 0.5f;
    const Vec3 sideNormal = outwardNormalForSide(BoundarySide::Bottom);

    MeshBoundarySegment segment;
    segment.a = {-width * 0.5f, 0.0f, 0.0f};
    segment.b = {width * 0.5f, 0.0f, 0.0f};
    segment.normal = sideNormal;
    segment.startNormal = sideNormal;
    segment.endNormal = sideNormal;
    segment.side = BoundarySide::Bottom;

    WallPanelMesh result;
    result.displacedQuads = buildWallQuadsFromBoundarySegment(
        segment,
        baseHeight,
        topHeight,
        request.fadeDisplacementAtBottom,
        0.0f,
        inputSettings,
        &result.baseQuads);
    return result;
}

std::vector<MeshQuad> buildDisplacedWallPanel(const WallPanelBuildRequest& request, const MeshBuildSettings& inputSettings) {
    const float width = std::max(0.1f, request.width);
    const float height = std::max(0.1f, request.height);
    const float baseHeight = -height * 0.5f;
    const float topHeight = height * 0.5f;
    const Vec3 sideNormal = outwardNormalForSide(BoundarySide::Bottom);

    MeshBoundarySegment segment;
    segment.a = {-width * 0.5f, 0.0f, 0.0f};
    segment.b = {width * 0.5f, 0.0f, 0.0f};
    segment.normal = sideNormal;
    segment.startNormal = sideNormal;
    segment.endNormal = sideNormal;
    segment.side = BoundarySide::Bottom;

    return buildWallPanelMesh(request, inputSettings).displacedQuads;
}

CompositionResult composeLandscapeMesh(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& inputSettings) {
    CompositionResult result;
    if (grid.empty()) {
        return result;
    }

    MeshBuildSettings settings = inputSettings;
    settings.levelHeight = grid.levelHeight;

    auto appendResult = [&](const CompositionResult& part) {
        result.quads.insert(result.quads.end(), part.quads.begin(), part.quads.end());
        result.stats.surfaceTileCount += part.stats.surfaceTileCount;
        result.stats.wallTileCount += part.stats.wallTileCount;
        result.stats.reusedTileMeshCount += part.stats.reusedTileMeshCount;
        result.stats.generatedTileMeshCount += part.stats.generatedTileMeshCount;
        result.stats.uniqueTileMeshCount += part.stats.uniqueTileMeshCount;
        result.stats.topQuadCount += part.stats.topQuadCount;
        result.stats.cliffWallQuadCount += part.stats.cliffWallQuadCount;
        result.stats.boundarySegmentCount += part.stats.boundarySegmentCount;
        result.stats.beveledSegmentCount += part.stats.beveledSegmentCount;
        result.stats.cornerCapCount += part.stats.cornerCapCount;
        mergeNormalOrientation(result.normalOrientation, part.normalOrientation);
    };

    for (int level = 0; level < grid.levelCount; level++) {
        SolidMaskGrid mask;
        mask.width = grid.width;
        mask.height = grid.height;
        mask.solidCells.assign((std::size_t)grid.width * (std::size_t)grid.height, 0);
        mask.topCells.assign(mask.solidCells.size(), 0);
        mask.zones.assign(mask.solidCells.size(), LandscapeZone::Lowland);

        int solidCount = 0;
        int topCount = 0;
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                const std::uint8_t cellLevel = grid.cellLevelAt(x, y);
                const std::size_t index = (std::size_t)mask.cellIndex(x, y);
                const bool solid = level == 0 ? true : cellLevel >= level;
                const bool top = cellLevel == level;
                mask.solidCells[index] = solid ? 1 : 0;
                mask.topCells[index] = top ? 1 : 0;
                mask.zones[index] = grid.zoneAt(x, y);
                if (solid) solidCount++;
                if (top) topCount++;
            }
        }

        if (solidCount == 0 || topCount == 0 && level == 0) {
            continue;
        }

        SolidMeshBuildRequest request;
        request.mask = std::move(mask);
        request.baseHeight = level == 0 ? 0.0f : (float)(level - 1) * grid.levelHeight;
        request.topHeight = (float)level * grid.levelHeight;
        request.level = (std::uint8_t)level;
        request.maxLevel = (std::uint8_t)std::max(1, grid.levelCount - 1);
        request.includeWalls = level > 0;
        request.fadeWallDisplacementAtBottom = level > 0;

        CompositionResult part = composeSolidMaskMesh(request, settings);
        part.stats.surfaceTileCount = topCount;
        part.stats.wallTileCount = part.stats.beveledSegmentCount;
        part.stats.uniqueTileMeshCount = part.stats.beveledSegmentCount + topCount;
        appendResult(part);
    }

    result.seams = validateLandscapeSeams(grid, settings);
    logNormalOrientation("composeLandscapeMesh", result.normalOrientation);
    return result;
}

SeamValidation validateLandscapeSeams(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings) {
    SeamValidation result;
    if (grid.empty()) {
        return result;
    }

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const std::uint8_t level = grid.cellLevelAt(x, y);
            if (x + 1 < grid.width && level != grid.cellLevelAt(x + 1, y)) {
                result.checkedEdges++;
            }
            if (y + 1 < grid.height && level != grid.cellLevelAt(x, y + 1)) {
                result.checkedEdges++;
            }
        }
    }

    result.maxGap = 0.0f;
    result.passed = result.mismatches == 0;
    return result;
}

} // namespace brepmesh
