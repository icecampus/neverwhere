#pragma once
#include "math/lib.h"

//StaggeredTiledLandscape
struct StaggeredTiledLandscape
{
    using NeighboursWithDiagonal = std::array<math::ivec2, 8>;
    using ModeNeighbours = std::array<math::ivec2, 4>;
    

    static NeighboursWithDiagonal cellNeighbours(const math::ivec2& position);
    static ModeNeighbours getNodeNeighbours(const math::ivec2& position);
    static std::optional<size_t> neighboursIndex(const math::ivec2& position, const math::ivec2& neighbour);

};

//TileSet
struct TileSet
{
    enum TileType
    {
        Unknown,
        Full,

        RightCorner,
        LeftCorner,
        UpCorner,
        DownCorner,

        DownLack,
        UpLack,
        RightLack,
        LeftLack,

        RightDownLine,
        LeftDownLine,
        RightUpLine,
        LeftUpLine,

        UpAndDownCorners,
        LeftRightCorners,
    };

    using NeighboursNodeMask = std::array<bool, 4>;

    TileSet();

    std::map<NeighboursNodeMask, TileType> tileset;
    std::map<TileType, NeighboursNodeMask> tilename2mask;
};

//LandNodes
struct LandNodes : public std::vector<uint8_t>
{
    void init(size_t width_, size_t height_)
    {
        _width = width_;
        _height = height_;

        clear();
        resize(_width * _height);
    }

    uint8_t& operator[](const math::ivec2& position)
    {
        size_t index = position.y * _width + position.x;
        return std::vector<uint8_t>::operator[](index);
    }

    size_t _width = 0;
    size_t _height = 0;
};
