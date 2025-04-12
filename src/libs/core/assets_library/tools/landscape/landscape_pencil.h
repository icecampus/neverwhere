#pragma once
#include "assets_library/tools/tool.h"

class LandscapePencil: public Tool
{
public:
    LandscapePencil(QObject* parent);


    void click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso) override;
};