#include "MeshBridge.h"

#include "PlaygroundState.h"
#include "RectangleCliffScenario.h"

namespace meshgen_playground {

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
    result.color = IM_COL32(quad.color.r, quad.color.g, quad.color.b, quad.color.a);
    result.cliffWall = quad.cliffWall;
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
    return meshSettings;
}

} // namespace meshgen_playground
