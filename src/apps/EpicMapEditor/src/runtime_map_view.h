#pragma once

#include <QQuickItem>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <math/ivec.h>
#include <memory>
#include <vector>

// Forward declarations
namespace game_runtime { class Runtime; class GameSession; }
namespace render_core { class LandscapeRenderer; struct LandscapeTile; }
namespace topology_core { class StaggeredIsometry; class Camera2D; }

/**
 * @brief QML элемент для рендеринга карты через Runtime (OpenGL)
 * 
 * Заменяет стандартный QML-рендеринг на OpenGL рендеринг через LandscapeRenderer.
 * Работает аналогично EpicGameRuntime, но встраивается в QML.
 */
class RuntimeMapView : public QQuickItem, protected QOpenGLFunctions
{
    Q_OBJECT
    Q_PROPERTY(QString mapPath READ mapPath WRITE setMapPath NOTIFY mapPathChanged)
    Q_PROPERTY(float cameraZoom READ cameraZoom WRITE setCameraZoom NOTIFY cameraZoomChanged)
    Q_PROPERTY(QPointF cameraOffset READ cameraOffset WRITE setCameraOffset NOTIFY cameraOffsetChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit RuntimeMapView(QQuickItem* parent = nullptr);
    ~RuntimeMapView();

    QString mapPath() const { return m_mapPath; }
    void setMapPath(const QString& path);

    float cameraZoom() const { return m_cameraZoom; }
    void setCameraZoom(float zoom);

    QPointF cameraOffset() const { return m_cameraOffset; }
    void setCameraOffset(const QPointF& offset);

    bool running() const { return m_running; }

    game_runtime::Runtime* runtime() const { return m_runtime.get(); }
    game_runtime::GameSession* session() const { return m_session; }

    // Рендеринг
    Q_INVOKABLE void updateTiles();

signals:
    void mapPathChanged();
    void cameraZoomChanged();
    void cameraOffsetChanged();
    void runningChanged();
    void runtimeInitialized();
    void cellHovered(int x, int y);

protected:
    void componentComplete() override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;

private slots:
    void onWindowChanged(QQuickWindow* window);
    void render();
    void sync();

private:
    QString m_mapPath;
    float m_cameraZoom = 1.0f;
    QPointF m_cameraOffset;
    bool m_running = false;

    // Runtime
    std::unique_ptr<game_runtime::Runtime> m_runtime;
    game_runtime::GameSession* m_session = nullptr;

    // Renderer (shared with EpicGameRuntime)
    std::unique_ptr<render_core::LandscapeRenderer> m_landRenderer;
    std::unique_ptr<topology_core::StaggeredIsometry> m_iso;
    std::unique_ptr<topology_core::Camera2D> m_camera;
    std::vector<render_core::LandscapeTile> m_tiles;
    
    bool m_initialized = false;
    bool m_dirty = true;
    
    // Interaction
    bool m_dragging = false;
    QPointF m_dragStart;
    QPointF m_cameraStart;
    QPointF m_hoveredScreenPos;

    void initialize();
    void shutdown();
    void loadMap();
    math::ivec2 screenToCell(const QPointF& screenPos) const;
};
