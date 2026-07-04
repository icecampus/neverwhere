#include "landscape_pencil.h"
#include "game_objects/landscape.h"
#include "math/lib.h"
#include "topology/diamond_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"


LandscapePencil::LandscapePencil(QObject* parent):
    Tool("LandscapePencil", "land_pencil", parent)
{

}

void LandscapePencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    spdlog::info("Screen pos {}", screenPos);

    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset && layerModel)
    {
        LandNodes landNodes = DiamondTiledLandscape::buildLandNodes(*layerModel);

        //set node UP upder cursor
        const math::ivec2 nodePos = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));
        if (!ctrlModifier)
        {
            landNodes[nodePos] = 1;
        }
        else
        {
            landNodes[nodePos] = 0;
        }

        //update cells around node
        DiamondIsometry::Neighbours neighbours = DiamondIsometry::nodeNeighboursCell(nodePos);
        for (const math::ivec2& curCellPosition : neighbours)
        {
            TileSet::TileType tileType = DiamondTiledLandscape::tileTypeAt(landNodes, curCellPosition);
            updateLandscapeCell(layerModel, sliceAsset, curCellPosition, tileType);
        }
    }
}

void LandscapePencil::updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset, const math::ivec2& cellPosition, TileSet::TileType tileType)
{
    // Clean all existing objects in this cell to prevent duplicates and 'ghost' nodes
    layerModel->removeAll(cellPosition);

    if (tileType != TileSet::Unknown)
    {
        std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
        landObject->setName(QString("Landscape"));
        landObject->setPosition(cellPosition);
        landObject->setAssetUiid(sliceAsset->uuid());
        landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
        layerModel->addGameObject(std::move(landObject));
    }
}
