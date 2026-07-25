#pragma once
#include "assets_library/tools/tool.h"
#include "assets_library/assets/slice_asset.h"

class LandscapePencil: public Tool
{
public:
    LandscapePencil(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;

    // Drag-stroke (continuous painting while the mouse moves): Begin/Move
    // paint with a per-stroke dedup on (node, action), End resets the dedup.
    void stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
        DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier) override;

protected:
    // For subclasses with their own display name (Shape3dPencil).
    LandscapePencil(const QString& name, const QString& icon, QObject* parent);

private:
    void paintAt(QPoint screenPos, Asset* currentAsset, LayerModel* layerModel, DiamondIsometryView* iso,
        bool erase);

    math::ivec2 m_lastNode{0, 0};
    bool m_lastErase = false;
    bool m_hasLast = false;
};