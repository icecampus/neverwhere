#pragma once
#include "math/lib.h"

class SliceAsset;
class LayerModel;

//StaggeredTiledLandscape
struct StaggeredTiledLandscape
{
    using NeighboursWithDiagonal = std::array<math::ivec2, 8>;
    using ModeNeighbours = std::array<math::ivec2, 4>;
    

    static NeighboursWithDiagonal cellNeighbours(const math::ivec2& position);
    static ModeNeighbours getNeighboursNodeForCell(const math::ivec2& position);
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


// template<>
// struct fmt::formatter<TileSet::NeighboursNodeMask> : fmt::formatter<std::string>
// {
//     auto format(TileSet::NeighboursNodeMask nodesMask, format_context& ctx) const -> decltype(ctx.out())
//     {
//         return fmt::format_to(ctx.out(), "[{}, {}, {}, {}]", nodesMask[0], nodesMask[1], nodesMask[2], nodesMask[3] );
//     }
// };

//LandNodes
struct LandNodes : public std::vector<uint8_t>
{
    static LandNodes createByMap(LayerModel& map, SliceAsset& sliceAsset);

    void init(size_t width_, size_t height_);

    uint8_t& operator[](const math::ivec2& position);
    uint8_t operator[](const math::ivec2& position) const;
    uint8_t at(const math::ivec2& position) const;

    TileSet::TileType getTileType(const math::ivec2& cellPosition, SliceAsset& sliceAsset);

    size_t _width = 0;
    size_t _height = 0;
};
