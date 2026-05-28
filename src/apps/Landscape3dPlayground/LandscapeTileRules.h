#pragma once

#include <array>
#include <cstdint>

#include <landscape_core/landscape_logic.h>

using LandscapeTileType = landscape_core::LandscapeTileType;

struct LandscapeValleyGeometry {
    // Point order: Left, Up, Right, Down, Center.
    std::array<float, 5> heights{};
    std::array<std::array<std::uint8_t, 3>, 4> triangles{};
    int triangleCount = 0;
};

inline LandscapeTileType nodeMaskToTileType(const std::array<bool, 4>& mask) {
    return landscape_core::nodeMaskToTileType(mask);
}

inline int tileTypeToAtlasIndex(LandscapeTileType type) {
    switch (type) {
    case LandscapeTileType::Full:
        return 0;
    case LandscapeTileType::DownLack:
        return 4;
    case LandscapeTileType::LeftLack:
        return 5;
    case LandscapeTileType::UpLack:
        return 6;
    case LandscapeTileType::RightLack:
        return 7;
    case LandscapeTileType::UpCorner:
        return 8;
    case LandscapeTileType::RightCorner:
        return 9;
    case LandscapeTileType::DownCorner:
        return 10;
    case LandscapeTileType::LeftCorner:
        return 11;
    case LandscapeTileType::RightUpLine:
        return 12;
    case LandscapeTileType::RightDownLine:
        return 13;
    case LandscapeTileType::LeftDownLine:
        return 14;
    case LandscapeTileType::LeftUpLine:
        return 15;
    case LandscapeTileType::UpAndDownCorners:
        return 20;
    case LandscapeTileType::LeftRightCorners:
        return 21;
    case LandscapeTileType::Unknown:
    default:
        return 22;
    }
}

inline LandscapeTileType tileTypeFromAtlasIndex(int index) {
    switch (index) {
    case 0:
    case 1:
    case 2:
    case 3:
        return LandscapeTileType::Full;
    case 4:
        return LandscapeTileType::DownLack;
    case 5:
        return LandscapeTileType::LeftLack;
    case 6:
        return LandscapeTileType::UpLack;
    case 7:
        return LandscapeTileType::RightLack;
    case 8:
        return LandscapeTileType::UpCorner;
    case 9:
        return LandscapeTileType::RightCorner;
    case 10:
        return LandscapeTileType::DownCorner;
    case 11:
        return LandscapeTileType::LeftCorner;
    case 12:
    case 16:
        return LandscapeTileType::RightUpLine;
    case 13:
    case 17:
        return LandscapeTileType::RightDownLine;
    case 14:
    case 18:
        return LandscapeTileType::LeftDownLine;
    case 15:
    case 19:
        return LandscapeTileType::LeftUpLine;
    case 20:
        return LandscapeTileType::UpAndDownCorners;
    case 21:
        return LandscapeTileType::LeftRightCorners;
    default:
        return LandscapeTileType::Unknown;
    }
}

inline LandscapeValleyGeometry valleyGeometryForTile(LandscapeTileType type) {
    constexpr std::array<std::array<std::uint8_t, 3>, 4> fan{{
        {{4, 0, 1}},
        {{4, 1, 2}},
        {{4, 2, 3}},
        {{4, 3, 0}},
    }};
    constexpr std::array<std::array<std::uint8_t, 3>, 4> splitLR{{
        {{0, 1, 2}},
        {{0, 2, 3}},
        {{0, 0, 0}},
        {{0, 0, 0}},
    }};
    constexpr std::array<std::array<std::uint8_t, 3>, 4> splitUD{{
        {{0, 1, 3}},
        {{1, 2, 3}},
        {{0, 0, 0}},
        {{0, 0, 0}},
    }};

    switch (type) {
    case LandscapeTileType::Full:
        return {{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}}, splitLR, 2};
    case LandscapeTileType::DownLack:
        return {{{1.0f, 1.0f, 1.0f, 0.0f, 1.0f}}, splitLR, 2};
    case LandscapeTileType::UpLack:
        return {{{1.0f, 0.0f, 1.0f, 1.0f, 1.0f}}, splitLR, 2};
    case LandscapeTileType::LeftLack:
        return {{{0.0f, 1.0f, 1.0f, 1.0f, 1.0f}}, splitUD, 2};
    case LandscapeTileType::RightLack:
        return {{{1.0f, 1.0f, 0.0f, 1.0f, 1.0f}}, splitUD, 2};

    case LandscapeTileType::UpCorner:
        return {{{0.0f, 1.0f, 0.0f, 0.0f, 0.0f}}, fan, 4};
    case LandscapeTileType::RightCorner:
        return {{{0.0f, 0.0f, 1.0f, 0.0f, 0.0f}}, fan, 4};
    case LandscapeTileType::DownCorner:
        return {{{0.0f, 0.0f, 0.0f, 1.0f, 0.0f}}, fan, 4};
    case LandscapeTileType::LeftCorner:
        return {{{1.0f, 0.0f, 0.0f, 0.0f, 0.0f}}, fan, 4};

    case LandscapeTileType::RightUpLine:
        return {{{0.0f, 1.0f, 1.0f, 0.0f, 0.5f}}, splitUD, 2};
    case LandscapeTileType::RightDownLine:
        return {{{0.0f, 0.0f, 1.0f, 1.0f, 0.5f}}, splitUD, 2};
    case LandscapeTileType::LeftDownLine:
        return {{{1.0f, 0.0f, 0.0f, 1.0f, 0.5f}}, splitUD, 2};
    case LandscapeTileType::LeftUpLine:
        return {{{1.0f, 1.0f, 0.0f, 0.0f, 0.5f}}, splitUD, 2};

    case LandscapeTileType::UpAndDownCorners:
        return {{{0.0f, 1.0f, 0.0f, 1.0f, 1.0f}}, fan, 4};
    case LandscapeTileType::LeftRightCorners:
        return {{{1.0f, 0.0f, 1.0f, 0.0f, 1.0f}}, fan, 4};
    case LandscapeTileType::Unknown:
    default:
        return {{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}, splitUD, 0};
    }
}

