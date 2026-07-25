#include "topology_common.h"

//TileSet
// A 4-bit corner mask describes which of a diamond's 4 corners are "land".
// The table maps each of the 16 masks to a TileType. This is purely a data
// description — it does not know how the 4 corners are sampled, that is the
// topology's job (see getNeighboursNodeForCell).
//
// Mask bit order is the SLOT order produced by getNeighboursNodeForCell.
// For the diamond topology that order is [Left, Up, Right, Down]
// (counter-clockwise starting from the left vertex of the diamond). Tile
// names like "UpCorner" describe which geometric corner is the land bit,
// so the UpCorner mask sets slot[1] (the Up slot) to 1.
//
// This mirrors landscape_core::nodeMaskToTileType so the editor and the
// runtime/3D pipeline agree on tile semantics — see
// SharedTileResolverMatchesTileSetMasks in src/tests/landscape.
TileSet::TileSet()
{
    //corners
    {
        TileType tilename = RightCorner;
        NeighboursNodeMask mask = { 0, 0, 1, 0 };    // [Left, Up, Right, Down]

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftCorner;
        NeighboursNodeMask mask = { 1, 0, 0, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpCorner;
        NeighboursNodeMask mask = { 0, 1, 0, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = DownCorner;
        NeighboursNodeMask mask = { 0, 0, 0, 1 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //double corners
    {
        TileType tilename = LeftRightCorners;
        NeighboursNodeMask mask = { 1, 0, 1, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpAndDownCorners;
        NeighboursNodeMask mask = { 0, 1, 0, 1 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }


    //
    {
        TileType tilename = DownLack;
        NeighboursNodeMask mask = { 1, 1, 1, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpLack;
        NeighboursNodeMask mask = { 1, 0, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = RightLack;
        NeighboursNodeMask mask = { 1, 1, 0, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftLack;
        NeighboursNodeMask mask = { 0, 1, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //line
    {
        TileType tilename = RightDownLine;
        NeighboursNodeMask mask = { 0, 0, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftDownLine;
        NeighboursNodeMask mask = { 1, 0, 0, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = RightUpLine;
        NeighboursNodeMask mask = { 0, 1, 1, 0 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftUpLine;
        NeighboursNodeMask mask = { 1, 1, 0, 0 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //full
    {
        TileType tilename = Full;
        NeighboursNodeMask mask = { 1, 1, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }
}

//LandNodes
uint8_t& LandNodes::operator[](const math::ivec2& position)
{
    return nodes[position];
}

uint8_t LandNodes::operator[](const math::ivec2& position) const
{
    return at(position);
}

uint8_t LandNodes::at(const math::ivec2& position) const
{
    const auto it = nodes.find(position);
    return it != nodes.end() ? it->second : 0;
}

