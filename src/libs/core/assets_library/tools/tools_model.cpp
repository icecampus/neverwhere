#include "tools_model.h"
#include "math/lib.h"

#include "tile_2d/cursor.h"
#include "tile_2d/pencil.h"
#include "tile_2d/eraser.h"
#include "landscape/landscape_pencil.h"
#include "landscape/shape3d_pencil.h"
#include "landscape/cliff_pencil.h"


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

void AssetToolsModel::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    if (currentTool >= 0 && currentAsset && layerModel)
    {
        element(currentTool)->click(screenPos, currentAsset, layerModel, iso, ctrlModifier, shiftModifier, altModifier);
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
    assetType2ToolsModel[AssetTypes::slice]->addElement<LandscapePencil>(this);

    assetType2ToolsModel[AssetTypes::shape3d].reset(new AssetToolsModel(this));
    assetType2ToolsModel[AssetTypes::shape3d]->addElement<Shape3dPencil>(this);

    assetType2ToolsModel[AssetTypes::cliff3d].reset(new AssetToolsModel(this));
    assetType2ToolsModel[AssetTypes::cliff3d]->addElement<CliffPencil>(this);

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

void AssetToolsSelector::click(QPoint screenPos, MapModel* mapModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    AssetToolsModel* currentToolsModel = getToolsModel();
    if (currentToolsModel && currentAsset)
    {
        LayerModel* layerModel = mapModel->layer(currentAsset->getLayerType());

        if (layerModel)
        {
            currentToolsModel->click(screenPos, currentAsset, layerModel, iso, ctrlModifier, shiftModifier, altModifier);
        }
    }
}
