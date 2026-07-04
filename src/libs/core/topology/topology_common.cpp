#include "topology_common.h"

//TileSet
TileSet::TileSet()
{
    //corners
    {
        TileType tilename = RightCorner; //"testset_1";
        NeighboursNodeMask mask = { 0, 0, 1, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftCorner; //"testset_2";
        NeighboursNodeMask mask = { 1, 0, 0, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpCorner; //"testset_3";
        NeighboursNodeMask mask = { 0, 1, 0, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = DownCorner; //"testset_4";
        NeighboursNodeMask mask = { 0, 0, 0, 1 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //double corners
    {
        TileType tilename = LeftRightCorners; //"testset_1_2";
        NeighboursNodeMask mask = { 1, 0, 1, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpAndDownCorners; //"testset_3_4";
        NeighboursNodeMask mask = { 0, 1, 0, 1 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }


    //
    {
        TileType tilename = DownLack; //"testset_5";
        NeighboursNodeMask mask = { 1, 1, 1, 0 };

        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = UpLack; //"testset_6";
        NeighboursNodeMask mask = { 1, 0, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = RightLack; //"testset_7";
        NeighboursNodeMask mask = { 1, 1, 0, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftLack; //"testset_8";
        NeighboursNodeMask mask = { 0, 1, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //line
    {
        TileType tilename = RightDownLine; //"testset_9";
        NeighboursNodeMask mask = { 0, 0, 1, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftDownLine; //"testset_10";
        NeighboursNodeMask mask = { 1, 0, 0, 1 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = RightUpLine; //"testset_11";
        NeighboursNodeMask mask = { 0, 1, 1, 0 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    {
        TileType tilename = LeftUpLine; //"testset_12";
        NeighboursNodeMask mask = { 1, 1, 0, 0 };
        tileset[mask] = tilename;
        tilename2mask[tilename] = mask;
    }

    //full
    {
        TileType tilename = Full; //"testset_1_land";
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

