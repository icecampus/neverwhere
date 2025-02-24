#include "staggered_isometry.h"

StaggeredDimensions::StaggeredDimensions(const float& cellWidth_, float aspectRatio_) :
    cellWidth(cellWidth_), 
    aspectRatio(aspectRatio_)
{
}


glm::vec2 StaggeredDimensions::cellSize() const
{ 
    return glm::vec2{ cellWidth, cellWidth / aspectRatio }; 
}


//StaggeredIsometry
StaggeredIsometry::StaggeredIsometry(const StaggeredDimensions& dimensions_):
    dimensions(dimensions_)
{

}

glm::ivec2 StaggeredIsometry::screenToMap(const glm::vec2& screenPosition) const
{
    const glm::vec2 cellSz = dimensions.cellSize();

    const glm::vec2 normalizedPos = screenPosition / cellSz;

    int baseX = static_cast<int>(std::floor(normalizedPos.x));
    int baseY = static_cast<int>(std::floor(normalizedPos.y)) * 2;

    const glm::vec2 remainder = normalizedPos - glm::vec2(baseX, baseY * 0.5f);

    glm::ivec2 cellPos(baseX, baseY);
    const float manhattanDist = std::abs(remainder.x - 0.5f) + std::abs(remainder.y - 0.5f);

    if (manhattanDist > 0.5f) {
        int offsetX = static_cast<int>(std::floor(normalizedPos.x - 0.5f));
        int offsetY = (static_cast<int>(std::floor(normalizedPos.y - 0.5f)) * 2) + 1;

        const glm::vec2 offsetRemainder = normalizedPos - glm::vec2(offsetX + 0.5f, (offsetY * 0.5f) + 0.5f);
        const float offsetDist = std::abs(offsetRemainder.x) + std::abs(offsetRemainder.y);

        if (offsetDist <= 0.5f) {
            cellPos = glm::ivec2(offsetX, offsetY);
        }
    }

    return (cellPos.x >= 0 && cellPos.y >= 0) ? cellPos : glm::ivec2(-1, -1);
}

glm::vec2 StaggeredIsometry::mapToScreen(const glm::ivec2& cellPosition) const
{
    const glm::vec2 cellSz = dimensions.cellSize();
    const glm::vec2 halfCellSz = cellSz * 0.5f;

    float x = static_cast<float>(cellPosition.x) * cellSz.x + halfCellSz.x;
    float y = static_cast<float>(cellPosition.y) * halfCellSz.y + halfCellSz.y;
    
    if (cellPosition.y & 1) 
    {  
        x += halfCellSz.x;
    }

    return glm::vec2(x, y);
}
