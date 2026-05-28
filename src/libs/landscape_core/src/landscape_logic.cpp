#include "landscape_core/landscape_logic.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace landscape_core {
namespace {

float clampFloat(float value, float minValue, float maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    return std::clamp(value, minValue, maxValue);
}

int clampInt(int value, int minValue, int maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    return std::clamp(value, minValue, maxValue);
}

float smoothStep(float edge0, float edge1, float x) {
    const float t = clampFloat((x - edge0) / std::max(0.0001f, edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float deterministicFloat01(int seed, int index) {
    std::uint32_t value = (std::uint32_t)seed;
    value ^= (std::uint32_t)index * 0x9e3779b9u;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (float)(value & 0x00ffffffu) / (float)0x01000000u;
}

float valueNoise(int seed, float x, float y) {
    const float a = std::sin(x * 12.9898f + y * 78.233f + (float)seed * 0.137f) * 43758.5453f;
    return (a - std::floor(a)) * 2.0f - 1.0f;
}

float blendedNoise(int seed, float x, float y, float scale) {
    const float s = std::max(0.001f, scale);
    const float nx = x / s;
    const float ny = y / s;
    return valueNoise(seed, nx, ny) * 0.58f
        + valueNoise(seed + 31, nx * 2.0f + 7.1f, ny * 2.0f - 3.7f) * 0.28f
        + valueNoise(seed + 73, nx * 4.0f - 11.0f, ny * 4.0f + 5.0f) * 0.14f;
}

std::uint8_t levelForSample(const LandscapeBowlSettings& settings, float px, float py, LandscapeZone* outZone) {
    const float centerX = (float)settings.gridWidth * 0.5f;
    const float centerY = (float)settings.gridHeight * 0.58f;
    const float dx = px - centerX;
    const float dy = py - centerY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float topHalfMask = smoothStep(0.0f, settings.highGroundRadius * 0.5f, -dy + settings.clearingRadius * 0.2f);
    const float arcNoise = blendedNoise(settings.seed + 101, px, py, settings.arcNoiseScale);
    const float distortedRadius = settings.highGroundRadius + arcNoise * settings.arcNoiseAmplitude;
    const float ringDistance = std::abs(distance - distortedRadius);
    const float ringMask = 1.0f - smoothStep(settings.highGroundWidth * 0.35f, settings.highGroundWidth, ringDistance);
    const float highGroundMask = clampFloat(topHalfMask * ringMask, 0.0f, 1.0f);
    const float clearingMask = 1.0f - smoothStep(
        settings.clearingRadius,
        settings.clearingRadius + settings.clearingSoftness,
        distance);

    float hillContribution = 0.0f;
    for (int hill = 0; hill < settings.hillCount; hill++) {
        const float angleT = 0.12f + deterministicFloat01(settings.seed, hill * 7 + 1) * 0.76f;
        const float angle = 3.14159265f * angleT;
        const float radius = settings.highGroundRadius + (deterministicFloat01(settings.seed, hill * 7 + 2) - 0.5f) * settings.highGroundWidth;
        const float hillX = centerX + std::cos(angle) * radius;
        const float hillY = centerY - std::sin(angle) * radius;
        const float hillDx = px - hillX;
        const float hillDy = py - hillY;
        const float hillDistanceSq = hillDx * hillDx + hillDy * hillDy;
        const float localHillRadius = settings.hillRadius * (0.75f + deterministicFloat01(settings.seed, hill * 7 + 3) * 0.65f);
        const float local = std::exp(-hillDistanceSq / std::max(0.001f, localHillRadius * localHillRadius));
        hillContribution += local * settings.hillHeight * (0.7f + deterministicFloat01(settings.seed, hill * 7 + 4) * 0.6f);
    }

    const int maxLevel = settings.heightLevels - 1;
    int level = 0;
    if (clearingMask <= 0.62f) {
        const float hillLevelBoost = clampFloat(hillContribution / std::max(0.001f, settings.hillHeight), 0.0f, 1.5f);
        const float levelScore = highGroundMask * (float)maxLevel + hillLevelBoost;
        level = clampInt((int)std::round(levelScore), 0, maxLevel);
        if (highGroundMask > 0.18f) {
            level = std::max(level, 1);
        }
        if (highGroundMask > 0.55f) {
            level = std::max(level, std::min(maxLevel, 2));
        }
        if (highGroundMask > 0.82f || hillLevelBoost > 1.05f) {
            level = std::max(level, maxLevel);
        }
    }

    LandscapeZone zone = LandscapeZone::Lowland;
    if (clearingMask > 0.7f) {
        zone = LandscapeZone::Clearing;
        level = 0;
    } else if (hillContribution > settings.hillHeight * 0.35f && level > 0) {
        zone = LandscapeZone::Hill;
    } else if (level >= std::max(1, maxLevel - 1)) {
        zone = LandscapeZone::HighGround;
    } else if (level > 0) {
        zone = LandscapeZone::Slope;
    }

    if (outZone != nullptr) {
        *outZone = zone;
    }
    return (std::uint8_t)level;
}

std::vector<int> computeClearingDistances(const LandscapeLevelGrid& grid) {
    constexpr int unreachable = 1000000;
    std::vector<int> distances(grid.cellLevels.size(), unreachable);
    std::queue<int> pending;

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const int index = grid.cellIndex(x, y);
            if (grid.zones[(std::size_t)index] != LandscapeZone::Clearing) {
                continue;
            }
            distances[(std::size_t)index] = 0;
            pending.push(index);
        }
    }

    while (!pending.empty()) {
        const int index = pending.front();
        pending.pop();
        const int x = index % grid.width;
        const int y = index / grid.width;
        const int nextDistance = distances[(std::size_t)index] + 1;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                    continue;
                }
                const int neighborIndex = grid.cellIndex(nx, ny);
                if (nextDistance >= distances[(std::size_t)neighborIndex]) {
                    continue;
                }
                distances[(std::size_t)neighborIndex] = nextDistance;
                pending.push(neighborIndex);
            }
        }
    }

    return distances;
}

