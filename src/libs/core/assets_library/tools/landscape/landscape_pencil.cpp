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

    // Fast strokes outpace the mouse-event rate: one node per event leaves
    // holes in the node field, and every hole turns the cells touching it
    // into partial tiles (the grass "tearing into stripes"). Walk the line
    // from the previous event's node to this one so the trail is continuous.
    std::vector<std::pair<math::ivec2, uint8_t>> updates;
    if (m_hasLast && erase == m_lastErase)
    {
        // Bresenham over the node grid; the first point is already painted.
        int x0 = m_lastNode.x, y0 = m_lastNode.y;
        const int x1 = nodePos.x, y1 = nodePos.y;
        const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (x0 != x1 || y0 != y1)
        {
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
            updates.emplace_back(math::ivec2(x0, y0), erase ? 0 : 1);
        }
    }
    else
    {
        updates.emplace_back(nodePos, erase ? 0 : 1);
    }

    SliceAsset* sliceAsset = dynamic_cast<SliceAsset*>(currentAsset);
    if (sliceAsset && layerModel && !updates.empty())
    {
        MapAuthoring::applyLandscapeUpdates(*layerModel, sliceAsset, updates);
    }

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
