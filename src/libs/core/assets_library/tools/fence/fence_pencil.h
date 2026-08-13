#pragma once
#include "assets_library/tools/tool.h"

#include <QVariantList>
#include <QVariantMap>

#include <fence_core/fence_model.h>

// Fence brush (fence3d assets, FenceLandscape layer): the FencePathPlayground
// tool UX on the editor's Tool contract. LMB-drag draws an axis-locked fence
// stroke (from a post = extension) with a live ghost preview; double-click on
// a post selects the whole fence (amber tint); dragging the selection moves
// it; Delete erases the selection, Escape clears it. All writes go through
// MapAuthoring; the fence graph is re-derived from the layer on every event
// (fence_core::FenceModel), so the members below are pure drag/UI state.
class FencePencil : public Tool
{
    Q_OBJECT
public:
    explicit FencePencil(QObject* parent);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier) override;
    void stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
        DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier) override;
    void keyPress(int key, Asset* currentAsset, LayerModel* mapModel) override;

    // Transient preview state: read by AssetToolsSelector after every event
    // and forwarded QML -> MapRenderItem -> WorldFrame::fenceGhost.
    QVariantList ghostPieces() const;
    bool ghostValid() const { return m_ghostValid; }
    int selectedFenceId() const { return m_selectedFence; }

private:
    void rebuildGhost(LayerModel* layerModel);

    // Stroke drag (LMB held): axis-locked fence stroke from m_strokeStart.
    bool m_strokeDrag = false;
    bool m_strokeFromPost = false; // start cell held a post at Begin (extension)
    math::ivec2 m_strokeStart{0, 0};
    math::ivec2 m_strokeDir{0, 0};
    int m_strokeCells = 0;

    // Move drag: parallel shift of the selected fence.
    bool m_moveDrag = false;
    math::ivec2 m_moveStart{0, 0};
    math::ivec2 m_moveDelta{0, 0};

    // Double-click detection (the editor's QML forwards no double-click
    // signal): same cell within 350 ms, as in the playground.
    math::ivec2 m_lastClickCell{0, 0};
    qint64 m_lastClickMs = 0;
    bool m_hasLastClick = false;

    int m_selectedFence = -1;
    math::ivec2 m_selectedFenceCell{0, 0}; // a cell on the selection (erase target)

    std::vector<fence_core::FencePieceData> m_ghost;
    bool m_ghostValid = false;
};
