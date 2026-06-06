#include "LandscapeBowlScenario.h"

#include "MeshBridge.h"
#include "PlaygroundState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <landscape_core/landscape_logic.h>
#include <landscape_mesh/landscape_mesh.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

void addMeshQuad(LandscapeBowlModel& model, MeshQuad quad) {
    quad.depth = meshQuadDepth(quad);
    model.meshQuads.push_back(quad);
    if (quad.cliffWall) {
        model.cliffWallQuadCount++;
    } else {
        model.topQuadCount++;
    }
}

Vec3 quadCenter(const MeshQuad& quad) {
    return {
        (quad.a.x + quad.b.x + quad.c.x + quad.d.x) * 0.25f,
        (quad.a.y + quad.b.y + quad.c.y + quad.d.y) * 0.25f,
        (quad.a.z + quad.b.z + quad.c.z + quad.d.z) * 0.25f,
    };
}

std::uint64_t sampleBucketKey(int x, int z) {
    return ((std::uint64_t)(std::uint32_t)x << 32) | (std::uint32_t)z;
}

void computeTopCliffDistances(LandscapeBowlModel& model) {
    constexpr float kBucketSize = 1.0f;
    constexpr float kMaxSearchDistance = 4.0f;
    constexpr float kSameLevelTolerance = 0.75f;
    constexpr int kSearchBucketRadius = (int)(kMaxSearchDistance / kBucketSize) + 2;

    std::unordered_map<std::uint64_t, std::vector<Vec3>> cliffSamples;
    for (const MeshQuad& quad : model.meshQuads) {
        if (!quad.cliffWall) {
            continue;
        }

        const float topY = std::max(std::max(quad.a.y, quad.b.y), std::max(quad.c.y, quad.d.y));
        Vec3 sample{};
        int sampleCount = 0;
        const Vec3 points[] = {quad.a, quad.b, quad.c, quad.d};
        for (const Vec3& point : points) {
            if (std::abs(point.y - topY) <= 0.001f) {
                sample.x += point.x;
                sample.y += point.y;
                sample.z += point.z;
                sampleCount++;
            }
        }
        if (sampleCount == 0) {
            continue;
        }

        sample.x /= (float)sampleCount;
        sample.y /= (float)sampleCount;
        sample.z /= (float)sampleCount;

        const int bucketX = (int)std::floor(sample.x / kBucketSize);
        const int bucketZ = (int)std::floor(sample.z / kBucketSize);
        cliffSamples[sampleBucketKey(bucketX, bucketZ)].push_back(sample);
    }

    for (MeshQuad& quad : model.meshQuads) {
        if (quad.cliffWall) {
            quad.cliffDistance = 0.0f;
            continue;
        }

        const Vec3 center = quadCenter(quad);
        const int centerBucketX = (int)std::floor(center.x / kBucketSize);
        const int centerBucketZ = (int)std::floor(center.z / kBucketSize);
        float nearestSq = kMaxSearchDistance * kMaxSearchDistance;

        for (int dz = -kSearchBucketRadius; dz <= kSearchBucketRadius; dz++) {
            for (int dx = -kSearchBucketRadius; dx <= kSearchBucketRadius; dx++) {
                const auto it = cliffSamples.find(sampleBucketKey(centerBucketX + dx, centerBucketZ + dz));
                if (it == cliffSamples.end()) {
                    continue;
                }

                for (const Vec3& sample : it->second) {
                    if (std::abs(center.y - sample.y) > kSameLevelTolerance) {
                        continue;
                    }
                    const float x = center.x - sample.x;
                    const float z = center.z - sample.z;
                    nearestSq = std::min(nearestSq, x * x + z * z);
                }
            }
        }

        quad.cliffDistance = std::sqrt(nearestSq);
    }
}

} // namespace

int landscapeIndex(int x, int y, int width) {
    return y * width + x;
}

void sanitizeSettings(LandscapeBowlSettings& settings) {
    settings.gridWidth = clampInt(settings.gridWidth, 12, 72);
    settings.gridHeight = clampInt(settings.gridHeight, 10, 56);
    settings.clearingRadius = clampFloat(settings.clearingRadius, 2.0f, (float)std::min(settings.gridWidth, settings.gridHeight) * 0.45f);
    settings.clearingSoftness = clampFloat(settings.clearingSoftness, 0.25f, 8.0f);
    settings.highGroundRadius = clampFloat(settings.highGroundRadius, settings.clearingRadius + 1.0f, (float)std::max(settings.gridWidth, settings.gridHeight));
    settings.highGroundWidth = clampFloat(settings.highGroundWidth, 1.0f, 10.0f);
    settings.highGroundHeight = clampFloat(settings.highGroundHeight, 0.5f, 8.0f);
    settings.heightLevels = clampInt(settings.heightLevels, 2, 6);
    settings.arcNoiseScale = clampFloat(settings.arcNoiseScale, 0.5f, 18.0f);
    settings.arcNoiseAmplitude = clampFloat(settings.arcNoiseAmplitude, 0.0f, 5.0f);
    settings.hillCount = clampInt(settings.hillCount, 0, 12);
    settings.hillHeight = clampFloat(settings.hillHeight, 0.0f, 5.0f);
    settings.hillRadius = clampFloat(settings.hillRadius, 0.75f, 8.0f);
}

