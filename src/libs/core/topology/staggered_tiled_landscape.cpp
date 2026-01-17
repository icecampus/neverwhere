#include "staggered_tiled_landscape.h"
#include "assets_library/assets/slice_asset.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"


StaggeredTiledLandscape::NeighboursWithDiagonal StaggeredTiledLandscape::cellNeighbours(const math::ivec2& position)
{
    static const NeighboursWithDiagonal evenNeighbours = { {    math::ivec2(0, -1),  math::ivec2(1, -1), math::ivec2(1, 1), math::ivec2(0, 1),
                                                                math::ivec2(-1, 0),  math::ivec2(0, -2), math::ivec2(1, 0), math::ivec2(0, 2)   } };

    static const NeighboursWithDiagonal oddNeighbours = { { math::ivec2(-1, -1), math::ivec2(0, -1), math::ivec2(0, 1), math::ivec2(-1, 1),
                                                            math::ivec2(-1, 0),  math::ivec2(0, -2),  math::ivec2(1, 0), math::ivec2(0, 2)       } };

    const NeighboursWithDiagonal& cellNeighbours = (position.y % 2) ? evenNeighbours : oddNeighbours;

    NeighboursWithDiagonal result;
    for (int i = 0; i < cellNeighbours.size(); ++i)
    {
        result[i] = position + cellNeighbours[i];
    }

    return result;
}

StaggeredTiledLandscape::ModeNeighbours StaggeredTiledLandscape::getNeighboursNodeForCell(const math::ivec2& cellPosition)
{
    static const ModeNeighbours evenNeighbours = { { math::ivec2(-1, 1), math::ivec2(0, 0),  math::ivec2(0, 1), math::ivec2(0, 2) } };

    static const ModeNeighbours oddNeighbours = { { math::ivec2(0, 1), math::ivec2(0, 0),  math::ivec2(1, 1), math::ivec2(0, 2)  } };

    const ModeNeighbours& cellNeighbours = (cellPosition.y % 2) ? oddNeighbours : evenNeighbours;

    ModeNeighbours result;
    for (int i = 0; i < cellNeighbours.size(); ++i)
    {
        result[i] = cellPosition + cellNeighbours[i];
    }

    return result;
}


std::optional<size_t> StaggeredTiledLandscape::neighboursIndex(const math::ivec2& position, const math::ivec2& neighbour)
{
    using Delata2NeighboursIndex = std::unordered_map<math::ivec2, size_t>;

    const math::ivec2 delta = neighbour - position;

    static const Delata2NeighboursIndex evenDelta2index = { { math::ivec2(0, -1),  0 },
                                                          { math::ivec2(1, -1),  1 },
                                                          { math::ivec2(1, 1),   2 },
                                                          { math::ivec2(0, 1),   3 },

                                                          { math::ivec2(-1, 0),  4 },
                                                          { math::ivec2(0, -2),  5 },
                                                          { math::ivec2(1, 0),   6 },
                                                          { math::ivec2(0, 2),   7 }
    };

    static const Delata2NeighboursIndex oddDelta2index = { { math::ivec2(-1, -1),   0 },
                                                        { math::ivec2(0, -1),    1 },
                                                        { math::ivec2(0, 1),     2 },
                                                        { math::ivec2(-1, 1),    3 },

                                                        { math::ivec2(-1, 0),    4 },
                                                        { math::ivec2(0, -2),    5 },
                                                        { math::ivec2(1, 0),     6 },
                                                        { math::ivec2(0, 2),     7 }
    };

    const Delata2NeighboursIndex& delta2index = (position.y % 2) ? evenDelta2index : oddDelta2index;

    if (delta2index.contains(delta))
    {
        return std::make_optional(delta2index.at(delta));
    }

    return std::nullopt;
}

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

LandNodes LandNodes::createByMap(LayerModel& map)
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

            StaggeredTiledLandscape::ModeNeighbours neighbours = StaggeredTiledLandscape::getNeighboursNodeForCell(gameObject.getPosition());
            for (size_t i = 0; i < mask.size(); ++i)
            {
                math::ivec2 nodePos = neighbours[i];
                landMask[nodePos] = mask[i];
            }
        }
    });

    return landMask;
}

//LandNodes
void LandNodes::init(size_t width_, size_t height_)
{
    _width = width_;
    _height = height_;

    clear();
    resize(_width * _height);
}

uint8_t& LandNodes::operator[](const math::ivec2& position)
{
    uint8_t index = position.y * _width + position.x;
    return std::vector<uint8_t>::operator[](index);
}

uint8_t LandNodes::operator[](const math::ivec2& position) const
{
    return at(position);
}

uint8_t LandNodes::at(const math::ivec2& position) const
{
    uint8_t index = position.y * _width + position.x;
    return std::vector<uint8_t>::operator[](index);
}

TileSet::TileType LandNodes::getTileType(const math::ivec2& cellPosition)
{
    TileSet::TileType result;

    TileSet::NeighboursNodeMask nodesMaskForCell;
    StaggeredTiledLandscape::ModeNeighbours cellNeighboursNodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPosition);

    for (size_t i = 0; i < nodesMaskForCell.size(); ++i)
    {
        math::ivec2 neighbourNodePos = cellNeighboursNodes[i];
        bool nodeState = at(cellNeighboursNodes[i]);

        nodesMaskForCell[i] = nodeState;
    }

    static TileSet tileSet;
    result = tileSet.tileset[nodesMaskForCell];

    return result;
}
