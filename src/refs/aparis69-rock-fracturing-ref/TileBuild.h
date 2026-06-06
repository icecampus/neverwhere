#pragma once

#include "rock_fracture/Blocks.h"

#include <vector>

namespace render_playground {

enum class RockFractureKind : int;
struct RockFractureSettings;

struct TileBuildResult {
    rock_fracture::PointSet3 samples;
    rock_fracture::FractureSet fractures;
    std::vector<rock_fracture::BlockCluster> clusters;
    rock_fracture::SDFNode* sdfRoot = nullptr;
    bool usedTextureWarp = false;
    bool usedFallbackTexture = false;
};

rock_fracture::FractureType toRockFractureType(RockFractureKind kind);

bool setupWarpingField(const RockFractureSettings& settings, TileBuildResult& out);

TileBuildResult buildFractureTile(const RockFractureSettings& settings);

void releaseTileSdf(rock_fracture::SDFNode* root);

void applyPaperQualityPreset(RockFractureSettings& settings);

// Tile parameters only — does not change scene.mcResolution or single-tile mcResolution.
void applyPaperTilePreset(RockFractureSettings& settings);

// Default playground startup: cliff cube + all-vertical stone replication + paper tile quality.
void applyCliffSceneDefaults(RockFractureSettings& settings);

} // namespace render_playground
