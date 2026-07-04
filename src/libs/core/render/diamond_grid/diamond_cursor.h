#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include "topology/diamond_isometry.h"

class DiamondCursor : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(math::ivec2 mapPosition READ mapPosition WRITE setMapPosition NOTIFY mapPositionChanged);
    Q_PROPERTY(DiamondIsometryView* topology READ getTopology WRITE setTopology NOTIFY topologyChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    DiamondCursor(QQuickItem* parent = nullptr);

    math::ivec2 mapPosition() const;
    void setMapPosition(const math::ivec2& newPosition);

    DiamondIsometryView* getTopology() const;
    void setTopology(DiamondIsometryView* topology);

    QColor color() const;
    void setColor(const QColor& newColor);

signals:
    void mapPositionChanged();
    void topologyChanged();
    void colorChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:

    DiamondIsometryView* _topology = nullptr;
    QColor _color = Qt::blue;

    math::ivec2 _pos{ 0,0 };
};

