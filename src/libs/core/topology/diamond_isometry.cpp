#include "diamond_isometry.h"

diamond_dimensions::diamond_dimensions(const float& cellWidth_, float aspectRatio_) :
    cellWidth(cellWidth_),
    aspectRatio(aspectRatio_)
{
}


math::vec2 diamond_dimensions::cellSize() const
{
    return math::vec2{ cellWidth, cellWidth / aspectRatio };
}


//DiamondIsometry
DiamondIsometry::DiamondIsometry(QObject* parent):
    QObject(parent),
    dimensions({ 128, 2.0f })
{

}

math::ivec2 DiamondIsometry::fieldToMap(const math::vec2& fieldPosition) const
{
    // Diamond (cartesian) projection — the exact inverse of mapToField:
    //   halfW = cellWidth/2, halfH = cellHeight/2
    //   world.x = (cx - cy) * halfW + halfW
    //   world.y = (cx + cy) * halfH + halfH
    // Inverting for (cx, cy):
    //   cx = (world.x / halfW + world.y / halfH) / 2 - 0.5
    //   cy = (world.y / halfH - world.x / halfW) / 2 - 0.5
    //
    // Rounding each coordinate independently to the nearest integer cell is
    // equivalent to snapping to the nearest diamond center (L1 Voronoi cell),
    // so no two-candidate hit-test is needed (contrast with staggered).
    const math::vec2 cellSz = dimensions.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float sx = fieldPosition.x / halfW;
    const float sy = fieldPosition.y / halfH;

    const float cx = (sx + sy) * 0.5f - 0.5f;
    const float cy = (sy - sx) * 0.5f - 0.5f;

    return math::ivec2(static_cast<int>(std::round(cx)), static_cast<int>(std::round(cy)));
}

math::vec2 DiamondIsometry::mapToField(const math::ivec2& cellPosition) const
{
    // Diamond center in world space — uniform for every cell, no row parity.
    const math::vec2 cellSz = dimensions.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const float cx = static_cast<float>(cellPosition.x);
    const float cy = static_cast<float>(cellPosition.y);

    const float x = (cx - cy) * halfW + halfW;
    const float y = (cx + cy) * halfH + halfH;

    return math::vec2(x, y);
}

uint64_t DiamondIsometry::zOffset(const math::ivec2& cellPosition)
{
     return (static_cast<uint64_t>(cellPosition.y) << 32) | cellPosition.x;
}


VisibleRegion DiamondIsometry::getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const
{
    // For the diamond grid, the visible region is the AABB of the four screen
    // corners in cell space. Since fieldToMap is now a clean affine map (no
    // two-candidate ambiguity), sampling just the four corners plus a margin
    // is enough.
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
        math::ivec2 cell = fieldToMap(point);
        minCell.x = std::min(minCell.x, cell.x);
        minCell.y = std::min(minCell.y, cell.y);
        maxCell.x = std::max(maxCell.x, cell.x);
        maxCell.y = std::max(maxCell.y, cell.y);
    }

    minCell -= math::ivec2(1, 1);
    maxCell += math::ivec2(1, 1);

    return VisibleRegion( minCell, maxCell );
}


math::ivec2 DiamondIsometry::fieldToNode(const math::vec2& fieldPosition) const
{
    // A node is the vertex shared by 4 cells, offset by half a cell along the
    // world Y axis from the cell center (same geometric meaning as in the
    // staggered version: nodes sit at diamond corners).
    math::vec2 delta(0, dimensions.cellSize().y / 2);
    return fieldToMap(fieldPosition + delta);
}

DiamondIsometry::Neighbours DiamondIsometry::nodeNeighboursCell(const math::ivec2& nodePosition)
{
    // Cartesian: a node is shared by its 4 orthogonal neighbours. No parity,
    // one table for every node.
    static const Neighbours mask = { { math::ivec2(0, 0),
                                       math::ivec2(-1, 0),
                                       math::ivec2(0, -1),
                                       math::ivec2(-1, -1) } };

    Neighbours result;
    for (size_t i = 0; i < mask.size(); ++i)
    {
        result[i] = nodePosition + mask[i];
    }

    return result;
}

//DiamondIsometryView
DiamondIsometryView::DiamondIsometryView(QObject* parent) :
    DiamondIsometry(parent),
    m_cameraX(0.0f),
    m_cameraY(0.0f),
    m_cameraZoom(1.0f)
{
}


math::ivec2 DiamondIsometryView::screenToMap(const math::vec2& screenPosition) const
{

    math::vec2 adjustedPos = (screenPosition - math::vec2(m_cameraX, m_cameraY)) / m_cameraZoom;
    return fieldToMap(adjustedPos);
}

math::vec2 DiamondIsometryView::mapToScreen(const math::ivec2& cellPosition) const
{
    math::vec2 screenPos = mapToField(cellPosition);
    return screenPos * m_cameraZoom + math::vec2(m_cameraX, m_cameraY);
}

VisibleRegion DiamondIsometryView::getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const
{
    math::vec2 adjustedCameraOffset = (cameraOffset - math::vec2(m_cameraX, m_cameraY)) / m_cameraZoom;
    math::vec2 adjustedViewSize = viewSize / m_cameraZoom;
    return DiamondIsometry::getVisibleCellBounds(adjustedViewSize, adjustedCameraOffset);
}

math::ivec2 DiamondIsometryView::screendToNode(const math::vec2& screenPosition) const
{
    math::vec2 adjustedPos = (screenPosition - math::vec2(m_cameraX, m_cameraY)) / m_cameraZoom;
    return fieldToNode(adjustedPos);
}

float DiamondIsometryView::getCameraX() const
{
    return m_cameraX;
}

float DiamondIsometryView::getCameraY() const
{
    return m_cameraY;
}

float DiamondIsometryView::getCameraZoom() const
{
    return m_cameraZoom;
}

void DiamondIsometryView::setCameraX(float x)
{
    if (m_cameraX != x)
    {
        m_cameraX = x;
        emit cameraXChanged();
    }
}

void DiamondIsometryView::setCameraY(float y)
{
    if (m_cameraY != y)
    {
        m_cameraY = y;
        emit cameraYChanged();
    }
}

void DiamondIsometryView::setCameraZoom(float zoom)
{
    if (m_cameraZoom != zoom) {
        m_cameraZoom = zoom;
        emit cameraZoomChanged();
    }
}
