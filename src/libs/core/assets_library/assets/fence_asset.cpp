#include "fence_asset.h"

FenceAsset::FenceAsset(QObject* parent):
    SliceAsset(AssetTypes::fence3d, parent)
{

}

void FenceAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (data.fence3dData && !data.fence3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.fence3dData->thumbnail, {});
    }
}
