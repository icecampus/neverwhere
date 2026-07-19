#include "pch.h"
#include "map_frame_bridge.h"

#include <unordered_set>

#include <boost/uuid/uuid_io.hpp>

#include <QUuid>

#include "core/map/map_model.h"
#include "core/game_object.h"
#include "core/assets_library/assets_library_model.h"
#include "core/assets_library/asset.h"

namespace map_frame_bridge {

void buildWorldFrame(MapModel& mapModel, render_core::WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.sprites.clear();

    // BaseLandscape layer -> landscape tiles (same order as the game client).
    if (LayerModel* layer = mapModel.layer(LayerTypes::BaseLandscape)) {
        layer->iterate([&outFrame](GameObject& obj) {
            const BaseData::GameObject data = obj.getData();
            if (data.type != GameObjectTypes::Landscape || !data.landscapeData) return;

            render_core::LandscapeTile t;
            t.cell = data.position; // math::ivec2 publicly derives from glm::ivec2
            t.assetUuid = boost::uuids::to_string(data.assetUuid);
            t.tileIndex = data.landscapeData->tileIndex;
            outFrame.landscapeTiles.push_back(std::move(t));
        });
    }

    // Decoration + GameplayInteractive -> sprites on top (client parity).
    for (const LayerTypes::Type layerType : {LayerTypes::Decoration, LayerTypes::GameplayInteractive}) {
        LayerModel* layer = mapModel.layer(layerType);
        if (!layer) continue;

        layer->iterate([&outFrame](GameObject& obj) {
            const BaseData::GameObject data = obj.getData();
            if (data.type == GameObjectTypes::Landscape) return;

            render_core::SpriteInstance s;
            s.cell = data.position;
            s.assetUuid = boost::uuids::to_string(data.assetUuid);
            outFrame.sprites.push_back(std::move(s));
        });
    }
}

void ensureFrameAssets(AssetsLibraryModel& assetsLibrary, const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) {
    std::unordered_set<std::string> uniqueAssets;
    for (const auto& t : frame.landscapeTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& s : frame.sprites) uniqueAssets.insert(s.assetUuid);

    for (const auto& uuid : uniqueAssets) {
        Asset* asset = assetsLibrary.getAsset(QUuid::fromString(QString::fromStdString(uuid)));
        if (!asset) continue;

        const BaseData::AssetData& data = asset->getData();

        if (data.sliceData) {
            // Editor convention: landscape atlases are split into 4x6 tiles.
            renderer.ensureLandscapeAtlas(uuid, data.root() / data.sliceData->atlas, 4, 6);
        }
        if (data.imageData) {
            renderer.ensureSpriteImage(uuid, data.root() / data.imageData->imageFilename, data.imageData->width, data.pivot);
        }
    }
}

} // namespace map_frame_bridge
