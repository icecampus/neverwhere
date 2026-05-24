#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "LandscapeTileRules.h"

enum class TerrainMaterial : std::uint8_t {
    Grass = 0,
    Sand = 1,
    Rock = 2,
};

struct TerrainSceneSettings {
    int gridSize = 32;
    int seed = 7;
    int minHeight = 1;
    int maxHeight = 8;
    float nodeThreshold = 0.48f;
};

class TerrainScene {
public:
    void generate(const TerrainSceneSettings& settings);

    int gridSize() const { return m_gridSize; }
    int minHeight() const { return m_minHeight; }
    int maxHeight() const { return m_maxHeight; }
    float heightAt(int x, int z) const;
    int columnHeightAt(int x, int z) const;
    TerrainMaterial materialAt(int x, int z) const;
    LandscapeTileType tileTypeAt(int x, int z) const;
    LandscapeTileType tileTypeAtLevel(int x, int z, std::uint8_t minLevel) const;
    bool landNodeAt(int x, int z) const;
    std::uint8_t landNodeLevelAt(int x, int z) const;
    bool setLandNode(int x, int z, bool enabled);
    bool setLandNodeLevel(int x, int z, std::uint8_t level);
    bool toggleLandNode(int x, int z);
    bool cycleLandNodeLevel(int x, int z);
    void clearLandNodes();

    static std::array<glm::ivec2, 4> nodeNeighboursCells(const glm::ivec2& nodePosition);

private:
    int m_gridSize = 0;
    int m_minHeight = 1;
    int m_maxHeight = 8;
    int m_landNodeWidth = 0;
    int m_landNodeHeight = 0;
    int m_landNodeXOffset = 1;
    std::vector<int> m_columnHeights;
    std::vector<TerrainMaterial> m_materials;
    std::vector<std::uint8_t> m_landNodes;

    int cellIndex(int x, int z) const;
    int materialIndex(int x, int z) const;
    int landNodeIndex(int x, int z) const;
};

