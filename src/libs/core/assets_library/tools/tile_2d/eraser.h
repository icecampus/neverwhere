#pragma once
#include "assets_library/tools/tool.h"

class Eraser: public Tool
{
public:
    Eraser(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso) override;

};