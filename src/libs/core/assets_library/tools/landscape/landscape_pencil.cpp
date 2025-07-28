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

                StaggeredTiledLandscape::ModeNeighbours neighbours = StaggeredTiledLandscape::getNeighboursNodeForCell(gameObject.getPosition());
                for (size_t i = 0; i < mask.size(); ++i)
                {
                    math::ivec2 nodePos = neighbours[i];
                    landMask[nodePos] = mask[i];
                }
            }
        });

        return landMask;
    }

    TileSet::TileType getTileByMask(const LandNodes& landNodes, const math::ivec2& cellPosition, SliceAsset& sliceAsset)
    {
        TileSet::TileType result;

        TileSet::NeighboursNodeMask nodesMaskForCell;
        StaggeredTiledLandscape::ModeNeighbours cellNeighboursNodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPosition);

        for (size_t i = 0; i < nodesMaskForCell.size(); ++i)
        {
            math::ivec2 neighbourNodePos = cellNeighboursNodes[i];
            bool nodeState = landNodes[cellNeighboursNodes[i]];

            nodesMaskForCell[i] = nodeState;
        }

        static TileSet tileSet;
        result = tileSet.tileset[nodesMaskForCell];

        //spdlog::info("neighbours nodes: {}", nodesMaskForCell);

        return result;
    }
}

LandscapePencil::LandscapePencil(QObject* parent):
    Tool("LandscapePencil", "land_pencil", parent)
{

}

void LandscapePencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, StaggeredIsometryView* iso)
{
    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset && layerModel)
    {
        LandNodes landNodes = getLandNodes(*layerModel, *sliceAsset);

        //set node UP upder cursor 
        const math::ivec2 nodePos = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));
        landNodes[nodePos] = 1;



        StaggeredIsometry::Neighbours neighbours = StaggeredIsometry::nodeNeighboursCell(nodePos);
        for (const math::ivec2& curCellPosition : neighbours)
        {
            //spdlog::info("neighbour: {}",  curCellPosition);

            TileSet::TileType tileType = getTileByMask(landNodes, curCellPosition, *sliceAsset);
            //spdlog::info("neighbour: tile_type{}",  magic_enum::enum_name(tileType));

            if (tileType != TileSet::Unknown)
            {
                std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
                landObject->setName(QString("Landscape"));
                landObject->setPosition(curCellPosition);
                landObject->setAssetUiid(currentAsset->uuid());
                landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
                layerModel->addGameObject(std::move(landObject));
            }
            else
            {
                //canvas.getMap()->removeObjects(curCellPosition, Tiles);
            }
        }
    }
}

