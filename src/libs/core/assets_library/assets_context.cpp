#include "assets_context.h"

AssetsContext::AssetsContext(QObject* parent)
{

}

AssetsLibraryModel* AssetsContext::getAssetsLibrary()
{
    return assetsLibrary;
}

void AssetsContext::setAssetsLibrary(AssetsLibraryModel* assetsLibrary_)
{
    if (assetsLibrary!=assetsLibrary_)
    {
        assetsLibrary = assetsLibrary_;
        emit assetsLibraryChanged();

        if (assetsLibrary->size())
        {
            setAssetsPack(assetsLibrary->element(0));
        }
    }
}

AssetsPackModel* AssetsContext::getAssetsPack()
{
    return assetsPack;
}

void AssetsContext::setAssetsPack(AssetsPackModel* assetsPack_)
{
    if (assetsLibrary && assetsPack != assetsPack_)
    {
        assetsPack = assetsPack_;
        emit assetsPackChanged();
    }
}

Asset* AssetsContext::getAsset()
{
    return asset;
}

void AssetsContext::setAsset(Asset* asset_)
{
    if (asset != asset_)
    {
        asset = asset_;
        emit assetChanged();
    }
}

