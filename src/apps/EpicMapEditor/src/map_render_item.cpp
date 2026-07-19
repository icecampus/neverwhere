#include "pch.h"
#include "map_render_item.h"

#include <glad/glad.h>

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <sokol_gfx.h>
#include <sokol_log.h>

#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QTimer>

#include <atomic>
#include <mutex>

#include "map_frame_bridge.h"

namespace {

// Global sokol state for the whole editor — initialized lazily on the render
// thread, where Qt's GL context is current (battle rules from EcsPlayground).
struct SokolGlobal {
    std::atomic<bool> initialized{false};
    std::atomic<bool> valid{false};

    void lazyInit() {
        if (initialized.load(std::memory_order_acquire)) return;
        static std::mutex initMutex;
        std::lock_guard<std::mutex> lock(initMutex);
        if (initialized.load(std::memory_order_relaxed)) return;

        // The OpenGL context is already active here (Qt render thread).
        // Qt owns the context and the swap chain, so no desc.environment.
        sg_desc desc = {};
        desc.logger.func = slog_func;
        sg_setup(&desc);

        valid.store(sg_isvalid(), std::memory_order_release);
        initialized.store(true, std::memory_order_release);
    }
};

SokolGlobal g_sokol;

class MapRenderItemRenderer : public QQuickFramebufferObject::Renderer {
public:
    MapRenderItemRenderer() = default;

    ~MapRenderItemRenderer() override {
        // Per-item sg resources only; sg_shutdown is global (aboutToQuit).
        if (m_colorAttView.id != SG_INVALID_ID) sg_destroy_view(m_colorAttView);
        if (m_passImg.id != SG_INVALID_ID) sg_destroy_image(m_passImg);
        if (m_worldInit) m_worldRenderer.shutdown();
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* mapItem = static_cast<MapRenderItem*>(item);
        // Qt blocks the GUI thread during synchronize — safe to read the models.
        m_cameraOffset = {mapItem->cameraX(), mapItem->cameraY()};
        m_cameraZoom = mapItem->cameraZoom();
        m_cursorCell = mapItem->cursorCell();
        m_showGrid = mapItem->showGrid();
        m_assetsLibrary = mapItem->assetsLibrary();

        if (MapModel* model = mapItem->mapModel()) {
            map_frame_bridge::buildWorldFrame(*model, m_frame);
        } else {
            m_frame.landscapeTiles.clear();
            m_frame.sprites.clear();
        }
    }

    void render() override {
        g_sokol.lazyInit();
        if (!g_sokol.valid.load(std::memory_order_acquire)) {
            return;
        }

        if (!m_worldInit) {
            // The Qt FBO depth attachment is not wrapped into the sokol pass
            // (EcsPlayground battle rules), so pipelines must expect no depth.
            m_worldRenderer.init(SG_PIXELFORMAT_NONE);
            m_worldInit = true;
        }

        // Qt messes with the GL state between frames; drop sokol's state cache.
        sg_reset_state_cache();

        QOpenGLFramebufferObject* fbo = framebufferObject();
        if (fbo && (fbo->size().width() != m_width || fbo->size().height() != m_height || m_colorAttView.id == SG_INVALID_ID)) {
            updatePass(fbo);
        }
        if (m_colorAttView.id == SG_INVALID_ID) {
            return;
        }

        m_frame.showGrid = m_showGrid;
        m_frame.cursorCell = glm::ivec2(m_cursorCell.x(), m_cursorCell.y());

        if (m_assetsLibrary) {
            map_frame_bridge::ensureFrameAssets(*m_assetsLibrary, m_frame, m_worldRenderer);
        }

        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        // Same background as the MapView.qml Rectangle it replaces.
        action.colors[0].clear_value = {1.0f, 1.0f, 1.0f, 1.0f};

        sg_pass pass = {};
        pass.action = action;
        pass.attachments.colors[0] = m_colorAttView;
        sg_begin_pass(&pass);

        topology_core::Camera2D camera;
        camera.offset = m_cameraOffset;
        camera.zoom = m_cameraZoom;
        m_worldRenderer.render(m_frame, m_iso, camera, m_width, m_height);

        sg_end_pass();
        // Exactly one sg_commit per frame at the top level (EcsPlayground rules).
        sg_commit();

        // Restore the GL state Qt expects for its own rendering.
        auto* gl = QOpenGLContext::currentContext()->functions();
        gl->glUseProgram(0);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        return new QOpenGLFramebufferObject(size, format);
    }

private:
    void updatePass(QOpenGLFramebufferObject* fbo) {
        if (!fbo) return;

        if (m_colorAttView.id != SG_INVALID_ID) {
            sg_destroy_view(m_colorAttView);
            m_colorAttView.id = SG_INVALID_ID;
        }
        if (m_passImg.id != SG_INVALID_ID) {
            sg_destroy_image(m_passImg);
            m_passImg.id = SG_INVALID_ID;
        }

        m_width = fbo->size().width();
        m_height = fbo->size().height();

        // Wrap the Qt FBO color texture as a sokol image (EcsPlayground pattern).
        sg_image_desc color_desc = {};
        color_desc.usage.color_attachment = true;
        color_desc.width = m_width;
        color_desc.height = m_height;
        // Qt FBO color textures are GL_RGBA8.
        color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        color_desc.gl_texture_target = GL_TEXTURE_2D; // required!
        color_desc.gl_textures[0] = fbo->texture();
        m_passImg = sg_make_image(&color_desc);

        sg_view_desc view_desc = {};
        view_desc.color_attachment.image = m_passImg;
        m_colorAttView = sg_make_view(&view_desc);
    }

