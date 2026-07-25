#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Cliff3D asset (TileShapePlayground "Cliff 3D" brush port): no atlas — the
// whole cliff-field generator parameter set + shading palette from the
// "cliff3d" payload in index.json. Lives on the CliffLandscape layer; its
// Landscape tiles encode the vertex nodes (same tileIndex convention as
// slice/raised, so the pencil/RPC/authoring flow works unchanged).
//
// Params are edited live from the right-panel settings UI through
// cliffParams()/setCliffParam(); they write the canonical AssetData payload,
// so the renderer (ensureFrameAssets) picks changes on the next sync and
// AssetsLibraryModel::save serializes them back to index.json.
class CliffAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit CliffAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Cliff3dAssetData& cliffData() const { return *data.cliff3dData; }
    BaseData::Cliff3dAssetData& cliffData() { return *data.cliff3dData; }

    // Flat parameter map for the settings UI: plain scalar keys ("cellSize",
    // "groundEnabled", ...), shading scalars ("shading.ambient"), color
    // channels ("shading.darkColor.0") and groove angle elems
    // ("grooveAngles.0.0").
    Q_INVOKABLE QVariantMap cliffParams() const;
    Q_INVOKABLE void setCliffParam(const QString& name, const QVariant& value);

private:
    void ensureCliffData();
};
