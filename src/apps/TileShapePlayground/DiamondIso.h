#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

// Cartesian diamond isometry — port of core::DiamondIsometry (no Qt).
// Not staggered; matches editor LandscapePencil / VERTEX_CENTRIC_LANDSCAPE.
struct DiamondDims {
    float cellWidth = 128.0f;
    float aspectRatio = 2.0f;

    glm::vec2 cellSize() const {
        return glm::vec2(cellWidth, cellWidth / aspectRatio);
    }
};

class DiamondIso {
public:
    DiamondDims dims{};

    glm::ivec2 fieldToMap(const glm::vec2& fieldPosition) const;
    glm::vec2 mapToField(const glm::ivec2& cellPosition) const;
    glm::ivec2 fieldToNode(const glm::vec2& fieldPosition) const;
    glm::vec2 nodeToField(const glm::ivec2& nodePosition) const;

    // 4 cells sharing a node: (n), (n-1,n), (n,n-1), (n-1,n-1)
    static std::array<glm::ivec2, 4> nodeNeighbourCells(const glm::ivec2& node);

    // Corner nodes of a cell in slot order [Left, Up, Right, Down]
    static std::array<glm::ivec2, 4> cellCornerNodes(const glm::ivec2& cell);

    // Diamond corner positions in world space (Left, Up, Right, Down)
    std::array<glm::vec2, 4> cellDiamondCorners(const glm::ivec2& cell) const;

    std::uint64_t zOffset(const glm::ivec2& cell) const;
};
