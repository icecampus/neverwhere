#include "PlaygroundSmokeTest.h"

#include "PlaygroundState.h"

#include <mutex>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

bool runTestScenario() {
    std::lock_guard<std::mutex> lock(g_modelMutex);
    const bool rectangleOk =
        g_rectModel.solidCellCount > 0 &&
        !g_rectModel.boundarySegments.empty() &&
        g_rectModel.topQuadCount > 0 &&
        g_rectModel.cliffWallQuadCount > 0 &&
        g_rectModel.beveledSegmentCount > (int)g_rectModel.boundarySegments.size() &&
        g_rectModel.cornerCapCount > 0;
    const bool landscapeOk =
        g_landscapeModel.surfaceTileCount > 0 &&
        g_landscapeModel.wallTileCount > 0 &&
        g_landscapeModel.uniqueTileMeshCount > 0 &&
        g_landscapeModel.seamMismatchCount == 0 &&
        g_landscapeModel.maxAdjacentLevelDelta <= 1 &&
        g_landscapeModel.topQuadCount > 0 &&
        g_landscapeModel.cliffWallQuadCount > 0 &&
        g_landscapeModel.beveledSegmentCount > 0;

    if (rectangleOk && landscapeOk) {
        spdlog::info(
            "TEST PASS MeshGenerationPlayground pipeline: rectangle quads={}/{}, bevel/caps={}/{}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, pyramid maxAdjacentDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
            g_rectModel.topQuadCount,
            g_rectModel.cliffWallQuadCount,
            g_rectModel.beveledSegmentCount,
            g_rectModel.cornerCapCount,
            g_landscapeModel.surfaceTileCount,
            g_landscapeModel.wallTileCount,
            g_landscapeModel.uniqueTileMeshCount,
            g_landscapeModel.beveledSegmentCount,
            g_landscapeModel.cornerCapCount,
            g_landscapeModel.maxAdjacentLevelDelta,
            g_landscapeModel.seamCheckedEdges,
            g_landscapeModel.seamMismatchCount,
            g_landscapeModel.seamMaxGap);
        return true;
    }

    spdlog::error(
        "TEST FAIL MeshGenerationPlayground pipeline: rectangleOk={}, landscapeOk={}, rectangle quads={}/{}, bevel/caps={}/{}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, pyramid maxAdjacentDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
        rectangleOk,
        landscapeOk,
        g_rectModel.topQuadCount,
        g_rectModel.cliffWallQuadCount,
        g_rectModel.beveledSegmentCount,
        g_rectModel.cornerCapCount,
        g_landscapeModel.surfaceTileCount,
        g_landscapeModel.wallTileCount,
        g_landscapeModel.uniqueTileMeshCount,
        g_landscapeModel.beveledSegmentCount,
        g_landscapeModel.cornerCapCount,
        g_landscapeModel.maxAdjacentLevelDelta,
        g_landscapeModel.seamCheckedEdges,
        g_landscapeModel.seamMismatchCount,
        g_landscapeModel.seamMaxGap);
    return false;
}

} // namespace meshgen_playground
