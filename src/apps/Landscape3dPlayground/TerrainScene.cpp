#include "TerrainScene.h"

#include <algorithm>
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
    m_heights.assign((m_gridSize + 1) * (m_gridSize + 1), 0.0f);
    m_materials.assign(m_gridSize * m_gridSize, TerrainMaterial::Grass);

    const float half = (float)m_gridSize * 0.5f;

    for (int z = 0; z <= m_gridSize; z++) {
        for (int x = 0; x <= m_gridSize; x++) {
            const float dx = ((float)x - half) / half;
            const float dz = ((float)z - half) / half;
            const float radial = std::sqrt(dx * dx + dz * dz);
            const float ridge = std::sin(((float)x + settings.seed) * 0.37f) * std::cos(((float)z - settings.seed) * 0.29f);
            float h = fbm((float)x, (float)z, settings.seed);
            h = (h - 0.45f) * 2.2f + ridge * 0.18f;
            h -= std::max(0.0f, radial - 0.65f) * 1.4f;
            m_heights[heightIndex(x, z)] = h;
        }
    }

    for (int z = 0; z < m_gridSize; z++) {
        for (int x = 0; x < m_gridSize; x++) {
            const float h = 0.25f * (
                heightAt(x, z) +
                heightAt(x + 1, z) +
                heightAt(x, z + 1) +
                heightAt(x + 1, z + 1));

            TerrainMaterial material = TerrainMaterial::Grass;
            if (h < -0.18f) {
                material = TerrainMaterial::Sand;
            } else if (h > 0.42f) {
                material = TerrainMaterial::Rock;
            }

            const float dx = ((float)x - half) / half;
            const float dz = ((float)z - half) / half;
            if ((dx * dx + dz * dz) < 0.12f && h < 0.28f) {
                material = TerrainMaterial::Sand;
            }

            m_materials[materialIndex(x, z)] = material;
        }
    }
}

float TerrainScene::heightAt(int x, int z) const {
    if (m_gridSize <= 0) return 0.0f;
    x = std::clamp(x, 0, m_gridSize);
    z = std::clamp(z, 0, m_gridSize);
    return m_heights[heightIndex(x, z)];
}

TerrainMaterial TerrainScene::materialAt(int x, int z) const {
    if (m_gridSize <= 0) return TerrainMaterial::Grass;
    x = std::clamp(x, 0, m_gridSize - 1);
    z = std::clamp(z, 0, m_gridSize - 1);
    return m_materials[materialIndex(x, z)];
}

int TerrainScene::heightIndex(int x, int z) const {
    return z * (m_gridSize + 1) + x;
}

int TerrainScene::materialIndex(int x, int z) const {
    return z * m_gridSize + x;
}

