#include "staggered_cursor.h"

StaggeredCursor::StaggeredCursor(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

math::ivec2 StaggeredCursor::mapPosition() const
{
    return _pos;
}

void StaggeredCursor::setMapPosition(const math::ivec2& newPosition)
{
    if (_pos != newPosition) 
    {
        _pos = newPosition;
        emit mapPositionChanged();
        update(); 
    }
}

StaggeredIsometryView* StaggeredCursor::getTopology() const
{
    return _topology;
}

void StaggeredCursor::setTopology(StaggeredIsometryView* topology_)
{
    if (topology_ != _topology)
    {
        _topology = topology_;
        emit topologyChanged();
        update();
    }
}

QColor StaggeredCursor::color() const
{
    return _color;
}

void StaggeredCursor::setColor(const QColor& newColor)
{
    if (_color == newColor)
        return;
    _color = newColor;
    emit colorChanged();
    update();
}

QSGNode* StaggeredCursor::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
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

    if (_topology)
    {
        QSGFlatColorMaterial* material = static_cast<QSGFlatColorMaterial*>(node->material());
        material->setColor(_color);

        int pointInCell = 8;
        int bufferSize = pointInCell;

        geometry->allocate(bufferSize);
        QSGGeometry::Point2D* v = geometry->vertexDataAsPoint2D();

        float halfCellWidthInPixel = _topology->dimensions.cellSize().x / 2;
        float halfCellHeightInPixel = _topology->dimensions.cellSize().y / 2;

        math::vec2 pos = _topology->mapToField(_pos);
        float x = pos.x;
        float y = pos.y;

        v[0].set(x - halfCellWidthInPixel, y);
        v[1].set(x, y - halfCellHeightInPixel);

        v[2].set(x, y - halfCellHeightInPixel);
        v[3].set(x + halfCellWidthInPixel, y);

        v[4].set(x + halfCellWidthInPixel, y);
        v[5].set(x, y + halfCellHeightInPixel);

        v[6].set(x, y + halfCellHeightInPixel);
        v[7].set(x - halfCellWidthInPixel, y);

        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    }


    return node;
}