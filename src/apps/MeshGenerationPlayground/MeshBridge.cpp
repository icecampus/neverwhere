#include "MeshBridge.h"

#include "PlaygroundState.h"
#include "RectangleCliffScenario.h"

#include <cmath>

#include <landscape_core/sun_shadow.h>

namespace meshgen_playground {

namespace {

Vec3 normalizeVec3(const Vec3& value) {
    const float lenSq = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lenSq <= 1e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    return {value.x * invLen, value.y * invLen, value.z * invLen};
}

} // namespace

LandscapeZone toLocalZone(landscape_core::LandscapeZone zone) {
    switch (zone) {
    case landscape_core::LandscapeZone::Clearing:
        return LandscapeZone::Clearing;
    case landscape_core::LandscapeZone::Slope:
        return LandscapeZone::Slope;
    case landscape_core::LandscapeZone::HighGround:
        return LandscapeZone::HighGround;
    case landscape_core::LandscapeZone::Hill:
        return LandscapeZone::Hill;
    case landscape_core::LandscapeZone::Lowland:
    default:
        return LandscapeZone::Lowland;
    }
}

landscape_core::LandscapeBowlSettings toCoreLandscapeSettings(const LandscapeBowlSettings& settings) {
    landscape_core::LandscapeBowlSettings result;
    result.gridWidth = settings.gridWidth;
    result.gridHeight = settings.gridHeight;
    result.seed = settings.seed;
    result.clearingRadius = settings.clearingRadius;
    result.clearingSoftness = settings.clearingSoftness;
    result.highGroundRadius = settings.highGroundRadius;
    result.highGroundWidth = settings.highGroundWidth;
    result.highGroundHeight = settings.highGroundHeight;
    result.heightLevels = settings.heightLevels;
    result.arcNoiseScale = settings.arcNoiseScale;
    result.arcNoiseAmplitude = settings.arcNoiseAmplitude;
    result.hillCount = settings.hillCount;
    result.hillHeight = settings.hillHeight;
    result.hillRadius = settings.hillRadius;
    return result;
}

MeshQuad toAppMeshQuad(const landscape_mesh::MeshQuad& quad) {
    MeshQuad result;
    result.a = {quad.a.x, quad.a.y, quad.a.z};
    result.b = {quad.b.x, quad.b.y, quad.b.z};
    result.c = {quad.c.x, quad.c.y, quad.c.z};
    result.d = {quad.d.x, quad.d.y, quad.d.z};
    result.normal = {quad.normal.x, quad.normal.y, quad.normal.z};
    result.color = IM_COL32(quad.color.r, quad.color.g, quad.color.b, quad.color.a);
    result.cliffWall = quad.cliffWall;
    result.relief = quad.relief;
    result.heightFraction = quad.heightFraction;
    result.boundarySide = static_cast<BoundarySide>(static_cast<int>(quad.boundarySide));
    result.outwardHint = {quad.outwardHint.x, quad.outwardHint.y, quad.outwardHint.z};
    return result;
}

landscape_mesh::MeshQuad toLandscapeMeshQuad(const MeshQuad& quad) {
    landscape_mesh::MeshQuad result;
    result.a = {quad.a.x, quad.a.y, quad.a.z};
    result.b = {quad.b.x, quad.b.y, quad.b.z};
    result.c = {quad.c.x, quad.c.y, quad.c.z};
    result.d = {quad.d.x, quad.d.y, quad.d.z};
    result.normal = {quad.normal.x, quad.normal.y, quad.normal.z};
    result.color = {
        (std::uint8_t)((quad.color >> IM_COL32_R_SHIFT) & 0xff),
        (std::uint8_t)((quad.color >> IM_COL32_G_SHIFT) & 0xff),
        (std::uint8_t)((quad.color >> IM_COL32_B_SHIFT) & 0xff),
        (std::uint8_t)((quad.color >> IM_COL32_A_SHIFT) & 0xff),
    };
    result.cliffWall = quad.cliffWall;
    result.relief = quad.relief;
    result.heightFraction = quad.heightFraction;
    result.boundarySide = static_cast<landscape_mesh::BoundarySide>(static_cast<int>(quad.boundarySide));
    result.outwardHint = {quad.outwardHint.x, quad.outwardHint.y, quad.outwardHint.z};
    return result;
}

landscape_mesh::SolidMaskGrid toSharedSolidMaskGrid(
    const std::vector<std::uint8_t>& solidCells,
    int width,
    int height) {

    landscape_mesh::SolidMaskGrid mask;
    mask.width = width;
    mask.height = height;
    mask.solidCells = solidCells;
    mask.topCells = solidCells;
    mask.zones.assign(solidCells.size(), landscape_core::LandscapeZone::Lowland);
    return mask;
}

landscape_mesh::MeshBuildSettings makeSharedLandscapeMeshSettings(float levelHeight) {
    RectangleCliffSettings qualitySettings = g_rectSettings;
    sanitizeSettings(qualitySettings);

    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cellSize = 1.0f;
    meshSettings.levelHeight = levelHeight;
    meshSettings.cornerBevel = qualitySettings.cornerBevel;
    meshSettings.rockEnabled = qualitySettings.rockEnabled;
    meshSettings.rockSeed = qualitySettings.rockSeed;
    meshSettings.rockScale = qualitySettings.rockScale;
    meshSettings.rockAmplitude = qualitySettings.rockAmplitude;
    meshSettings.wallHorizontalSubdivisions = qualitySettings.wallHorizontalSubdivisions;
    meshSettings.wallVerticalSubdivisions = qualitySettings.wallVerticalSubdivisions;
    meshSettings.terraceSteps = qualitySettings.terraceSteps;
    meshSettings.wallStyle = g_productionWallStyle == 1
        ? landscape_mesh::WallStyleId::Cyclopean
        : landscape_mesh::WallStyleId::BlockCliff;
    return meshSettings;
}

void applySunShadowToMeshQuads(
    std::vector<MeshQuad>& quads,
    int gridWidth,
    int gridHeight,
    const std::vector<std::uint8_t>& cellLevels,
    const Vec3& lightDirection) {

    if (gridWidth <= 0 || gridHeight <= 0 || cellLevels.empty()) {
        return;
    }

    landscape_core::SunShadowSettings settings;
    settings.lightDirectionX = lightDirection.x;
    settings.lightDirectionY = lightDirection.y;
    settings.lightDirectionZ = lightDirection.z;
    const std::vector<float> shadowField =
        landscape_core::computeSunShadowField(gridWidth, gridHeight, cellLevels, settings);

    const Vec3 lightDir = normalizeVec3(lightDirection);
    for (MeshQuad& quad : quads) {
        const float centerX =
            (quad.a.x + quad.b.x + quad.c.x + quad.d.x) * 0.25f;
        const float centerZ =
            (quad.a.z + quad.b.z + quad.c.z + quad.d.z) * 0.25f;
        const float groundVisibility = landscape_core::sampleShadowFieldBilinear(
            shadowField,
            gridWidth,
            gridHeight,
            centerX,
            centerZ);

        if (!quad.cliffWall) {
            quad.sunShadow = groundVisibility;
            continue;
        }

        const landscape_mesh::Vec3 litNormal = landscape_mesh::litWallNormal(toLandscapeMeshQuad(quad));
        const float facing = std::max(
            0.0f,
            litNormal.x * lightDir.x + litNormal.y * lightDir.y + litNormal.z * lightDir.z);
        const float wallAmbient = 0.40f + 0.18f * facing;
        quad.sunShadow = std::clamp(wallAmbient + (1.0f - wallAmbient) * groundVisibility * (0.42f + 0.58f * facing), 0.28f, 1.0f);
    }
}

} // namespace meshgen_playground
