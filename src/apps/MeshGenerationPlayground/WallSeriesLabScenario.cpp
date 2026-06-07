#include "WallSeriesLabScenario.h"

#include "MeshBridge.h"
#include "PlaygroundState.h"

#include <cmath>
#include <cstddef>
#include <mutex>

#include <landscape_mesh/landscape_mesh.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec3 rotateYawPitchPoint(const Vec3& value, float yawRadians, float pitchRadians) {
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

Vec3 addOffset(const Vec3& base, const Vec3& offset) {
    return {base.x + offset.x, base.y + offset.y, base.z + offset.z};
}

Vec3 normalizeVec3(const Vec3& value, const Vec3& fallback) {
    const float lengthSq = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSq <= 0.0001f) {
        return fallback;
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    return {value.x * invLength, value.y * invLength, value.z * invLength};
}

MeshQuad transformWallQuad(const MeshQuad& quad, const WallSeriesLabSettings& settings) {
    const float yaw = settings.yawDegrees * (kPi / 180.0f);
    const float pitch = settings.pitchDegrees * (kPi / 180.0f);
    const Vec3 center{settings.wallCenterX, settings.wallCenterY, settings.wallCenterZ};

    MeshQuad result = quad;
    result.a = addOffset(center, rotateYawPitchPoint(quad.a, yaw, pitch));
    result.b = addOffset(center, rotateYawPitchPoint(quad.b, yaw, pitch));
    result.c = addOffset(center, rotateYawPitchPoint(quad.c, yaw, pitch));
    result.d = addOffset(center, rotateYawPitchPoint(quad.d, yaw, pitch));
    result.normal = normalizeVec3(rotateYawPitchPoint(quad.normal, yaw, pitch), quad.normal);
    result.outwardHint = normalizeVec3(rotateYawPitchPoint(quad.outwardHint, yaw, pitch), quad.outwardHint);
    result.depth = meshQuadDepth(result);
    return result;
}

landscape_mesh::MeshBuildSettings makeWallLabMeshSettings(const WallSeriesLabSettings& settings) {
    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.rockEnabled = settings.rockEnabled;
    meshSettings.rockSeed = settings.rockSeed;
    meshSettings.rockScale = settings.rockScale;
    meshSettings.rockAmplitude = settings.rockAmplitude;
    meshSettings.wallHorizontalSubdivisions = settings.wallHorizontalSubdivisions;
    meshSettings.wallVerticalSubdivisions = settings.wallVerticalSubdivisions;
    meshSettings.terraceSteps = settings.terraceSteps;
    return meshSettings;
}

} // namespace

void resetWallSeriesLabCamera(QuadLabPreviewCamera& camera) {
    camera.zoom = 1.0f;
    camera.pan = {0.0f, 0.0f};
    // Wall mesh faces +Z; camera on -Z looks toward the outward cliff face.
    camera.orbitYawDegrees = 180.0f;
    camera.orbitPitchDegrees = 18.0f;
}

void sanitizeWallSeriesLabSettings(WallSeriesLabSettings& settings) {
    settings.wallWidth = clampFloat(settings.wallWidth, 0.5f, 16.0f);
    settings.wallHeight = clampFloat(settings.wallHeight, 0.5f, 12.0f);
    settings.wallHorizontalSubdivisions = clampInt(settings.wallHorizontalSubdivisions, 1, 32);
    settings.wallVerticalSubdivisions = clampInt(settings.wallVerticalSubdivisions, 1, 32);
    settings.wallCenterX = clampFloat(settings.wallCenterX, -8.0f, 8.0f);
    settings.wallCenterY = clampFloat(settings.wallCenterY, -2.0f, 8.0f);
    settings.wallCenterZ = clampFloat(settings.wallCenterZ, -8.0f, 8.0f);
    settings.yawDegrees = clampFloat(settings.yawDegrees, -180.0f, 180.0f);
    settings.pitchDegrees = clampFloat(settings.pitchDegrees, -89.0f, 89.0f);
    settings.rockScale = clampFloat(settings.rockScale, 0.25f, 24.0f);
    settings.rockAmplitude = clampFloat(settings.rockAmplitude, 0.0f, 1.25f);
    settings.terraceSteps = clampInt(settings.terraceSteps, 0, 12);
}

void rebuildWallSeriesLabModel() {
    WallSeriesLabSettings settings = g_wallSeriesLabSettings;
    sanitizeWallSeriesLabSettings(settings);

    landscape_mesh::WallPanelBuildRequest panelRequest;
    panelRequest.width = settings.wallWidth;
    panelRequest.height = settings.wallHeight;
    panelRequest.fadeDisplacementAtBottom = settings.fadeDisplacementAtBottom;

    const std::vector<landscape_mesh::MeshQuad> localQuads = landscape_mesh::buildDisplacedWallPanel(
        panelRequest,
        makeWallLabMeshSettings(settings));

    WallSeriesLabModel model;
    model.subdivisionsX = settings.wallHorizontalSubdivisions;
    model.subdivisionsY = settings.wallVerticalSubdivisions;
    model.quads.reserve(localQuads.size());
    for (const landscape_mesh::MeshQuad& localQuad : localQuads) {
        model.quads.push_back(transformWallQuad(toAppMeshQuad(localQuad), settings));
    }
    model.quadCount = (int)model.quads.size();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_wallSeriesLabSettings = settings;
        g_wallSeriesLabModel = std::move(model);
    }

    spdlog::info(
        "rebuildWallSeriesLabModel: size={:.2f}x{:.2f}, subdivisions={}x{}, quads={}, rockAmp={:.3f}",
        g_wallSeriesLabSettings.wallWidth,
        g_wallSeriesLabSettings.wallHeight,
        g_wallSeriesLabModel.subdivisionsX,
        g_wallSeriesLabModel.subdivisionsY,
        g_wallSeriesLabModel.quadCount,
        g_wallSeriesLabSettings.rockAmplitude);
}

bool runWallSeriesLabSmokeTest() {
    const WallSeriesLabSettings previousSettings = g_wallSeriesLabSettings;

    WallSeriesLabSettings testSettings = previousSettings;
    testSettings.wallHorizontalSubdivisions = 4;
    testSettings.wallVerticalSubdivisions = 3;
    testSettings.rockEnabled = true;
    testSettings.rockAmplitude = 0.25f;
    g_wallSeriesLabSettings = testSettings;
    rebuildWallSeriesLabModel();

    const int expectedQuads = testSettings.wallHorizontalSubdivisions * testSettings.wallVerticalSubdivisions;
    const int actualQuads = g_wallSeriesLabModel.quadCount;
    int wallQuadCount = 0;
    for (const MeshQuad& quad : g_wallSeriesLabModel.quads) {
        if (quad.cliffWall) {
            wallQuadCount++;
        }
    }
    const bool ok = actualQuads == expectedQuads && wallQuadCount == expectedQuads;

    g_wallSeriesLabSettings = previousSettings;
    rebuildWallSeriesLabModel();

    spdlog::info(
        "{} wall lab displaced panel: quads={}, wallQuads={}, expectedQuads={}",
        ok ? "TEST PASS" : "TEST FAIL",
        actualQuads,
        wallQuadCount,
        expectedQuads);
    return ok;
}

} // namespace meshgen_playground
