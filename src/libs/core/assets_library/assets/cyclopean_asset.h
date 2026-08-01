#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Cyclopean3D asset (SDFGeneratedLandscape "Cyclopean 3D" brush port): no
// atlas — the landscape_mesh composer params (Cyclopean wall style) from the
// "cyclopean3d" payload in index.json. Lives on the CyclopeanLandscape layer;
// its Landscape tiles encode the vertex nodes (same tileIndex convention as
// slice/cliff, so the pencil/RPC/authoring flow works unchanged).
//
// Params are edited live from the right-panel settings UI through
// cyclopeanParams()/setCyclopeanParam(); they write the canonical AssetData
// payload, so the renderer (ensureFrameAssets) picks changes on the next sync
// and AssetsLibraryModel::save serializes them back to index.json.
class CyclopeanAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit CyclopeanAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Cyclopean3dAssetData& cyclopeanData() const { return *data.cyclopean3dData; }
    BaseData::Cyclopean3dAssetData& cyclopeanData() { return *data.cyclopean3dData; }

    // Flat parameter map for the settings UI: plain scalar keys
    // ("raisedHeight", "rockSeed", ...).
    Q_INVOKABLE QVariantMap cyclopeanParams() const;
    Q_INVOKABLE void setCyclopeanParam(const QString& name, const QVariant& value);

private:
    void ensureCyclopeanData();
};
