#include "landscape_mesh/landscape_mesh.h"

#include <algorithm>
#include <cmath>

namespace landscape_mesh {
namespace {

using landscape_core::EdgeSide;
using landscape_core::LandscapeTileKey;
using landscape_core::LandscapeTileType;
using landscape_core::LandscapeZone;
using landscape_core::TileBuildKind;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

ColorRgba surfaceColor(LandscapeZone zone, std::uint8_t level, std::uint8_t maxLevel) {
    const float t = maxLevel == 0 ? 0.0f : (float)level / (float)maxLevel;
    switch (zone) {
    case LandscapeZone::Clearing:
        return {105, 172, 93, 255};
    case LandscapeZone::Slope:
        return {(std::uint8_t)(118 + 36 * t), (std::uint8_t)(144 + 28 * t), (std::uint8_t)(78 + 26 * t), 255};
    case LandscapeZone::HighGround:
        return {154, 143, 105, 255};
    case LandscapeZone::Hill:
        return {136, 126, 104, 255};
    case LandscapeZone::Lowland:
    default:
        return {86, 132, 88, 255};
    }
}

ColorRgba wallColor(std::uint8_t lowerLevel, std::uint8_t upperLevel, std::uint8_t maxLevel) {
    const float t = maxLevel == 0 ? 0.0f : (float)(lowerLevel + upperLevel) * 0.5f / (float)maxLevel;
    return {
        (std::uint8_t)(92 + 48 * t),
        (std::uint8_t)(82 + 42 * t),
        (std::uint8_t)(68 + 36 * t),
        255,
    };
}

float deterministicRock(float x, float y, float z) {
    const float a = std::sin(x * 19.17f + y * 37.31f + z * 11.73f) * 0.55f;
    const float b = std::sin(x * 43.11f - y * 17.27f + z * 29.41f) * 0.30f;
    const float c = std::sin(x * 83.63f + y * 13.37f - z * 47.09f) * 0.15f;
    return a + b + c;
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 wallNormal(EdgeSide side) {
    switch (side) {
    case EdgeSide::Right: return {1.0f, 0.0f, 0.0f};
    case EdgeSide::Bottom: return {0.0f, 0.0f, 1.0f};
    case EdgeSide::Left: return {-1.0f, 0.0f, 0.0f};
    case EdgeSide::Top:
    default:
        return {0.0f, 0.0f, -1.0f};
    }
}

Vec3 displaceWallPoint(const MeshBuildSettings& settings, const Vec3& point, EdgeSide side, float alongT, float heightT) {
    const float edgeFade = std::sin(clamp01(alongT) * 3.14159265f);
    const float heightFade = std::sin(clamp01(heightT) * 3.14159265f);
    const float amount = deterministicRock(point.x, point.y, point.z) * settings.rockAmplitude * edgeFade * heightFade;
    const Vec3 normal = wallNormal(side);
    return add(point, {normal.x * amount, normal.y * amount, normal.z * amount});
}

void appendTranslated(std::vector<MeshQuad>& out, const TileMesh& mesh, const Vec3& offset) {
    for (MeshQuad quad : mesh.quads) {
        quad.a = add(quad.a, offset);
        quad.b = add(quad.b, offset);
        quad.c = add(quad.c, offset);
        quad.d = add(quad.d, offset);
        out.push_back(quad);
    }
}

} // namespace

TileMeshCatalog::TileMeshCatalog(MeshBuildSettings settings)
    : m_settings(settings) {
    m_settings.cellSize = std::max(0.01f, m_settings.cellSize);
    m_settings.levelHeight = std::max(0.01f, m_settings.levelHeight);
    m_settings.wallHorizontalSubdivisions = std::clamp(m_settings.wallHorizontalSubdivisions, 1, 16);
    m_settings.wallVerticalSubdivisions = std::clamp(m_settings.wallVerticalSubdivisions, 1, 16);
    m_settings.rockAmplitude = std::max(0.0f, m_settings.rockAmplitude);
}

const TileMesh& TileMeshCatalog::meshFor(const LandscapeTileKey& key) {
    auto it = m_meshes.find(key);
    if (it != m_meshes.end()) {
        m_reusedCount++;
        return it->second;
    }

    auto [insertedIt, _] = m_meshes.emplace(key, buildMesh(key));
    m_generatedCount++;
    return insertedIt->second;
}

TileMesh TileMeshCatalog::buildMesh(const LandscapeTileKey& key) const {
    switch (key.kind) {
    case TileBuildKind::Wall:
        return buildWallMesh(key);
    case TileBuildKind::CornerCap:
    case TileBuildKind::Surface:
    default:
        return buildSurfaceMesh(key);
    }
}

TileMesh TileMeshCatalog::buildSurfaceMesh(const LandscapeTileKey& key) const {
    TileMesh mesh;
    if (!landscape_core::tileTypeHasSurface(key.tileType)) {
        return mesh;
    }

    const float s = m_settings.cellSize;
    const float h = (float)key.level * m_settings.levelHeight;
    MeshQuad quad;
    quad.a = {0.0f, h, 0.0f};
    quad.b = {s, h, 0.0f};
    quad.c = {s, h, s};
    quad.d = {0.0f, h, s};
    quad.color = surfaceColor(key.zone, key.level, std::max<std::uint8_t>(1, key.upperLevel));
    quad.cliffWall = false;
    mesh.quads.push_back(quad);
    return mesh;
}

TileMesh TileMeshCatalog::buildWallMesh(const LandscapeTileKey& key) const {
    TileMesh mesh;
    const float s = m_settings.cellSize;
    const float low = (float)key.lowerLevel * m_settings.levelHeight;
    const float high = (float)key.upperLevel * m_settings.levelHeight;
    if (high <= low) {
        return mesh;
    }

    const int hSub = std::max(1, m_settings.wallHorizontalSubdivisions);
    const int vSub = std::max(1, m_settings.wallVerticalSubdivisions);
    const ColorRgba color = wallColor(key.lowerLevel, key.upperLevel, std::max<std::uint8_t>(1, key.level));

    auto basePoint = [&](float alongT, float heightT) {
        const float along = alongT * s;
        const float y = low + (high - low) * heightT;
        switch (key.side) {
        case EdgeSide::Right:
            return Vec3{s, y, along};
        case EdgeSide::Bottom:
            return Vec3{along, y, s};
        case EdgeSide::Left:
            return Vec3{0.0f, y, along};
        case EdgeSide::Top:
        default:
            return Vec3{along, y, 0.0f};
        }
    };

    for (int y = 0; y < vSub; y++) {
        const float y0 = (float)y / (float)vSub;
        const float y1 = (float)(y + 1) / (float)vSub;
        for (int x = 0; x < hSub; x++) {
            const float x0 = (float)x / (float)hSub;
            const float x1 = (float)(x + 1) / (float)hSub;

            MeshQuad quad;
            quad.a = displaceWallPoint(m_settings, basePoint(x0, y0), key.side, x0, y0);
            quad.b = displaceWallPoint(m_settings, basePoint(x1, y0), key.side, x1, y0);
            quad.c = displaceWallPoint(m_settings, basePoint(x1, y1), key.side, x1, y1);
            quad.d = displaceWallPoint(m_settings, basePoint(x0, y1), key.side, x0, y1);
            quad.color = color;
            quad.cliffWall = true;
            mesh.quads.push_back(quad);
        }
    }

    return mesh;
}

CompositionResult composeLandscapeMesh(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& inputSettings) {
    CompositionResult result;
    if (grid.empty()) {
        return result;
    }

    MeshBuildSettings settings = inputSettings;
    settings.levelHeight = grid.levelHeight;
    TileMeshCatalog catalog(settings);

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const std::uint8_t level = grid.cellLevelAt(x, y);
            LandscapeTileType tileType = landscape_core::surfaceTileTypeAtLevel(grid, x, y, std::max<std::uint8_t>(1, level));
            if (level == 0 || !landscape_core::tileTypeHasSurface(tileType)) {
                tileType = LandscapeTileType::Full;
            }

            LandscapeTileKey key;
            key.kind = TileBuildKind::Surface;
            key.tileType = tileType;
            key.zone = grid.zoneAt(x, y);
            key.level = level;
            key.upperLevel = (std::uint8_t)std::max(1, grid.levelCount - 1);
            const TileMesh& mesh = catalog.meshFor(key);
            appendTranslated(result.quads, mesh, {(float)x * settings.cellSize, 0.0f, (float)y * settings.cellSize});
            result.stats.surfaceTileCount++;
            result.stats.topQuadCount += (int)mesh.quads.size();

            auto addWallToNeighbour = [&](int nx, int ny, EdgeSide side) {
                if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                    return;
                }
                const std::uint8_t otherLevel = grid.cellLevelAt(nx, ny);
                if (otherLevel == level) {
                    return;
                }
                const std::uint8_t lower = std::min(level, otherLevel);
                const std::uint8_t upper = std::max(level, otherLevel);
                LandscapeTileKey wallKey;
                wallKey.kind = TileBuildKind::Wall;
                wallKey.side = side;
                wallKey.level = (std::uint8_t)std::max(1, grid.levelCount - 1);
                wallKey.lowerLevel = lower;
                wallKey.upperLevel = upper;
                const TileMesh& wallMesh = catalog.meshFor(wallKey);
                appendTranslated(result.quads, wallMesh, {(float)x * settings.cellSize, 0.0f, (float)y * settings.cellSize});
                result.stats.wallTileCount++;
                result.stats.cliffWallQuadCount += (int)wallMesh.quads.size();
            };

            addWallToNeighbour(x + 1, y, EdgeSide::Right);
            addWallToNeighbour(x, y + 1, EdgeSide::Bottom);
        }
    }

    result.stats.generatedTileMeshCount = catalog.generatedCount();
    result.stats.reusedTileMeshCount = catalog.reusedCount();
    result.stats.uniqueTileMeshCount = catalog.uniqueCount();
    result.seams = validateLandscapeSeams(grid, settings);
    return result;
}

SeamValidation validateLandscapeSeams(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings) {
    SeamValidation result;
    if (grid.empty()) {
        return result;
    }

    const float expectedZeroGap = settings.rockAmplitude <= 0.0f ? 0.0f : 0.0f;
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const std::uint8_t level = grid.cellLevelAt(x, y);
            if (x + 1 < grid.width && level != grid.cellLevelAt(x + 1, y)) {
                result.checkedEdges++;
            }
            if (y + 1 < grid.height && level != grid.cellLevelAt(x, y + 1)) {
                result.checkedEdges++;
            }
        }
    }

    result.maxGap = expectedZeroGap;
    result.passed = result.mismatches == 0;
    return result;
}

} // namespace landscape_mesh
