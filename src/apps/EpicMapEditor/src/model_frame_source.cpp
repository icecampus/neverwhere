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

namespace {

// BaseData -> render_core parameter conversion for cliff3d assets (the
// runtime's world_frame_builder has its own game_data twin).
render_core::CliffParams cliffParamsFromAssetData(const BaseData::Cliff3dAssetData& d) {
    render_core::CliffParams params;
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
    render_core::CliffShading& s = params.shading;
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
    return params;
}

} // namespace

void ModelFrameSource::buildWorldFrame(render_core::WorldFrame& outFrame) {
    outFrame.landscapeTiles.clear();
    outFrame.raisedTiles.clear();
    outFrame.cliffTiles.clear();
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

    // CliffLandscape layer -> cliff tiles (Cliff3d assets, surface-nets cliffs).
    if (LayerModel* layer = m_mapModel->layer(LayerTypes::CliffLandscape)) {
        layer->iterate([&outFrame](GameObject& obj) {
            const BaseData::GameObject data = obj.getData();
            if (data.type != GameObjectTypes::Landscape || !data.landscapeData) return;

            render_core::LandscapeTile t;
            t.cell = data.position;
            t.assetUuid = boost::uuids::to_string(data.assetUuid);
            t.tileIndex = data.landscapeData->tileIndex;
            outFrame.cliffTiles.push_back(std::move(t));
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
    for (const auto& t : frame.cliffTiles) uniqueAssets.insert(t.assetUuid);
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
        if (data.cliff3dData) {
            // Cliff tiles: the full generator + shading parameter set (no
            // atlas — geometry comes from the cliff field).
            renderer.ensureCliffAsset(uuid, cliffParamsFromAssetData(*data.cliff3dData));
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
