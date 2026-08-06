#include "render_core/world_frame_builder.h"

#include <unordered_set>

#include <spdlog/spdlog.h>

namespace render_core {

void collectWorldFrame(const game_data::Map& map, WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.raisedTiles.clear();
    outFrame.cliffTiles.clear();
    outFrame.cyclopeanTiles.clear();
    outFrame.stoneTiles.clear();
    outFrame.textureTiles.clear();
    outFrame.techTiles.clear();
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

    // CyclopeanLandscape layer -> cyclopean tiles (Cyclopean3d assets,
    // landscape_mesh plateau with Cyclopean walls).
    for (const auto& obj : map.layer(game_data::LayerType::CyclopeanLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.cyclopeanTiles.push_back(std::move(t));
    }

    // StoneLandscape layer -> stone tiles (Stone3d assets, voronoi-carved
    // surface-nets plateau sharing the cliff pass).
    for (const auto& obj : map.layer(game_data::LayerType::StoneLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.stoneTiles.push_back(std::move(t));
    }

    // TextureLandscape layer -> texture tiles (Texture2d assets, tiling
    // world-UV textures blending across shared nodes).
    for (const auto& obj : map.layer(game_data::LayerType::TextureLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.textureTiles.push_back(std::move(t));
    }

    // TechLandscape layer -> tech tiles (Tech3d assets, TechnicalGrass
    // ridge/valley heightfield sharing the cliff pass).
    for (const auto& obj : map.layer(game_data::LayerType::TechLandscape)) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        outFrame.techTiles.push_back(std::move(t));
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

// game_data -> render_core parameter conversion for cyclopean3d assets.
static CyclopeanParams cyclopeanParamsFromAssetData(const game_data::Cyclopean3dAssetData& d) {
    CyclopeanParams params;
    params.raisedHeight = d.raisedHeight;
    params.rockSeed = d.rockSeed;
    params.rockAmplitude = d.rockAmplitude;
    params.rockEnabled = d.rockEnabled;
    params.cornerBevel = d.cornerBevel;
    params.wallSubdivH = d.wallSubdivH;
    params.wallSubdivV = d.wallSubdivV;
    return params;
}

// game_data -> render_core parameter conversion for stone3d assets (the
// editor's ModelFrameSource has its own BaseData twin). The mesh params ride
// in CliffParams::stoneField; CliffParams::field stays unused.
static CliffParams stoneParamsFromAssetData(const game_data::Stone3dAssetData& d) {
    CliffParams params;
    params.heightScale = d.raisedHeight;
    stone_gen::StoneFieldParams stone;
    // Base slab (mirror of cliff::FieldParams; its grooves/fbm stay unused by
    // the stone field, the ground slab stays off by the data default).
    cliff::FieldParams& f = stone.base;
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
    // Stone carve. The shared slots (blurPasses, grooveMaskWidth,
    // fbmAmplitude, fbmFrequency) feed both the base slab above and the stone
    // field's own carve band / fbm / sampled-field blur.
    stone.voroScale = d.voroScale;
    stone.cellJitter = d.cellJitter;
    stone.grooveDepth = d.grooveDepth;
    stone.grooveK = d.grooveK;
    stone.grooveMaskWidth = d.grooveMaskWidth;
    stone.fbmAmplitude = d.fbmAmplitude;
    stone.fbmFrequency = d.fbmFrequency;
    stone.seed = d.seed;
    stone.blurPasses = d.blurPasses;
    stone.flatTopLo = d.flatTopLo;
    stone.flatTopHi = d.flatTopHi;
    stone.rimWidth = d.rimWidth;
    stone.rimBulge = d.rimBulge;
    stone.rimNotch = d.rimNotch;
    stone.flatTop = d.flatTop;
    params.stoneField = stone;
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
    s.bottomDarken = d.shading.bottomDarken;
    s.bottomBand = d.shading.bottomBand;
    s.strataStrength = d.shading.strataStrength;
    s.texScale = d.topTexTiles; // stone top tiling lives in the extras, not in shading.texScale
    // Stone shading extras: the flat-top plane (boulders above it keep the
    // wall palette), the below-plane grass fade, the rim gradient strength
    // and the top texture mix.
    params.stonePlaneY = d.plateauHeight + d.edgeRadius;
    params.stoneGrassFade = d.grassFade;
    params.stoneRimShade = d.rimShade;
    params.stoneTopTexMix = d.topTexMix;
    return params;
}

// game_data -> render_core parameter conversion for tech3d assets (the
// editor's ModelFrameSource has its own BaseData twin). The mesh params ride
// in CliffParams::techField; CliffParams::field stays unused. The tech look is
// the per-asset palette (TechnicalGrass earth ramps, no veins) — the crease
// groove channel of the field draws the tile contour.
static CliffParams techParamsFromAssetData(const game_data::Tech3dAssetData& d) {
    CliffParams params;
    params.heightScale = d.raisedHeight;
    tech::TechFieldParams t;
    t.cellSize = d.cellSize;
    t.padding = d.padding;
    t.levelHeight = d.levelHeight;
    t.groundDepth = d.groundDepth;
    t.style = d.style;
    t.soften = d.soften;
    t.creaseWidth = d.creaseWidth;
    t.blurPasses = d.blurPasses;
    t.outlineDepth = d.outlineDepth;
    params.techField = t;
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
    return params;
}

void ensureWorldAssets(const game_data::AssetIndex& assetIndex, const WorldFrame& frame, WorldRenderer& renderer) {
    std::unordered_set<std::string> uniqueAssets;
    for (const auto& t : frame.landscapeTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.raisedTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.cliffTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.cyclopeanTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.stoneTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.textureTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.techTiles) uniqueAssets.insert(t.assetUuid);
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
        if (entry->isCyclopean3d()) {
            renderer.ensureCyclopeanAsset(uuid, cyclopeanParamsFromAssetData(entry->cyclopean));
        }
        if (entry->isStone3d()) {
            CliffParams params = stoneParamsFromAssetData(entry->stone);
            params.topTexturePath = entry->topTexturePath;
            renderer.ensureStoneAsset(uuid, params);
        }
        if (entry->isTexture2d()) {
            renderer.ensureTextureAsset(uuid, entry->texturePath, entry->textureData.tilingRepeats);
        }
        if (entry->isTech3d()) {
            renderer.ensureTechAsset(uuid, techParamsFromAssetData(entry->tech));
        }
        if (entry->isImage()) {
            renderer.ensureSpriteImage(uuid, entry->imagePath, entry->widthCells, entry->pivot);
        }
    }
}

} // namespace render_core
