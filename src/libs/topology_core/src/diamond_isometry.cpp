#include "topology_core/diamond_isometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace topology_core {

glm::ivec2 DiamondIsometry::fieldToMap(const glm::vec2& fieldPosition) const {
    // Diamond (cartesian) projection — the exact inverse of mapToField:
    //   halfW = cellWidth/2, halfH = cellHeight/2
    //   world.x = (cx - cy) * halfW + halfW
    //   world.y = (cx + cy) * halfH + halfH
    // Solving for (cx, cy) by adding/subtracting the two equations:
    //   world.x/halfW = (cx - cy) + 1     ... (sx)
    //   world.y/halfH = (cx + cy) + 1     ... (sy)
    //   (sx + sy) = 2*cx + 2   →  cx = (sx + sy)/2 - 1
    //   (sy - sx) = 2*cy      →  cy = (sy - sx)/2
    //
    // Rounding each coordinate independently to the nearest integer cell is
    // equivalent to snapping to the nearest diamond center (L1 Voronoi cell),
    // so no two-candidate hit-test is needed (contrast with staggered).
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float sx = fieldPosition.x / halfW;
    const float sy = fieldPosition.y / halfH;

    const float cx = (sx + sy) * 0.5f - 1.0f;
    const float cy = (sy - sx) * 0.5f;

    return glm::ivec2(static_cast<int>(std::round(cx)), static_cast<int>(std::round(cy)));
}

glm::vec2 DiamondIsometry::mapToField(const glm::ivec2& cellPosition) const {
    // Diamond center in world space — uniform for every cell, no row parity.
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float cx = static_cast<float>(cellPosition.x);
    const float cy = static_cast<float>(cellPosition.y);

    const float x = (cx - cy) * halfW + halfW;
    const float y = (cx + cy) * halfH + halfH;

    return glm::vec2(x, y);
}

std::uint64_t DiamondIsometry::zOffset(const glm::ivec2& cellPosition) const {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cellPosition.y)) << 32)
         | static_cast<std::uint32_t>(cellPosition.x);
}

glm::ivec2 DiamondIsometry::fieldToNode(const glm::vec2& fieldPosition) const {
    // Same diamond projection as fieldToMap, shifted by half a cell:
    // nodes sit on diamond corners, not centers.
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float sx = fieldPosition.x / halfW;
    const float sy = fieldPosition.y / halfH;

    const float nx = (sx + sy) * 0.5f - 0.5f;
    const float ny = (sy - sx) * 0.5f + 0.5f;

    return glm::ivec2(static_cast<int>(std::round(nx)), static_cast<int>(std::round(ny)));
}

glm::vec2 DiamondIsometry::nodeToField(const glm::ivec2& nodePosition) const {
    const glm::vec2 cellSz = dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float nx = static_cast<float>(nodePosition.x);
    const float ny = static_cast<float>(nodePosition.y);

    // Up-corner of cell (nx, ny) — no +halfH (unlike cell centers).
    return glm::vec2((nx - ny) * halfW + halfW, (nx + ny) * halfH);
}

std::array<glm::ivec2, 4> DiamondIsometry::nodeNeighbourCells(const glm::ivec2& node) {
    return {
        glm::ivec2(node.x, node.y),
        glm::ivec2(node.x - 1, node.y),
        glm::ivec2(node.x, node.y - 1),
        glm::ivec2(node.x - 1, node.y - 1),
    };
}

std::array<glm::ivec2, 4> DiamondIsometry::cellCornerNodes(const glm::ivec2& cell) {
    return {
        glm::ivec2(cell.x, cell.y + 1),     // Left
        glm::ivec2(cell.x, cell.y),         // Up
        glm::ivec2(cell.x + 1, cell.y),     // Right
        glm::ivec2(cell.x + 1, cell.y + 1), // Down
    };
}

std::array<glm::vec2, 4> DiamondIsometry::cellDiamondCorners(const glm::ivec2& cell) const {
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

CellRegion DiamondIsometry::visibleCellBounds(const glm::vec2& viewSize, const glm::vec2& offset) const {
    // For the diamond grid, the visible region is the AABB of the screen
    // corners in cell space. Since fieldToMap is a clean affine map (no
    // two-candidate ambiguity), sampling just the four corners plus edge
    // midpoints plus a margin is enough.
    glm::vec2 screenPoints[9] = {
        offset,
        glm::vec2(offset.x + viewSize.x, offset.y),
        glm::vec2(offset.x, offset.y + viewSize.y),
        offset + viewSize,
        offset + glm::vec2(viewSize.x * 0.5f, 0.0f),
        offset + glm::vec2(viewSize.x, viewSize.y * 0.5f),
        offset + glm::vec2(viewSize.x * 0.5f, viewSize.y),
        offset + glm::vec2(0.0f, viewSize.y * 0.5f),
        offset + viewSize * 0.5f,
    };

    glm::ivec2 minCell(std::numeric_limits<int>::max());
    glm::ivec2 maxCell(std::numeric_limits<int>::min());

    for (const auto& point : screenPoints) {
        const glm::ivec2 cell = fieldToMap(point);
        minCell.x = std::min(minCell.x, cell.x);
        minCell.y = std::min(minCell.y, cell.y);
        maxCell.x = std::max(maxCell.x, cell.x);
        maxCell.y = std::max(maxCell.y, cell.y);
    }

    minCell -= glm::ivec2(1, 1);
    maxCell += glm::ivec2(1, 1);

    return CellRegion{minCell, maxCell};
}

} // namespace topology_core