void enforcePyramidLevelSpacing(LandscapeLevelGrid& grid) {
    if (grid.empty()) {
        return;
    }

    const int maxLevel = std::max(0, grid.levelCount - 1);
    const std::vector<int> clearingDistances = computeClearingDistances(grid);

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const int index = grid.cellIndex(x, y);
            std::uint8_t& level = grid.cellLevels[(std::size_t)index];
            if (grid.zones[(std::size_t)index] == LandscapeZone::Clearing) {
                level = 0;
                continue;
            }
            if (clearingDistances[(std::size_t)index] < 1000000) {
                level = (std::uint8_t)std::min<int>(level, clearingDistances[(std::size_t)index]);
            }
        }
    }

    for (int level = maxLevel; level >= 2; level--) {
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                if ((int)grid.cellLevelAt(x, y) < level) {
                    continue;
                }

                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                            continue;
                        }
                        const int neighborIndex = grid.cellIndex(nx, ny);
                        if (grid.zones[(std::size_t)neighborIndex] == LandscapeZone::Clearing) {
                            continue;
                        }

                        int targetLevel = level - 1;
                        if (clearingDistances[(std::size_t)neighborIndex] < 1000000) {
                            targetLevel = std::min(targetLevel, clearingDistances[(std::size_t)neighborIndex]);
                        }

                        std::uint8_t& neighborLevel = grid.cellLevels[(std::size_t)neighborIndex];
                        if ((int)neighborLevel < targetLevel) {
                            neighborLevel = (std::uint8_t)std::clamp(targetLevel, 0, maxLevel);
                        }
                    }
                }
            }
        }
    }
}

