#include "tools_model.h"
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
    tmpToolsMode(this)
{
    tmpToolsMode.addElement<Pencil>(this);
    tmpToolsMode.addElement<Eraser>(this);

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
    return &tmpToolsMode;
}
