#include "WallSeriesLabScenario.h"

#include "PlaygroundState.h"
#include "QuadLabMeshOps.h"

#include <cmath>
#include <cstddef>
#include <mutex>
#include <random>
#include <vector>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int kGridColumns = 4;
constexpr int kGridRows = 4;

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

Vec3 subtractVec(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 crossVec(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

float dotVec(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
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

Vec3 transformLocalPoint(const Vec3& local, const WallSeriesLabSettings& settings) {
    const float yaw = settings.yawDegrees * (kPi / 180.0f);
    const float pitch = settings.pitchDegrees * (kPi / 180.0f);
    const Vec3 center{settings.wallCenterX, settings.wallCenterY, settings.wallCenterZ};
    return addOffset(center, rotateYawPitchPoint(local, yaw, pitch));
}

Vec3 wallOutwardHint(const WallSeriesLabSettings& settings) {
    const float yaw = settings.yawDegrees * (kPi / 180.0f);
    const float pitch = settings.pitchDegrees * (kPi / 180.0f);
    const Vec3 localNormal{0.0f, 0.0f, -1.0f};
    return normalizeVec3(rotateYawPitchPoint(localNormal, yaw, pitch), localNormal);
}

bool isOuterWallCorner(int gridX, int gridY) {
    const bool onLeft = gridX == 0;
    const bool onRight = gridX == kGridColumns;
    const bool onBottom = gridY == 0;
    const bool onTop = gridY == kGridRows;
    return (onLeft || onRight) && (onBottom || onTop);
}

float chaoticForwardPush(int gridX, int gridY, const WallSeriesLabSettings& settings) {
    if (isOuterWallCorner(gridX, gridY) || settings.vertexPushMax <= 0.0001f) {
        return 0.0f;
    }

    std::mt19937 rng((unsigned)settings.vertexPushSeed * 2654435761u +
                     (unsigned)gridX * 73856093u +
                     (unsigned)gridY * 19349663u);
    std::uniform_real_distribution<float> pushAmount(0.0f, settings.vertexPushMax);
    return pushAmount(rng);
}

Vec3 localGridVertex(int gridX, int gridY, const WallSeriesLabSettings& settings) {
    const float halfWidth = settings.wallWidth * 0.5f;
    const float halfHeight = settings.wallHeight * 0.5f;
    const float u = (float)gridX / (float)kGridColumns;
    const float v = (float)gridY / (float)kGridRows;
    return {
        -halfWidth + u * settings.wallWidth,
        -halfHeight + v * settings.wallHeight,
        -chaoticForwardPush(gridX, gridY, settings),
    };
}

int gridVertexIndex(int gridX, int gridY) {
    return gridY * (kGridColumns + 1) + gridX;
}

void assignFacetNormal(MeshQuad& quad, const Vec3& outwardHint) {
    Vec3 areaNormal = crossVec(subtractVec(quad.b, quad.a), subtractVec(quad.c, quad.a));
    areaNormal = normalizeVec3(areaNormal, outwardHint);
    if (dotVec(areaNormal, outwardHint) < 0.0f) {
        const Vec3 reversedB = quad.d;
        const Vec3 reversedD = quad.b;
        quad.b = reversedB;
        quad.d = reversedD;
        areaNormal = normalizeVec3(
            crossVec(subtractVec(quad.b, quad.a), subtractVec(quad.c, quad.a)),
            outwardHint);
    }
    quad.normal = areaNormal;
    quad.outwardHint = outwardHint;
}

MeshQuad makeWallLabQuadFromGrid(
    int gridAx,
    int gridAy,
    int gridBx,
    int gridBy,
    int gridCx,
    int gridCy,
    int gridDx,
    int gridDy,
    const std::vector<Vec3>& worldVertices,
    const Vec3& outwardHint,
    ImU32 color) {
    MeshQuad quad;
    quad.a = worldVertices[(std::size_t)gridVertexIndex(gridAx, gridAy)];
    quad.b = worldVertices[(std::size_t)gridVertexIndex(gridBx, gridBy)];
    quad.c = worldVertices[(std::size_t)gridVertexIndex(gridCx, gridCy)];
    quad.d = worldVertices[(std::size_t)gridVertexIndex(gridDx, gridDy)];
    assignFacetNormal(quad, outwardHint);
    quad.color = color;
    quad.cliffWall = false;
    quad.depth = meshQuadDepth(quad);
    return quad;
}

void buildWallLabVertexGrid(const WallSeriesLabSettings& settings, std::vector<Vec3>& worldVertices) {
    const int vertexColumns = kGridColumns + 1;
    const int vertexRows = kGridRows + 1;
    worldVertices.resize((std::size_t)vertexColumns * (std::size_t)vertexRows);

    for (int gridY = 0; gridY <= kGridRows; gridY++) {
        for (int gridX = 0; gridX <= kGridColumns; gridX++) {
            worldVertices[(std::size_t)gridVertexIndex(gridX, gridY)] =
                transformLocalPoint(localGridVertex(gridX, gridY, settings), settings);
        }
    }
}

void appendWallLabBaseQuads(std::vector<MeshQuad>& baseQuads, const WallSeriesLabSettings& settings) {
    std::vector<Vec3> worldVertices;
    buildWallLabVertexGrid(settings, worldVertices);
    const Vec3 outwardHint = wallOutwardHint(settings);

    baseQuads.reserve((std::size_t)kGridColumns * (std::size_t)kGridRows);
    for (int cellY = 0; cellY < kGridRows; cellY++) {
        for (int cellX = 0; cellX < kGridColumns; cellX++) {
            const int quadIndex = cellY * kGridColumns + cellX;
            const ImU32 color = quadLabPanelTintColor(quadIndex, kGridColumns * kGridRows);
            // CCW from -Z: bottom-left, top-left, top-right, bottom-right.
            baseQuads.push_back(makeWallLabQuadFromGrid(
                cellX,
                cellY,
                cellX,
                cellY + 1,
                cellX + 1,
                cellY + 1,
                cellX + 1,
                cellY,
                worldVertices,
                outwardHint,
                color));
        }
    }
}

int extrudeSeedForWallQuad(const WallSeriesLabSettings& settings, int quadIndex) {
    if (!settings.extrudeVaryPerQuad) {
        return settings.extrudeHeightSeed;
    }
    return settings.extrudeHeightSeed + quadIndex * 104729;
}

} // namespace

void resetWallSeriesLabCamera(QuadLabPreviewCamera& camera) {
    camera.zoom = 1.0f;
    camera.pan = {0.0f, 0.0f};
    camera.orbitYawDegrees = 180.0f;
    camera.orbitPitchDegrees = 18.0f;
}

void sanitizeWallSeriesLabSettings(WallSeriesLabSettings& settings) {
    settings.wallWidth = clampFloat(settings.wallWidth, 0.5f, 16.0f);
    settings.wallHeight = clampFloat(settings.wallHeight, 0.5f, 12.0f);
    settings.wallCenterX = clampFloat(settings.wallCenterX, -8.0f, 8.0f);
    settings.wallCenterY = clampFloat(settings.wallCenterY, -2.0f, 8.0f);
    settings.wallCenterZ = clampFloat(settings.wallCenterZ, -8.0f, 8.0f);
    settings.yawDegrees = clampFloat(settings.yawDegrees, -180.0f, 180.0f);
    settings.pitchDegrees = clampFloat(settings.pitchDegrees, -89.0f, 89.0f);
    settings.vertexPushMax = clampFloat(settings.vertexPushMax, 0.0f, 2.0f);
    settings.extrudeDepth = clampFloat(settings.extrudeDepth, 0.0f, 2.0f);
    settings.extrudeTopScale = clampFloat(settings.extrudeTopScale, 0.1f, 1.0f);
    settings.extrudeTopHeightSpread = clampFloat(settings.extrudeTopHeightSpread, 0.0f, 1.0f);
    settings.extrudeTopScaleSpread = clampFloat(settings.extrudeTopScaleSpread, 0.0f, 1.0f);
}

void rebuildWallSeriesLabModel() {
    WallSeriesLabSettings settings = g_wallSeriesLabSettings;
    sanitizeWallSeriesLabSettings(settings);

    std::vector<MeshQuad> baseQuads;
    appendWallLabBaseQuads(baseQuads, settings);

    const bool applyQuadLabExtrude =
        settings.quadLabOperation == QuadLabOperation::Extrude && settings.extrudeDepth > 0.0001f;

    WallSeriesLabModel model;
    model.gridColumns = kGridColumns;
    model.gridRows = kGridRows;
    model.baseQuadCount = (int)baseQuads.size();
    model.quads.reserve(applyQuadLabExtrude ? baseQuads.size() * 11 : baseQuads.size());

    for (int quadIndex = 0; quadIndex < model.baseQuadCount; quadIndex++) {
        MeshQuad baseQuad = baseQuads[(std::size_t)quadIndex];
        if (applyQuadLabExtrude) {
            if (!settings.colorizeFaces) {
                baseQuad.color = quadLabPanelTintColor(quadIndex, model.baseQuadCount);
            }
            appendExtrudedQuad(
                model.quads,
                baseQuad,
                baseQuad.normal,
                settings.extrudeDepth,
                settings.extrudeTopScale,
                settings.extrudeTopHeightSpread,
                settings.extrudeTopScaleSpread,
                extrudeSeedForWallQuad(settings, quadIndex),
                settings.colorizeFaces);
        } else {
            model.quads.push_back(baseQuad);
        }
    }

    model.meshPanelCount = (int)model.quads.size();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_wallSeriesLabSettings = settings;
        g_wallSeriesLabModel = std::move(model);
    }

    spdlog::info(
        "rebuildWallSeriesLabModel: size={:.2f}x{:.2f}, grid={}x{}, vertexPushMax={:.3f}, baseQuads={}, meshPanels={}, extrude={}",
        g_wallSeriesLabSettings.wallWidth,
        g_wallSeriesLabSettings.wallHeight,
        g_wallSeriesLabModel.gridColumns,
        g_wallSeriesLabModel.gridRows,
        g_wallSeriesLabSettings.vertexPushMax,
        g_wallSeriesLabModel.baseQuadCount,
        g_wallSeriesLabModel.meshPanelCount,
        applyQuadLabExtrude ? "on" : "off");
}

bool runWallSeriesLabSmokeTest() {
    const WallSeriesLabSettings previousSettings = g_wallSeriesLabSettings;

    WallSeriesLabSettings testSettings = previousSettings;
    testSettings.wallWidth = 2.0f;
    testSettings.wallHeight = 1.5f;
    testSettings.vertexPushMax = 0.18f;
    testSettings.vertexPushSeed = 4242;
    testSettings.quadLabOperation = QuadLabOperation::Extrude;
    testSettings.extrudeDepth = 0.22f;
    testSettings.extrudeTopHeightSpread = 0.35f;
    testSettings.extrudeTopScaleSpread = 0.35f;
    testSettings.extrudeVaryPerQuad = false;
    g_wallSeriesLabSettings = testSettings;
    rebuildWallSeriesLabModel();

    const int expectedBaseQuads = kGridColumns * kGridRows;
    const int expectedMeshPanels = expectedBaseQuads * 11;
    const int actualBaseQuads = g_wallSeriesLabModel.baseQuadCount;
    const int actualMeshPanels = g_wallSeriesLabModel.meshPanelCount;
    const bool ok = actualBaseQuads == expectedBaseQuads && actualMeshPanels == expectedMeshPanels;

    g_wallSeriesLabSettings = previousSettings;
    rebuildWallSeriesLabModel();

    spdlog::info(
        "{} wall lab 4x4 chaotic vertex push + extrude: baseQuads={}, meshPanels={}",
        ok ? "TEST PASS" : "TEST FAIL",
        actualBaseQuads,
        actualMeshPanels);
    return ok;
}

} // namespace meshgen_playground