    int m_width = 0;
    int m_height = 0;
    sg_image m_passImg{SG_INVALID_ID};
    sg_view m_colorAttView{SG_INVALID_ID};

    bool m_worldInit = false;
    render_core::WorldRenderer m_worldRenderer;
    render_core::WorldFrame m_frame;
    topology_core::DiamondIsometry m_iso;

    glm::vec2 m_cameraOffset{0.0f, 0.0f};
    float m_cameraZoom = 1.0f;
    QPoint m_cursorCell{0, 0};
    bool m_showGrid = true;
    AssetsLibraryModel* m_assetsLibrary = nullptr;
};

} // namespace

MapRenderItem::MapRenderItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    setFlag(ItemHasContents, true);
    // Sokol renders into the FBO texture with GL orientation (row 0 = bottom),
    // so the item must mirror it vertically to match the sokol_app output
    // (verified against EpicGameClient screenshots).
    setMirrorVertically(true);
    // The map models don't emit signals for every visual change (asset pivot
    // edits, object moves), so repaint continuously like EcsPlayground does.
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MapRenderItem::update);
    timer->start(16);
}

QQuickFramebufferObject::Renderer* MapRenderItem::createRenderer() const {
    return new MapRenderItemRenderer();
}

void MapRenderItem::setMapModel(MapModel* model) {
    if (m_mapModel == model) return;
    m_mapModel = model;
    emit mapModelChanged();
    update();
}

void MapRenderItem::setAssetsLibrary(AssetsLibraryModel* library) {
    if (m_assetsLibrary == library) return;
    m_assetsLibrary = library;
    emit assetsLibraryChanged();
    update();
}

void MapRenderItem::setCameraX(float x) {
    if (m_cameraX == x) return;
    m_cameraX = x;
    emit cameraXChanged();
    update();
}

void MapRenderItem::setCameraY(float y) {
    if (m_cameraY == y) return;
    m_cameraY = y;
    emit cameraYChanged();
    update();
}

void MapRenderItem::setCameraZoom(float zoom) {
    if (m_cameraZoom == zoom) return;
    m_cameraZoom = zoom;
    emit cameraZoomChanged();
    update();
}

void MapRenderItem::setCursorCell(const QPoint& cell) {
    if (m_cursorCell == cell) return;
    m_cursorCell = cell;
    emit cursorCellChanged();
    update();
}

void MapRenderItem::setShowGrid(bool show) {
    if (m_showGrid == show) return;
    m_showGrid = show;
    emit showGridChanged();
    update();
}

void shutdownEditorSokol() {
    if (g_sokol.valid.load(std::memory_order_acquire)) {
        sg_shutdown();
        g_sokol.valid.store(false, std::memory_order_release);
    }
}
