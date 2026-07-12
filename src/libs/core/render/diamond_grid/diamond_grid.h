#pragma once
#include <QQuickItem>
#include <QSizeF>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include "topology/diamond_isometry.h"

// Draws the diamond grid lines for the editor's visible viewport.
//
// Unlike the previous fixed (0,0)..(200,200) rectangle, this version reads
// the visible screen size via `viewSize` and queries the topology for the
// cell-coordinate AABB of that viewport (DiamondIsometryView::
// getVisibleCellBounds). The grid is then drawn only for cells inside that
// AABB, so it is effectively unbounded — panning or zooming just shifts /
// shrinks the AABB, and the grid follows the camera.
//
// The item draws in WORLD space (via topology->mapToField); the QML side
// applies the camera transform via `transform: [Scale, Translate]`.
class DiamondGrid : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QSizeF viewSize READ viewSize WRITE setViewSize NOTIFY viewSizeChanged)
    Q_PROPERTY(DiamondIsometryView* topology READ getTopology WRITE setTopology NOTIFY topologyChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    DiamondGrid(QQuickItem* parent = nullptr);

    QSizeF viewSize() const;
    void setViewSize(const QSizeF& newViewSize);

    DiamondIsometryView* getTopology() const;
    void setTopology(DiamondIsometryView* topology);

    QColor color() const;
    void setColor(const QColor& newColor);


signals:
    void viewSizeChanged();
    void colorChanged();
    void topologyChanged();


protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;

private:

    QSizeF _viewSize = QSizeF(0, 0);
    DiamondIsometryView* _topology = nullptr;
    QColor _color = Qt::blue;
};
