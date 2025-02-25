#pragma once
#include <glm/glm.hpp>
#include "math/lib.h"

//StaggeredDimensions
struct StaggeredDimensions
{
    StaggeredDimensions(const float& cellWidth, float aspectRatio);

    math::vec2 cellSize() const;

    bool operator==(const StaggeredDimensions&) const = default;

    float cellWidth;
    float aspectRatio{ 2.0 }; 
};

//StaggeredIsometry
class StaggeredIsometry 
{
    
public:
    struct VisibleRegion
    {
        math::ivec2 min;
        math::ivec2 max;
    };

    StaggeredIsometry(const StaggeredDimensions& dimensions);

    virtual math::ivec2 screenToMap(const math::vec2& screenPosition) const;
    virtual math::vec2  mapToScreen(const math::ivec2& cellPosition) const;

    virtual VisibleRegion getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const;

    StaggeredDimensions dimensions;
};

//StaggeredIsometryView
class StaggeredIsometryView: public QObject, public StaggeredIsometry
{
    Q_OBJECT

    Q_PROPERTY(float cameraX READ getCameraX WRITE setCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(float cameraY READ getCameraY WRITE setCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(float cameraZoom READ getCameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)

public:
    StaggeredIsometryView(const StaggeredDimensions& dimensions, QObject* parent = nullptr): 
        QObject(parent),
        StaggeredIsometry(dimensions), m_cameraX(0.0f), m_cameraY(0.0f), m_cameraZoom(1.0f) 
    {
    }


    float getCameraX() const;
    void setCameraX(float x);

    float getCameraY() const;
    void setCameraY(float y);

    float getCameraZoom() const;
    void setCameraZoom(float zoom);
    
    Q_INVOKABLE math::ivec2 screenToMap(const math::vec2& screenPosition) const override;
    Q_INVOKABLE math::vec2 mapToScreen(const math::ivec2& cellPosition) const override;
    VisibleRegion getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const override;


signals:
    void cameraXChanged();
    void cameraYChanged();
    void cameraZoomChanged();

private:
    float m_cameraX;
    float m_cameraY;
    float m_cameraZoom;
};