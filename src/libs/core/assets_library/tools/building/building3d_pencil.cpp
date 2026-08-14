#include "building3d_pencil.h"

#include "map/map_authoring.h"

Building3dPencil::Building3dPencil(QObject* parent):
    Tool("Pencil", "pencil", parent)
{
}

void Building3dPencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    (void)ctrlModifier;
    (void)shiftModifier;
    (void)altModifier;
    MapAuthoring::setTile(*layerModel, iso->screenToMap(math::vec2(screenPos.x(), screenPos.y())), currentAsset);
}

Building3dEraser::Building3dEraser(QObject* parent):
    Tool("Eraser", "eraser", parent)
{
}

void Building3dEraser::click(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    (void)currentAsset;
    (void)ctrlModifier;
    (void)shiftModifier;
    (void)altModifier;
    MapAuthoring::eraseBuildingAt(*layerModel, iso->screenToMap(math::vec2(screenPos.x(), screenPos.y())));
}
