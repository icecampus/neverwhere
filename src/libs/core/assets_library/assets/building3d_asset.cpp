#include "building3d_asset.h"

Building3dAsset::Building3dAsset(QObject* parent):
    SliceAsset(AssetTypes::building3d, parent)
{
}

void Building3dAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    if (data.building3dData && !data.building3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.building3dData->thumbnail, {});
    }
}

int Building3dAsset::footprintWidth() const
{
    return data.building3dData ? data.building3dData->footprintWidth : 3;
}

int Building3dAsset::footprintHeight() const
{
    return data.building3dData ? data.building3dData->footprintHeight : 3;
}
