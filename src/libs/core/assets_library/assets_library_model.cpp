#include "assets_library_model.h"


//LayersModel
AssetsLibraryModel::AssetsLibraryModel(QObject* parent):
    SimpleModel<AssetsPackModel>(parent)
{

}

Asset* AssetsLibraryModel::getAsset(const QUuid& uuid)
{
    auto found = uuid2Asset.find(uuid);
    if (found != uuid2Asset.end())
    {
        return found->second;
    }
    
    qWarning() << "Can't find asset: " << uuid;

    return nullptr;
}

void AssetsLibraryModel::save(Asset* asset)
{
    auto indexPath = asset->getData().indexPath;
    BaseData::AssetData::save(asset->getData(), asset->getData().indexPath);
}

void AssetsLibraryModel::processElement(AssetsPackModel& assetPack)
{
    QObject::connect(&assetPack, &AssetsPackModel::rowsInserted,
        [assetPackPtr = &assetPack, this](const QModelIndex& parent, int first, int last) 
        {
            for (int i=first; i<=last; i++)
            {
                processAsset(assetPackPtr->element(i));
            }
            
        });


    for (int i=0; i < assetPack.size(); ++i)
    {
        Asset* asset = assetPack.element(i);
        processAsset(asset);
    }
}

void AssetsLibraryModel::processAsset(Asset* asset)
{
    uuid2Asset[asset->uuid()] = asset;

}
