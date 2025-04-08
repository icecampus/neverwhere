#include "tools_model.h"
#include "pencil.h"
#include "eraser.h"

//AssetToolsModel
AssetToolsModel::AssetToolsModel(QObject* parent):
    SimpleModel<Tool>(parent)
{

}

//ToolsModel
ToolsModel::ToolsModel(QObject* parent):
    tmpToolsMode(this)
{
    tmpToolsMode.addElement<Pencil>(this);
    tmpToolsMode.addElement<Eraser>(this);

}

Asset* ToolsModel::getCurrentAsset()
{
    return currentAsset;
}

void ToolsModel::setCurrentAsset(Asset* asset)
{
    if (asset != currentAsset)
    {
        currentAsset = asset;
        emit currentAssetChanged();
        emit toolsModelChanged();
        
    }
}

AssetToolsModel* ToolsModel::getToolsModel()
{
    return &tmpToolsMode;
}
