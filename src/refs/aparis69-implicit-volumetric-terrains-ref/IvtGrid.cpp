#include "IvtGrid.h"

#include <algorithm>
#include <cmath>

namespace ivt_view {

WorldGridParams computeWorldGridParams(const IvtModel& model, const Vec3& center) {
    constexpr float kMinExtent = 16.0f;
    constexpr float kPadding = 1.25f;

    auto roundUpToCell = [](float halfExtent, float cellSize) {
        return std::max(kMinExtent, std::ceil(halfExtent / cellSize) * cellSize);
    };

    const float halfSpanX = (model.boundsMax.x - model.boundsMin.x) * 0.5f;
    const float halfSpanZ = (model.boundsMax.z - model.boundsMin.z) * 0.5f;
    float halfFootprint = std::max(halfSpanX, halfSpanZ);

    if (!model.meshVertices.empty()) {
        float minX = model.meshVertices[0].x;
        float maxX = minX;
        float minZ = model.meshVertices[0].z;
        float maxZ = minZ;
        for (const Vec3& v : model.meshVertices) {
            minX = std::min(minX, v.x);
            maxX = std::max(maxX, v.x);
            minZ = std::min(minZ, v.z);
            maxZ = std::max(maxZ, v.z);
        }
        halfFootprint = std::max(std::max(maxX - minX, maxZ - minZ) * 0.5f, halfFootprint);
    }

    if (halfFootprint < 1.0f) {
        halfFootprint = 10.0f;
    }

    float cellSize = 1.0f;
    if (halfFootprint > 35.0f) {
        cellSize = 2.0f;
    }
    if (halfFootprint > 70.0f) {
        cellSize = 5.0f;
    }

    WorldGridParams params{};
    params.extent = roundUpToCell(halfFootprint * kPadding, cellSize);
    params.cellSize = cellSize;
    params.majorEvery = 5;
    params.groundY = model.meshVertices.empty() ? center.y : model.boundsMin.y;
    (void)center;
    return params;
}

} // namespace ivt_view
