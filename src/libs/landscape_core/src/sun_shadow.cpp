#include "landscape_core/sun_shadow.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

void blurOcclusionFieldBox(std::vector<float>& occlusion, int width, int height, int radius) {
    if (radius <= 0 || occlusion.empty()) {
        return;
    }

    const std::vector<float> source = occlusion;
    const int kernel = radius * 2 + 1;
    const float invKernel = 1.0f / (float)kernel;

    std::vector<float> temp(occlusion.size(), 0.0f);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; k++) {
                const int sampleX = std::clamp(x + k, 0, width - 1);
                sum += source[(std::size_t)y * (std::size_t)width + (std::size_t)sampleX];
            }
            temp[(std::size_t)y * (std::size_t)width + (std::size_t)x] = sum * invKernel;
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            float sum = 0.0f;
            for (int k = -radius; k <= radius; k++) {
                const int sampleY = std::clamp(y + k, 0, height - 1);
                sum += temp[(std::size_t)sampleY * (std::size_t)width + (std::size_t)x];
            }
            occlusion[(std::size_t)y * (std::size_t)width + (std::size_t)x] = sum * invKernel;
        }
    }
}

} // namespace

float sampleShadowFieldBilinear(
    const std::vector<float>& field,
    int width,
    int height,
    float x,
    float z) {

    if (width <= 0 || height <= 0 || field.empty()) {
        return 1.0f;
    }

    x = std::clamp(x, 0.0f, (float)(width - 1));
    z = std::clamp(z, 0.0f, (float)(height - 1));

    const int x0 = (int)std::floor(x);
    const int z0 = (int)std::floor(z);
    const int x1 = std::min(x0 + 1, width - 1);
    const int z1 = std::min(z0 + 1, height - 1);
    const float tx = x - (float)x0;
    const float tz = z - (float)z0;

    const auto sample = [&](int sx, int sz) {
        return field[(std::size_t)sz * (std::size_t)width + (std::size_t)sx];
    };

    const float v00 = sample(x0, z0);
    const float v10 = sample(x1, z0);
    const float v01 = sample(x0, z1);
    const float v11 = sample(x1, z1);
    const float vx0 = v00 + (v10 - v00) * tx;
    const float vx1 = v01 + (v11 - v01) * tx;
    return clamp01(vx0 + (vx1 - vx0) * tz);
}

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
    const float lightRisePerStep =
        std::max(0.0f, settings.lightDirectionY) / dirLen * settings.levelRisePerStep;
    const float minDelta = std::max(0.01f, settings.minOcclusionDelta);
    const float softRange = std::max(0.35f, settings.softness * 1.35f);
    std::vector<float> occlusionField(cellCount, 0.0f);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const std::size_t index = (std::size_t)y * (std::size_t)width + (std::size_t)x;
            const int level0 = (int)cellLevels[index];
            float occlusion = 0.0f;

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
                const float rayLevel = (float)level0 + (float)step * lightRisePerStep;
                const float levelDelta = std::max(
                    (float)(neighborLevel - level0),
                    (float)neighborLevel - rayLevel);
                if (levelDelta <= minDelta) {
                    continue;
                }

                const float distanceFade = 1.0f - smoothstep(0.0f, penumbraSpan, (float)step);
                const float blocker =
                    smoothstep(minDelta, minDelta + softRange, levelDelta) *
                    distanceFade *
                    settings.occlusionStrength;
                occlusion = std::max(occlusion, blocker);
                if (occlusion >= 0.98f) {
                    occlusion = 1.0f;
                    break;
                }
            }

            occlusionField[index] = clamp01(occlusion);
        }
    }

    const std::vector<float> rawOcclusion = occlusionField;
    blurOcclusionFieldBox(occlusionField, width, height, settings.blurRadius);
    for (std::size_t i = 0; i < cellCount; i++) {
        if (rawOcclusion[i] <= 0.01f) {
            shadowField[i] = clamp01(1.0f - occlusionField[i] * 0.45f);
            continue;
        }
        const float softenedOcclusion = std::max(rawOcclusion[i], occlusionField[i]);
        shadowField[i] = clamp01(1.0f - softenedOcclusion);
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
