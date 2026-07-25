#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <map>
#include <unordered_map>
#include "math/lib.h"

// Topology-agnostic building blocks shared by every grid topology
// (staggered, diamond, future variants). Anything that depends on neighbour
// parity or a specific projection lives in the topology-specific headers.

//VisibleRegion
// AABB over cell coordinates. Independent of how cells are projected.
struct VisibleRegion
{
    Q_GADGET;
    Q_PROPERTY(math::ivec2 min READ getMin CONSTANT);
    Q_PROPERTY(math::ivec2 max READ getMax CONSTANT);
public:
    VisibleRegion() : min(0), max(0) {}
    VisibleRegion(const math::ivec2& minPos, const math::ivec2& maxPos)
        : min(minPos), max(maxPos) {
    }

    math::ivec2 getMin() const { return min; }
    math::ivec2 getMax() const { return max; }
    math::ivec2 min;
    math::ivec2 max;
};

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
// Sparse map of node states (0/1) keyed by node coordinate. Unbounded — any
// (x, y) is valid, including negative (the grid extends in all directions);
// a missing key reads as 0. Building this map from a LayerModel and resolving
// a TileType from it depend on neighbour topology, so those operations live in
// each topology-specific header as free functions (buildLandNodes / tileTypeAt).
struct LandNodes
{
    uint8_t& operator[](const math::ivec2& position);
    uint8_t operator[](const math::ivec2& position) const;
    uint8_t at(const math::ivec2& position) const;

    std::unordered_map<math::ivec2, uint8_t> nodes;
};

