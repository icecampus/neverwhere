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
    paintAt(screenPos, currentAsset, layerModel, iso, ctrlModifier);
}

void LandscapePencil::stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* layerModel,
    DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    if (kind == StrokeKind::End)
    {
        m_hasLast = false;
        return;
    }

    // Per-stroke dedup: repeated Move events over the same node with the same
    // action are no-ops (mouse-move rate would otherwise spam recomputes).
    const math::ivec2 nodePos = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));
    const bool erase = ctrlModifier;
    if (m_hasLast && nodePos == m_lastNode && erase == m_lastErase)
    {
        return;
    }

    paintAt(screenPos, currentAsset, layerModel, iso, erase);
    m_lastNode = nodePos;
    m_lastErase = erase;
    m_hasLast = true;
}

void LandscapePencil::paintAt(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
    bool erase)
{
    spdlog::info("Screen pos {}", screenPos);

    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset && layerModel)
    {
        LandNodes landNodes = DiamondTiledLandscape::buildLandNodes(*layerModel);

        //set node UP upder cursor
        const math::ivec2 nodePos = iso->screendToNode(math::vec2(screenPos.x(), screenPos.y()));
        if (!erase)
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
