#include "landscape_core/sun_shadow.h"

#include <algorithm>
#include <cmath>

namespace landscape_core {
namespace {

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothstep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

std::vector<float> computeSunShadowField(
    int width,
    int height,
    const std::vector<std::uint8_t>& cellLevels,
    const SunShadowSettings& settings) {

    const std::size_t cellCount = (std::size_t)width * (std::size_t)height;
    std::vector<float> shadowField(cellCount, 1.0f);
    if (width <= 0 || height <= 0 || (int)cellLevels.size() < width * height) {
        return shadowField;
    }

    // March toward the sun (same horizontal direction as lightDirection) to find occluders.
    const float dirX = settings.lightDirectionX;
    const float dirZ = settings.lightDirectionZ;
    const float dirLen = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (dirLen <= 0.0001f) {
        return shadowField;
    }

    const float stepX = dirX / dirLen;
    const float stepZ = dirZ / dirLen;
    const int maxSteps = std::max(1, settings.maxSteps);
    const float penumbraSpan = std::max(1.0f, settings.softness * (float)maxSteps);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const std::size_t index = (std::size_t)y * (std::size_t)width + (std::size_t)x;
            const int level0 = (int)cellLevels[index];
            float visibility = 1.0f;

            float px = (float)x + 0.5f;
            float pz = (float)y + 0.5f;
            for (int step = 1; step <= maxSteps; step++) {
                px += stepX;
                pz += stepZ;
                const int sampleX = (int)std::floor(px);
                const int sampleY = (int)std::floor(pz);
                if (sampleX < 0 || sampleY < 0 || sampleX >= width || sampleY >= height) {
                    break;
                }

                const std::size_t sampleIndex =
                    (std::size_t)sampleY * (std::size_t)width + (std::size_t)sampleX;
                const int neighborLevel = (int)cellLevels[sampleIndex];
                if (neighborLevel <= level0) {
                    continue;
                }

                const float levelDelta = (float)(neighborLevel - level0);
                const float distanceFade = 1.0f - smoothstep(0.0f, penumbraSpan, (float)step);
                const float blocker = clamp01(levelDelta * 0.55f * distanceFade);
                visibility = std::min(visibility, 1.0f - blocker);
                if (visibility <= 0.01f) {
                    visibility = 0.0f;
                    break;
                }
            }

            shadowField[index] = clamp01(visibility);
        }
    }

    return shadowField;
}

std::vector<float> computeSunShadowField(
    const LandscapeLevelGrid& grid,
    const SunShadowSettings& settings) {

    if (grid.empty()) {
        return {};
    }
    return computeSunShadowField(grid.width, grid.height, grid.cellLevels, settings);
}

} // namespace landscape_core
