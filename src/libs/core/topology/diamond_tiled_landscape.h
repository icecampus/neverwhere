#pragma once
#include "math/lib.h"
#include "topology_common.h"

class SliceAsset;
class LayerModel;

//DiamondTiledLandscape
// Pure cartesian grid topology. Neighbour offsets do NOT depend on row
// parity: there is a single table for every cell.
struct DiamondTiledLandscape
{
    using NeighboursWithDiagonal = std::array<math::ivec2, 8>;
    using ModeNeighbours = std::array<math::ivec2, 4>;


    static NeighboursWithDiagonal cellNeighbours(const math::ivec2& position);
    static ModeNeighbours getNeighboursNodeForCell(const math::ivec2& position);
    static std::optional<size_t> neighboursIndex(const math::ivec2& position, const math::ivec2& neighbour);

    // Topology-specific LandNodes operations. Storage lives in topology_common.h.
    static LandNodes buildLandNodes(LayerModel& map);
    static TileSet::TileType tileTypeAt(const LandNodes& nodes, const math::ivec2& cellPosition);
};


