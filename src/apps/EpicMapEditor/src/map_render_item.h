#pragma once

#include <QQuickFramebufferObject>
#include <QPoint>

// Fully-defined types are required for the pointer Q_PROPERTYs below.
#include "core/map/map_model.h"
#include "core/assets_library/assets_library_model.h"

// FBO-based map view: renders the editor's map through the shared sokol
// WorldRenderer (the same renderer the game client uses) into a Qt Quick
// item. Replaces the old QML delegates (Tile2DView/LandscapeView) and the
// QSG-based DiamondGrid/DiamondCursor.
class MapRenderItem : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(MapModel* mapModel READ mapModel WRITE setMapModel NOTIFY mapModelChanged)
    Q_PROPERTY(AssetsLibraryModel* assetsLibrary READ assetsLibrary WRITE setAssetsLibrary NOTIFY assetsLibraryChanged)
    Q_PROPERTY(float cameraX READ cameraX WRITE setCameraX NOTIFY cameraXChanged)
    Q_PROPERTY(float cameraY READ cameraY WRITE setCameraY NOTIFY cameraYChanged)
    Q_PROPERTY(float cameraZoom READ cameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)
    Q_PROPERTY(QPoint cursorCell READ cursorCell WRITE setCursorCell NOTIFY cursorCellChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)

public:
    explicit MapRenderItem(QQuickItem* parent = nullptr);

    Renderer* createRenderer() const override;

    MapModel* mapModel() const { return m_mapModel; }
    void setMapModel(MapModel* model);

    AssetsLibraryModel* assetsLibrary() const { return m_assetsLibrary; }
    void setAssetsLibrary(AssetsLibraryModel* library);

    float cameraX() const { return m_cameraX; }
    void setCameraX(float x);

    float cameraY() const { return m_cameraY; }
    void setCameraY(float y);

    float cameraZoom() const { return m_cameraZoom; }
    void setCameraZoom(float zoom);

    QPoint cursorCell() const { return m_cursorCell; }
    void setCursorCell(const QPoint& cell);

    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool show);

signals:
    void mapModelChanged();
    void assetsLibraryChanged();
    void cameraXChanged();
    void cameraYChanged();
    void cameraZoomChanged();
    void cursorCellChanged();
    void showGridChanged();

private:
    MapModel* m_mapModel = nullptr;
    AssetsLibraryModel* m_assetsLibrary = nullptr;
    float m_cameraX = 0.0f;
    float m_cameraY = 0.0f;
    float m_cameraZoom = 1.0f;
    QPoint m_cursorCell{0, 0};
    bool m_showGrid = true;
};

// Global sokol shutdown for the editor — call from QGuiApplication::aboutToQuit
// (sokol is shared by all MapRenderItem instances and must not be shut down
// from an item/renderer destructor).
void shutdownEditorSokol();
