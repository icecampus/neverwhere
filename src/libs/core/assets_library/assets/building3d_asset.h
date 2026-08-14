#pragma once
#include "assets_library/assets/slice_asset.h"

// Building3D: a GLB mesh placed as a discrete Buildings GameObject. No atlas
// — the payload is the "building3d" block in index.json (model + albedo +
// cell footprint). Lives on GameplayInteractive.
class Building3dAsset: public SliceAsset
{
    Q_OBJECT
    Q_PROPERTY(int footprintWidth READ footprintWidth CONSTANT)
    Q_PROPERTY(int footprintHeight READ footprintHeight CONSTANT)

public:
    explicit Building3dAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    int footprintWidth() const;
    int footprintHeight() const;

    const BaseData::Building3dAssetData* buildingData() const
    {
        return data.building3dData ? &*data.building3dData : nullptr;
    }
};
