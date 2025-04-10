#pragma once
#include "tool.h"

class Pencil: public Tool
{
public:
    Pencil(QObject* parent);


    void click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso) override;
};