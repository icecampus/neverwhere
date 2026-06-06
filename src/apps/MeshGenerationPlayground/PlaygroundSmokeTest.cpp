#include "PlaygroundSmokeTest.h"

#include "PlaygroundState.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

Vec3 triNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 e0{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 e1{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 n{e0.y * e1.z - e0.z * e1.y, e0.z * e1.x - e0.x * e1.z, e0.x * e1.y - e0.y * e1.x};
    const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len < 1e-6f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {n.x / len, n.y / len, n.z / len};
}

void scanWallGeometryIntegrity() {
    int b45 = 0;
    int b90 = 0;
    int b135 = 0;
    int b170 = 0;
    int degenerate = 0;
    std::unordered_map<std::uint64_t, int> cellTwists;
    for (const MeshQuad& quad : g_landscapeModel.meshQuads) {
        if (!quad.cliffWall) {
            continue;
        }
        const Vec3 n0 = triNormal(quad.a, quad.b, quad.c);
        const Vec3 n1 = triNormal(quad.a, quad.c, quad.d);
        const bool n0bad = n0.x == 0.0f && n0.y == 0.0f && n0.z == 0.0f;
        const bool n1bad = n1.x == 0.0f && n1.y == 0.0f && n1.z == 0.0f;
        if (n0bad || n1bad) {
            degenerate++;
            continue;
        }
        const float dot = std::clamp(n0.x * n1.x + n0.y * n1.y + n0.z * n1.z, -1.0f, 1.0f);
        const float angleDeg = std::acos(dot) * 57.2957795f;
        if (angleDeg > 45.0f) b45++;
        if (angleDeg > 90.0f) b90++;
        if (angleDeg > 135.0f) b135++;
        if (angleDeg > 170.0f) {
            b170++;
            const int cx = (int)std::floor((quad.a.x + quad.b.x + quad.c.x + quad.d.x) * 0.25f);
            const int cz = (int)std::floor((quad.a.z + quad.b.z + quad.c.z + quad.d.z) * 0.25f);
            cellTwists[((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cz]++;
        }
    }
    std::uint64_t worstCell = 0;
    int worstCount = 0;
    for (const auto& entry : cellTwists) {
        if (entry.second > worstCount) {
            worstCount = entry.second;
            worstCell = entry.first;
        }
    }
    const int worstCx = (int)(std::int32_t)(worstCell >> 32);
    const int worstCz = (int)(std::int32_t)(worstCell & 0xffffffff);
    int spikes = 0;
    const int w = g_landscapeSettings.gridWidth;
    const int h = g_landscapeSettings.gridHeight;
    if (!g_landscapeModel.heightLevels.empty() && (int)g_landscapeModel.heightLevels.size() >= w * h) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int lv = g_landscapeModel.heightLevels[(std::size_t)(y * w + x)];
                const int left = x > 0 ? g_landscapeModel.heightLevels[(std::size_t)(y * w + x - 1)] : lv;
                const int right = x < w - 1 ? g_landscapeModel.heightLevels[(std::size_t)(y * w + x + 1)] : lv;
                const int up = y > 0 ? g_landscapeModel.heightLevels[(std::size_t)((y - 1) * w + x)] : lv;
                const int down = y < h - 1 ? g_landscapeModel.heightLevels[(std::size_t)((y + 1) * w + x)] : lv;
                const int lower = (left < lv) + (right < lv) + (up < lv) + (down < lv);
                if (lower >= 3) spikes++;
            }
        }
    }

    const char* marker = (spikes == 0 && b170 == 0) ? "TEST PASS" : "TEST WARN";
    spdlog::info(
        "{} geometry integrity: wallQuads={}, oneCellSpikes={}, twist>45={}, >90={}, >135={}, >170(folded)={}, degenerate={}, worstFoldCell=({},{}) count={}",
        marker, g_landscapeModel.cliffWallQuadCount, spikes, b45, b90, b135, b170, degenerate, worstCx, worstCz, worstCount);
}

} // namespace

bool runTestScenario() {
    std::lock_guard<std::mutex> lock(g_modelMutex);
    scanWallGeometryIntegrity();
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
            "TEST PASS MeshGenerationPlayground pipeline: rectangle quads={}/{}, bevel/caps={}/{}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, maxAdjacentLevelDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
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
        "TEST FAIL MeshGenerationPlayground pipeline: rectangleOk={}, landscapeOk={}, rectangle quads={}/{}, bevel/caps={}/{}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, maxAdjacentLevelDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
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