void deriveNodeLevelsFromCells(LandscapeLevelGrid& grid) {
    grid.nodeLevels.assign((std::size_t)(grid.width + 1) * (std::size_t)(grid.height + 1), 0);
    for (int y = 0; y <= grid.height; y++) {
        for (int x = 0; x <= grid.width; x++) {
            std::uint8_t level = 0;
            for (int cellY = y - 1; cellY <= y; cellY++) {
                for (int cellX = x - 1; cellX <= x; cellX++) {
                    if (cellX < 0 || cellY < 0 || cellX >= grid.width || cellY >= grid.height) {
                        continue;
                    }
                    level = std::max(level, grid.cellLevelAt(cellX, cellY));
                }
            }
            grid.nodeLevels[(std::size_t)grid.nodeIndex(x, y)] = level;
        }
    }
}

int maxAdjacentLevelDelta(const LandscapeLevelGrid& grid) {
    int maxDelta = 0;
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const int level = (int)grid.cellLevelAt(x, y);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                        continue;
                    }
                    const int neighborLevel = (int)grid.cellLevelAt(nx, ny);
                    const int delta = level > neighborLevel ? level - neighborLevel : neighborLevel - level;
                    maxDelta = std::max(maxDelta, delta);
                }
            }
        }
    }
    return maxDelta;
}

} // namespace

bool LandscapeLevelGrid::empty() const {
    return width <= 0 || height <= 0 || cellLevels.empty() || nodeLevels.empty();
}

int LandscapeLevelGrid::cellIndex(int x, int y) const {
    return y * width + x;
}

int LandscapeLevelGrid::nodeIndex(int x, int y) const {
    return y * (width + 1) + x;
}

std::uint8_t LandscapeLevelGrid::cellLevelAt(int x, int y) const {
    if (empty() || x < 0 || y < 0 || x >= width || y >= height) {
        return 0;
    }
    return cellLevels[(std::size_t)cellIndex(x, y)];
}

std::uint8_t LandscapeLevelGrid::nodeLevelAt(int x, int y) const {
    if (empty() || x < 0 || y < 0 || x > width || y > height) {
        return 0;
    }
    return nodeLevels[(std::size_t)nodeIndex(x, y)];
}

LandscapeZone LandscapeLevelGrid::zoneAt(int x, int y) const {
    if (empty() || x < 0 || y < 0 || x >= width || y >= height) {
        return LandscapeZone::Lowland;
    }
    return zones[(std::size_t)cellIndex(x, y)];
}

bool LandscapeTileKey::operator<(const LandscapeTileKey& other) const {
    if (kind != other.kind) return kind < other.kind;
    if (tileType != other.tileType) return tileType < other.tileType;
    if (zone != other.zone) return zone < other.zone;
    if (side != other.side) return side < other.side;
    if (level != other.level) return level < other.level;
    if (lowerLevel != other.lowerLevel) return lowerLevel < other.lowerLevel;
    return upperLevel < other.upperLevel;
}

LandscapeBowlSettings sanitize(LandscapeBowlSettings settings) {
    settings.gridWidth = clampInt(settings.gridWidth, 8, 96);
    settings.gridHeight = clampInt(settings.gridHeight, 8, 96);
    settings.clearingRadius = clampFloat(settings.clearingRadius, 1.0f, 30.0f);
    settings.clearingSoftness = clampFloat(settings.clearingSoftness, 0.1f, 12.0f);
    settings.highGroundRadius = clampFloat(settings.highGroundRadius, 2.0f, 40.0f);
    settings.highGroundWidth = clampFloat(settings.highGroundWidth, 0.5f, 16.0f);
    settings.highGroundHeight = clampFloat(settings.highGroundHeight, 0.5f, 16.0f);
    settings.heightLevels = clampInt(settings.heightLevels, 2, 8);
    settings.arcNoiseScale = clampFloat(settings.arcNoiseScale, 0.25f, 32.0f);
    settings.arcNoiseAmplitude = clampFloat(settings.arcNoiseAmplitude, 0.0f, 8.0f);
    settings.hillCount = clampInt(settings.hillCount, 0, 24);
    settings.hillHeight = clampFloat(settings.hillHeight, 0.0f, 8.0f);
    settings.hillRadius = clampFloat(settings.hillRadius, 0.5f, 12.0f);
    return settings;
}

