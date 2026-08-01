#include "FlatAtlasGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <landscape_core/landscape_logic.h>

#include "LandBrush.h"

namespace {

constexpr int kCols = 4;
constexpr int kRows = 6;
constexpr int kTile = 64;
constexpr int kAtlasW = kCols * kTile;
constexpr int kAtlasH = kRows * kTile;

landscape_core::LandscapeTileType typeForAtlasIndex(int index) {
    using T = landscape_core::LandscapeTileType;
    switch (index) {
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

void putPixel(std::vector<std::uint8_t>& rgba, int ax, int ay, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (ax < 0 || ay < 0 || ax >= kAtlasW || ay >= kAtlasH) {
        return;
    }
    const std::size_t i = (static_cast<std::size_t>(ay) * static_cast<std::size_t>(kAtlasW) + static_cast<std::size_t>(ax)) * 4;
    rgba[i + 0] = r;
    rgba[i + 1] = g;
    rgba[i + 2] = b;
    rgba[i + 3] = a;
}

void rasterizeTile(std::vector<std::uint8_t>& rgba, int tileIndex, const std::array<bool, 4>& mask) {
    const int col = tileIndex % kCols;
    const int row = tileIndex / kCols;
    const int ox = col * kTile;
    const int oy = row * kTile;

    // Fill color (sand/ochre) and edge outline.
    constexpr std::uint8_t fillR = 210, fillG = 170, fillB = 90;
    constexpr std::uint8_t edgeR = 120, edgeG = 90, edgeB = 40;

    const float s = static_cast<float>(kTile);
    const float half = s * 0.5f;
    const float halfH = s * 0.25f; // 2:1 diamond vertical half-diagonal

    for (int ly = 0; ly < kTile; ++ly) {
        for (int lx = 0; lx < kTile; ++lx) {
            const float cx = static_cast<float>(lx) + 0.5f;
            const float cy = static_cast<float>(ly) + 0.5f;
            const float hx = (cx - half) / half;
            const float hy = (cy - half) / halfH;
            const float diamond = std::abs(hx) + std::abs(hy);
            if (diamond > 1.0f) {
                continue;
            }

            if (diamondNodeFill(mask, {hx, hy}) < 0.5f) {
                continue;
            }

            const bool edge = diamond > 0.92f;
            if (edge) {
                putPixel(rgba, ox + lx, oy + ly, edgeR, edgeG, edgeB, 255);
            } else {
                putPixel(rgba, ox + lx, oy + ly, fillR, fillG, fillB, 255);
            }
        }
    }
}

} // namespace

FlatAtlasImage generateFlatAtlas() {
    FlatAtlasImage out;
    out.width = kAtlasW;
    out.height = kAtlasH;
    out.cols = kCols;
    out.rows = kRows;
    out.tileSize = kTile;
    out.rgba.assign(static_cast<std::size_t>(kAtlasW) * static_cast<std::size_t>(kAtlasH) * 4, 0);

    // Used Grass slots + duplicates that SliceAsset also maps.
    static constexpr int kIndices[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};

    for (int index : kIndices) {
        const auto type = typeForAtlasIndex(index);
        if (type == landscape_core::LandscapeTileType::Unknown) {
            continue;
        }
        rasterizeTile(out.rgba, index, landscape_core::tileTypeToNodeMask(type));
    }

    return out;
}

int flatAtlasOpaquePixelCount(const FlatAtlasImage& atlas, int tileIndex) {
    if (atlas.rgba.empty() || atlas.tileSize <= 0 || atlas.cols <= 0) {
        return 0;
    }
    const int col = tileIndex % atlas.cols;
    const int row = tileIndex / atlas.cols;
    if (row < 0 || row >= atlas.rows || col < 0 || col >= atlas.cols) {
        return 0;
    }

    const int ox = col * atlas.tileSize;
    const int oy = row * atlas.tileSize;
    int count = 0;
    for (int ly = 0; ly < atlas.tileSize; ++ly) {
        for (int lx = 0; lx < atlas.tileSize; ++lx) {
            const int ax = ox + lx;
            const int ay = oy + ly;
            const std::size_t i =
                (static_cast<std::size_t>(ay) * static_cast<std::size_t>(atlas.width) + static_cast<std::size_t>(ax)) * 4;
            if (atlas.rgba[i + 3] > 10) {
                ++count;
            }
        }
    }
    return count;
}
