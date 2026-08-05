#include "texture_asset.h"

TextureAsset::TextureAsset(QObject* parent):
    SliceAsset(AssetTypes::texture2d, parent)
{

}

void TextureAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (data.texture2dData && !data.texture2dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.texture2dData->thumbnail, {});
    }
}
