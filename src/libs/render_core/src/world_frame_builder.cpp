#include "render_core/world_frame_builder.h"

#include <unordered_set>

#include <spdlog/spdlog.h>

namespace render_core {

void collectWorldFrame(const game_data::Map& map, WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.raisedTiles.clear();
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

void ensureWorldAssets(const game_data::AssetIndex& assetIndex, const WorldFrame& frame, WorldRenderer& renderer) {
    std::unordered_set<std::string> uniqueAssets;
    for (const auto& t : frame.landscapeTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.raisedTiles) uniqueAssets.insert(t.assetUuid);
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
            renderer.ensureRaisedAtlas(uuid, entry->atlasPath, entry->cols, entry->rows, params);
        } else if (entry->isSlice()) {
            renderer.ensureLandscapeAtlas(uuid, entry->atlasPath, entry->cols, entry->rows);
        }
        if (entry->isImage()) {
            renderer.ensureSpriteImage(uuid, entry->imagePath, entry->widthCells, entry->pivot);
        }
    }
}

} // namespace render_core
