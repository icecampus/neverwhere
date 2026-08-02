#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Stone3D asset (SDFGeneratedLandscape "Stone 3D" brush port): no atlas — the
// whole stone-field generator parameter set + shading palette from the
// "stone3d" payload in index.json. Lives on the StoneLandscape layer; its
// Landscape tiles encode the vertex nodes (same tileIndex convention as
// slice/raised, so the pencil/RPC/authoring flow works unchanged).
//
// Params are edited live from the right-panel settings UI through
// stoneParams()/setStoneParam(); they write the canonical AssetData payload,
// so the renderer (ensureFrameAssets) picks changes on the next sync and
// AssetsLibraryModel::save serializes them back to index.json.
class StoneAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit StoneAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Stone3dAssetData& stoneData() const { return *data.stone3dData; }
    BaseData::Stone3dAssetData& stoneData() { return *data.stone3dData; }

    // Flat parameter map for the settings UI: plain scalar keys ("cellSize",
    // "flatTop", ...), shading scalars ("shading.ambient"), color channels
    // ("shading.darkColor.0") and groove angle elems ("grooveAngles.0.0").
    Q_INVOKABLE QVariantMap stoneParams() const;
    Q_INVOKABLE void setStoneParam(const QString& name, const QVariant& value);

private:
    void ensureStoneData();
};
