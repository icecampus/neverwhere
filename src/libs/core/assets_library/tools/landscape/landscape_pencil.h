#pragma once
#include "assets_library/tools/tool.h"
#include "assets_library/assets/slice_asset.h"

class LandscapePencil: public Tool
{
public:
    LandscapePencil(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;
    static void updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset, const math::ivec2& cellPosition, TileSet::TileType tileType);
};