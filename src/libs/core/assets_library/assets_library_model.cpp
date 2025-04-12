#include "assets_library_model.h"


//LayersModel
AssetsLibraryModel::AssetsLibraryModel(QObject* parent):
    SimpleModel<AssetsPackModel>(parent)
{

}

Q_INVOKABLE Asset* AssetsLibraryModel::getAsset(const QUuid& uuid)
{
    return uuid2Asset[uuid];
}

void AssetsLibraryModel::processElement(AssetsPackModel& assetPack)
{
    for (int i=0; i < assetPack.size(); ++i)
    {
        Asset* asset = assetPack.element(i);

        uuid2Asset[asset->uuid()] = asset;
    }
}
