#include "TerrainScene.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

float hash01(int x, int z, int seed) {
    const float n = std::sin((float)(x * 127 + z * 311 + seed * 919) * 0.0137f) * 43758.5453f;
    return n - std::floor(n);
}

float smoothNoise(float x, float z, int seed) {
    const int x0 = (int)std::floor(x);
    const int z0 = (int)std::floor(z);
    const float tx = x - (float)x0;
    const float tz = z - (float)z0;
    const float sx = tx * tx * (3.0f - 2.0f * tx);
    const float sz = tz * tz * (3.0f - 2.0f * tz);

    const float a = hash01(x0, z0, seed);
    const float b = hash01(x0 + 1, z0, seed);
    const float c = hash01(x0, z0 + 1, seed);
    const float d = hash01(x0 + 1, z0 + 1, seed);
    const float ab = a + (b - a) * sx;
    const float cd = c + (d - c) * sx;
    return ab + (cd - ab) * sz;
}

float fbm(float x, float z, int seed) {
    float value = 0.0f;
    float amplitude = 0.55f;
    float frequency = 0.085f;
    float norm = 0.0f;

    for (int octave = 0; octave < 4; octave++) {
        value += smoothNoise(x * frequency, z * frequency, seed + octave * 17) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return (norm > 0.0f) ? (value / norm) : 0.0f;
}

} // namespace

void TerrainScene::generate(const TerrainSceneSettings& settings) {
    m_gridSize = std::clamp(settings.gridSize, 4, 96);
    m_minHeight = std::clamp(settings.minHeight, 0, 32);
    m_maxHeight = std::clamp(settings.maxHeight, m_minHeight + 1, 48);
    m_landNodeWidth = m_gridSize + 2;
    m_landNodeHeight = m_gridSize + 2;
    m_landNodeXOffset = 1;
    m_columnHeights.assign(m_gridSize * m_gridSize, m_minHeight);
    m_materials.assign(m_gridSize * m_gridSize, TerrainMaterial::Grass);
    m_landNodes.assign(m_landNodeWidth * m_landNodeHeight, 0);

    const float half = (float)m_gridSize * 0.5f;
    const float nodeThreshold = std::clamp(settings.nodeThreshold, 0.05f, 0.95f);

    for (int z = 0; z < m_landNodeHeight; z++) {
        for (int x = -m_landNodeXOffset; x < m_landNodeWidth - m_landNodeXOffset; x++) {
            const float dx = ((float)x - half) / half;
            const float dz = ((float)z - half) / half;
            const float radial = std::sqrt(dx * dx + dz * dz);
            const float ridge = std::sin(((float)x + settings.seed) * 0.31f) * std::cos(((float)z - settings.seed) * 0.23f);
            float land = fbm((float)x + 37.0f, (float)z - 19.0f, settings.seed + 101);
            land = land * 0.82f + ridge * 0.18f;
            land -= std::max(0.0f, radial - 0.78f) * 0.75f;
            m_landNodes[landNodeIndex(x, z)] = land >= nodeThreshold ? 1 : 0;
        }
    }

    for (int z = 0; z < m_gridSize; z++) {
        for (int x = 0; x < m_gridSize; x++) {
            const float dx = ((float)x - half) / half;
            const float dz = ((float)z - half) / half;
            const float radial = std::sqrt(dx * dx + dz * dz);
            const float ridge = std::sin(((float)x + settings.seed) * 0.37f) * std::cos(((float)z - settings.seed) * 0.29f);
            float h = fbm((float)x, (float)z, settings.seed);
            h = (h - 0.35f) * 1.25f + ridge * 0.18f;
            h -= std::max(0.0f, radial - 0.72f) * 0.95f;
            h = std::clamp(h, 0.0f, 1.0f);

            const int heightRange = std::max(1, m_maxHeight - m_minHeight);
            const int columnHeight = std::clamp(
                m_minHeight + (int)std::round(h * (float)heightRange),
                m_minHeight,
                m_maxHeight);
            m_columnHeights[cellIndex(x, z)] = columnHeight;

            TerrainMaterial material = TerrainMaterial::Grass;
            if (columnHeight <= m_minHeight + 1) {
                material = TerrainMaterial::Sand;
            } else if (columnHeight >= m_maxHeight - 1) {
                material = TerrainMaterial::Rock;
            }

            if ((dx * dx + dz * dz) < 0.10f && columnHeight <= m_minHeight + 3) {
                material = TerrainMaterial::Sand;
            }

            m_materials[materialIndex(x, z)] = material;
        }
    }
}

float TerrainScene::heightAt(int x, int z) const {
    return (float)columnHeightAt(x, z);
}

