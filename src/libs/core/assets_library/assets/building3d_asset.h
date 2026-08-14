#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Building3D: a GLB mesh placed as a discrete Buildings GameObject. No atlas
// — the payload is the "building3d" block in index.json (model + albedo +
// cell footprint). Lives on GameplayInteractive.
//
// Params are edited live from the right-panel settings UI through
// buildingParams()/setBuildingParam(); they write the canonical AssetData
// payload, so the renderer (ensureFrameAssets) picks changes on the next
// sync and AssetsLibraryModel::save serializes them back to index.json.
class Building3dAsset: public SliceAsset
{
    Q_OBJECT
    Q_PROPERTY(int footprintWidth READ footprintWidth NOTIFY footprintChanged)
    Q_PROPERTY(int footprintHeight READ footprintHeight NOTIFY footprintChanged)

public:
    explicit Building3dAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    int footprintWidth() const;
    int footprintHeight() const;

    const BaseData::Building3dAssetData* buildingData() const
    {
        return data.building3dData ? &*data.building3dData : nullptr;
    }

    // Flat parameter map for the settings UI: footprint/heightScale/yaw/scale
    // scalars + the descriptor strings ("model", "albedo", "thumbnail").
    Q_INVOKABLE QVariantMap buildingParams() const;
    Q_INVOKABLE void setBuildingParam(const QString& name, const QVariant& value);

signals:
    void footprintChanged();

private:
    void ensureBuildingData();
};