int landscapeLevelAtCell(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y) {
    x = clampInt(x, 0, settings.gridWidth - 1);
    y = clampInt(y, 0, settings.gridHeight - 1);
    return (int)model.heightLevels[(std::size_t)landscapeIndex(x, y, settings.gridWidth)];
}

void rebuildLandscapeBowlModel() {
    spdlog::info("rebuildLandscapeBowlModel: start");

    LandscapeBowlSettings settings = g_landscapeSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_landscapeSettings = settings;
    }

    LandscapeBowlModel model;
    landscape_core::BowlGenerationStats generationStats;
    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(
        toCoreLandscapeSettings(settings),
        &generationStats);

    model.heights.assign((std::size_t)settings.gridWidth * (std::size_t)settings.gridHeight, 0.0f);
    model.heightLevels.assign(model.heights.size(), 0);
    model.zones.assign(model.heights.size(), LandscapeZone::Lowland);
    model.levelCellCounts = generationStats.levelCellCounts;
    model.clearingCellCount = generationStats.clearingCellCount;
    model.highGroundCellCount = generationStats.highGroundCellCount;
    model.hillCellCount = generationStats.hillCellCount;
    model.minHeight = generationStats.minHeight;
    model.maxHeight = generationStats.maxHeight;
    model.maxAdjacentLevelDelta = generationStats.maxAdjacentLevelDelta;

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const std::size_t index = (std::size_t)landscapeIndex(x, y, settings.gridWidth);
            const std::uint8_t level = grid.cellLevelAt(x, y);
            model.heightLevels[index] = level;
            model.heights[index] = (float)level * grid.levelHeight;
            model.zones[index] = toLocalZone(grid.zoneAt(x, y));
        }
    }

    const landscape_mesh::MeshBuildSettings meshSettings = makeSharedLandscapeMeshSettings(grid.levelHeight);
    const landscape_mesh::CompositionResult composedMesh = landscape_mesh::composeLandscapeMesh(grid, meshSettings);
    model.surfaceTileCount = composedMesh.stats.surfaceTileCount;
    model.wallTileCount = composedMesh.stats.wallTileCount;
    model.uniqueTileMeshCount = composedMesh.stats.uniqueTileMeshCount;
    model.reusedTileMeshCount = composedMesh.stats.reusedTileMeshCount;
    model.seamCheckedEdges = composedMesh.seams.checkedEdges;
    model.seamMismatchCount = composedMesh.seams.mismatches;
    model.seamMaxGap = composedMesh.seams.maxGap;
    model.beveledSegmentCount = composedMesh.stats.beveledSegmentCount;
    model.cornerCapCount = composedMesh.stats.cornerCapCount;
    model.outwardFailCount = composedMesh.normalOrientation.outwardFailCount;
    model.outwardWarnCount = composedMesh.normalOrientation.outwardWarnCount;
    model.minWallOutwardDot = composedMesh.normalOrientation.minWallOutwardDot;
    for (const landscape_mesh::MeshQuad& quad : composedMesh.quads) {
        addMeshQuad(model, toAppMeshQuad(quad));
    }
    computeTopCliffDistances(model);

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_landscapeModel = std::move(model);
    }

    spdlog::info("rebuildLandscapeBowlModel: done, grid={}x{}, heightRange={:.2f}-{:.2f}, maxAdjacentDelta={}, cells clearing/high/hill={}/{}/{}, meshQuality hSub/vSub/rockAmp={}/{}/{:.2f}, tiles surface/walls/unique={}/{}/{}, bevelSegments/caps={}/{}, seams checked/mismatch/gap={}/{}/{:.4f}, quads top/walls/total={}/{}/{}",
        settings.gridWidth,
        settings.gridHeight,
        g_landscapeModel.minHeight,
        g_landscapeModel.maxHeight,
        g_landscapeModel.maxAdjacentLevelDelta,
        g_landscapeModel.clearingCellCount,
        g_landscapeModel.highGroundCellCount,
        g_landscapeModel.hillCellCount,
        meshSettings.wallHorizontalSubdivisions,
        meshSettings.wallVerticalSubdivisions,
        meshSettings.rockAmplitude,
        g_landscapeModel.surfaceTileCount,
        g_landscapeModel.wallTileCount,
        g_landscapeModel.uniqueTileMeshCount,
        g_landscapeModel.beveledSegmentCount,
        g_landscapeModel.cornerCapCount,
        g_landscapeModel.seamCheckedEdges,
        g_landscapeModel.seamMismatchCount,
        g_landscapeModel.seamMaxGap,
        g_landscapeModel.topQuadCount,
        g_landscapeModel.cliffWallQuadCount,
        g_landscapeModel.meshQuads.size());
}

} // namespace meshgen_playground
