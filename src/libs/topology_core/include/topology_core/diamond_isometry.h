#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace topology_core {

struct DiamondDimensions {
    float cellWidth = 128.0f;
    float aspectRatio = 2.0f;

    glm::vec2 cellSize() const {
        return glm::vec2(cellWidth, cellWidth / aspectRatio);
    }
};

struct CellRegion {
    glm::ivec2 min{0, 0};
    glm::ivec2 max{0, 0};
};

// Pure isometric projection: a cartesian (cx, cy) grid rendered as diamonds.
// No even/odd row stagger — every row shares the same X mapping, so any
// affine op (pan, rotate field 90°, ...) is trivial.
//
//   mapToField(cx, cy) = ((cx - cy) * halfW + halfW,
//                         (cx + cy) * halfH + halfH)
//   where halfW = cellWidth/2, halfH = cellHeight/2.
//
// No-Qt port of the editor's DiamondIsometry (src/libs/core/topology/diamond_isometry.cpp).
// This is the single world topology shared by the editor and the game client.
class DiamondIsometry {
public:
    DiamondDimensions dims{};

    glm::ivec2 fieldToMap(const glm::vec2& fieldPosition) const;
    glm::vec2 mapToField(const glm::ivec2& cellPosition) const;

    // Vertex nodes (the vertex-centric landscape contract): a node is the
    // Up-corner of the cell with the same coordinates.
    glm::ivec2 fieldToNode(const glm::vec2& fieldPosition) const;
    glm::vec2 nodeToField(const glm::ivec2& nodePosition) const;

    // 4 cells sharing a node: (n), (n-1,n), (n,n-1), (n-1,n-1)
    static std::array<glm::ivec2, 4> nodeNeighbourCells(const glm::ivec2& node);

    // Corner nodes of a cell in slot order [Left, Up, Right, Down]
    // (matches core::DiamondIsometry / landscape_core contract).
    static std::array<glm::ivec2, 4> cellCornerNodes(const glm::ivec2& cell);

    // Diamond corner positions in world space (Left, Up, Right, Down)
    std::array<glm::vec2, 4> cellDiamondCorners(const glm::ivec2& cell) const;

    std::uint64_t zOffset(const glm::ivec2& cellPosition) const;

    // AABB of visible cells for a world-space viewport rect (offset, viewSize).
    CellRegion visibleCellBounds(const glm::vec2& viewSize, const glm::vec2& offset) const;
};

} // namespace topology_core
