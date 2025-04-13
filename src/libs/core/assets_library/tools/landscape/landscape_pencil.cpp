#include "landscape_pencil.h"
#include "game_objects/landscape.h"
#include "math/lib.h"
#include "topology/staggered_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"





namespace
{
    //LandNodes
    LandNodes getLandNodes(LayerModel& map, SliceAsset& sliceAsset)
    {
        LandNodes landMask;
        landMask.init(200, 200);

        TileSet tileSet;
        map.iterate([&landMask, &tileSet, &sliceAsset](const GameObject& gameObject)
        {
            const Landscape* landObject = dynamic_cast<const Landscape*>(&gameObject);        
            if (landObject && landObject->getPosition().x < 200 && landObject->getPosition().y < 200)
            {
                size_t tileIndex = landObject->getTileIndex();
                TileSet::TileType tileName = sliceAsset.subTileTypeByIndex(tileIndex);

                TileSet::NeighboursNodeMask mask = tileSet.tilename2mask[tileName];

                StaggeredTiledLandscape::ModeNeighbours neighbours = StaggeredTiledLandscape::getNodeNeighbours(gameObject.getPosition());
                for (size_t i = 0; i < mask.size(); ++i)
                {
                    math::ivec2 nodePos = neighbours[i];
                    landMask[nodePos] = mask[i];
                }
            }
        });

        return landMask;
    }

    TileSet::TileType getTileByMask(LayerModel& map, const math::ivec2& nodePos, const math::ivec2& cellPosition, SliceAsset& sliceAsset)
    {
        TileSet::TileType result;
        LandNodes landNodes = getLandNodes(map, sliceAsset);
        landNodes[nodePos] = 1;

        TileSet::NeighboursNodeMask nodesMaskForCell;
        StaggeredTiledLandscape::ModeNeighbours cellNodeNeighbours = StaggeredTiledLandscape::getNodeNeighbours(cellPosition);

        for (size_t i = 0; i < nodesMaskForCell.size(); ++i)
        {
            math::ivec2 neighbourNodePos = cellNodeNeighbours[i];
            bool nodeState = landNodes[cellNodeNeighbours[i]];

            nodesMaskForCell[i] = nodeState;
        }

        static TileSet tileSet;
        result = tileSet.tileset[nodesMaskForCell];

        return result;
    }

    /*
    std::string getTileByMask(LandNodes landNodes, const math::ivec2& cellPosition)
    {
        std::string result;

        TileSet::NeighboursNodeMask nodesMaskForCell;
        StaggeredTiledLandscape::ModeNeighbours cellNodeNeighbours = StaggeredTiledLandscape::getNodeNeighbours(cellPosition);

        for (size_t i = 0; i < nodesMaskForCell.size(); ++i)
        {
            math::ivec2 neighbourNodePos = cellNodeNeighbours[i];
            bool nodeState = landNodes[cellNodeNeighbours[i]];

            nodesMaskForCell[i] = nodeState;
        }

        static TileSet tileSet;
        result = tileSet.tileset[nodesMaskForCell];

        return result;
    }
    */

}




LandscapePencil::LandscapePencil(QObject* parent):
    Tool("LandscapePencil", "land_pencil", parent)
{

}

void LandscapePencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso)
{
    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset)
    {
        const math::ivec2 nodeMapPosition = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));

        StaggeredIsometry::Neighbours neighbours = StaggeredIsometry::nodeNeighboursCell(nodeMapPosition);
        for (const math::ivec2& curCellPosition : neighbours)
        {
            TileSet::TileType tileType = getTileByMask(*mapModel, nodeMapPosition, curCellPosition, *sliceAsset);

            if (tileType > 0)
            {
                std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>();
                landObject->setName(QString("Landscape"));
                landObject->setPosition(curCellPosition);
                landObject->setAssetUiid(currentAsset->uuid());
                landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
                mapModel->addGameObject(std::move(landObject));
            }
            else
            {
                //canvas.getMap()->removeObjects(curCellPosition, Tiles);
            }
        }
    }

    //math::ivec2 position = iso->screenToMap(math::vec2(screenPos.x(), screenPos.y()));
    //std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>();

    //landObject->setName(QString("Landscape"));
    //landObject->setPosition(position);
    //landObject->setAssetUiid(currentAsset->uuid());
    //landObject->setTileIndex(0);

    //mapModel->addGameObject(std::move(landObject));

}

