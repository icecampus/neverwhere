#pragma once
#include "assets_library/tools/tool.h"

class Cursor: public Tool
{
public:
    Cursor(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso) override;

};