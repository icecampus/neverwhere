#include "SingleQuadLabScenario.h"

#include "PlaygroundState.h"

#include <cmath>
#include <cstddef>
#include <mutex>
#include <random>

#include <spdlog/spdlog.h>

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

Vec3 quadCentroid(const MeshQuad& quad) {
    return scaleVec(addVec(addVec(quad.a, quad.b), addVec(quad.c, quad.d)), 0.25f);
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

MeshQuad makeBaseQuad(const SingleQuadLabSettings& settings) {
    const float halfWidth = settings.quadWidth * 0.5f;
    const float halfHeight = settings.quadHeight * 0.5f;
    const Vec3 center{settings.centerX, settings.centerY, settings.centerZ};
    const float yaw = settings.yawDegrees * (kPi / 180.0f);
    const float pitch = settings.pitchDegrees * (kPi / 180.0f);

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
    quad.color = IM_COL32(118, 126, 138, 255);
    quad.cliffWall = true;
    quad.depth = meshQuadDepth(quad);
    return quad;
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

Vec3 projectOntoPlane(const Vec3& point, const Vec3& planeOrigin, const Vec3& planeNormal) {
    const Vec3 offset = subtractVec(point, planeOrigin);
    const float alongNormal = dotVec(offset, planeNormal);
    return subtractVec(point, scaleVec(planeNormal, alongNormal));
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

void appendExtrudedQuad(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int heightSeed,
    bool colorizeFaces) {

    if (depth <= 0.0001f) {
        out.push_back(quad);
        return;
    }

    const Vec3 extrudeDir = normalizeVec(extrudeNormal, quad.outwardHint);
    const Vec3 center = quadCentroid(quad);
    const float topScale = clampFloat(topCornerScale, 0.01f, 1.0f);
    const float heightSpread = clampFloat(topHeightSpread, 0.0f, 1.0f);
    const float scaleSpread = clampFloat(topScaleSpread, 0.0f, 1.0f);

    std::mt19937 heightRng((unsigned)heightSeed * 2654435761u + 424242u);
    std::mt19937 scaleRng((unsigned)heightSeed * 2654435761u + 818181u);
    const float liftDepthA = cornerExtrudeDepth(depth, heightSpread, heightRng);
    const float liftDepthB = cornerExtrudeDepth(depth, heightSpread, heightRng);
    const float liftDepthC = cornerExtrudeDepth(depth, heightSpread, heightRng);
    const float liftDepthD = cornerExtrudeDepth(depth, heightSpread, heightRng);
    const float cornerScaleA = cornerExtrudeScale(topScale, scaleSpread, scaleRng);
    const float cornerScaleB = cornerExtrudeScale(topScale, scaleSpread, scaleRng);
    const float cornerScaleC = cornerExtrudeScale(topScale, scaleSpread, scaleRng);
    const float cornerScaleD = cornerExtrudeScale(topScale, scaleSpread, scaleRng);

    const Vec3 topA = liftCorner(quad.a, center, cornerScaleA, extrudeDir, liftDepthA);
    const Vec3 topB = liftCorner(quad.b, center, cornerScaleB, extrudeDir, liftDepthB);
    const Vec3 topC = liftCorner(quad.c, center, cornerScaleC, extrudeDir, liftDepthC);
    const Vec3 topD = liftCorner(quad.d, center, cornerScaleD, extrudeDir, liftDepthD);

    const ImU32 bottomColor = colorizeFaces ? IM_COL32(118, 126, 138, 255) : quad.color;
    const ImU32 topColor = colorizeFaces ? IM_COL32(168, 176, 188, 255) : quad.color;
    const ImU32 sideColors[4] = {
        colorizeFaces ? IM_COL32(196, 128, 88, 255) : quad.color,
        colorizeFaces ? IM_COL32(176, 112, 78, 255) : quad.color,
        colorizeFaces ? IM_COL32(156, 98, 70, 255) : quad.color,
        colorizeFaces ? IM_COL32(176, 112, 78, 255) : quad.color,
    };

    MeshQuad bottom = quad;
    bottom.color = bottomColor;
    ensureOutwardWinding(bottom, extrudeDir);
    bottom.outwardHint = bottom.normal;
    bottom.depth = meshQuadDepth(bottom);
    out.push_back(bottom);

    appendConvexTopCap(out, topA, topB, topC, topD, center, extrudeDir, topColor, quad);

    appendOrientedConvexSidePanels(out, quad.a, quad.b, topB, topA, center, quad, sideColors[0]);
    appendOrientedConvexSidePanels(out, quad.b, quad.c, topC, topB, center, quad, sideColors[1]);
    appendOrientedConvexSidePanels(out, quad.c, quad.d, topD, topC, center, quad, sideColors[2]);
    appendOrientedConvexSidePanels(out, quad.a, quad.d, topD, topA, center, quad, sideColors[3]);
}

void addMeshQuad(SingleQuadLabModel& model, MeshQuad quad) {
    quad.depth = meshQuadDepth(quad);
    model.quads.push_back(quad);
}

} // namespace

void sanitizeSingleQuadLabSettings(SingleQuadLabSettings& settings) {
    settings.quadWidth = clampFloat(settings.quadWidth, 0.1f, 8.0f);
    settings.quadHeight = clampFloat(settings.quadHeight, 0.1f, 8.0f);
    settings.yawDegrees = clampFloat(settings.yawDegrees, -180.0f, 180.0f);
    settings.pitchDegrees = clampFloat(settings.pitchDegrees, -89.0f, 89.0f);
    settings.centerX = clampFloat(settings.centerX, -8.0f, 8.0f);
    settings.centerY = clampFloat(settings.centerY, -2.0f, 8.0f);
    settings.centerZ = clampFloat(settings.centerZ, -8.0f, 8.0f);
    settings.extrudeDepth = clampFloat(settings.extrudeDepth, 0.0f, 2.0f);
    settings.extrudeTopScale = clampFloat(settings.extrudeTopScale, 0.1f, 1.0f);
    settings.extrudeTopHeightSpread = clampFloat(settings.extrudeTopHeightSpread, 0.0f, 1.0f);
    settings.extrudeTopScaleSpread = clampFloat(settings.extrudeTopScaleSpread, 0.0f, 1.0f);
}

void rebuildSingleQuadLabModel() {
    SingleQuadLabSettings settings = g_singleQuadLabSettings;
    sanitizeSingleQuadLabSettings(settings);

    SingleQuadLabModel model;
    model.baseQuad = makeBaseQuad(settings);

    if (settings.operation == QuadLabOperation::Extrude && settings.extrudeDepth > 0.0001f) {
        appendExtrudedQuad(
            model.quads,
            model.baseQuad,
            model.baseQuad.normal,
            settings.extrudeDepth,
            settings.extrudeTopScale,
            settings.extrudeTopHeightSpread,
            settings.extrudeTopScaleSpread,
            settings.extrudeHeightSeed,
            settings.colorizeFaces);
    } else {
        addMeshQuad(model, model.baseQuad);
    }

    model.panelCount = (int)model.quads.size();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_singleQuadLabSettings = settings;
        g_singleQuadLabModel = std::move(model);
    }

    spdlog::info(
        "rebuildSingleQuadLabModel: operation={}, panels={}, extrudeDepth={:.3f}, topScale={:.3f}, heightSpread={:.3f}, scaleSpread={:.3f}",
        settings.operation == QuadLabOperation::Extrude ? "extrude" : "flat",
        g_singleQuadLabModel.panelCount,
        settings.extrudeDepth,
        settings.extrudeTopScale,
        settings.extrudeTopHeightSpread,
        settings.extrudeTopScaleSpread);
}

bool runSingleQuadLabSmokeTest() {
    const SingleQuadLabSettings previousSettings = g_singleQuadLabSettings;

    SingleQuadLabSettings testSettings = previousSettings;
    testSettings.operation = QuadLabOperation::Extrude;
    testSettings.extrudeDepth = 0.25f;
    testSettings.extrudeTopScale = 0.9f;
    testSettings.extrudeTopHeightSpread = 0.35f;
    testSettings.extrudeTopScaleSpread = 0.35f;
    testSettings.extrudeHeightSeed = 1337;
    g_singleQuadLabSettings = testSettings;
    rebuildSingleQuadLabModel();

    const int panelCount = g_singleQuadLabModel.panelCount;
    bool bowTieDetected = false;
    bool uniformTopHeights = true;
    bool uniformTopScales = true;
    Vec3 topA;
    Vec3 topB;
    Vec3 topC;
    Vec3 topD;
    if (panelCount >= 3
        && meshQuadIsTrianglePanel(g_singleQuadLabModel.quads[1])
        && meshQuadIsTrianglePanel(g_singleQuadLabModel.quads[2])
        && assignTopCapCornersFromPanels(
            g_singleQuadLabModel.baseQuad,
            g_singleQuadLabModel.quads[1],
            g_singleQuadLabModel.quads[2],
            topA,
            topB,
            topC,
            topD)) {
        const MeshQuad& baseQuad = g_singleQuadLabModel.baseQuad;
        const Vec3 center = quadCentroid(baseQuad);
        const Vec3 extrudeDir = normalizeVec(baseQuad.normal, baseQuad.outwardHint);
        const auto liftAlongExtrude = [&](const Vec3& baseCorner, const Vec3& topCorner) {
            return dotVec(subtractVec(topCorner, baseCorner), extrudeDir);
        };
        const auto pinchRatio = [&](const Vec3& baseCorner, const Vec3& topCorner) {
            const Vec3 baseFlat = projectOntoPlane(baseCorner, center, extrudeDir);
            const Vec3 topFlat = projectOntoPlane(topCorner, center, extrudeDir);
            const Vec3 baseVec = subtractVec(baseFlat, center);
            const Vec3 topVec = subtractVec(topFlat, center);
            const float baseLenSq = dotVec(baseVec, baseVec);
            const float topLenSq = dotVec(topVec, topVec);
            if (baseLenSq <= 0.0001f) {
                return 1.0f;
            }
            return std::sqrt(topLenSq / baseLenSq);
        };

        const float liftA = liftAlongExtrude(baseQuad.a, topA);
        const float liftB = liftAlongExtrude(baseQuad.b, topB);
        const float liftC = liftAlongExtrude(baseQuad.c, topC);
        const float liftD = liftAlongExtrude(baseQuad.d, topD);
        uniformTopHeights =
            std::fabs(liftB - liftA) <= 0.0001f
            && std::fabs(liftC - liftA) <= 0.0001f
            && std::fabs(liftD - liftA) <= 0.0001f;

        const float pinchA = pinchRatio(baseQuad.a, topA);
        const float pinchB = pinchRatio(baseQuad.b, topB);
        const float pinchC = pinchRatio(baseQuad.c, topC);
        const float pinchD = pinchRatio(baseQuad.d, topD);
        uniformTopScales =
            std::fabs(pinchB - pinchA) <= 0.0001f
            && std::fabs(pinchC - pinchA) <= 0.0001f
            && std::fabs(pinchD - pinchA) <= 0.0001f;
    }
    for (const MeshQuad& quad : g_singleQuadLabModel.quads) {
        if (meshQuadIsTrianglePanel(quad)) {
            continue;
        }
        const Vec3 diagonalAc = subtractVec(quad.c, quad.a);
        const Vec3 diagonalBd = subtractVec(quad.d, quad.b);
        if (dotVec(crossVec(diagonalAc, diagonalBd), quad.normal) < 0.0f) {
            bowTieDetected = true;
            break;
        }
    }

    g_singleQuadLabSettings = previousSettings;
    rebuildSingleQuadLabModel();

    const bool ok = panelCount == 11 && !bowTieDetected && !uniformTopHeights && !uniformTopScales;
    spdlog::info(
        "{} single quad lab extrude: panels={}, bowTie={}, uniformTopHeights={}, uniformTopScales={}",
        ok ? "TEST PASS" : "TEST FAIL",
        panelCount,
        bowTieDetected,
        uniformTopHeights,
        uniformTopScales);
    return ok;
}

} // namespace meshgen_playground
