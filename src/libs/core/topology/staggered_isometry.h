#pragma once
#include <glm/glm.hpp>

struct StaggeredDimensions
{
    StaggeredDimensions(const float& cellWidth, float aspectRatio);

    glm::vec2 cellSize() const;

    bool operator==(const StaggeredDimensions&) const = default;

    float cellWidth;
    float aspectRatio{ 2.0 }; 
};


struct StaggeredIsometry
{

    StaggeredIsometry(const StaggeredDimensions& dimensions);

    glm::ivec2 screenToMap(const glm::vec2& screenPosition) const;
    glm::vec2    mapToScreen(const glm::ivec2& cellPosition) const;

    StaggeredDimensions dimensions;
};