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
};

class TerrainScene {
public:
    void generate(const TerrainSceneSettings& settings);

    int gridSize() const { return m_gridSize; }
    float heightAt(int x, int z) const;
    TerrainMaterial materialAt(int x, int z) const;

private:
    int m_gridSize = 0;
    std::vector<float> m_heights;
    std::vector<TerrainMaterial> m_materials;

    int heightIndex(int x, int z) const;
    int materialIndex(int x, int z) const;
};

