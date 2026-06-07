#include "SingleQuadLabScenario.h"

#include "PlaygroundState.h"
#include "QuadLabMeshOps.h"

#include <cmath>
#include <cstddef>
#include <mutex>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

Vec3 subtractVec3(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

float dotVec3(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 crossVec3(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 normalizeVec3(const Vec3& value, const Vec3& fallback) {
    const float lengthSq = dotVec3(value, value);
    if (lengthSq <= 0.0001f) {
        const float fallbackLength = std::sqrt(dotVec3(fallback, fallback));
        if (fallbackLength <= 0.0001f) {
            return {0.0f, 1.0f, 0.0f};
        }
        return {
            fallback.x / fallbackLength,
            fallback.y / fallbackLength,
            fallback.z / fallbackLength,
        };
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, value.y * invLength, value.z * invLength};
}

void addMeshQuad(SingleQuadLabModel& model, MeshQuad quad) {
    quad.depth = meshQuadDepth(quad);
    model.quads.push_back(quad);
}

MeshQuad makeBaseQuad(const SingleQuadLabSettings& settings) {
    OrientedQuadParams params;
    params.width = settings.quadWidth;
    params.height = settings.quadHeight;
    params.centerX = settings.centerX;
    params.centerY = settings.centerY;
    params.centerZ = settings.centerZ;
    params.yawDegrees = settings.yawDegrees;
    params.pitchDegrees = settings.pitchDegrees;
    return makeOrientedQuad(params);
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
        const Vec3 extrudeDir = normalizeVec3(baseQuad.normal, baseQuad.outwardHint);
        const auto liftAlongExtrude = [&](const Vec3& baseCorner, const Vec3& topCorner) {
            return dotVec3(subtractVec3(topCorner, baseCorner), extrudeDir);
        };
        const auto pinchRatio = [&](const Vec3& baseCorner, const Vec3& topCorner) {
            const Vec3 baseFlat = projectOntoPlane(baseCorner, center, extrudeDir);
            const Vec3 topFlat = projectOntoPlane(topCorner, center, extrudeDir);
            const Vec3 baseVec = subtractVec3(baseFlat, center);
            const Vec3 topVec = subtractVec3(topFlat, center);
            const float baseLenSq = dotVec3(baseVec, baseVec);
            const float topLenSq = dotVec3(topVec, topVec);
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
        const Vec3 diagonalAc = subtractVec3(quad.c, quad.a);
        const Vec3 diagonalBd = subtractVec3(quad.d, quad.b);
        if (dotVec3(crossVec3(diagonalAc, diagonalBd), quad.normal) < 0.0f) {
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