int TerrainScene::columnHeightAt(int x, int z) const {
    if (m_gridSize <= 0) return 0;
    x = std::clamp(x, 0, m_gridSize - 1);
    z = std::clamp(z, 0, m_gridSize - 1);
    return m_columnHeights[cellIndex(x, z)];
}

TerrainMaterial TerrainScene::materialAt(int x, int z) const {
    if (m_gridSize <= 0) return TerrainMaterial::Grass;
    x = std::clamp(x, 0, m_gridSize - 1);
    z = std::clamp(z, 0, m_gridSize - 1);
    return m_materials[materialIndex(x, z)];
}

LandscapeTileType TerrainScene::tileTypeAt(int x, int z) const {
    return tileTypeAtLevel(x, z, 1);
}

LandscapeTileType TerrainScene::tileTypeAtLevel(int x, int z, std::uint8_t minLevel) const {
    if (m_gridSize <= 0) return LandscapeTileType::Unknown;

    const bool oddRow = (z & 1) != 0;
    const std::array<bool, 4> mask = oddRow
        ? std::array<bool, 4>{
            landNodeLevelAt(x, z + 1) >= minLevel,
            landNodeLevelAt(x, z) >= minLevel,
            landNodeLevelAt(x + 1, z + 1) >= minLevel,
            landNodeLevelAt(x, z + 2) >= minLevel,
        }
        : std::array<bool, 4>{
            landNodeLevelAt(x - 1, z + 1) >= minLevel,
            landNodeLevelAt(x, z) >= minLevel,
            landNodeLevelAt(x, z + 1) >= minLevel,
            landNodeLevelAt(x, z + 2) >= minLevel,
        };

    return nodeMaskToTileType(mask);
}

bool TerrainScene::landNodeAt(int x, int z) const {
    return landNodeLevelAt(x, z) > 0;
}

std::uint8_t TerrainScene::landNodeLevelAt(int x, int z) const {
    if (m_landNodeWidth <= 0 || m_landNodeHeight <= 0) return 0;
    const int storageX = x + m_landNodeXOffset;
    if (storageX < 0 || storageX >= m_landNodeWidth || z < 0 || z >= m_landNodeHeight) {
        return 0;
    }
    return std::min<std::uint8_t>(m_landNodes[landNodeIndex(x, z)], 2);
}

bool TerrainScene::setLandNode(int x, int z, bool enabled) {
    return setLandNodeLevel(x, z, enabled ? 1 : 0);
}

bool TerrainScene::setLandNodeLevel(int x, int z, std::uint8_t level) {
    if (m_landNodeWidth <= 0 || m_landNodeHeight <= 0) return false;
    const int storageX = x + m_landNodeXOffset;
    if (storageX < 0 || storageX >= m_landNodeWidth || z < 0 || z >= m_landNodeHeight) {
        return false;
    }

    std::uint8_t& value = m_landNodes[landNodeIndex(x, z)];
    const std::uint8_t newValue = std::min<std::uint8_t>(level, 2);
    if (value == newValue) {
        return false;
    }

    value = newValue;
    return true;
}

bool TerrainScene::toggleLandNode(int x, int z) {
    return setLandNode(x, z, !landNodeAt(x, z));
}

bool TerrainScene::cycleLandNodeLevel(int x, int z) {
    const std::uint8_t current = landNodeLevelAt(x, z);
    return setLandNodeLevel(x, z, (current >= 2) ? 2 : (std::uint8_t)(current + 1));
}

void TerrainScene::clearLandNodes() {
    std::fill(m_landNodes.begin(), m_landNodes.end(), 0);
}

std::array<glm::ivec2, 4> TerrainScene::nodeNeighboursCells(const glm::ivec2& nodePosition) {
    const std::array<glm::ivec2, 4> oddMask{
        glm::ivec2{0, 0},
        glm::ivec2{0, -2},
        glm::ivec2{0, -1},
        glm::ivec2{1, -1},
    };
    const std::array<glm::ivec2, 4> evenMask{
        glm::ivec2{0, 0},
        glm::ivec2{0, -2},
        glm::ivec2{-1, -1},
        glm::ivec2{0, -1},
    };

    const std::array<glm::ivec2, 4>& mask = (nodePosition.y & 1) ? oddMask : evenMask;
    std::array<glm::ivec2, 4> result{};
    for (std::size_t i = 0; i < result.size(); i++) {
        result[i] = nodePosition + mask[i];
    }
    return result;
}

int TerrainScene::cellIndex(int x, int z) const {
    return z * m_gridSize + x;
}

int TerrainScene::materialIndex(int x, int z) const {
    return cellIndex(x, z);
}

int TerrainScene::landNodeIndex(int x, int z) const {
    return z * m_landNodeWidth + (x + m_landNodeXOffset);
}

