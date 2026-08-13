#include "fence_pencil.h"

#include <QDateTime>

#include "assets_library/asset.h"
#include "map/map_authoring.h"

FencePencil::FencePencil(QObject* parent):
    Tool("FencePencil", "land_pencil", parent)
{

}

void FencePencil::click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    // The fence tool is stroke-driven (the selector calls click only from RPC
    // "click", which means little for a drag tool); keep it a no-op.
    Q_UNUSED(screenPos);
    Q_UNUSED(currentAsset);
    Q_UNUSED(mapModel);
    Q_UNUSED(iso);
    Q_UNUSED(ctrlModifier);
    Q_UNUSED(shiftModifier);
    Q_UNUSED(altModifier);
}

void FencePencil::stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
    DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    Q_UNUSED(ctrlModifier);
    Q_UNUSED(shiftModifier);
    Q_UNUSED(altModifier);
    if (!mapModel || !iso)
    {
        return;
    }
    const math::ivec2 cell = iso->screenToMap(math::vec2(screenPos.x(), screenPos.y()));

    if (kind == StrokeKind::Begin)
    {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool doubleClick =
            m_hasLastClick && m_lastClickCell == cell && (nowMs - m_lastClickMs) < 350;
        m_lastClickCell = cell;
        m_lastClickMs = nowMs;
        m_hasLastClick = true;

        fence_core::FenceModel model = MapAuthoring::buildFenceModel(*mapModel);

        if (doubleClick)
        {
            // Select the whole fence by its post; the second click never
            // starts a stroke/move.
            m_strokeDrag = false;
            m_moveDrag = false;
            const fence_core::FencePiece* piece = model.pieceAt(cell);
            if (piece && piece->kind == fence_core::FencePieceKind::Post)
            {
                m_selectedFence = piece->fenceId;
                m_selectedFenceCell = cell;
            }
            rebuildGhost(mapModel);
            return;
        }

        if (m_selectedFence >= 0)
        {
            const fence_core::FencePiece* piece = model.pieceAt(cell);
            if (piece && piece->fenceId == m_selectedFence)
            {
                // Drag on the selected fence = parallel shift of the whole fence.
                m_moveDrag = true;
                m_moveStart = cell;
                m_moveDelta = math::ivec2(0, 0);
                rebuildGhost(mapModel);
                return;
            }
        }

        m_strokeDrag = true;
        m_strokeStart = cell;
        m_strokeFromPost = model.pieceAt(cell) != nullptr;
        m_strokeDir = math::ivec2(0, 0);
        m_strokeCells = 0;
        rebuildGhost(mapModel);
        return;
    }

    if (kind == StrokeKind::Move)
    {
        if (m_strokeDrag)
        {
            // Dominant-axis lock: the fence is always axis-parallel, so the
            // direction is the larger cell delta component (once non-zero).
            const math::ivec2 delta = cell - m_strokeStart;
            if (delta.x != 0 || delta.y != 0)
            {
                if (std::abs(delta.x) >= std::abs(delta.y))
                {
                    m_strokeDir = math::ivec2(delta.x > 0 ? 1 : -1, 0);
                    m_strokeCells = std::abs(delta.x);
                }
                else
                {
                    m_strokeDir = math::ivec2(0, delta.y > 0 ? 1 : -1);
                    m_strokeCells = std::abs(delta.y);
                }
                // planStroke counts the cells to cover: a new fence covers
                // the start cell too, an extension starts past the anchor.
                if (!m_strokeFromPost)
                {
                    m_strokeCells += 1;
                }
            }
            rebuildGhost(mapModel);
        }
        else if (m_moveDrag)
        {
            m_moveDelta = cell - m_moveStart;
            rebuildGhost(mapModel);
        }
        return;
    }

    // StrokeKind::End
    if (m_strokeDrag)
    {
        if ((m_strokeDir.x != 0 || m_strokeDir.y != 0) && m_strokeCells > 0)
        {
            MapAuthoring::applyFenceStroke(*mapModel, currentAsset, m_strokeStart, m_strokeDir, m_strokeCells);
        }
        else
        {
            fence_core::FenceModel model = MapAuthoring::buildFenceModel(*mapModel);
            if (!model.pieceAt(m_strokeStart))
            {
                // Plain click on empty ground: drop the selection.
                m_selectedFence = -1;
            }
        }
        m_strokeDrag = false;
    }
    if (m_moveDrag)
    {
        if ((m_moveDelta.x != 0 || m_moveDelta.y != 0) && m_selectedFence >= 0)
        {
            MapAuthoring::translateFenceAt(*mapModel, m_moveStart, m_moveDelta);
        }
        m_moveDrag = false;
    }
    m_strokeCells = 0;
    rebuildGhost(mapModel);
}

void FencePencil::keyPress(int key, Asset* currentAsset, LayerModel* mapModel)
{
    Q_UNUSED(currentAsset);
    if (key == Qt::Key_Escape)
    {
        m_selectedFence = -1;
        rebuildGhost(mapModel);
        return;
    }
    if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && mapModel && m_selectedFence >= 0)
    {
        MapAuthoring::eraseFenceAt(*mapModel, m_selectedFenceCell, true);
        m_selectedFence = -1;
        rebuildGhost(mapModel);
    }
}

QVariantList FencePencil::ghostPieces() const
{
    QVariantList out;
    out.reserve(static_cast<qsizetype>(m_ghost.size()));
    for (const fence_core::FencePieceData& p : m_ghost)
    {
        QVariantMap m;
        m["x"] = p.cell.x;
        m["y"] = p.cell.y;
        m["kind"] = p.kind == fence_core::FencePieceKind::Section ? 1 : 0;
        m["axisX"] = p.axis.x;
        m["axisY"] = p.axis.y;
        m["length"] = p.length;
        out.append(m);
    }
    return out;
}

void FencePencil::rebuildGhost(LayerModel* mapModel)
{
    m_ghost.clear();
    m_ghostValid = false;
    if (!mapModel)
    {
        return;
    }

    if (m_strokeDrag && (m_strokeDir.x != 0 || m_strokeDir.y != 0) && m_strokeCells > 0)
    {
        fence_core::FenceModel model = MapAuthoring::buildFenceModel(*mapModel);
        const fence_core::FenceModel::StrokePlan plan =
            model.planStroke(m_strokeStart, m_strokeDir, m_strokeCells);
        if (plan.valid)
        {
            m_ghostValid = true;
            for (const fence_core::FenceModel::StrokePiece& sp : plan.pieces)
            {
                m_ghost.push_back({sp.kind, sp.cell, sp.axis, sp.length});
            }
        }
        else
        {
            // Rejected raw line (red): plain post placeholders along the run.
            const math::ivec2 runStart = m_strokeFromPost ? m_strokeStart + m_strokeDir : m_strokeStart;
            for (int i = 0; i < m_strokeCells; ++i)
            {
                const math::ivec2 cell = runStart + m_strokeDir * i;
                m_ghost.push_back({fence_core::FencePieceKind::Post, cell, glm::ivec2{0, 0}, 1});
            }
        }
    }
    else if (m_moveDrag && (m_moveDelta.x != 0 || m_moveDelta.y != 0) && m_selectedFence >= 0)
    {
        fence_core::FenceModel model = MapAuthoring::buildFenceModel(*mapModel);
        m_ghostValid = model.canTranslate(m_selectedFence, m_moveDelta);
        for (const fence_core::FencePiece& piece : model.pieces())
        {
            if (piece.fenceId == m_selectedFence)
            {
                m_ghost.push_back({piece.kind, piece.cell + m_moveDelta, piece.axis, piece.length});
            }
        }
    }
}
