#include "topology_core/staggered_isometry.h"

#include <glm/glm.hpp>

namespace topology_core {

glm::ivec2 StaggeredIsometry::fieldToMap(const glm::vec2& fieldPosition) const {
    const glm::vec2 cellSize = dims.cellSize();
    const glm::vec2 normPos = fieldPosition / cellSize;

    // First candidate (even rows)
    const glm::vec2 floorNorm = glm::floor(normPos);
    const glm::ivec2 cellFloor((int)floorNorm.x, (int)floorNorm.y);
    const glm::vec2 center1 = floorNorm + glm::vec2(0.5f, 0.5f);
    const glm::vec2 diff1 = normPos - center1;
    if (std::abs(diff1.x) + std::abs(diff1.y) <= 0.5f) {
        return glm::ivec2(cellFloor.x, cellFloor.y * 2);
    }

    // Second candidate (odd rows)
    const glm::vec2 shiftedNorm = normPos - glm::vec2(0.5f, 0.5f);
    const glm::vec2 floorShifted = glm::floor(shiftedNorm);
    const glm::ivec2 cellShifted((int)floorShifted.x, (int)floorShifted.y);
    const glm::vec2 center2 = floorShifted + glm::vec2(1.0f, 1.0f);
    const glm::vec2 diff2 = normPos - center2;
    if (std::abs(diff2.x) + std::abs(diff2.y) <= 0.5f) {
        return glm::ivec2(cellShifted.x, cellShifted.y * 2 + 1);
    }

    return glm::ivec2(-1, -1);
}

glm::vec2 StaggeredIsometry::mapToField(const glm::ivec2& cellPosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const glm::vec2 halfCellSz = cellSz * 0.5f;

    float x = (float)cellPosition.x * cellSz.x + halfCellSz.x;
    float y = (float)cellPosition.y * halfCellSz.y + halfCellSz.y;

    if (cellPosition.y & 1) {
        x += halfCellSz.x;
    }

    return glm::vec2(x, y);
}

std::uint64_t StaggeredIsometry::zOffset(const glm::ivec2& cellPosition) const {
    return (std::uint64_t((std::uint32_t)cellPosition.y) << 32) | (std::uint32_t)cellPosition.x;
}

} // namespace topology_core

