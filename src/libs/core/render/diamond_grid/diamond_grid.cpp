#include "diamond_grid.h"


DiamondGrid::DiamondGrid(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QSizeF DiamondGrid::viewSize() const
{
    return _viewSize;
}

void DiamondGrid::setViewSize(const QSizeF& newViewSize)
{
    if (_viewSize != newViewSize)
    {
        _viewSize = newViewSize;
        emit viewSizeChanged();
        update();
    }
}

DiamondIsometryView* DiamondGrid::getTopology() const
{
    return _topology;
}

void DiamondGrid::setTopology(DiamondIsometryView* topology)
{
    if (_topology == topology)
        return;

    if (_topology)
    {
        // Drop any camera-change subscriptions from the previous topology.
        disconnect(_topology, nullptr, this, nullptr);
    }

    _topology = topology;

    if (_topology)
    {
        // The grid AABB is derived from the topology's camera state, so any
        // camera move / zoom must trigger a repaint. The QML `transform`
        // handles the actual on-screen offset; we only need to recompute the
        // cell range that falls inside the viewport.
        connect(_topology, &DiamondIsometryView::cameraXChanged, this, [this]() { update(); });
        connect(_topology, &DiamondIsometryView::cameraYChanged, this, [this]() { update(); });
        connect(_topology, &DiamondIsometryView::cameraZoomChanged, this, [this]() { update(); });
    }

    emit topologyChanged();
    update();
}

QColor DiamondGrid::color() const
{
    return _color;
}

void DiamondGrid::setColor(const QColor& newColor)
{
    if (_color == newColor)
        return;
    _color = newColor;
    emit colorChanged();
    update();
}

QSGNode* DiamondGrid::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    QSGGeometryNode* node = nullptr;
    QSGGeometry* geometry = nullptr;

    if (!oldNode)
    {
        node = new QSGGeometryNode;
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0, 0, QSGGeometry::UnsignedIntType);

        geometry->setDrawingMode(QSGGeometry::DrawLines);
        geometry->setLineWidth(1);

        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);

        auto material = new QSGFlatColorMaterial;
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    else
    {
        node = static_cast<QSGGeometryNode*>(oldNode);
        geometry = node->geometry();
    }

    if (!_topology || _viewSize.isEmpty())
    {
        // Nothing to draw — drop any previously allocated geometry so we
        // don't render stale lines from the last viewport.
        geometry->allocate(0);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
        return node;
    }

    QSGFlatColorMaterial* material = static_cast<QSGFlatColorMaterial*>(node->material());
    material->setColor(_color);

    // Convert the screen-space viewport into a cell-coordinate AABB.
    // cameraOffset is (0,0): DiamondIsometryView::getVisibleCellBounds
    // already subtracts its own m_cameraX/Y/Zoom internally, so the caller
    // passes the raw screen view size + a zero offset.
    const VisibleRegion region = _topology->getVisibleCellBounds(
        math::vec2(_viewSize.width(), _viewSize.height()),
        math::vec2(0.0f, 0.0f));

    const int minCx = region.min.x;
    const int minCy = region.min.y;
    const int maxCx = region.max.x;
    const int maxCy = region.max.y;

    // Defensive cap: a wildly zoomed-out view (e.g. zoom = 0.01) would
    // otherwise produce millions of line endpoints and OOM the GPU vertex
    // buffer. 256x256 cells = ~524K vertices = a few MB, plenty for any
    // practical editor window; if the viewport really is larger, the user
    // cannot distinguish individual cells anyway.
    constexpr int kMaxCellsPerAxis = 256;
    int cellsX = maxCx - minCx + 1;
    int cellsY = maxCy - minCy + 1;
    if (cellsX > kMaxCellsPerAxis) cellsX = kMaxCellsPerAxis;
    if (cellsY > kMaxCellsPerAxis) cellsY = kMaxCellsPerAxis;

    const int pointInCell = 8;
    const int bufferSize = cellsX * cellsY * pointInCell;

    geometry->allocate(bufferSize);
    QSGGeometry::Point2D* v = geometry->vertexDataAsPoint2D();

    const float halfCellWidthInPixel = _topology->dimensions.cellSize().x / 2;
    const float halfCellHeightInPixel = _topology->dimensions.cellSize().y / 2;

    int vertex = 0;
    for (int i = 0; i < cellsX; ++i)
    {
        const int cx = minCx + i;
        for (int j = 0; j < cellsY; ++j)
        {
            const int cy = minCy + j;

            const math::vec2 pos = _topology->mapToField({cx, cy});
            const float x = pos.x;
            const float y = pos.y;

            v[vertex + 0].set(x - halfCellWidthInPixel, y);
            v[vertex + 1].set(x, y - halfCellHeightInPixel);

            v[vertex + 2].set(x, y - halfCellHeightInPixel);
            v[vertex + 3].set(x + halfCellWidthInPixel, y);

            v[vertex + 4].set(x + halfCellWidthInPixel, y);
            v[vertex + 5].set(x, y + halfCellHeightInPixel);

            v[vertex + 6].set(x, y + halfCellHeightInPixel);
            v[vertex + 7].set(x - halfCellWidthInPixel, y);

            vertex += pointInCell;
        }
    }

    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    return node;
}
