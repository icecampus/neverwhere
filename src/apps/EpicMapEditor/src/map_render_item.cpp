#include "pch.h"
#include "map_render_item.h"

// On macOS Qt's qopengl.h already pulls in the system <OpenGL/gl3.h>, which
// conflicts with glad's gl* macro redefinitions. GL constants used here
// (GL_TEXTURE_2D, GL_NO_ERROR) come from the system header instead.
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif

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
#include <QElapsedTimer>

#include <atomic>
#include <mutex>

#include <render_core/world_renderer.h>

#include "water_background.h"

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
        //
        // Drain foreign GL errors BEFORE sg_setup: the context is shared with
        // Qt, whose leftovers trip the glGetError()==0 assert inside
        // _sg_gl_init_limits (observed: crash on the first chapter tab open —
        // the per-frame drain in render() runs only after sg_setup). Log the
        // codes once to help identify the polluter.
        {
            auto* glErr = QOpenGLContext::currentContext()->functions();
            GLenum err = GL_NO_ERROR;
            while ((err = glErr->glGetError()) != GL_NO_ERROR) {
                qWarning() << "[render] drained foreign GL error before sg_setup:"
                           << Qt::hex << static_cast<unsigned>(err) << Qt::dec;
            }
        }

        {
            auto* gl = QOpenGLContext::currentContext()->functions();
            const GLubyte* ver = gl->glGetString(GL_VERSION);
            const GLubyte* ren = gl->glGetString(GL_RENDERER);
            qInfo() << "[render] GL context for sokol:"
                    << (ver ? reinterpret_cast<const char*>(ver) : "?") << "|"
                    << (ren ? reinterpret_cast<const char*>(ren) : "?");
        }

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
        // After shutdownEditorSokol() the sg API must not be touched — the GL
        // context is gone by then. Skipping the destroys at process exit is
        // fine: the driver reclaims everything when the context dies.
        if (!g_sokol.valid.load(std::memory_order_acquire)) return;

        if (m_colorAttView.id != SG_INVALID_ID) sg_destroy_view(m_colorAttView);
        if (m_passImg.id != SG_INVALID_ID) sg_destroy_image(m_passImg);
        if (m_depthAttView.id != SG_INVALID_ID) sg_destroy_view(m_depthAttView);
        if (m_depthImg.id != SG_INVALID_ID) sg_destroy_image(m_depthImg);
        if (m_worldInit) {
            m_water.shutdown();
            m_worldRenderer.shutdown();
        }
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* mapItem = static_cast<MapRenderItem*>(item);
        // Qt blocks the GUI thread during synchronize — safe to read the models
        // and to tick the source (game logic) right here.
        m_cameraOffset = {mapItem->cameraX(), mapItem->cameraY()};
        m_cameraZoom = mapItem->cameraZoom();
        m_cursorCell = mapItem->cursorCell();
        m_showGrid = mapItem->showGrid();
        m_frameSource = mapItem->frameSource();

        if (m_frameSource) {
            m_frameSource->tick();
            m_frameSource->buildWorldFrame(m_frame);
        } else {
            m_frame.landscapeTiles.clear();
            m_frame.sprites.clear();
        }

        // QML coordinates (mouse, camera, isoView) are logical pixels;
        // capture the item's logical size for DPI-correct rendering.
        m_logicalWidth = (float)mapItem->width();
        m_logicalHeight = (float)mapItem->height();
    }

    void render() override {
        g_sokol.lazyInit();
        if (!g_sokol.valid.load(std::memory_order_acquire)) {
            return;
        }

        if (!m_worldInit) {
            // The pass wraps the Qt FBO color texture plus our own sokol
            // depth-stencil image (updatePass), so pipelines see a real
            // depth format and the raised landscape resolves overlaps via
            // the GPU depth buffer.
            m_worldRenderer.init(SG_PIXELFORMAT_DEPTH_STENCIL);
            m_water.init(SG_PIXELFORMAT_DEPTH_STENCIL);
            m_time.start();
            m_worldInit = true;
        }

        // Qt messes with the GL state between frames; drop sokol's state cache.
        sg_reset_state_cache();

        // The GL context is shared with Qt: errors from the Qt render phase
        // linger in glGetError and would trip sokol's debug _SG_GL_CHECK_ERROR
        // asserts at our next call (observed: crash on opening a chapter tab).
        // Drain them here — sokol still catches errors from OUR calls, because
        // its checkpoints follow them within this same frame.
        {
            auto* glErr = QOpenGLContext::currentContext()->functions();
            while (glErr->glGetError() != GL_NO_ERROR) { /* drain foreign errors */ }
        }

        QOpenGLFramebufferObject* fbo = framebufferObject();
        // Re-wrap when the FBO changes size OR when Qt reallocates its texture
        // (hidden tabs get their FBO freed and recreated with a new texture id;
        // a stale wrapped texture made one tab's render land in another tab's FBO).
        if (fbo && (fbo->size().width() != m_width || fbo->size().height() != m_height
                    || fbo->texture() != m_wrappedTexture || m_colorAttView.id == SG_INVALID_ID)) {
            updatePass(fbo);
        }
        if (m_colorAttView.id == SG_INVALID_ID || m_depthAttView.id == SG_INVALID_ID) {
            return;
        }

        m_frame.showGrid = m_showGrid;
        m_frame.cursorCell = glm::ivec2(m_cursorCell.x(), m_cursorCell.y());

        if (m_frameSource) {
            m_frameSource->ensureFrameAssets(m_frame, m_worldRenderer);
        }

        // Offscreen world work (contact-AO field rebuild) — before the pass
        // is begun (texture re-creation is not allowed inside a pass).
        m_worldRenderer.prepare(m_frame, m_time.elapsed() / 1000.0);

        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        // Same background as the MapView.qml Rectangle it replaces.
        action.colors[0].clear_value = {1.0f, 1.0f, 1.0f, 1.0f};
        // Depth is cleared every frame for the z-buffered raised landscape.
        action.depth.load_action = SG_LOADACTION_CLEAR;
        action.depth.clear_value = 1.0f;

        sg_pass pass = {};
        pass.action = action;
        pass.attachments.colors[0] = m_colorAttView;
        pass.attachments.depth_stencil = m_depthAttView;
        sg_begin_pass(&pass);

        // Render in LOGICAL coordinates (QML space): the FBO itself is physical
        // (item size * devicePixelRatio), but Qt maps it back onto the item, so
        // using logical sizes keeps the mouse and the rendered world aligned on
        // High-DPI displays (the grid cursor was offset from the mouse cursor).
        const int viewW = (m_logicalWidth > 0.5f) ? (int)(m_logicalWidth + 0.5f) : m_width;
        const int viewH = (m_logicalHeight > 0.5f) ? (int)(m_logicalHeight + 0.5f) : m_height;

        topology_core::Camera2D camera;
        camera.offset = m_cameraOffset;
        camera.zoom = m_cameraZoom;

        // Water caustics background first (fullscreen world-anchored pass),
        // the map on top.
        m_water.render(camera, viewW, viewH, (float)m_time.elapsed() / 1000.0f);
        m_worldRenderer.render(m_frame, m_iso, camera, viewW, viewH, m_time.elapsed() / 1000.0);

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
        // sokol's GLCORE backend hard-enables GL_FRAMEBUFFER_SRGB at the
        // start of every offscreen pass (the "crude hack" in the sokol_gfx
        // begin_pass) and never restores it. On macOS the window drawable is
        // sRGB-encoded, so everything Qt draws after our pass (the whole UI
        // plus the map FBO itself) gets a second linear->sRGB conversion and
        // the window washes out. Windows/Linux default framebuffers are
        // LINEAR-encoded, so the flag is a no-op there.
        gl->glDisable(GL_FRAMEBUFFER_SRGB);
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
        if (m_depthAttView.id != SG_INVALID_ID) {
            sg_destroy_view(m_depthAttView);
            m_depthAttView.id = SG_INVALID_ID;
        }
        if (m_depthImg.id != SG_INVALID_ID) {
            sg_destroy_image(m_depthImg);
            m_depthImg.id = SG_INVALID_ID;
        }

        m_width = fbo->size().width();
        m_height = fbo->size().height();
        m_wrappedTexture = fbo->texture();

        // Wrap the Qt FBO color texture as a sokol image (EcsPlayground pattern).
        sg_image_desc color_desc = {};
        color_desc.usage.color_attachment = true;
        color_desc.width = m_width;
        color_desc.height = m_height;
        // Qt FBO color textures are GL_RGBA8.
        color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        color_desc.gl_texture_target = GL_TEXTURE_2D; // required!
        color_desc.gl_textures[0] = m_wrappedTexture;
        m_passImg = sg_make_image(&color_desc);

        sg_view_desc view_desc = {};
        view_desc.color_attachment.image = m_passImg;
        m_colorAttView = sg_make_view(&view_desc);

        // The Qt FBO depth attachment is a renderbuffer and cannot be wrapped,
        // so the pass gets its own sokol depth-stencil image instead: sokol's
        // GL backend assembles a framebuffer from the wrapped color texture +
        // this depth texture. The raised landscape z-buffers against it.
        sg_image_desc depth_desc = {};
        depth_desc.usage.depth_stencil_attachment = true;
        depth_desc.width = m_width;
        depth_desc.height = m_height;
        depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        depth_desc.sample_count = 1;
        depth_desc.label = "map-render-depth";
        m_depthImg = sg_make_image(&depth_desc);

        sg_view_desc depth_view_desc = {};
        depth_view_desc.depth_stencil_attachment.image = m_depthImg;
        m_depthAttView = sg_make_view(&depth_view_desc);
    }

    int m_width = 0;
    int m_height = 0;
    GLuint m_wrappedTexture = 0;
    sg_image m_passImg{SG_INVALID_ID};
    sg_view m_colorAttView{SG_INVALID_ID};
    sg_image m_depthImg{SG_INVALID_ID};
    sg_view m_depthAttView{SG_INVALID_ID};

    bool m_worldInit = false;
    render_core::WorldRenderer m_worldRenderer;
    render_core::WorldFrame m_frame;
    topology_core::DiamondIsometry m_iso;
    WaterBackground m_water;
    QElapsedTimer m_time;

    glm::vec2 m_cameraOffset{0.0f, 0.0f};
    float m_cameraZoom = 1.0f;
    QPoint m_cursorCell{0, 0};
    bool m_showGrid = true;
    float m_logicalWidth = 0.0f;
    float m_logicalHeight = 0.0f;
    MapFrameSource* m_frameSource = nullptr;
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

void MapRenderItem::setFrameSource(MapFrameSource* source) {
    if (m_frameSource == source) return;
    m_frameSource = source;
    emit frameSourceChanged();
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
    // Do NOT call sg_shutdown() here: aboutToQuit fires on the GUI thread,
    // where the Qt GL context is not current — GL resource destruction needs
    // the (render-thread) context and debug-asserts otherwise (proven via
    // cdb: wassert in _sg_gl_discard_buffer ← sg_shutdown). The process is
    // exiting anyway, so just block further sg use: renderer destructors
    // check this flag and skip their sg calls, and render() early-outs.
    g_sokol.valid.store(false, std::memory_order_release);
}
