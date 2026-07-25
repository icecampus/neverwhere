#include "cliff_asset.h"

CliffAsset::CliffAsset(QObject* parent):
    SliceAsset(AssetTypes::cliff3d, parent)
{

}

void CliffAsset::load(const BaseData::AssetData& data)
{
    Asset::load(data);

    if (!data.cliff3dData)
    {
        return;
    }

    m_cliffData = *data.cliff3dData;

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (!m_cliffData.thumbnail.empty())
    {
        loadAtlasFiles(data.root() / m_cliffData.thumbnail, {});
    }
}
