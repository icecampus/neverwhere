#include "diamond_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"


//DiamondTiledLandscape
// Single neighbour table — no even/odd split. The cell-grid is cartesian:
//   - orthogonal neighbours: (±1, 0), (0, ±1)
//   - diagonal neighbours:   (±1, ±1)
// The 8-entry layout keeps the same slot order as the staggered version so
// callers that read by index (neighboursIndex) stay consistent.
DiamondTiledLandscape::NeighboursWithDiagonal DiamondTiledLandscape::cellNeighbours(const math::ivec2& position)
{
    static const NeighboursWithDiagonal neighbours = { { math::ivec2(0, -1),  math::ivec2(1, 0),
                                                         math::ivec2(0, 1),   math::ivec2(-1, 0),
                                                         math::ivec2(-1, -1), math::ivec2(1, -1),
                                                         math::ivec2(1, 1),   math::ivec2(-1, 1) } };

    NeighboursWithDiagonal result;
    for (int i = 0; i < neighbours.size(); ++i)
    {
        result[i] = position + neighbours[i];
    }

    return result;
}

// Returns the 4 corner-nodes of the cell at `cellPosition`. On a cartesian
// grid a cell (cx, cy) is bounded by nodes at (cx, cy), (cx+1, cy),
// (cx, cy+1), (cx+1, cy+1). The order [Left, Up, Right, Down] matches the
// mask-bit order consumed by TileSet, so existing tile art stays valid.
DiamondTiledLandscape::ModeNeighbours DiamondTiledLandscape::getNeighboursNodeForCell(const math::ivec2& cellPosition)
{
    static const ModeNeighbours nodes = { { math::ivec2(0, 0),  // Left
                                            math::ivec2(0, 1),  // Up
                                            math::ivec2(1, 1),  // Right
                                            math::ivec2(1, 0)   // Down
                                          } };

    ModeNeighbours result;
    for (int i = 0; i < nodes.size(); ++i)
    {
        result[i] = cellPosition + nodes[i];
    }

    return result;
}


std::optional<size_t> DiamondTiledLandscape::neighboursIndex(const math::ivec2& position, const math::ivec2& neighbour)
{
    using Delta2NeighboursIndex = std::unordered_map<math::ivec2, size_t>;

    const math::ivec2 delta = neighbour - position;

    static const Delta2NeighboursIndex delta2index = { { math::ivec2(0, -1),  0 },
                                                       { math::ivec2(1, 0),   1 },
                                                       { math::ivec2(0, 1),   2 },
                                                       { math::ivec2(-1, 0),  3 },

                                                       { math::ivec2(-1, -1), 4 },
                                                       { math::ivec2(1, -1),  5 },
                                                       { math::ivec2(1, 1),   6 },
                                                       { math::ivec2(-1, 1),  7 } };

    if (delta2index.contains(delta))
    {
        return std::make_optional(delta2index.at(delta));
    }

    return std::nullopt;
}

//LandNodes helpers
LandNodes DiamondTiledLandscape::buildLandNodes(LayerModel& map)
{
    LandNodes landMask;
    landMask.init(200, 200);

    TileSet tileSet;
    map.iterate([&landMask, &tileSet](const GameObject& gameObject)
    {
        const Landscape* landObject = dynamic_cast<const Landscape*>(&gameObject);
        if (landObject && landObject->getPosition().x < 200 && landObject->getPosition().y < 200)
        {
            size_t tileIndex = landObject->getTileIndex();
            TileSet::TileType tileName = SliceAsset::subTileTypeByIndex(tileIndex);

            TileSet::NeighboursNodeMask mask = tileSet.tilename2mask[tileName];

            DiamondTiledLandscape::ModeNeighbours neighbours = DiamondTiledLandscape::getNeighboursNodeForCell(gameObject.getPosition());
            for (size_t i = 0; i < mask.size(); ++i)
            {
                math::ivec2 nodePos = neighbours[i];
                if (mask[i])
                {
                    landMask[nodePos] = 1;
                }
            }
        }
    });

    return landMask;
}

TileSet::TileType DiamondTiledLandscape::tileTypeAt(const LandNodes& nodes, const math::ivec2& cellPosition)
{
    TileSet::NeighboursNodeMask nodesMaskForCell;
    DiamondTiledLandscape::ModeNeighbours cellNeighboursNodes = DiamondTiledLandscape::getNeighboursNodeForCell(cellPosition);

    for (size_t i = 0; i < nodesMaskForCell.size(); ++i)
    {
        bool nodeState = nodes.at(cellNeighboursNodes[i]);
        nodesMaskForCell[i] = nodeState;
    }

    static TileSet tileSet;
    return tileSet.tileset[nodesMaskForCell];
}


