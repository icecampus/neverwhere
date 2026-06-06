#include "TileBuild.h"
#include "RockFractureScene.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <spdlog/spdlog.h>

namespace render_playground {

namespace {

bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}

} // namespace

rock_fracture::FractureType toRockFractureType(RockFractureKind kind) {
    switch (kind) {
    case RockFractureKind::Equidimensional: return rock_fracture::Equidimensional;
    case RockFractureKind::Rhombohedral:    return rock_fracture::Rhombohedral;
    case RockFractureKind::Polyhedral:      return rock_fracture::Polyhedral;
    case RockFractureKind::Tabular:         return rock_fracture::Tabular;
    }
    return rock_fracture::Equidimensional;
}

bool setupWarpingField(const RockFractureSettings& settings, TileBuildResult& out) {
    if (!settings.useTextureWarp) {
        rock_fracture::GenerateProceduralWarpingField(6.0, 4, settings.seed);
        out.usedFallbackTexture = true;
        return false;
    }

    namespace fs = std::filesystem;
    const fs::path relative = fs::path("src") / "apps" / "CliffsGenerationPlayground" / "resources" / "textures" / "rock1.png";

    fs::path dir = fs::current_path();
    for (int i = 0; i < 16; i++) {
        const fs::path candidate = dir / relative;
        if (fileExists(candidate)) {
            rock_fracture::LoadImageFileForWarping(candidate.string().c_str(), 0.0, 1.0);
            out.usedTextureWarp = true;
            spdlog::info("TileBuild: loaded warping texture from {}", candidate.string());
            return true;
        }
        fs::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }

    spdlog::warn("TileBuild: rock1.png not found, using procedural warping field");
    rock_fracture::GenerateProceduralWarpingField(6.0, 4, 42);
    out.usedFallbackTexture = true;
    return false;
}

TileBuildResult buildFractureTile(const RockFractureSettings& settings) {
    TileBuildResult result;
    std::srand((unsigned)settings.seed);

    setupWarpingField(settings, result);

    const float halfTile = settings.tileSize * 0.5f;
    const rock_fracture::Box tile(rock_fracture::Vector3(0), halfTile);

    result.samples = rock_fracture::PoissonSamplingBox(tile, settings.poissonRadius, settings.poissonTries);
    result.fractures = rock_fracture::GenerateFractures(
        toRockFractureType(settings.kind), tile, settings.fractureInflate);
    result.clusters = rock_fracture::ComputeBlockClusters(result.samples, result.fractures);
    result.sdfRoot = rock_fracture::ComputeBlockSDF(
        result.clusters, settings.blockSmoothingRadius, settings.bvhTransitionRadius);

    return result;
}

void releaseTileSdf(rock_fracture::SDFNode* root) {
    delete root;
}

void applyPaperQualityPreset(RockFractureSettings& settings) {
    settings.kind = RockFractureKind::Equidimensional;
    settings.seed = 1234;
    settings.tileSize = 20.0f;
    settings.poissonRadius = 0.5f;
    settings.poissonTries = 10000;
    settings.fractureInflate = 3.0f;
    settings.mcResolution = 200;
    settings.blockSmoothingRadius = 0.25;
    settings.bvhTransitionRadius = 0.5;
    settings.useOpenMP = true;
    settings.useTextureWarp = true;
}

void applyPaperTilePreset(RockFractureSettings& settings) {
    applyPaperQualityPreset(settings);
}

void applyCliffSceneDefaults(RockFractureSettings& settings) {
    settings.mode = GenerationMode::CliffScene;
    settings.enableBlockReplication = true;
    settings.scene.replicationMode = CliffReplicationMode::AllVerticalFaces;
    settings.scene.surfaceBand = 8.0f;
    settings.scene.protrusionMargin = 2.0f;
    settings.scene.gapFill = 0.0f;
    if (settings.scene.mcResolution < 32) {
        settings.scene.mcResolution = 140;
    }
    const int sceneMc = settings.scene.mcResolution;
    applyPaperTilePreset(settings);
    settings.poissonRadius = 0.38f;
    settings.blockSmoothingRadius = 0.06;
    settings.bvhTransitionRadius = 0.22;
    settings.fractureInflate = 4.5f;
    settings.scene.mcResolution = sceneMc;
    settings.mcResolution = sceneMc;
}

} // namespace render_playground
