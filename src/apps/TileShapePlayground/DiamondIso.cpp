#include "DiamondIso.h"

#include <cmath>

glm::ivec2 DiamondIso::fieldToMap(const glm::vec2& fieldPosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float sx = fieldPosition.x / halfW;
    const float sy = fieldPosition.y / halfH;

    const float cx = (sx + sy) * 0.5f - 1.0f;
    const float cy = (sy - sx) * 0.5f;

    return glm::ivec2(static_cast<int>(std::round(cx)), static_cast<int>(std::round(cy)));
}

glm::vec2 DiamondIso::mapToField(const glm::ivec2& cellPosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float cx = static_cast<float>(cellPosition.x);
    const float cy = static_cast<float>(cellPosition.y);

    return glm::vec2((cx - cy) * halfW + halfW, (cx + cy) * halfH + halfH);
}

glm::ivec2 DiamondIso::fieldToNode(const glm::vec2& fieldPosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float sx = fieldPosition.x / halfW;
    const float sy = fieldPosition.y / halfH;

    const float nx = (sx + sy) * 0.5f - 0.5f;
    const float ny = (sy - sx) * 0.5f + 0.5f;

    return glm::ivec2(static_cast<int>(std::round(nx)), static_cast<int>(std::round(ny)));
}

glm::vec2 DiamondIso::nodeToField(const glm::ivec2& nodePosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float nx = static_cast<float>(nodePosition.x);
    const float ny = static_cast<float>(nodePosition.y);

    // Up-corner of cell (nx, ny) — no +halfH (unlike cell centers).
    return glm::vec2((nx - ny) * halfW + halfW, (nx + ny) * halfH);
}

std::array<glm::ivec2, 4> DiamondIso::nodeNeighbourCells(const glm::ivec2& node) {
    return {
        glm::ivec2(node.x, node.y),
        glm::ivec2(node.x - 1, node.y),
        glm::ivec2(node.x, node.y - 1),
        glm::ivec2(node.x - 1, node.y - 1),
    };
}

std::array<glm::ivec2, 4> DiamondIso::cellCornerNodes(const glm::ivec2& cell) {
    return {
        glm::ivec2(cell.x, cell.y + 1),     // Left
        glm::ivec2(cell.x, cell.y),         // Up
        glm::ivec2(cell.x + 1, cell.y),     // Right
        glm::ivec2(cell.x + 1, cell.y + 1), // Down
    };
}

std::array<glm::vec2, 4> DiamondIso::cellDiamondCorners(const glm::ivec2& cell) const {
    const glm::vec2 center = mapToField(cell);
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    return {
        glm::vec2(center.x - halfW, center.y), // Left
        glm::vec2(center.x, center.y - halfH), // Up
        glm::vec2(center.x + halfW, center.y), // Right
        glm::vec2(center.x, center.y + halfH), // Down
    };
}

std::uint64_t DiamondIso::zOffset(const glm::ivec2& cell) const {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 32) |
        static_cast<std::uint32_t>(cell.x);
}
