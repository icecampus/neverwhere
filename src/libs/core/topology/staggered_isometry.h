#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <glm/glm.hpp>
#include "math/lib.h"


//StaggeredDimensions
struct staggered_dimensions
{
    Q_GADGET;
    QML_NAMED_ELEMENT(staggered_dimensions)
    Q_PROPERTY(float cellWidth READ getCellWidth CONSTANT);
    Q_PROPERTY(float aspectRatio READ getAspectRatio CONSTANT);
    Q_PROPERTY(math::vec2 cellSize READ cellSize CONSTANT);

public:
    staggered_dimensions(const float& cellWidth, float aspectRatio);

    math::vec2 cellSize() const;
    float getCellWidth() const { return cellWidth; }
    float getAspectRatio() const { return aspectRatio; }

    bool operator==(const staggered_dimensions&) const = default;

    float cellWidth;
    float aspectRatio{ 2.0 }; 
};

//VisibleRegion
struct VisibleRegion
{
    Q_GADGET;
    Q_PROPERTY(math::ivec2 min READ getMin CONSTANT);
    Q_PROPERTY(math::ivec2 max READ getMax CONSTANT);
public:
    VisibleRegion() : min(0), max(0) {}
    VisibleRegion(const math::ivec2& minPos, const math::ivec2& maxPos)
        : min(minPos), max(maxPos) {
    }

    math::ivec2 getMin() const { return min; }
    math::ivec2 getMax() const { return max; }
    math::ivec2 min;
    math::ivec2 max;
};


//StaggeredIsometry
class StaggeredIsometry: public QObject
{
    Q_OBJECT
    Q_PROPERTY(staggered_dimensions dimensions READ getDimensions CONSTANT)
public:
    using Neighbours = std::array<math::ivec2, 4>;


    StaggeredIsometry(QObject* parent = nullptr);

    staggered_dimensions getDimensions() const { return dimensions; }

    Q_INVOKABLE math::ivec2 fieldToMap(const math::vec2& fieldPosition) const;
    Q_INVOKABLE math::vec2  mapToField(const math::ivec2& cellPosition) const;
    Q_INVOKABLE uint64_t zOffset(const math::ivec2& cellPosition);

    virtual VisibleRegion getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const;

    math::ivec2 fieldToNode(const math::vec2& fieldPosition) const;
    static Neighbours nodeNeighboursCell(const math::ivec2& nodePosition);


    staggered_dimensions dimensions;
};

//StaggeredIsometryView
class StaggeredIsometryView:  public StaggeredIsometry
{
    Q_OBJECT

    Q_PROPERTY(float cameraX READ getCameraX WRITE setCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(float cameraY READ getCameraY WRITE setCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(float cameraZoom READ getCameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)

public:
    StaggeredIsometryView(QObject* parent = nullptr);

    float getCameraX() const;
    void setCameraX(float x);

    float getCameraY() const;
    void setCameraY(float y);

    float getCameraZoom() const;
    void setCameraZoom(float zoom);
    
    Q_INVOKABLE math::ivec2 screenToMap(const math::vec2& screenPosition) const;
    Q_INVOKABLE math::vec2 mapToScreen(const math::ivec2& cellPosition) const;
    Q_INVOKABLE VisibleRegion getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const override;

    math::ivec2 screendToNode(const math::vec2& screenPosition) const;


signals:
    void cameraXChanged();
    void cameraYChanged();
    void cameraZoomChanged();

private:
    float m_cameraX;
    float m_cameraY;
    float m_cameraZoom;
};