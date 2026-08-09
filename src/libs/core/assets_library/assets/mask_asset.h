#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Mask3D asset (the SDFWithMaterialLandscape "Mask 3D" brush graduated into
// the editor): no atlas — the mask-field generator parameter set + the
// PBR-lite material (an ambientCG-style map set in the bundle) + shading
// palette from the "mask3d" payload in index.json. Lives on the
// MaskLandscape layer; its Landscape tiles encode the vertex nodes (same
// tileIndex convention as slice/raised, so the pencil/RPC/authoring flow
// works unchanged).
//
// Params are edited live from the right-panel settings UI through
// maskParams()/setMaskParam(); they write the canonical AssetData payload,
// so the renderer (ensureFrameAssets) picks changes on the next sync and
// AssetsLibraryModel::save serializes them back to index.json.
class MaskAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit MaskAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Mask3dAssetData& maskData() const { return *data.mask3dData; }
    BaseData::Mask3dAssetData& maskData() { return *data.mask3dData; }

    // Flat parameter map for the settings UI: plain scalar keys ("cellSize",
    // "spreadDistance", "matTiling", ...), shading scalars ("shading.ambient")
    // and color channels ("shading.darkColor.0").
    Q_INVOKABLE QVariantMap maskParams() const;
    Q_INVOKABLE void setMaskParam(const QString& name, const QVariant& value);

private:
    void ensureMaskData();
};
