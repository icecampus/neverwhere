#include "landscape_pencil.h"
#include "game_objects/landscape.h"
#include "math/lib.h"
#include "topology/staggered_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"



LandscapePencil::LandscapePencil(QObject* parent):
    Tool("LandscapePencil", "land_pencil", parent)
{

}

void LandscapePencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, StaggeredIsometryView* iso)
{
    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset && layerModel)
    {
        LandNodes landNodes = LandNodes::createByMap(*layerModel, *sliceAsset);

        //set node UP upder cursor 
        const math::ivec2 nodePos = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));
        landNodes[nodePos] = 1;

        //update cells around node
        StaggeredIsometry::Neighbours neighbours = StaggeredIsometry::nodeNeighboursCell(nodePos);
        for (const math::ivec2& curCellPosition : neighbours)
        {
            TileSet::TileType tileType = landNodes.getTileType(curCellPosition, *sliceAsset);
            updateLandscapeCell(layerModel, sliceAsset, curCellPosition, tileType);
        }
    }
}

void LandscapePencil::updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset, const math::ivec2& cellPosition, TileSet::TileType tileType)
{
    std::vector<GameObject*> objects = layerModel->getObjectsAt(cellPosition);

    if (tileType != TileSet::Unknown)
    {
        if (objects.size())
        {
            GameObject* gameObject = objects.back();
            if (gameObject->getType() == GameObjectTypes::Landscape)
            {
                Landscape* landObject = dynamic_cast<Landscape*>(gameObject);
                if (landObject)
                {
                    landObject->setName(QString("Landscape"));
                    landObject->setPosition(cellPosition);
                    landObject->setAssetUiid(sliceAsset->uuid());
                    landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
                }
            }
        }
        else
        {
            std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
            landObject->setName(QString("Landscape"));
            landObject->setPosition(cellPosition);
            landObject->setAssetUiid(sliceAsset->uuid());
            landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
            layerModel->addGameObject(std::move(landObject));
        }
    }
    else
    {
        //canvas.getMap()->removeObjects(curCellPosition, Tiles);
    }
}