LandscapeTileType nodeMaskToTileType(const std::array<bool, 4>& mask) {
    const bool a = mask[0];
    const bool b = mask[1];
    const bool c = mask[2];
    const bool d = mask[3];

    if (a && b && c && d) return LandscapeTileType::Full;
    if (!a && !b && c && !d) return LandscapeTileType::RightCorner;
    if (a && !b && !c && !d) return LandscapeTileType::LeftCorner;
    if (!a && b && !c && !d) return LandscapeTileType::UpCorner;
    if (!a && !b && !c && d) return LandscapeTileType::DownCorner;
    if (a && !b && c && !d) return LandscapeTileType::LeftRightCorners;
    if (!a && b && !c && d) return LandscapeTileType::UpAndDownCorners;
    if (a && b && c && !d) return LandscapeTileType::DownLack;
    if (a && !b && c && d) return LandscapeTileType::UpLack;
    if (a && b && !c && d) return LandscapeTileType::RightLack;
    if (!a && b && c && d) return LandscapeTileType::LeftLack;
    if (!a && !b && c && d) return LandscapeTileType::RightDownLine;
    if (a && !b && !c && d) return LandscapeTileType::LeftDownLine;
    if (!a && b && c && !d) return LandscapeTileType::RightUpLine;
    if (a && b && !c && !d) return LandscapeTileType::LeftUpLine;

    return LandscapeTileType::Unknown;
}

std::array<bool, 4> tileTypeToNodeMask(LandscapeTileType type) {
    switch (type) {
    case LandscapeTileType::Full: return {true, true, true, true};
    case LandscapeTileType::RightCorner: return {false, false, true, false};
    case LandscapeTileType::LeftCorner: return {true, false, false, false};
    case LandscapeTileType::UpCorner: return {false, true, false, false};
    case LandscapeTileType::DownCorner: return {false, false, false, true};
    case LandscapeTileType::DownLack: return {true, true, true, false};
    case LandscapeTileType::UpLack: return {true, false, true, true};
    case LandscapeTileType::RightLack: return {true, true, false, true};
    case LandscapeTileType::LeftLack: return {false, true, true, true};
    case LandscapeTileType::RightDownLine: return {false, false, true, true};
    case LandscapeTileType::LeftDownLine: return {true, false, false, true};
    case LandscapeTileType::RightUpLine: return {false, true, true, false};
    case LandscapeTileType::LeftUpLine: return {true, true, false, false};
    case LandscapeTileType::UpAndDownCorners: return {false, true, false, true};
    case LandscapeTileType::LeftRightCorners: return {true, false, true, false};
    case LandscapeTileType::Unknown:
    default:
        return {false, false, false, false};
    }
}

bool tileTypeHasSurface(LandscapeTileType type) {
    return type != LandscapeTileType::Unknown;
}

std::string_view tileTypeName(LandscapeTileType type) {
    switch (type) {
    case LandscapeTileType::Full: return "Full";
    case LandscapeTileType::RightCorner: return "RightCorner";
    case LandscapeTileType::LeftCorner: return "LeftCorner";
    case LandscapeTileType::UpCorner: return "UpCorner";
    case LandscapeTileType::DownCorner: return "DownCorner";
    case LandscapeTileType::DownLack: return "DownLack";
    case LandscapeTileType::UpLack: return "UpLack";
    case LandscapeTileType::RightLack: return "RightLack";
    case LandscapeTileType::LeftLack: return "LeftLack";
    case LandscapeTileType::RightDownLine: return "RightDownLine";
    case LandscapeTileType::LeftDownLine: return "LeftDownLine";
    case LandscapeTileType::RightUpLine: return "RightUpLine";
    case LandscapeTileType::LeftUpLine: return "LeftUpLine";
    case LandscapeTileType::UpAndDownCorners: return "UpAndDownCorners";
    case LandscapeTileType::LeftRightCorners: return "LeftRightCorners";
    case LandscapeTileType::Unknown:
    default:
        return "Unknown";
    }
}

