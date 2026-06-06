#pragma once

#include <cstdint>
#include <vector>

#include "landscape_core/landscape_logic.h"

namespace landscape_core {

struct SunShadowSettings {
    float lightDirectionX = -0.35f;
    float lightDirectionY = 0.82f;
    float lightDirectionZ = -0.45f;
    int maxSteps = 64;
    float softness = 0.55f;
    float minOcclusionDelta = 0.12f;
    float occlusionStrength = 0.88f;
    float levelRisePerStep = 0.42f;
    int blurRadius = 1;
};

// Returns per-cell shadow visibility in [0, 1]: 1 = fully lit, 0 = full shadow.
std::vector<float> computeSunShadowField(
    int width,
    int height,
    const std::vector<std::uint8_t>& cellLevels,
    const SunShadowSettings& settings);

std::vector<float> computeSunShadowField(
    const LandscapeLevelGrid& grid,
    const SunShadowSettings& settings);

float sampleShadowFieldBilinear(
    const std::vector<float>& field,
    int width,
    int height,
    float x,
    float z);

} // namespace landscape_core
