#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace topology_core {

struct StaggeredDimensions {
    float cellWidth = 128.0f;
    float aspectRatio = 2.0f;

    glm::vec2 cellSize() const {
        return glm::vec2(cellWidth, cellWidth / aspectRatio);
    }
};

class StaggeredIsometry {
public:
    StaggeredDimensions dims{};

    glm::ivec2 fieldToMap(const glm::vec2& fieldPosition) const;
    glm::vec2 mapToField(const glm::ivec2& cellPosition) const;

    std::uint64_t zOffset(const glm::ivec2& cellPosition) const;
};

} // namespace topology_core

