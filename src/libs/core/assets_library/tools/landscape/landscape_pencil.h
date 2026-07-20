#pragma once
#include "assets_library/tools/tool.h"
#include "assets_library/assets/slice_asset.h"

class LandscapePencil: public Tool
{
public:
    LandscapePencil(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;
};