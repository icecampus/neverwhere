#pragma once
#include <QObject>
#include <QtQml/qqml.h>
#include <glm/glm.hpp>
#include "math/lib.h"
#include "topology_common.h"


//diamond_dimensions
struct diamond_dimensions
{
    Q_GADGET;
    QML_NAMED_ELEMENT(diamond_dimensions)
    Q_PROPERTY(float cellWidth READ getCellWidth CONSTANT);
    Q_PROPERTY(float aspectRatio READ getAspectRatio CONSTANT);
    Q_PROPERTY(math::vec2 cellSize READ cellSize CONSTANT);

public:
    diamond_dimensions(const float& cellWidth, float aspectRatio);

    math::vec2 cellSize() const;
    float getCellWidth() const { return cellWidth; }
    float getAspectRatio() const { return aspectRatio; }

    bool operator==(const diamond_dimensions&) const = default;

    float cellWidth;
    float aspectRatio{ 2.0 };
};


//DiamondIsometry
// Pure isometric projection: a cartesian (cx, cy) grid rendered as diamonds.
// No even/odd row stagger — every row shares the same X mapping, so any
// affine op (pan, rotate field 90°, ...) is trivial.
//
//   cellCenter(cx, cy) world = ((cx - cy) * halfW + halfW,
//                               (cx + cy) * halfH + halfH)
//   where halfW = cellWidth/2, halfH = cellHeight/2.
class DiamondIsometry: public QObject
{
    Q_OBJECT
    // NOTE: the gadget `dimensions` is exposed as a Q_PROPERTY for parity with
    // the C++ side, but QML must NOT read it through `isoView.dimensions.X`
    // chains. Reading a gadget-property in QML goes through
    // QQmlValueTypeWrapper::readReference, which under Qt 6.11 invokes
    // qt_static_metacall with argv[0]==nullptr (Qt regression; also tracked
    // in MuseScale #33015). Use the scalar mirror properties below
    // (cellWidth / cellHeight / cellSizeX / cellSizeY) from QML instead —
    // scalar reads do not involve value-type wrapping and are crash-free.
    Q_PROPERTY(diamond_dimensions dimensions READ getDimensions CONSTANT)
    Q_PROPERTY(float cellWidth READ getCellWidth CONSTANT)
    Q_PROPERTY(float cellHeight READ getCellHeight CONSTANT)
    Q_PROPERTY(float cellSizeX READ getCellSizeX CONSTANT)
    Q_PROPERTY(float cellSizeY READ getCellSizeY CONSTANT)
    Q_PROPERTY(float aspectRatio READ getAspectRatio CONSTANT)
public:
    using Neighbours = std::array<math::ivec2, 4>;


    DiamondIsometry(QObject* parent = nullptr);

    diamond_dimensions getDimensions() const { return dimensions; }
    // Scalar mirrors for safe QML access (see Q_PROPERTY note above).
    float getCellWidth() const { return dimensions.cellWidth; }
    float getCellHeight() const { return dimensions.cellWidth / dimensions.aspectRatio; }
    float getCellSizeX() const { return dimensions.cellSize().x; }
    float getCellSizeY() const { return dimensions.cellSize().y; }
    float getAspectRatio() const { return dimensions.aspectRatio; }

    Q_INVOKABLE math::ivec2 fieldToMap(const math::vec2& fieldPosition) const;
    Q_INVOKABLE math::vec2  mapToField(const math::ivec2& cellPosition) const;
    Q_INVOKABLE uint64_t zOffset(const math::ivec2& cellPosition);

    virtual VisibleRegion getVisibleCellBounds(const math::vec2& viewSize, const math::vec2& cameraOffset) const;

    math::ivec2 fieldToNode(const math::vec2& fieldPosition) const;
    static Neighbours nodeNeighboursCell(const math::ivec2& nodePosition);


    diamond_dimensions dimensions;
};

//DiamondIsometryView
class DiamondIsometryView:  public DiamondIsometry
{
    Q_OBJECT

    Q_PROPERTY(float cameraX READ getCameraX WRITE setCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(float cameraY READ getCameraY WRITE setCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(float cameraZoom READ getCameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)

public:
    DiamondIsometryView(QObject* parent = nullptr);

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
