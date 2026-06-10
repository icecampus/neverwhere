#include "QuadLabMeshOps.h"

#include <cmath>
#include <cstddef>
#include <random>

namespace meshgen_playground {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec3 addVec(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 subtractVec(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 scaleVec(const Vec3& value, float scalar) {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dotVec(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 crossVec(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 normalizeVec(const Vec3& value, const Vec3& fallback) {
    const float lengthSq = dotVec(value, value);
    if (lengthSq <= 0.0001f) {
        return fallback;
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    return scaleVec(value, invLength);
}

Vec3 quadAreaNormal(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
    return crossVec(subtractVec(b, a), subtractVec(c, a));
}

Vec3 pinchCornerTowardCenter(const Vec3& corner, const Vec3& center, float cornerScale) {
    return addVec(center, scaleVec(subtractVec(corner, center), cornerScale));
}

Vec3 rotateYawPitch(const Vec3& value, float yawRadians, float pitchRadians) {
    const float cosYaw = std::cos(yawRadians);
    const float sinYaw = std::sin(yawRadians);
    const float cosPitch = std::cos(pitchRadians);
    const float sinPitch = std::sin(pitchRadians);

    const float yawX = value.x * cosYaw - value.z * sinYaw;
    const float yawZ = value.x * sinYaw + value.z * cosYaw;
    const Vec3 yawed{yawX, value.y, yawZ};

    return {
        yawed.x,
        yawed.y * cosPitch - yawed.z * sinPitch,
        yawed.y * sinPitch + yawed.z * cosPitch,
    };
}

void ensureOutwardWinding(MeshQuad& quad, const Vec3& outwardDir) {
    Vec3 areaNormal = quadAreaNormal(quad.a, quad.b, quad.c, quad.d);
    areaNormal = normalizeVec(areaNormal, outwardDir);
    if (dotVec(areaNormal, outwardDir) < 0.0f) {
        const Vec3 reversedB = quad.d;
        const Vec3 reversedD = quad.b;
        quad.b = reversedB;
        quad.d = reversedD;
        areaNormal = scaleVec(areaNormal, -1.0f);
    }
    quad.normal = normalizeVec(areaNormal, outwardDir);
}

Vec3 resolveSideOutwardDir(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& shellCentroid,
    const MeshQuad& source,
    Vec3& outA,
    Vec3& outB,
    Vec3& outC,
    Vec3& outD) {
    outA = a;
    outB = b;
    outC = c;
    outD = d;

    Vec3 areaNormal = quadAreaNormal(outA, outB, outC, outD);
    const Vec3 sideMid = scaleVec(addVec(addVec(outA, outB), addVec(outC, outD)), 0.25f);
    const Vec3 outwardHint = subtractVec(sideMid, shellCentroid);
    areaNormal = normalizeVec(areaNormal, source.normal);
    if (dotVec(areaNormal, outwardHint) < 0.0f) {
        const Vec3 reversedB = outD;
        const Vec3 reversedD = outB;
        outB = reversedB;
        outD = reversedD;
        areaNormal = scaleVec(areaNormal, -1.0f);
    }
    return normalizeVec(areaNormal, source.normal);
}

float cornerExtrudeDepth(float baseDepth, float heightSpread, std::mt19937& rng) {
    if (heightSpread <= 0.0001f) {
        return baseDepth;
    }
    std::uniform_real_distribution<float> heightFactor(1.0f - heightSpread, 1.0f + heightSpread);
    return baseDepth * heightFactor(rng);
}

float cornerExtrudeScale(float baseScale, float scaleSpread, std::mt19937& rng) {
    if (scaleSpread <= 0.0001f) {
        return baseScale;
    }
    std::uniform_real_distribution<float> scaleFactor(1.0f - scaleSpread, 1.0f + scaleSpread);
    return clampFloat(baseScale * scaleFactor(rng), 0.01f, 1.0f);
}

Vec3 liftCorner(
    const Vec3& corner,
    const Vec3& center,
    float topScale,
    const Vec3& extrudeDir,
    float liftDepth) {
    return addVec(pinchCornerTowardCenter(corner, center, topScale), scaleVec(extrudeDir, liftDepth));
}

MeshQuad makeTrianglePanel(
    const Vec3& p0,
    const Vec3& p1,
    const Vec3& p2,
    const Vec3& outwardDir,
    ImU32 color,
    const MeshQuad& source) {
    MeshQuad tri = source;
    tri.a = p0;
    tri.b = p1;
    tri.c = p2;
    tri.d = p2;
    tri.color = color;

    Vec3 normal = crossVec(subtractVec(p1, p0), subtractVec(p2, p0));
    normal = normalizeVec(normal, outwardDir);
    if (dotVec(normal, outwardDir) < 0.0f) {
        tri.b = p2;
        tri.c = p1;
        normal = scaleVec(normal, -1.0f);
        normal = normalizeVec(normal, outwardDir);
    }
    tri.normal = normal;
    tri.outwardHint = normal;
    tri.depth = meshQuadDepth(tri);
    return tri;
}

void appendConvexQuadAsTrianglePanels(
    std::vector<MeshQuad>& out,
    const Vec3& cornerA,
    const Vec3& cornerB,
    const Vec3& cornerC,
    const Vec3& cornerD,
    const Vec3& referenceCenter,
    const Vec3& outwardDir,
    ImU32 color,
    const MeshQuad& source) {
    const auto heightAlongOutward = [&](const Vec3& point) {
        return dotVec(subtractVec(point, referenceCenter), outwardDir);
    };
    const float cornerHeights[4] = {
        heightAlongOutward(cornerA),
        heightAlongOutward(cornerB),
        heightAlongOutward(cornerC),
        heightAlongOutward(cornerD),
    };

    if (meshQuadPreferAcDiagonalFromHeights(cornerHeights)) {
        out.push_back(makeTrianglePanel(cornerA, cornerB, cornerC, outwardDir, color, source));
        out.push_back(makeTrianglePanel(cornerA, cornerC, cornerD, outwardDir, color, source));
    } else {
        out.push_back(makeTrianglePanel(cornerA, cornerB, cornerD, outwardDir, color, source));
        out.push_back(makeTrianglePanel(cornerB, cornerC, cornerD, outwardDir, color, source));
    }
}

void appendConvexTopCap(
    std::vector<MeshQuad>& out,
    const Vec3& topA,
    const Vec3& topB,
    const Vec3& topC,
    const Vec3& topD,
    const Vec3& referenceCenter,
    const Vec3& extrudeDir,
    ImU32 color,
    const MeshQuad& source) {
    appendConvexQuadAsTrianglePanels(out, topA, topB, topC, topD, referenceCenter, extrudeDir, color, source);
}

void appendOrientedConvexSidePanels(
    std::vector<MeshQuad>& out,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& shellCentroid,
    const MeshQuad& source,
    ImU32 color) {
    Vec3 cornerA;
    Vec3 cornerB;
    Vec3 cornerC;
    Vec3 cornerD;
    const Vec3 outwardDir = resolveSideOutwardDir(a, b, c, d, shellCentroid, source, cornerA, cornerB, cornerC, cornerD);
    const Vec3 sideCenter = scaleVec(addVec(addVec(cornerA, cornerB), addVec(cornerC, cornerD)), 0.25f);
    appendConvexQuadAsTrianglePanels(
        out,
        cornerA,
        cornerB,
        cornerC,
        cornerD,
        sideCenter,
        outwardDir,
        color,
        source);
}

ExtrudeQuadColors defaultExtrudeColors() {
    ExtrudeQuadColors colors;
    colors.bottom = IM_COL32(118, 126, 138, 255);
    colors.top = IM_COL32(168, 176, 188, 255);
    colors.sides[0] = IM_COL32(196, 128, 88, 255);
    colors.sides[1] = IM_COL32(176, 112, 78, 255);
    colors.sides[2] = IM_COL32(156, 98, 70, 255);
    colors.sides[3] = IM_COL32(176, 112, 78, 255);
    return colors;
}

} // namespace

MeshQuad makeOrientedQuad(const OrientedQuadParams& params) {
    const float halfWidth = params.width * 0.5f;
    const float halfHeight = params.height * 0.5f;
    const Vec3 center{params.centerX, params.centerY, params.centerZ};
    const float yaw = params.yawDegrees * (kPi / 180.0f);
    const float pitch = params.pitchDegrees * (kPi / 180.0f);

    const Vec3 localA{-halfWidth, -halfHeight, 0.0f};
    const Vec3 localB{halfWidth, -halfHeight, 0.0f};
    const Vec3 localC{halfWidth, halfHeight, 0.0f};
    const Vec3 localD{-halfWidth, halfHeight, 0.0f};

    MeshQuad quad;
    quad.a = addVec(center, rotateYawPitch(localA, yaw, pitch));
    quad.b = addVec(center, rotateYawPitch(localB, yaw, pitch));
    quad.c = addVec(center, rotateYawPitch(localC, yaw, pitch));
    quad.d = addVec(center, rotateYawPitch(localD, yaw, pitch));
    quad.normal = normalizeVec(
        quadAreaNormal(quad.a, quad.b, quad.c, quad.d),
        rotateYawPitch({0.0f, 0.0f, 1.0f}, yaw, pitch));
    quad.outwardHint = quad.normal;
    quad.color = params.color;
    quad.cliffWall = true;
    quad.depth = meshQuadDepth(quad);
    return quad;
}

Vec3 quadCentroid(const MeshQuad& quad) {
    return scaleVec(addVec(addVec(quad.a, quad.b), addVec(quad.c, quad.d)), 0.25f);
}

Vec3 projectOntoPlane(const Vec3& point, const Vec3& planeOrigin, const Vec3& planeNormal) {
    const Vec3 offset = subtractVec(point, planeOrigin);
    const float alongNormal = dotVec(offset, planeNormal);
    return subtractVec(point, scaleVec(planeNormal, alongNormal));
}

Vec3 liftExtrudeCornerFromSeed(
    const Vec3& corner,
    const Vec3& shellCentroid,
    const Vec3& extrudeDir,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int cornerSeed) {

    const float topScale = clampFloat(topCornerScale, 0.01f, 1.0f);
    const float heightSpread = clampFloat(topHeightSpread, 0.0f, 1.0f);
    const float scaleSpread = clampFloat(topScaleSpread, 0.0f, 1.0f);

    std::mt19937 heightRng((unsigned)cornerSeed * 2654435761u + 424242u);
    std::mt19937 scaleRng((unsigned)cornerSeed * 2654435761u + 818181u);
    const float liftDepth = cornerExtrudeDepth(depth, heightSpread, heightRng);
    const float cornerScale = cornerExtrudeScale(topScale, scaleSpread, scaleRng);
    return liftCorner(corner, shellCentroid, cornerScale, extrudeDir, liftDepth);
}

void appendExtrudedQuadWithShell(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    const Vec3& topA,
    const Vec3& topB,
    const Vec3& topC,
    const Vec3& topD,
    const Vec3& shellCentroid,
    bool colorizeFaces,
    const bool emitSides[4],
    const ExtrudeQuadColors* customColors) {

    const Vec3 extrudeDir = normalizeVec(extrudeNormal, quad.outwardHint);

    ExtrudeQuadColors colors = customColors ? *customColors : defaultExtrudeColors();
    if (!colorizeFaces) {
        colors.bottom = quad.color;
        colors.top = quad.color;
        for (int i = 0; i < 4; i++) {
            colors.sides[i] = quad.color;
        }
    }

    MeshQuad bottom = quad;
    bottom.color = colors.bottom;
    bottom.normal = normalizeVec(extrudeDir, quad.normal);
    bottom.outwardHint = quad.outwardHint;
    bottom.depth = meshQuadDepth(bottom);
    out.push_back(bottom);

    appendConvexTopCap(out, topA, topB, topC, topD, shellCentroid, extrudeDir, colors.top, quad);

    if (emitSides[0]) {
        appendOrientedConvexSidePanels(out, quad.a, quad.b, topB, topA, shellCentroid, quad, colors.sides[0]);
    }
    if (emitSides[1]) {
        appendOrientedConvexSidePanels(out, quad.b, quad.c, topC, topB, shellCentroid, quad, colors.sides[1]);
    }
    if (emitSides[2]) {
        appendOrientedConvexSidePanels(out, quad.c, quad.d, topD, topC, shellCentroid, quad, colors.sides[2]);
    }
    if (emitSides[3]) {
        appendOrientedConvexSidePanels(out, quad.a, quad.d, topD, topA, shellCentroid, quad, colors.sides[3]);
    }
}

void appendExtrudedQuad(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int heightSeed,
    bool colorizeFaces,
    const ExtrudeQuadColors* customColors) {

    if (depth <= 0.0001f) {
        out.push_back(quad);
        return;
    }

    const Vec3 extrudeDir = normalizeVec(extrudeNormal, quad.outwardHint);
    const Vec3 center = quadCentroid(quad);
    const Vec3 topA = liftExtrudeCornerFromSeed(
        quad.a, center, extrudeDir, depth, topCornerScale, topHeightSpread, topScaleSpread, heightSeed + 0);
    const Vec3 topB = liftExtrudeCornerFromSeed(
        quad.b, center, extrudeDir, depth, topCornerScale, topHeightSpread, topScaleSpread, heightSeed + 1);
    const Vec3 topC = liftExtrudeCornerFromSeed(
        quad.c, center, extrudeDir, depth, topCornerScale, topHeightSpread, topScaleSpread, heightSeed + 2);
    const Vec3 topD = liftExtrudeCornerFromSeed(
        quad.d, center, extrudeDir, depth, topCornerScale, topHeightSpread, topScaleSpread, heightSeed + 3);

    const bool emitAllSides[4] = {true, true, true, true};
    appendExtrudedQuadWithShell(
        out,
        quad,
        extrudeNormal,
        topA,
        topB,
        topC,
        topD,
        center,
        colorizeFaces,
        emitAllSides,
        customColors);
}

void appendExtrudedQuadWithDisplacedBottom(
    std::vector<MeshQuad>& out,
    const MeshQuad& baseQuad,
    const MeshQuad& displacedBottom,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int heightSeed,
    bool colorizeFaces,
    const ExtrudeQuadColors* customColors) {

    const std::size_t panelStart = out.size();
    appendExtrudedQuad(
        out,
        baseQuad,
        extrudeNormal,
        depth,
        topCornerScale,
        topHeightSpread,
        topScaleSpread,
        heightSeed,
        colorizeFaces,
        customColors);
    if (out.size() <= panelStart) {
        return;
    }

    MeshQuad bottom = displacedBottom;
    if (colorizeFaces) {
        const ExtrudeQuadColors colors = customColors ? *customColors : defaultExtrudeColors();
        bottom.color = colors.bottom;
    }
    bottom.depth = meshQuadDepth(bottom);
    out[panelStart] = bottom;
}

bool assignTopCapCornersFromPanels(
    const MeshQuad& baseQuad,
    const MeshQuad& topTri0,
    const MeshQuad& topTri1,
    Vec3& outA,
    Vec3& outB,
    Vec3& outC,
    Vec3& outD) {
    const Vec3 center = quadCentroid(baseQuad);
    const Vec3 extrudeDir = normalizeVec(baseQuad.normal, baseQuad.outwardHint);
    const Vec3 baseCorners[4] = {baseQuad.a, baseQuad.b, baseQuad.c, baseQuad.d};
    const Vec3 candidates[6] = {topTri0.a, topTri0.b, topTri0.c, topTri1.a, topTri1.b, topTri1.c};
    bool used[6] = {};

    Vec3* outputs[4] = {&outA, &outB, &outC, &outD};
    for (int cornerIndex = 0; cornerIndex < 4; cornerIndex++) {
        const Vec3 baseFlat = projectOntoPlane(baseCorners[cornerIndex], center, extrudeDir);
        int bestCandidate = -1;
        float bestScore = -1.0f;
        for (int candidateIndex = 0; candidateIndex < 6; candidateIndex++) {
            if (used[candidateIndex]) {
                continue;
            }
            const Vec3 candidateFlat = projectOntoPlane(candidates[candidateIndex], center, extrudeDir);
            const Vec3 delta = subtractVec(candidateFlat, baseFlat);
            const float score = -dotVec(delta, delta);
            if (score > bestScore) {
                bestScore = score;
                bestCandidate = candidateIndex;
            }
        }
        if (bestCandidate < 0) {
            return false;
        }
        used[bestCandidate] = true;
        *outputs[cornerIndex] = candidates[bestCandidate];
    }
    return true;
}

ImU32 quadLabPanelTintColor(int panelIndex, int panelCount) {
    const int safeCount = std::max(panelCount, 1);
    const float hue = (float)(panelIndex % safeCount) / (float)safeCount;
    const float r = 0.55f + 0.25f * std::sin(hue * 6.2831853f);
    const float g = 0.58f + 0.18f * std::sin(hue * 6.2831853f + 2.094f);
    const float b = 0.62f + 0.16f * std::sin(hue * 6.2831853f + 4.188f);
    return IM_COL32(
        (int)(r * 255.0f),
        (int)(g * 255.0f),
        (int)(b * 255.0f),
        255);
}

} // namespace meshgen_playground
