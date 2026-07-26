#include "render_core/world_frame_builder.h"

#include <unordered_set>

#include <spdlog/spdlog.h>

namespace render_core {

void collectWorldFrame(const game_data::Map& map, WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.raisedTiles.clear();
    outFrame.cliffTiles.clear();
    outFrame.sprites.clear();

    for (const auto& obj : map.layer(game_data::LayerType::BaseLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.landscapeTiles.push_back(std::move(t));
    }

    // RaisedLandscape layer -> raised 3D tiles (Shape3d assets, walls + offset top).
    for (const auto& obj : map.layer(game_data::LayerType::RaisedLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.raisedTiles.push_back(std::move(t));
    }

    // CliffLandscape layer -> cliff tiles (Cliff3d assets, surface-nets cliffs).
    for (const auto& obj : map.layer(game_data::LayerType::CliffLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.cliffTiles.push_back(std::move(t));
    }

    for (const game_data::LayerType layerType : {game_data::LayerType::Decoration, game_data::LayerType::GameplayInteractive}) {
        for (const auto& obj : map.layer(layerType)) {
            if (obj.type == game_data::GameObjectType::Landscape) continue;

            SpriteInstance s;
            s.cell = obj.position;
            s.assetUuid = obj.assetUuid;
            outFrame.sprites.push_back(std::move(s));
        }
    }
}

// game_data -> render_core parameter conversion for cliff3d assets (the
// editor's ModelFrameSource has its own BaseData twin).
static CliffParams cliffParamsFromAssetData(const game_data::Cliff3dAssetData& d) {
    CliffParams params;
    params.heightScale = d.raisedHeight;
    cliff::FieldParams& f = params.field;
    f.cellSize = d.cellSize;
    f.padding = d.padding;
    f.plateauHeight = d.plateauHeight;
    f.d2Scale = d.d2Scale;
    f.blurRadiusCells = d.blurRadiusCells;
    f.blurPasses = d.blurPasses;
    f.edgeRadius = d.edgeRadius;
    f.grooveMaskWidth = d.grooveMaskWidth;
    f.grooveFadeK = d.grooveFadeK;
    f.grooveRimFade = d.grooveRimFade;
    f.fbmAmplitude = d.fbmAmplitude;
    f.fbmFrequency = d.fbmFrequency;
    f.fbmOctaves = d.fbmOctaves;
    f.groundDepth = d.groundDepth;
    f.groundMargin = d.groundMargin;
    f.groundRounding = d.groundRounding;
    f.groundEnabled = d.groundEnabled;
    f.groovePeriod = d.groovePeriod;
    f.groovePhase = d.groovePhase;
    f.grooveDepthMax = d.grooveDepthMax;
    f.grooveSmooth = d.grooveSmooth;
    f.grooveAngles[0][0] = d.grooveAngles[0][0];
    f.grooveAngles[0][1] = d.grooveAngles[0][1];
    f.grooveAngles[1][0] = d.grooveAngles[1][0];
    f.grooveAngles[1][1] = d.grooveAngles[1][1];
    f.grooveAngles[2][0] = d.grooveAngles[2][0];
    f.grooveAngles[2][1] = d.grooveAngles[2][1];
    CliffShading& s = params.shading;
    s.lightAzimuth = d.shading.lightAzimuth;
    s.lightElevation = d.shading.lightElevation;
    s.darkColor = d.shading.darkColor;
    s.goldColor = d.shading.goldColor;
    s.grassA = d.shading.grassA;
    s.grassB = d.shading.grassB;
    s.veinThreshold = d.shading.veinThreshold;
    s.ambient = d.shading.ambient;
    s.diffuse = d.shading.diffuse;
    s.backLight = d.shading.backLight;
    s.specStrength = d.shading.specStrength;
    s.specPower = d.shading.specPower;
    s.gamma = d.shading.gamma;
    s.texScale = d.shading.texScale;
    s.bottomDarken = d.shading.bottomDarken;
    s.bottomBand = d.shading.bottomBand;
    s.strataStrength = d.shading.strataStrength;
    params.flareAmount = d.flareAmount;
    params.flareBand = d.flareBand;
    return params;
}

void ensureWorldAssets(const game_data::AssetIndex& assetIndex, const WorldFrame& frame, WorldRenderer& renderer) {
    std::unordered_set<std::string> uniqueAssets;
    for (const auto& t : frame.landscapeTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.raisedTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.cliffTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& s : frame.sprites) uniqueAssets.insert(s.assetUuid);

    for (const auto& uuid : uniqueAssets) {
        const game_data::AssetIndexEntry* entry = assetIndex.find(uuid);
        if (!entry) {
            spdlog::warn("Asset not found for assetUuid={}", uuid);
            continue;
        }
        if (entry->isShape3d()) {
            RaisedParams params;
            params.height = entry->raisedHeight;
            params.rockWalls = entry->rockWalls;
            params.amplitude = entry->rockAmplitude;
            params.bevel = entry->rockBevel;
            renderer.ensureRaisedAtlas(uuid, entry->atlasPath, entry->cols, entry->rows, params, entry->topTexturePath);
        } else if (entry->isSlice()) {
            renderer.ensureLandscapeAtlas(uuid, entry->atlasPath, entry->cols, entry->rows);
        }
        if (entry->isCliff3d()) {
            CliffParams params = cliffParamsFromAssetData(entry->cliff);
            params.topTexturePath = entry->topTexturePath;
            renderer.ensureCliffAsset(uuid, params);
        }
        if (entry->isImage()) {
            renderer.ensureSpriteImage(uuid, entry->imagePath, entry->widthCells, entry->pivot);
        }
    }
}

} // namespace render_core
