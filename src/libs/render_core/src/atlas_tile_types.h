// Shared atlas-convention helpers: the stored Landscape tileIndex encodes the
// cell's LandscapeTileType (Grass 4x6 slice atlas layout), which encodes the
// 4 corner vertex nodes. Used by LandscapeRenderer (raised pass) and
// CliffRenderer (cliff-field pass) to reconstruct the node grid from tiles.
#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

#include <landscape_core/landscape_logic.h>

namespace render_core {

// Same packing as DiamondIsometry::zOffset.
inline std::uint64_t nodeKey(const glm::ivec2& node) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(node.y)) << 32)
        | static_cast<std::uint32_t>(node.x);
}

// Recover the LandscapeTileType from the stored atlas tileIndex. Mirrors
// SliceAsset::subTileTypeByIndex — the same table the vertex-centric pencil
// wrote the index with.
inline landscape_core::LandscapeTileType tileTypeFromAtlasIndex(std::size_t tileIndex) {
    using T = landscape_core::LandscapeTileType;
    switch (tileIndex) {
    case 0:
    case 1:
    case 2:
    case 3:
        return T::Full;
    case 4:
        return T::DownLack;
    case 5:
        return T::LeftLack;
    case 6:
        return T::UpLack;
    case 7:
        return T::RightLack;
    case 8:
        return T::UpCorner;
    case 9:
        return T::RightCorner;
    case 10:
        return T::DownCorner;
    case 11:
        return T::LeftCorner;
    case 12:
    case 16:
        return T::RightUpLine;
    case 13:
    case 17:
        return T::RightDownLine;
    case 14:
    case 18:
        return T::LeftDownLine;
    case 15:
    case 19:
        return T::LeftUpLine;
    case 20:
        return T::UpAndDownCorners;
    case 21:
        return T::LeftRightCorners;
    default:
        return T::Unknown;
    }
}

} // namespace render_core
