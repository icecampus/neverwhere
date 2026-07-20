#include "landscape_pencil.h"
#include "game_objects/landscape.h"
#include "map/map_authoring.h"
#include "math/lib.h"
#include "topology/diamond_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"


LandscapePencil::LandscapePencil(QObject* parent):
    LandscapePencil("LandscapePencil", "land_pencil", parent)
{

}

LandscapePencil::LandscapePencil(const QString& name, const QString& icon, QObject* parent):
    Tool(name, icon, parent)
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
            MapAuthoring::updateLandscapeCell(layerModel, sliceAsset, curCellPosition, tileType);
        }
    }
}
