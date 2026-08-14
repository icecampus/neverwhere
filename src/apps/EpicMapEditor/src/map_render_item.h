#pragma once

#include <QQuickFramebufferObject>
#include <QPoint>
#include <QVariantMap>

#include "frame_source.h"

// FBO-based map view: renders a world through the shared sokol WorldRenderer
// (the same renderer the game client uses) into a Qt Quick item. World data
// comes from a pluggable MapFrameSource (editor models, game runtime, ...).
class MapRenderItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(MapFrameSource* frameSource READ frameSource WRITE setFrameSource NOTIFY frameSourceChanged)
    Q_PROPERTY(float cameraX READ cameraX WRITE setCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(float cameraY READ cameraY WRITE setCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(float cameraZoom READ cameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)
    Q_PROPERTY(QPoint cursorCell READ cursorCell WRITE setCursorCell NOTIFY cursorCellChanged)
    Q_PROPERTY(QPoint cursorFootprint READ cursorFootprint WRITE setCursorFootprint NOTIFY cursorFootprintChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    // Transient fence-tool state from AssetToolsSelector (ghost preview +
    // selection): {selectedFenceId, valid, assetUuid, pieces:[...]}.
    Q_PROPERTY(QVariantMap fenceToolState READ fenceToolState WRITE setFenceToolState NOTIFY fenceToolStateChanged)

public:
    explicit MapRenderItem(QQuickItem* parent = nullptr);

    Renderer* createRenderer() const override;

    MapFrameSource* frameSource() const { return m_frameSource; }
    void setFrameSource(MapFrameSource* source);

    float cameraX() const { return m_cameraX; }
    void setCameraX(float x);

    float cameraY() const { return m_cameraY; }
    void setCameraY(float y);

    float cameraZoom() const { return m_cameraZoom; }
    void setCameraZoom(float zoom);

    QPoint cursorCell() const { return m_cursorCell; }
    void setCursorCell(const QPoint& cell);

    QPoint cursorFootprint() const { return m_cursorFootprint; }
    void setCursorFootprint(const QPoint& size);

    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool show);

    QVariantMap fenceToolState() const { return m_fenceToolState; }
    void setFenceToolState(const QVariantMap& state);

signals:
    void frameSourceChanged();
    void cameraXChanged();
    void cameraYChanged();
    void cameraZoomChanged();
    void cursorCellChanged();
    void cursorFootprintChanged();
    void showGridChanged();
    void fenceToolStateChanged();

private:
    MapFrameSource* m_frameSource = nullptr;
    float m_cameraX = 0.0f;
    float m_cameraY = 0.0f;
    float m_cameraZoom = 1.0f;
    QPoint m_cursorCell{0, 0};
    QPoint m_cursorFootprint{1, 1};
    bool m_showGrid = true;
    QVariantMap m_fenceToolState;
};

// Marks sokol unavailable for the editor — call from QGuiApplication::aboutToQuit.
// This intentionally does NOT call sg_shutdown(): at that point the GL context
// is not current on the GUI thread, and sokol resource destruction requires it.
// The flag makes render() early-out and renderer destructors skip their sg calls;
// the GL driver reclaims everything when the context dies with the process.
void shutdownEditorSokol();
