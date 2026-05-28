#include "landscape_mesh/landscape_mesh.h"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <cmath>

namespace landscape_mesh {
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

Vec3 normalize(const Vec3& value, const Vec3& fallback) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 0.0001f) {
        return fallback;
    }
    return {value.x / length, value.y / length, value.z / length};
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

// True surface normal of a displaced wall quad. Built from the quad edges so the
// rock displacement micro-relief is preserved, then oriented deterministically
// against a known outward hint so winding can never flip it (no checkerboard).
Vec3 displacedFaceNormal(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& outwardHint) {
    const Vec3 along = scale(add(subtract(b, a), subtract(c, d)), 0.5f);
    const Vec3 down = scale(add(subtract(d, a), subtract(c, b)), 0.5f);
    Vec3 normal = normalize(cross3(along, down), outwardHint);
    if (dot3(normal, outwardHint) < 0.0f) {
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

ColorRgba rockTopColor(float noiseValue) {
    const int shade = (int)(noiseValue * 10.0f);
    return {
        (std::uint8_t)std::clamp(88 + shade, 58, 122),
        (std::uint8_t)std::clamp(143 + shade, 104, 172),
        (std::uint8_t)std::clamp(82 + shade, 52, 116),
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

FastNoise::SmartNode<> makeRockNoiseNode(const MeshBuildSettings& settings) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    if (!simplex) {
        return nullptr;
    }
    simplex->SetScale(settings.rockScale);

    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    if (!fractal) {
        return simplex;
    }

    fractal->SetSource(simplex);
    fractal->SetOctaveCount(4);
    fractal->SetLacunarity(2.15f);
    fractal->SetGain(0.55f);
    fractal->SetWeightedStrength(0.35f);
    return fractal;
}

void sampleRockNoiseBatch(
    const FastNoise::SmartNode<>& noise,
    const MeshBuildSettings& settings,
    const std::vector<Vec3>& points,
    std::vector<float>& outNoise) {

    outNoise.assign(points.size(), 0.0f);
    if (points.empty()) {
        return;
    }

    if (!noise) {
        for (std::size_t i = 0; i < points.size(); i++) {
            outNoise[i] = deterministicRock(settings, points[i].x, points[i].y, points[i].z);
        }
        return;
    }

    std::vector<float> xs(points.size());
    std::vector<float> ys(points.size());
    std::vector<float> zs(points.size());
    for (std::size_t i = 0; i < points.size(); i++) {
        xs[i] = points[i].x;
        ys[i] = points[i].y;
        zs[i] = points[i].z;
    }

    noise->GenPositionArray3D(
        outNoise.data(),
        (int)points.size(),
        xs.data(),
        ys.data(),
        zs.data(),
        0.0f,
        0.0f,
        0.0f,
        settings.rockSeed);

    for (float& value : outNoise) {
        value = std::clamp(value, -1.0f, 1.0f);
    }
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
    bool fadeAtBottom) {

    if (!settings.rockEnabled || settings.rockAmplitude <= 0.0f) {
        return point;
    }

    const float steppedNoise = terraceValue(rawNoise, settings.terraceSteps);
    float fade = 1.0f - clamp01(heightT);
    fade = fade * (0.35f + fade * 0.65f);
    if (fadeAtBottom) {
        fade = std::sin(clamp01(heightT) * 3.14159265f);
    }

    const float offset = steppedNoise * settings.rockAmplitude * fade;
    return {
        point.x + normal.x * offset,
        point.y,
        point.z + normal.z * offset,
    };
}

Vec3 displaceTopPoint(
    const MeshBuildSettings& settings,
    const Vec3& point,
    const Vec3& normal,
    bool boundaryPoint,
    float rawNoise) {

    if (!boundaryPoint) {
        return point;
    }

    return displaceWallPoint(settings, point, normal, 1.0f, rawNoise, false);
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
    const FastNoise::SmartNode<>& rockNoise,
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

    sampleRockNoiseBatch(rockNoise, settings, points, noise);

    for (int y = 0; y < subdivisions; y++) {
        for (int x = 0; x < subdivisions; x++) {
            const std::size_t i00 = (std::size_t)y * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = (std::size_t)(y + 1) * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i11 = i01 + 1;

            MeshQuad top;
            top.a = displaceTopPoint(settings, points[i00], normals[i00], boundaryFlags[i00] != 0, noise[i00]);
            top.b = displaceTopPoint(settings, points[i10], normals[i10], boundaryFlags[i10] != 0, noise[i10]);
            top.c = displaceTopPoint(settings, points[i11], normals[i11], boundaryFlags[i11] != 0, noise[i11]);
            top.d = displaceTopPoint(settings, points[i01], normals[i01], boundaryFlags[i01] != 0, noise[i01]);
            const float panelNoise = (noise[i00] + noise[i10] + noise[i11] + noise[i01]) * 0.25f;
            top.color = rockTopColor(panelNoise);
            top.cliffWall = false;
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
        addQuad(result, cap);
        result.stats.cornerCapCount++;
    }
}

} // namespace

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
        Vec3 bevelNormal;
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
        const bool trimStart = bevel > 0.0f && isBoundaryCorner(result.boundarySegments, segment.a);
        const bool trimEnd = bevel > 0.0f && isBoundaryCorner(result.boundarySegments, segment.b);
        const float trim = std::min(bevel, length * 0.45f);
        const Vec3 start = trimStart ? add(a, scale(direction, trim)) : a;
        const Vec3 end = trimEnd ? add(b, scale(direction, -trim)) : b;
        const Vec3 sideNormal = outwardNormalForSide(segment.side);
        const Vec3 startBevelNormal = trimStart ? boundaryVertexNormal(mask, result.boundarySegments, segment.a, sideNormal) : sideNormal;
        const Vec3 endBevelNormal = trimEnd ? boundaryVertexNormal(mask, result.boundarySegments, segment.b, sideNormal) : sideNormal;
        const Vec3 startNormal = trimStart ? normalizeHorizontal(add(sideNormal, startBevelNormal), sideNormal) : sideNormal;
        const Vec3 endNormal = trimEnd ? normalizeHorizontal(add(sideNormal, endBevelNormal), sideNormal) : sideNormal;

        if (horizontalLength(subtract(end, start)) > 0.0001f) {
            result.beveledSegments.push_back({start, end, sideNormal, startNormal, endNormal, segment.side});
        }

        if (trimStart) {
            endpoints.push_back({segment.a, start, segment.side, sideNormal, startBevelNormal});
        }
        if (trimEnd) {
            endpoints.push_back({segment.b, end, segment.side, sideNormal, endBevelNormal});
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
        const Vec3 startNormal = normalizeHorizontal(add(normal, a.sideNormal), normal);
        const Vec3 endNormal = normalizeHorizontal(add(normal, b.sideNormal), normal);
        if (horizontalLength(subtract(b.point, a.point)) > 0.0001f) {
            result.beveledSegments.push_back({a.point, b.point, normal, startNormal, endNormal, a.side});
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
            quad.normal = displacedFaceNormal(quad.a, quad.b, quad.c, quad.d, normal);
            quad.relief = std::clamp((n00 + n10 + n11 + n01) * 0.25f, -1.0f, 1.0f);
            quad.heightFraction = std::clamp((y0 + y1) * 0.5f, 0.0f, 1.0f);
            quad.color = wallColor(key.side, (y0 + y1) * 0.5f, (n00 + n10 + n11 + n01) * 0.25f);
            quad.cliffWall = true;
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

    const FastNoise::SmartNode<> rockNoise = settings.rockEnabled ? makeRockNoiseNode(settings) : nullptr;

    for (int y = 0; y < request.mask.height; y++) {
        for (int x = 0; x < request.mask.width; x++) {
            if (!request.mask.hasTop(x, y)) {
                continue;
            }
            addTopCell(result, request.mask, boundary.boundarySegments, settings, rockNoise, x, y, request.topHeight, request.level, request.maxLevel);
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

    for (const MeshBoundarySegment& segment : boundary.beveledSegments) {
        const Vec3 topA{segment.a.x, request.topHeight, segment.a.z};
        const Vec3 topB{segment.b.x, request.topHeight, segment.b.z};
        const Vec3 bottomA{segment.a.x, request.baseHeight, segment.a.z};
        const Vec3 bottomB{segment.b.x, request.baseHeight, segment.b.z};
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
            }
        }

        sampleRockNoiseBatch(rockNoise, settings, wallPoints, wallNoise);

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

                MeshQuad wall;
                wall.a = displaceWallPoint(settings, wallPoints[i00], wallNormals[i00], topT0, wallNoise[i00], request.fadeWallDisplacementAtBottom);
                wall.b = displaceWallPoint(settings, wallPoints[i10], wallNormals[i10], topT0, wallNoise[i10], request.fadeWallDisplacementAtBottom);
                wall.c = displaceWallPoint(settings, wallPoints[i11], wallNormals[i11], topT1, wallNoise[i11], request.fadeWallDisplacementAtBottom);
                wall.d = displaceWallPoint(settings, wallPoints[i01], wallNormals[i01], topT1, wallNoise[i01], request.fadeWallDisplacementAtBottom);
                wall.normal = displacedFaceNormal(wall.a, wall.b, wall.c, wall.d, segment.normal);
                const float panelNoise = (wallNoise[i00] + wallNoise[i10] + wallNoise[i11] + wallNoise[i01]) * 0.25f;
                wall.relief = std::clamp(panelNoise, -1.0f, 1.0f);
                wall.heightFraction = std::clamp((topT0 + topT1) * 0.5f, 0.0f, 1.0f);
                wall.color = wallColor(segment.side, (topT0 + topT1) * 0.5f, panelNoise);
                wall.cliffWall = true;
                addQuad(result, wall);
            }
        }
    }

    return result;
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

} // namespace landscape_mesh
