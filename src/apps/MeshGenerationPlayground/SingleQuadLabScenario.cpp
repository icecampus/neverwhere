#include "SingleQuadLabScenario.h"

#include "PlaygroundState.h"

#include <cmath>
#include <cstddef>
#include <mutex>

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

MeshQuad orientedSideQuad(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& centroid,
    const MeshQuad& source,
    ImU32 color) {

    MeshQuad side = source;
    side.a = a;
    side.b = b;
    side.c = c;
    side.d = d;
    side.color = color;

    Vec3 areaNormal = quadAreaNormal(a, b, c, d);
    const Vec3 sideMid = scaleVec(addVec(addVec(a, b), addVec(c, d)), 0.25f);
    const Vec3 outwardHint = subtractVec(sideMid, centroid);
    areaNormal = normalizeVec(areaNormal, source.normal);
    if (dotVec(areaNormal, outwardHint) < 0.0f) {
        const Vec3 reversedB = side.d;
        const Vec3 reversedD = side.b;
        side.b = reversedB;
        side.d = reversedD;
        areaNormal = scaleVec(areaNormal, -1.0f);
    }
    side.normal = normalizeVec(areaNormal, source.normal);
    side.depth = meshQuadDepth(side);
    return side;
}

void appendExtrudedQuad(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    bool colorizeFaces) {

    if (depth <= 0.0001f) {
        out.push_back(quad);
        return;
    }

    const Vec3 extrudeDir = normalizeVec(extrudeNormal, quad.outwardHint);
    const Vec3 center = quadCentroid(quad);
    const float topScale = clampFloat(topCornerScale, 0.01f, 1.0f);
    const Vec3 lift = scaleVec(extrudeDir, depth);

    const Vec3 topA = addVec(pinchCornerTowardCenter(quad.a, center, topScale), lift);
    const Vec3 topB = addVec(pinchCornerTowardCenter(quad.b, center, topScale), lift);
    const Vec3 topC = addVec(pinchCornerTowardCenter(quad.c, center, topScale), lift);
    const Vec3 topD = addVec(pinchCornerTowardCenter(quad.d, center, topScale), lift);

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
    bottom.depth = meshQuadDepth(bottom);
    out.push_back(bottom);

    MeshQuad top;
    top = quad;
    top.a = topA;
    top.b = topB;
    top.c = topC;
    top.d = topD;
    top.color = topColor;
    ensureOutwardWinding(top, extrudeDir);
    top.depth = meshQuadDepth(top);
    out.push_back(top);

    out.push_back(orientedSideQuad(quad.a, quad.b, topB, topA, center, quad, sideColors[0]));
    out.push_back(orientedSideQuad(quad.b, quad.c, topC, topB, center, quad, sideColors[1]));
    out.push_back(orientedSideQuad(quad.c, quad.d, topD, topC, center, quad, sideColors[2]));
    out.push_back(orientedSideQuad(quad.a, quad.d, topD, topA, center, quad, sideColors[3]));
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
        "rebuildSingleQuadLabModel: operation={}, panels={}, extrudeDepth={:.3f}, topScale={:.3f}",
        settings.operation == QuadLabOperation::Extrude ? "extrude" : "flat",
        g_singleQuadLabModel.panelCount,
        settings.extrudeDepth,
        settings.extrudeTopScale);
}

bool runSingleQuadLabSmokeTest() {
    const SingleQuadLabSettings previousSettings = g_singleQuadLabSettings;

    SingleQuadLabSettings testSettings = previousSettings;
    testSettings.operation = QuadLabOperation::Extrude;
    testSettings.extrudeDepth = 0.25f;
    testSettings.extrudeTopScale = 0.9f;
    g_singleQuadLabSettings = testSettings;
    rebuildSingleQuadLabModel();

    const int panelCount = g_singleQuadLabModel.panelCount;
    bool bowTieDetected = false;
    for (const MeshQuad& quad : g_singleQuadLabModel.quads) {
        const Vec3 diagonalAc = subtractVec(quad.c, quad.a);
        const Vec3 diagonalBd = subtractVec(quad.d, quad.b);
        if (dotVec(crossVec(diagonalAc, diagonalBd), quad.normal) < 0.0f) {
            bowTieDetected = true;
            break;
        }
    }

    g_singleQuadLabSettings = previousSettings;
    rebuildSingleQuadLabModel();

    const bool ok = panelCount == 6 && !bowTieDetected;
    spdlog::info(
        "{} single quad lab extrude: panels={}, bowTie={}",
        ok ? "TEST PASS" : "TEST FAIL",
        panelCount,
        bowTieDetected);
    return ok;
}

} // namespace meshgen_playground
