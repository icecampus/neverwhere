#pragma once
#include "tool.h"

class Eraser: public Tool
{
public:
    Eraser(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso) override;

};