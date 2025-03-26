#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include "topology/staggered_isometry.h"

class StaggeredCursor : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(math::ivec2 mapPosition READ mapPosition WRITE setMapPosition NOTIFY mapPositionChanged);
    Q_PROPERTY(StaggeredIsometryView* topology READ getTopology WRITE setTopology NOTIFY topologyChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    StaggeredCursor(QQuickItem* parent = nullptr);

    math::ivec2 mapPosition() const;
    void setMapPosition(const math::ivec2& newPosition);

    StaggeredIsometryView* getTopology() const;
    void setTopology(StaggeredIsometryView* topology);

    QColor color() const;
    void setColor(const QColor& newColor);

signals:
    void mapPositionChanged();
    void topologyChanged();
    void colorChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:

    StaggeredIsometryView* _topology = nullptr;
    QColor _color = Qt::blue;

    math::ivec2 _pos{ 0,0 };
};

