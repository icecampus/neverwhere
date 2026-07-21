#include "pch.h"
#include "model_frame_source.h"

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <render_core/world_renderer.h>

#include <unordered_set>

#include <boost/uuid/uuid_io.hpp>

#include <QUuid>

#include "core/game_object.h"
#include "core/assets_library/asset.h"

void ModelFrameSource::buildWorldFrame(render_core::WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.raisedTiles.clear();
    outFrame.sprites.clear();

    if (!m_mapModel) return;

    // BaseLandscape layer -> landscape tiles (same order as the game client).
    if (LayerModel* layer = m_mapModel->layer(LayerTypes::BaseLandscape)) {
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

    // RaisedLandscape layer -> raised 3D tiles (Shape3d assets, walls + offset top).
    if (LayerModel* layer = m_mapModel->layer(LayerTypes::RaisedLandscape)) {
        layer->iterate([&outFrame](GameObject& obj) {
            const BaseData::GameObject data = obj.getData();
            if (data.type != GameObjectTypes::Landscape || !data.landscapeData) return;

            render_core::LandscapeTile t;
            t.cell = data.position;
            t.assetUuid = boost::uuids::to_string(data.assetUuid);
            t.tileIndex = data.landscapeData->tileIndex;
            outFrame.raisedTiles.push_back(std::move(t));
        });
    }

    // Decoration + GameplayInteractive -> sprites on top (client parity).
    for (const LayerTypes::Type layerType : {LayerTypes::Decoration, LayerTypes::GameplayInteractive}) {
        LayerModel* layer = m_mapModel->layer(layerType);
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

void ModelFrameSource::ensureFrameAssets(const render_core::WorldFrame& frame, render_core::WorldRenderer& renderer) {
    if (!m_assetsLibrary) return;

    std::unordered_set<std::string> uniqueAssets;
    for (const auto& t : frame.landscapeTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& t : frame.raisedTiles) uniqueAssets.insert(t.assetUuid);
    for (const auto& s : frame.sprites) uniqueAssets.insert(s.assetUuid);

    for (const auto& uuid : uniqueAssets) {
        Asset* asset = m_assetsLibrary->getAsset(QUuid::fromString(QString::fromStdString(uuid)));
        if (!asset) continue;

        const BaseData::AssetData& data = asset->getData();

        if (data.shape3dData) {
            // Raised tiles: same 4x6 atlas convention + presentation params.
            render_core::RaisedParams params;
            params.height = data.shape3dData->raisedHeight;
            params.rockWalls = data.shape3dData->rockWalls;
            params.amplitude = data.shape3dData->rockAmplitude;
            params.bevel = data.shape3dData->rockBevel;
            const std::filesystem::path topTexturePath = data.shape3dData->topTexture.empty()
                ? std::filesystem::path{}
                : data.root() / data.shape3dData->topTexture;
            renderer.ensureRaisedAtlas(uuid, data.root() / data.shape3dData->atlas, 4, 6, params, topTexturePath);
        }
        if (data.sliceData) {
            // Editor convention: landscape atlases are split into 4x6 tiles.
            renderer.ensureLandscapeAtlas(uuid, data.root() / data.sliceData->atlas, 4, 6);
        }
        if (data.imageData) {
            renderer.ensureSpriteImage(uuid, data.root() / data.imageData->imageFilename, data.imageData->width, data.pivot);
        }
    }
}

void ModelFrameSource::setMapModel(MapModel* model) {
    if (m_mapModel == model) return;
    m_mapModel = model;
    emit mapModelChanged();
}

void ModelFrameSource::setAssetsLibrary(AssetsLibraryModel* library) {
    if (m_assetsLibrary == library) return;
    m_assetsLibrary = library;
    emit assetsLibraryChanged();
}
