#pragma once

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

    std::uint64_t zOffset(const glm::ivec2& cellPosition) const;

    // AABB of visible cells for a world-space viewport rect (offset, viewSize).
    CellRegion visibleCellBounds(const glm::vec2& viewSize, const glm::vec2& offset) const;
};

} // namespace topology_core