std::array<bool, 4> cellNodeMaskAtLevel(const LandscapeLevelGrid& grid, int x, int y, std::uint8_t minLevel) {
    return {
        grid.nodeLevelAt(x, y + 1) >= minLevel,
        grid.nodeLevelAt(x, y) >= minLevel,
        grid.nodeLevelAt(x + 1, y) >= minLevel,
        grid.nodeLevelAt(x + 1, y + 1) >= minLevel,
    };
}

LandscapeTileType surfaceTileTypeAtLevel(const LandscapeLevelGrid& grid, int x, int y, std::uint8_t minLevel) {
    return nodeMaskToTileType(cellNodeMaskAtLevel(grid, x, y, minLevel));
}

LandscapeLevelGrid generateLandscapeBowl(const LandscapeBowlSettings& inputSettings, BowlGenerationStats* stats) {
    const LandscapeBowlSettings settings = sanitize(inputSettings);

    LandscapeLevelGrid grid;
    grid.width = settings.gridWidth;
    grid.height = settings.gridHeight;
    grid.levelCount = settings.heightLevels;
    grid.levelHeight = settings.highGroundHeight / (float)std::max(1, settings.heightLevels - 1);
    grid.cellLevels.assign((std::size_t)grid.width * (std::size_t)grid.height, 0);
    grid.nodeLevels.assign((std::size_t)(grid.width + 1) * (std::size_t)(grid.height + 1), 0);
    grid.zones.assign(grid.cellLevels.size(), LandscapeZone::Lowland);

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            LandscapeZone zone = LandscapeZone::Lowland;
            const std::uint8_t level = levelForSample(settings, (float)x + 0.5f, (float)y + 0.5f, &zone);
            const std::size_t index = (std::size_t)grid.cellIndex(x, y);
            grid.cellLevels[index] = level;
            grid.zones[index] = zone;
        }
    }

    enforcePyramidLevelSpacing(grid);
    deriveNodeLevelsFromCells(grid);

    BowlGenerationStats localStats;
    localStats.levelCellCounts.assign((std::size_t)settings.heightLevels, 0);
    localStats.minHeight = std::numeric_limits<float>::max();
    localStats.maxHeight = -std::numeric_limits<float>::max();
    localStats.maxAdjacentLevelDelta = maxAdjacentLevelDelta(grid);

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const std::uint8_t level = grid.cellLevelAt(x, y);
            const LandscapeZone zone = grid.zoneAt(x, y);
            if (level < localStats.levelCellCounts.size()) {
                localStats.levelCellCounts[(std::size_t)level]++;
            }
            if (zone == LandscapeZone::Clearing) {
                localStats.clearingCellCount++;
            } else if (zone == LandscapeZone::HighGround) {
                localStats.highGroundCellCount++;
            } else if (zone == LandscapeZone::Hill) {
                localStats.hillCellCount++;
            }
            const float height = (float)level * grid.levelHeight;
            localStats.minHeight = std::min(localStats.minHeight, height);
            localStats.maxHeight = std::max(localStats.maxHeight, height);
        }
    }

    if (localStats.minHeight == std::numeric_limits<float>::max()) {
        localStats.minHeight = 0.0f;
        localStats.maxHeight = 0.0f;
    }

    if (stats != nullptr) {
        *stats = std::move(localStats);
    }

    return grid;
}

} // namespace landscape_core
