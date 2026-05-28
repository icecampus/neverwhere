#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace landscape_core {

enum class LandscapeTileType : std::uint8_t {
    Unknown = 0,
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

enum class LandscapeZone : std::uint8_t {
    Lowland,
    Clearing,
    Slope,
    HighGround,
    Hill,
};

enum class TileBuildKind : std::uint8_t {
    Surface,
    Wall,
    CornerCap,
};

enum class EdgeSide : std::uint8_t {
    Top,
    Right,
    Bottom,
    Left,
};

struct LandscapeBowlSettings {
    int gridWidth = 32;
    int gridHeight = 24;
    int seed = 2027;
    float clearingRadius = 5.5f;
    float clearingSoftness = 2.2f;
    float highGroundRadius = 9.5f;
    float highGroundWidth = 3.5f;
    float highGroundHeight = 3.2f;
    int heightLevels = 4;
    float arcNoiseScale = 4.0f;
    float arcNoiseAmplitude = 1.6f;
    int hillCount = 5;
    float hillHeight = 1.2f;
    float hillRadius = 2.6f;
};

struct BowlGenerationStats {
    std::vector<int> levelCellCounts;
    int clearingCellCount = 0;
    int highGroundCellCount = 0;
    int hillCellCount = 0;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
};

struct LandscapeLevelGrid {
    int width = 0;
    int height = 0;
    int levelCount = 1;
    float levelHeight = 1.0f;
    std::vector<std::uint8_t> cellLevels;
    std::vector<std::uint8_t> nodeLevels;
    std::vector<LandscapeZone> zones;

    bool empty() const;
    int cellIndex(int x, int y) const;
    int nodeIndex(int x, int y) const;
    std::uint8_t cellLevelAt(int x, int y) const;
    std::uint8_t nodeLevelAt(int x, int y) const;
    LandscapeZone zoneAt(int x, int y) const;
};

struct LandscapeTileKey {
    TileBuildKind kind = TileBuildKind::Surface;
    LandscapeTileType tileType = LandscapeTileType::Unknown;
    LandscapeZone zone = LandscapeZone::Lowland;
    EdgeSide side = EdgeSide::Top;
    std::uint8_t level = 0;
    std::uint8_t lowerLevel = 0;
    std::uint8_t upperLevel = 0;

    bool operator<(const LandscapeTileKey& other) const;
};

LandscapeBowlSettings sanitize(LandscapeBowlSettings settings);

LandscapeTileType nodeMaskToTileType(const std::array<bool, 4>& mask);
std::array<bool, 4> tileTypeToNodeMask(LandscapeTileType type);
bool tileTypeHasSurface(LandscapeTileType type);
std::string_view tileTypeName(LandscapeTileType type);

std::array<bool, 4> cellNodeMaskAtLevel(const LandscapeLevelGrid& grid, int x, int y, std::uint8_t minLevel);
LandscapeTileType surfaceTileTypeAtLevel(const LandscapeLevelGrid& grid, int x, int y, std::uint8_t minLevel);

LandscapeLevelGrid generateLandscapeBowl(const LandscapeBowlSettings& settings, BowlGenerationStats* stats = nullptr);

} // namespace landscape_core
