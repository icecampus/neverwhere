#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include "topology/staggered_isometry.h"

class StaggeredGrid : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QSize size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(StaggeredIsometryView* topology READ getTopology WRITE setTopology NOTIFY topologyChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    StaggeredGrid(QQuickItem* parent = nullptr);

    QSize size() const;
    void setSize(const QSize& newSize);

    StaggeredIsometryView* getTopology() const;
    void setTopology(StaggeredIsometryView* topology);

    QColor color() const;
    void setColor(const QColor& newColor);


signals:
    void sizeChanged();
    void colorChanged();
    void topologyChanged();
    

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:
    
    QSize _size = QSize(1, 1);
    StaggeredIsometryView* _topology = nullptr;
    QColor _color = Qt::blue;
};