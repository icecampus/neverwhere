#pragma once
#include "assets_library/tools/tool.h"

class Building3dPencil: public Tool
{
public:
    Building3dPencil(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;
};

class Building3dEraser: public Tool
{
public:
    Building3dEraser(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;
};
