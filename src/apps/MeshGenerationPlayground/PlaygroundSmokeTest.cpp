#include "PlaygroundSmokeTest.h"

#include "MeshBridge.h"
#include "MeshPreview.h"
#include "PlaygroundState.h"
#include "SingleQuadLabScenario.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <landscape_mesh/landscape_mesh.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr float kOutwardWarnDotThreshold = 0.25f;

Vec3 normalizeVec3(const Vec3& value, const Vec3& fallback) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 0.0001f) {
        const float fallbackLength = std::sqrt(
            fallback.x * fallback.x + fallback.y * fallback.y + fallback.z * fallback.z);
        if (fallbackLength < 0.0001f) {
            return {0.0f, 1.0f, 0.0f};
        }
        return {
            fallback.x / fallbackLength,
            fallback.y / fallbackLength,
            fallback.z / fallbackLength,
        };
    }
    return {value.x / length, value.y / length, value.z / length};
}

float dotVec3(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

struct OutwardOrientationScan {
    int outwardFailCount = 0;
    int outwardWarnCount = 0;
    float minWallOutwardDot = 1.0f;
};

OutwardOrientationScan scanModelOutwardOrientation(const std::vector<MeshQuad>& quads) {
    OutwardOrientationScan scan;
    for (const MeshQuad& quad : quads) {
        if (!quad.cliffWall) {
            if (quad.normal.y < 0.0f) {
                scan.outwardFailCount++;
            }
            continue;
        }

        const Vec3 faceNormal = normalizeVec3(quad.normal, quad.outwardHint);
        const Vec3 outward = normalizeVec3(quad.outwardHint, faceNormal);
        const float dot = dotVec3(faceNormal, outward);
        scan.minWallOutwardDot = std::min(scan.minWallOutwardDot, dot);
        if (dot < 0.0f) {
            scan.outwardFailCount++;
        } else if (dot < kOutwardWarnDotThreshold) {
            scan.outwardWarnCount++;
        }
    }
    return scan;
}

struct SunShadowScan {
    float minShadow = 1.0f;
    float maxShadow = 0.0f;
};

struct LitWallNormalScan {
    float minHintDot = 1.0f;
    int wallQuadCount = 0;
};

LitWallNormalScan scanLitWallNormals(const std::vector<MeshQuad>& quads) {
    LitWallNormalScan scan;
    for (const MeshQuad& quad : quads) {
        if (!quad.cliffWall) {
            continue;
        }
        scan.wallQuadCount++;
        const landscape_mesh::MeshQuad sharedQuad = toLandscapeMeshQuad(quad);
        const landscape_mesh::Vec3 lit = landscape_mesh::litWallNormal(sharedQuad);
        const landscape_mesh::Vec3& hint = sharedQuad.outwardHint;
        const float hintHorizLength = std::sqrt(hint.x * hint.x + hint.z * hint.z);
        if (hintHorizLength < 0.0001f) {
            continue;
        }
        const float dot = (lit.x * hint.x + lit.z * hint.z) / hintHorizLength;
        scan.minHintDot = std::min(scan.minHintDot, dot);
    }
    return scan;
}

void logLitWallNormalScan(const char* label, const LitWallNormalScan& scan) {
    const char* marker = scan.wallQuadCount > 0 && scan.minHintDot > 0.99f ? "TEST PASS" : "TEST FAIL";
    spdlog::info(
        "{} {} lit wall normals: walls={}, minHintDot={:.4f}",
        marker,
        label,
        scan.wallQuadCount,
        scan.minHintDot);
}

SunShadowScan scanModelSunShadow(const std::vector<MeshQuad>& quads) {
    SunShadowScan scan;
    for (const MeshQuad& quad : quads) {
        if (quad.cliffWall) {
            continue;
        }
        scan.minShadow = std::min(scan.minShadow, quad.sunShadow);
        scan.maxShadow = std::max(scan.maxShadow, quad.sunShadow);
    }
    return scan;
}

void logOutwardOrientationScan(const char* label, const OutwardOrientationScan& scan) {
    const char* marker = scan.outwardFailCount > 0
        ? "TEST FAIL"
        : scan.outwardWarnCount > 0
            ? "TEST WARN"
            : "TEST PASS";
    spdlog::info(
        "{} {} outward orientation: fail={}, warn={}, minWallDot={:.4f}",
        marker,
        label,
        scan.outwardFailCount,
        scan.outwardWarnCount,
        scan.minWallOutwardDot);
}

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
    const bool singleQuadLabOk = runSingleQuadLabSmokeTest();
    const bool quadLabGpuOk = warmupQuadLabGpuPreview();

    std::lock_guard<std::mutex> lock(g_modelMutex);
    scanWallGeometryIntegrity();
    const OutwardOrientationScan rectangleOrientation = scanModelOutwardOrientation(g_rectModel.meshQuads);
    const OutwardOrientationScan landscapeOrientation = scanModelOutwardOrientation(g_landscapeModel.meshQuads);
    const SunShadowScan rectangleSunShadow = scanModelSunShadow(g_rectModel.meshQuads);
    const SunShadowScan landscapeSunShadow = scanModelSunShadow(g_landscapeModel.meshQuads);
    const LitWallNormalScan rectangleLitNormals = scanLitWallNormals(g_rectModel.meshQuads);
    const LitWallNormalScan landscapeLitNormals = scanLitWallNormals(g_landscapeModel.meshQuads);
    logOutwardOrientationScan("rectangle mesh", rectangleOrientation);
    logOutwardOrientationScan("landscape mesh", landscapeOrientation);
    logLitWallNormalScan("rectangle mesh", rectangleLitNormals);
    logLitWallNormalScan("landscape mesh", landscapeLitNormals);
    spdlog::info(
        "TEST PASS sun shadow range: rectangle min/max={:.4f}/{:.4f}, landscape min/max={:.4f}/{:.4f}",
        rectangleSunShadow.minShadow,
        rectangleSunShadow.maxShadow,
        landscapeSunShadow.minShadow,
        landscapeSunShadow.maxShadow);
    const bool outwardOk =
        rectangleOrientation.outwardFailCount == 0 &&
        landscapeOrientation.outwardFailCount == 0;
    const bool litNormalsOk =
        rectangleLitNormals.wallQuadCount > 0 &&
        landscapeLitNormals.wallQuadCount > 0 &&
        rectangleLitNormals.minHintDot > 0.99f &&
        landscapeLitNormals.minHintDot > 0.99f;
    const bool outwardWarn =
        rectangleOrientation.outwardWarnCount > 0 ||
        landscapeOrientation.outwardWarnCount > 0;
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
    if (rectangleOk && landscapeOk && outwardOk && litNormalsOk && singleQuadLabOk && quadLabGpuOk) {
        if (outwardWarn) {
            spdlog::warn(
                "TEST WARN MeshGenerationPlayground outward orientation: rectangle fail/warn/minDot={}/{}/{:.4f}, landscape fail/warn/minDot={}/{}/{:.4f}",
                rectangleOrientation.outwardFailCount,
                rectangleOrientation.outwardWarnCount,
                rectangleOrientation.minWallOutwardDot,
                landscapeOrientation.outwardFailCount,
                landscapeOrientation.outwardWarnCount,
                landscapeOrientation.minWallOutwardDot);
        }
        spdlog::info(
            "TEST PASS MeshGenerationPlayground pipeline: rectangle quads={}/{}, bevel/caps={}/{}, outward fail/warn/minDot={}/{}/{:.4f}, sunShadow min/max={:.4f}/{:.4f}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, outward fail/warn/minDot={}/{}/{:.4f}, sunShadow min/max={:.4f}/{:.4f}, maxAdjacentLevelDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
            g_rectModel.topQuadCount,
            g_rectModel.cliffWallQuadCount,
            g_rectModel.beveledSegmentCount,
            g_rectModel.cornerCapCount,
            rectangleOrientation.outwardFailCount,
            rectangleOrientation.outwardWarnCount,
            rectangleOrientation.minWallOutwardDot,
            rectangleSunShadow.minShadow,
            rectangleSunShadow.maxShadow,
            g_landscapeModel.surfaceTileCount,
            g_landscapeModel.wallTileCount,
            g_landscapeModel.uniqueTileMeshCount,
            g_landscapeModel.beveledSegmentCount,
            g_landscapeModel.cornerCapCount,
            landscapeOrientation.outwardFailCount,
            landscapeOrientation.outwardWarnCount,
            landscapeOrientation.minWallOutwardDot,
            landscapeSunShadow.minShadow,
            landscapeSunShadow.maxShadow,
            g_landscapeModel.maxAdjacentLevelDelta,
            g_landscapeModel.seamCheckedEdges,
            g_landscapeModel.seamMismatchCount,
            g_landscapeModel.seamMaxGap);
        return true;
    }

    spdlog::error(
        "TEST FAIL MeshGenerationPlayground pipeline: rectangleOk={}, landscapeOk={}, outwardOk={}, litNormalsOk={}, singleQuadLabOk={}, quadLabGpuOk={}, rectangle quads={}/{}, bevel/caps={}/{}, outward fail/warn/minDot={}/{}/{:.4f}, lit minHintDot={:.4f}, landscape tiles surface/walls/unique={}/{}/{}, bevel/caps={}/{}, outward fail/warn/minDot={}/{}/{:.4f}, lit minHintDot={:.4f}, maxAdjacentLevelDelta={}, seams checked/mismatch/maxGap={}/{}/{:.4f}",
        rectangleOk,
        landscapeOk,
        outwardOk,
        litNormalsOk,
        singleQuadLabOk,
        quadLabGpuOk,
        g_rectModel.topQuadCount,
        g_rectModel.cliffWallQuadCount,
        g_rectModel.beveledSegmentCount,
        g_rectModel.cornerCapCount,
        rectangleOrientation.outwardFailCount,
        rectangleOrientation.outwardWarnCount,
        rectangleOrientation.minWallOutwardDot,
        rectangleLitNormals.minHintDot,
        g_landscapeModel.surfaceTileCount,
        g_landscapeModel.wallTileCount,
        g_landscapeModel.uniqueTileMeshCount,
        g_landscapeModel.beveledSegmentCount,
        g_landscapeModel.cornerCapCount,
        landscapeOrientation.outwardFailCount,
        landscapeOrientation.outwardWarnCount,
        landscapeOrientation.minWallOutwardDot,
        landscapeLitNormals.minHintDot,
        g_landscapeModel.maxAdjacentLevelDelta,
        g_landscapeModel.seamCheckedEdges,
        g_landscapeModel.seamMismatchCount,
        g_landscapeModel.seamMaxGap);
    return false;
}

} // namespace meshgen_playground
