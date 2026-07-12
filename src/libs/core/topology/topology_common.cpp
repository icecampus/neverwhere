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
void LandNodes::init(size_t width_, size_t height_)
{
    _width = width_;
    _height = height_;

    clear();
    resize(_width * _height);
}

bool LandNodes::inBounds(const math::ivec2& position) const
{
    return position.x >= 0 && position.y >= 0
        && static_cast<size_t>(position.x) < _width
        && static_cast<size_t>(position.y) < _height;
}

uint8_t& LandNodes::operator[](const math::ivec2& position)
{
    static uint8_t dummy = 0;
    if (!inBounds(position))
    {
        // Out-of-range writes go to a throwaway cell. Diamond fieldToMap can
        // return negative coordinates (the grid extends in all directions
        // from the origin); silently dropping those is safer than UB.
        dummy = 0;
        return dummy;
    }
    size_t index = static_cast<size_t>(position.y) * _width + position.x;
    return std::vector<uint8_t>::operator[](index);
}

uint8_t LandNodes::operator[](const math::ivec2& position) const
{
    return at(position);
}

uint8_t LandNodes::at(const math::ivec2& position) const
{
    if (!inBounds(position))
    {
        return 0;
    }
    size_t index = static_cast<size_t>(position.y) * _width + position.x;
    return std::vector<uint8_t>::operator[](index);
}

