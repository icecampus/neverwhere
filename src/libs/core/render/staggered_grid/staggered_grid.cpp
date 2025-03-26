#include "staggered_grid.h"


StaggeredGrid::StaggeredGrid(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QSize StaggeredGrid::size() const 
{ 
    return _size; 
}

void StaggeredGrid::setSize(const QSize& newSize)
{
    QSize corrected = newSize;
    corrected.setWidth(std::max(1, newSize.width()));
    corrected.setHeight(std::max(1, newSize.height()));

    if (_size != corrected) 
    {
        _size = corrected;
        emit sizeChanged();
        update();
    }
}

StaggeredIsometryView* StaggeredGrid::getTopology() const
{
    return _topology;
}

void StaggeredGrid::setTopology(StaggeredIsometryView* topology_)
{
    if (topology_ != _topology)
    {
        _topology = topology_;
        emit topologyChanged();
        update();
    }
}

QColor StaggeredGrid::color() const
{
    return _color;
}

void StaggeredGrid::setColor(const QColor& newColor)
{
    if (_color == newColor)
        return;
    _color = newColor;
    emit colorChanged();
    update();
}

QSGNode* StaggeredGrid::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
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
        int bufferSize = _size.width() * _size.height() * pointInCell;

        geometry->allocate(bufferSize);
        QSGGeometry::Point2D* v = geometry->vertexDataAsPoint2D();

        float halfCellWidthInPixel = _topology->dimensions.cellSize().x / 2;
        float halfCellHeightInPixel = _topology->dimensions.cellSize().y /2 ;

        for (int i = 0; i < _size.width(); ++i)
        {
            for (int j = 0; j < _size.height(); ++j)
            {
                math::vec2 pos = _topology->mapToField({i, j});
                float x = pos.x;
                float y = pos.y;

                int baseIndex = (i * _size.height() + j) * pointInCell;

                v[baseIndex + 0].set(x - halfCellWidthInPixel, y);
                v[baseIndex + 1].set(x, y - halfCellHeightInPixel);

                v[baseIndex + 2].set(x, y - halfCellHeightInPixel);
                v[baseIndex + 3].set(x + halfCellWidthInPixel, y);

                v[baseIndex + 4].set(x + halfCellWidthInPixel, y);
                v[baseIndex + 5].set(x, y + halfCellHeightInPixel);

                v[baseIndex + 6].set(x, y + halfCellHeightInPixel);
                v[baseIndex + 7].set(x - halfCellWidthInPixel, y);
            }
        }

        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);

    }


    return node;
}