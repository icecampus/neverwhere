#include "tools_model.h"
#include "cursor.h"
#include "pencil.h"
#include "eraser.h"


//AssetToolsModel
AssetToolsModel::AssetToolsModel(QObject* parent):
    SimpleModel<Tool>(parent)
{

}

int AssetToolsModel::getCurrentTool() const
{
    return currentTool;
}

void AssetToolsModel::setCurrentTool(int currentIndex)
{
    if (currentTool != currentIndex)
    {
        currentTool = currentIndex;
        emit currentToolChanged();
    }
}

//ToolsModel
AssetToolsSelector::AssetToolsSelector(QObject* parent):
    QObject(parent)
{

    assetType2ToolsModel[AssetTypes::image].reset(new AssetToolsModel(this));
    assetType2ToolsModel[AssetTypes::image]->addElement<Cursor>(this);
    assetType2ToolsModel[AssetTypes::image]->addElement<Pencil>(this);
    assetType2ToolsModel[AssetTypes::image]->addElement<Eraser>(this);

    assetType2ToolsModel[AssetTypes::slice].reset(new AssetToolsModel(this));
    assetType2ToolsModel[AssetTypes::slice]->addElement<Pencil>(this);

}

Asset* AssetToolsSelector::getCurrentAsset()
{
    return currentAsset;
}

void AssetToolsSelector::setCurrentAsset(Asset* asset)
{
    if (asset != currentAsset)
    {
        currentAsset = asset;
    
        emit currentAssetChanged();
        emit toolsModelChanged();
        
    }
}

AssetToolsModel* AssetToolsSelector::getToolsModel()
{
    if (currentAsset)
    {
        return assetType2ToolsModel[currentAsset->type].get();
    }

    return nullptr;
}
