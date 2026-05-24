#include "runtime_map_view.h"

#include <QQuickWindow>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QScreen>
#include <QOpenGLContext>
#include <QSGSimpleTextureNode>

#include <game_runtime/lib.h>
#include <game_data/assets.h>
#include <render_core/landscape_renderer.h>
#include <topology_core/camera2d.h>
#include <topology_core/staggered_isometry.h>
#include <spdlog/spdlog.h>

#include <math/vec.h>

RuntimeMapView::RuntimeMapView(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    
    // Connect to window changes for OpenGL context
    connect(this, &QQuickItem::windowChanged, this, &RuntimeMapView::onWindowChanged);
}

RuntimeMapView::~RuntimeMapView()
{
    shutdown();
}

void RuntimeMapView::componentComplete()
{
    QQuickItem::componentComplete();
    
    if (!m_initialized && window()) {
        initialize();
    }
}

void RuntimeMapView::onWindowChanged(QQuickWindow* window)
{
    if (window) {
        connect(window, &QQuickWindow::beforeRendering, this, &RuntimeMapView::sync, Qt::DirectConnection);
        connect(window, &QQuickWindow::afterRendering, this, &RuntimeMapView::render, Qt::DirectConnection);
        
        if (!m_initialized) {
            initialize();
        }
    }
}

void RuntimeMapView::initialize()
{
    if (m_initialized) return;
    
    spdlog::info("RuntimeMapView: Initializing");
    
    // Initialize Runtime
    game_runtime::RuntimeConfig config;
    config.enableEditorExtensions = true;
    config.windowWidth = width();
    config.windowHeight = height();
    
    m_runtime = std::make_unique<game_runtime::Runtime>(config);
    
    if (!m_runtime->initialize()) {
        spdlog::error("RuntimeMapView: Failed to initialize Runtime");
        return;
    }
    
    // Initialize renderer components
    m_iso = std::make_unique<topology_core::StaggeredIsometry>();
    m_camera = std::make_unique<topology_core::Camera2D>();
    m_landRenderer = std::make_unique<render_core::LandscapeRenderer>();
    
    // Note: LandscapeRenderer needs proper OpenGL context setup
    // This is a simplified version - actual implementation would need
    // to handle OpenGL context properly in Qt Quick
    // m_landRenderer->init();
    
    m_initialized = true;
    m_running = true;
    
    emit runningChanged();
    emit runtimeInitialized();
    
    spdlog::info("RuntimeMapView: Initialized successfully");
    
    // Load map if path is set
    if (!m_mapPath.isEmpty()) {
        loadMap();
    }
}

void RuntimeMapView::shutdown()
{
    if (!m_initialized) return;
    
    spdlog::info("RuntimeMapView: Shutting down");
    
    m_landRenderer.reset();
    m_session = nullptr;
    m_runtime.reset();
    
    m_initialized = false;
    m_running = false;
    
    emit runningChanged();
}

void RuntimeMapView::loadMap()
{
    if (!m_runtime || m_mapPath.isEmpty()) return;
    
    spdlog::info("RuntimeMapView: Loading map {}", m_mapPath.toStdString());
    
    auto fixture = game_runtime::Fixture::create()
        .withName("Editor Map")
        .withMap(m_mapPath.toStdString())
        .build();
    
    m_session = m_runtime->createSession(fixture);
    
    if (m_session) {
        updateTiles();
    }
}

void RuntimeMapView::updateTiles()
{
    if (!m_session) return;
    
    m_tiles.clear();
    auto& world = m_session->world();
    
    const auto* layer = world.getLayer(game_data::LayerType::BaseLandscape);
    if (!layer) return;
    
    for (const auto& obj : *layer) {
        if (obj.type != game_data::GameObjectType::Landscape) continue;
        if (!obj.landscapeData) continue;

        render_core::LandscapeTile t;
        t.cell = obj.position;
        t.assetUuid = obj.assetUuid;
        t.tileIndex = obj.landscapeData->tileIndex;
        m_tiles.push_back(std::move(t));
    }
    
    m_dirty = true;
    update();
    
    spdlog::info("RuntimeMapView: Updated {} tiles", m_tiles.size());
}

void RuntimeMapView::setMapPath(const QString& path)
{
    if (m_mapPath != path) {
        m_mapPath = path;
        emit mapPathChanged();
        
        if (m_initialized) {
            loadMap();
        }
    }
}

void RuntimeMapView::setCameraZoom(float zoom)
{
    zoom = std::max(0.1f, std::min(3.0f, zoom));
    if (m_cameraZoom != zoom) {
        m_cameraZoom = zoom;
        if (m_camera) {
            m_camera->zoom = zoom;
        }
        emit cameraZoomChanged();
        m_dirty = true;
        update();
    }
}

void RuntimeMapView::setCameraOffset(const QPointF& offset)
{
    if (m_cameraOffset != offset) {
        m_cameraOffset = offset;
        if (m_camera) {
            m_camera->offset = math::vec2(offset.x(), offset.y());
        }
        emit cameraOffsetChanged();
        m_dirty = true;
        update();
    }
}

void RuntimeMapView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    m_dirty = true;
}

void RuntimeMapView::sync()
{
    // Synchronize data before rendering
    if (m_runtime && m_session) {
        m_runtime->update(1.0f / 60.0f); // Fixed timestep for editor
    }
}

void RuntimeMapView::render()
{
    if (!m_initialized || !m_camera || !m_iso) return;
    
    // Note: Actual OpenGL rendering would happen here
    // This requires proper integration with Qt Quick's OpenGL context
    // For now, this is a placeholder
    
    // In a full implementation, we would:
    // 1. Bind the FBO
    // 2. Clear the screen
    // 3. Render tiles using m_landRenderer->render()
    // 4. Unbind FBO
    // 5. Update the texture node
}

math::ivec2 RuntimeMapView::screenToCell(const QPointF& screenPos) const
{
    if (!m_camera || !m_iso) return math::ivec2(0, 0);
    
    math::vec2 screen(screenPos.x(), screenPos.y());
    math::vec2 world = m_camera->screenToWorld(screen);
    return m_iso->fieldToMap(world);
}

void RuntimeMapView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        m_dragging = true;
        m_dragStart = event->pos();
        m_cameraStart = m_cameraOffset;
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeMapView::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->pos();
    
    // Emit hovered cell
    math::ivec2 cell = screenToCell(pos);
    emit cellHovered(cell.x, cell.y);
    
    if (m_dragging) {
        QPointF delta = pos - m_dragStart;
        setCameraOffset(m_cameraStart + delta);
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeMapView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        m_dragging = false;
        event->accept();
    } else {
        event->ignore();
    }
}

void RuntimeMapView::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() / 120.0f;
    float zoomFactor = (delta > 0) ? 1.1f : 0.9f;
    float newZoom = m_cameraZoom * zoomFactor;
    
    // Zoom towards mouse position
    QPointF mousePos = event->position();
    QPointF worldPosBefore(
        (mousePos.x() - m_cameraOffset.x()) / m_cameraZoom,
        (mousePos.y() - m_cameraOffset.y()) / m_cameraZoom
    );
    
    QPointF newOffset;
    newOffset.setX(mousePos.x() - worldPosBefore.x() * newZoom);
    newOffset.setY(mousePos.y() - worldPosBefore.y() * newZoom);
    
    setCameraZoom(newZoom);
    setCameraOffset(newOffset);
    
    event->accept();
}

void RuntimeMapView::hoverMoveEvent(QHoverEvent* event)
{
    math::ivec2 cell = screenToCell(event->pos());
    emit cellHovered(cell.x, cell.y);
    event->accept();
}
