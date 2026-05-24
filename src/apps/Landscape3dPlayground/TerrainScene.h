#pragma once

#include <cstdint>
#include <vector>

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

private:
    int m_gridSize = 0;
    int m_minHeight = 1;
    int m_maxHeight = 8;
    std::vector<int> m_columnHeights;
    std::vector<TerrainMaterial> m_materials;

    int cellIndex(int x, int z) const;
    int materialIndex(int x, int z) const;
};

