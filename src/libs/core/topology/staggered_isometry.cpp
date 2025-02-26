#include "staggered_isometry.h"

staggered_dimensions::staggered_dimensions(const float& cellWidth_, float aspectRatio_) :
    cellWidth(cellWidth_), 
    aspectRatio(aspectRatio_)
{
}


math::vec2 staggered_dimensions::cellSize() const
{ 
    return math::vec2{ cellWidth, cellWidth / aspectRatio };
}


//StaggeredIsometry
StaggeredIsometry::StaggeredIsometry(const staggered_dimensions& dimensions_, QObject* parent):
    QObject(parent),
    dimensions(dimensions_) 
{

}


math::ivec2 StaggeredIsometry::screenToMap(const math::vec2& screenPosition) const
{
    const math::vec2 cellSz = dimensions.cellSize();

    const math::vec2 normalizedPos = screenPosition / cellSz;

    int baseX = static_cast<int>(std::floor(normalizedPos.x));
    int baseY = static_cast<int>(std::floor(normalizedPos.y)) * 2;

    const math::vec2 remainder = normalizedPos - math::vec2(baseX, baseY * 0.5f);

    math::ivec2 cellPos(baseX, baseY);
    const float manhattanDist = std::abs(remainder.x - 0.5f) + std::abs(remainder.y - 0.5f);

    if (manhattanDist > 0.5f) {
        int offsetX = static_cast<int>(std::floor(normalizedPos.x - 0.5f));
        int offsetY = (static_cast<int>(std::floor(normalizedPos.y - 0.5f)) * 2) + 1;

        const math::vec2 offsetRemainder = normalizedPos - math::vec2(offsetX + 0.5f, (offsetY * 0.5f) + 0.5f);
        const float offsetDist = std::abs(offsetRemainder.x) + std::abs(offsetRemainder.y);

        if (offsetDist <= 0.5f) {
            cellPos = glm::ivec2(offsetX, offsetY);
        }
    }

    return (cellPos.x >= 0 && cellPos.y >= 0) ? cellPos : glm::ivec2(-1, -1);
}

math::vec2 StaggeredIsometry::mapToScreen(const math::ivec2& cellPosition) const
{
    const math::vec2 cellSz = dimensions.cellSize();
    const math::vec2 halfCellSz = cellSz * 0.5f;

    float x = static_cast<float>(cellPosition.x) * cellSz.x + halfCellSz.x;
    float y = static_cast<float>(cellPosition.y) * halfCellSz.y + halfCellSz.y;
    
    if (cellPosition.y & 1) 
    {  
        x += halfCellSz.x;
    }

    return math::vec2(x, y);
}


VisibleRegion StaggeredIsometry::getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const
{
    std::vector<math::vec2> screenPoints;
    
    screenPoints.push_back(cameraOffset);
    screenPoints.push_back(math::vec2(cameraOffset.x + viewSize.x, cameraOffset.y));
    screenPoints.push_back(math::vec2(cameraOffset.x, cameraOffset.y + viewSize.y));
    screenPoints.push_back(cameraOffset + viewSize);
    
    screenPoints.push_back(cameraOffset + math::vec2(viewSize.x * 0.5f, 0.0f));
    screenPoints.push_back(cameraOffset + math::vec2(viewSize.x, viewSize.y * 0.5f));
    screenPoints.push_back(cameraOffset + math::vec2(viewSize.x * 0.5f, viewSize.y));
    screenPoints.push_back(cameraOffset + math::vec2(0.0f, viewSize.y * 0.5f));
    
    screenPoints.push_back(cameraOffset + viewSize * 0.5f);

    math::ivec2 minCell(std::numeric_limits<int>::max());
    math::ivec2 maxCell(std::numeric_limits<int>::min());

    for (const auto& point : screenPoints)
    {
        math::ivec2 cell = screenToMap(point);
        minCell.x = std::min(minCell.x, cell.x);
        minCell.y = std::min(minCell.y, cell.y);
        maxCell.x = std::max(maxCell.x, cell.x);
        maxCell.y = std::max(maxCell.y, cell.y);
    }
    
    minCell -= math::ivec2(1, 1);
    maxCell += math::ivec2(1, 1);

    return VisibleRegion( minCell, maxCell );
}


//
math::ivec2 StaggeredIsometryView::screenToMap(const math::vec2& screenPosition) const
{

    math::vec2 adjustedPos = (screenPosition - math::vec2(m_cameraX, m_cameraY)) / m_cameraZoom;
    return StaggeredIsometry::screenToMap(adjustedPos);
}

math::vec2 StaggeredIsometryView::mapToScreen(const math::ivec2& cellPosition) const
{
    math::vec2 screenPos = StaggeredIsometry::mapToScreen(cellPosition);
    return screenPos * m_cameraZoom + math::vec2(m_cameraX, m_cameraY);
}

VisibleRegion StaggeredIsometryView::getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const
{
    math::vec2 adjustedCameraOffset = (cameraOffset - math::vec2(m_cameraX, m_cameraY)) / m_cameraZoom;
    math::vec2 adjustedViewSize = viewSize / m_cameraZoom;
    return StaggeredIsometry::getVisibleCellBounds(adjustedViewSize, adjustedCameraOffset);
}

float StaggeredIsometryView::getCameraX() const 
{ 
    return m_cameraX; 
}

float StaggeredIsometryView::getCameraY() const 
{ 
    return m_cameraY; 
}

float StaggeredIsometryView::getCameraZoom() const 
{ 
    return m_cameraZoom; 
}

void StaggeredIsometryView::setCameraX(float x) 
{
    if (m_cameraX != x) 
    {
        m_cameraX = x;
        emit cameraXChanged();
    }
}

void StaggeredIsometryView::setCameraY(float y) 
{
    if (m_cameraY != y) 
    {
        m_cameraY = y;
        emit cameraYChanged();
    }
}

void StaggeredIsometryView::setCameraZoom(float zoom) 
{
    if (m_cameraZoom != zoom) {
        m_cameraZoom = zoom;
        emit cameraZoomChanged();
    }
}
