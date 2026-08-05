#include "GameView.h"
#include "EcsModel.h"
#include "graphics/lib.h"
#include <spdlog/spdlog.h>
#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QTimer>

std::atomic<bool> SokolGlobal::initialized{ false };
std::atomic<bool> SokolGlobal::valid{ false };

void SokolGlobal::lazyInit() 
{
    if (initialized.load(std::memory_order_acquire)) 
    {
        return;
    }

    // Double-checked locking pattern
    static std::mutex initMutex;
    std::lock_guard<std::mutex> lock(initMutex);

    if (initialized.load(std::memory_order_relaxed)) 
    {
        return;
    }

    // Here the OpenGL/Metal/D3D11 context is already active (Qt rendering thread)
    Graphics::init();
    bool isOk = sg_isvalid();
    valid.store(isOk, std::memory_order_release);
    initialized.store(true, std::memory_order_release);

    spdlog::info("Sokol global init: {}", isOk ? "SUCCESS" : "FAILED");
}


class GameViewRenderer : public QQuickFramebufferObject::Renderer
{
public:
    GameViewRenderer()
    {
        // Initialization moved to createFramebufferObject to ensure active context
    }

    ~GameViewRenderer() override 
    {
        // We don't shutdown Sokol here because it might be used by other views.
        // sg_shutdown(); 
        // If we want to manage resources per view, we should destroy attachment views/images here.
        if (m_color_att_view.id != SG_INVALID_ID) 
        {
            sg_destroy_view(m_color_att_view);
        }

        if (m_pass_img.id != SG_INVALID_ID) 
        {
            sg_destroy_image(m_pass_img);
        }

        if (m_depth_img.id != SG_INVALID_ID) 
        {
            sg_destroy_image(m_depth_img);
        }
    }

    void synchronize(QQuickFramebufferObject *item) override 
    {
        GameView* view = static_cast<GameView*>(item);
        if (!view) return;
        
        m_window = view->window();

        EcsModel* model = view->model();
        if (!model) return;

        // Generate vertices from model
        // This runs on Render Thread with Main Thread blocked, so safe to access model.
        m_vertices.clear();
        
        const auto& reg = model->registry();
        auto viewPos = reg.view<Position>();
        
        m_vertices.reserve(viewPos.size() * 6);
        
        float w = 40.0f;
        float h = 40.0f;
        
        viewPos.each([&](const Position& pos) 
        {
            float x = pos.x;
            float y = pos.y;
            float r = 0.0f, g = 1.0f, b = 1.0f, a = 1.0f; // Cyan
            
            // Quad (2 triangles)
            m_vertices.push_back({x, y, r, g, b, a});         // TL
            m_vertices.push_back({x + w, y, r, g, b, a});     // TR
            m_vertices.push_back({x, y + h, r, g, b, a});     // BL
            
            m_vertices.push_back({x, y + h, r, g, b, a});     // BL
            m_vertices.push_back({x + w, y, r, g, b, a});     // TR
            m_vertices.push_back({x + w, y + h, r, g, b, a}); // BR
        });

        if (m_vertices.empty()) 
        {
            // Debug triangle
            m_vertices.push_back({50, 50, 1, 0, 0, 1});
            m_vertices.push_back({150, 50, 1, 0, 0, 1});
            m_vertices.push_back({50, 150, 1, 0, 0, 1});
        }
    }

    void render() override 
    {
        m_frameCount++;
        spdlog::trace("GameViewRenderer::render() frame #{}", m_frameCount);

        SokolGlobal::lazyInit();
        if (!SokolGlobal::valid.load(std::memory_order_acquire)) 
        {
            spdlog::error("Sokol not valid, skipping render frame #{}", m_frameCount);
            return;
        }
        sg_reset_state_cache();

        QOpenGLFramebufferObject* fbo = framebufferObject();
        if (!fbo) {
            spdlog::warn("No FBO available, skipping render frame #{}", m_frameCount);
            return;
        }

        QSize size = fbo->size();
        if (m_width != size.width() || m_height != size.height() || m_color_att_view.id == SG_INVALID_ID) {
            spdlog::debug("Updating pass for frame #{}: size {}x{}", m_frameCount, size.width(), size.height());
            updatePass(fbo);
            if (m_color_att_view.id == SG_INVALID_ID) {
                spdlog::error("Failed to create attachment view for frame #{}", m_frameCount);
                return;
            }
        }

        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value = { 0.2f, 0.2f, 0.2f, 1.0f };

        spdlog::trace("Frame #{}: begin_pass, {} vertices", m_frameCount, m_vertices.size());
        sg_pass pass = {};
        pass.action = action;
        pass.attachments.colors[0] = m_color_att_view;
        sg_begin_pass(&pass);
        Graphics::draw_rects(m_vertices, m_width, m_height);
        sg_end_pass();
        
        // ✅ sg_commit() is REQUIRED to finalize each frame!
        // Without it, dynamic buffer updates will fail on subsequent frames.
        sg_commit();
        spdlog::trace("Frame #{}: sg_commit() done", m_frameCount);

        // Resetting the state for Qt
        auto gl = QOpenGLContext::currentContext()->functions();
        gl->glUseProgram(0);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
        // sokol's GLCORE backend hard-enables GL_FRAMEBUFFER_SRGB at the
        // start of every offscreen pass and never restores it; on macOS the
        // sRGB-encoded window drawable then double-converts everything Qt
        // draws afterwards (the whole window washes out).
        gl->glDisable(GL_FRAMEBUFFER_SRGB);

    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override 
    {
        
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    
        // format.setSamples(4); // MSAA if needed
        return new QOpenGLFramebufferObject(size, format);
    }

private:
    void updatePass(QOpenGLFramebufferObject* fbo) 
    {
        if (m_color_att_view.id != SG_INVALID_ID) {
            sg_destroy_view(m_color_att_view);
            m_color_att_view.id = SG_INVALID_ID;
        }
        if (m_pass_img.id != SG_INVALID_ID) {
            sg_destroy_image(m_pass_img);
            m_pass_img.id = SG_INVALID_ID;
        }
        if (m_depth_img.id != SG_INVALID_ID) {
             sg_destroy_image(m_depth_img);
             m_depth_img.id = SG_INVALID_ID;
        }

        m_width = fbo->size().width();
        m_height = fbo->size().height();
        
        // Create wrapper image for Color Attachment
        sg_image_desc color_desc = {};
        color_desc.usage.color_attachment = true;
        color_desc.width = m_width;
        color_desc.height = m_height;
        // Qt usually uses GL_RGBA8
        color_desc.pixel_format = SG_PIXELFORMAT_RGBA8; 
        color_desc.gl_texture_target = GL_TEXTURE_2D;  // ← NECESSARILY!!
        color_desc.gl_textures[0] = fbo->texture();
        m_pass_img = sg_make_image(&color_desc);
        
        if (m_pass_img.id == SG_INVALID_ID) 
        {
             spdlog::error("Failed to create Sokol color image wrapper");
             return;
        }

        // Create wrapper image for Depth/Stencil Attachment (optional, but good if we use depth)
        // Qt FBO has CombinedDepthStencil.
        // We need the GL ID.
        // QOpenGLFramebufferObject doesn't easily expose the depth buffer ID if it's a Renderbuffer.
        // But if we don't need depth testing for 2D rects, we can skip it.
        // If we skip it, Sokol might complain if the pipeline expects depth?
        // Our pipeline in lib.h/sokol_impl.cpp currently (probably) doesn't use depth test or uses default.
        // Let's assume no depth buffer wrapper for now, or check if we need it.
        // If we don't provide depth attachment to pass, Sokol won't clear/use it.
        
        sg_view_desc view_desc = {};
        view_desc.color_attachment.image = m_pass_img;
        m_color_att_view = sg_make_view(&view_desc);
        
        if (m_color_att_view.id == SG_INVALID_ID) 
        {
             spdlog::error("Failed to create Sokol color attachment view");
             return;
        }
        
        spdlog::info("Updated Sokol Pass for FBO: Size {}x{}, TexID {}", m_width, m_height, fbo->texture());
    }

    std::vector<Graphics::Vertex> m_vertices;
    QQuickWindow* m_window = nullptr;
    sg_view m_color_att_view = { SG_INVALID_ID };
    sg_image m_pass_img = { SG_INVALID_ID };
    sg_image m_depth_img = { SG_INVALID_ID };
    int m_width = 0;
    int m_height = 0;
    int m_frameCount = 0;
};

GameView::GameView()
{
    setFlag(ItemHasContents, true);
    // setMirrorVertically(true); // FBOs are usually flipped in Qt Quick, but better handle in shader
    
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &QQuickItem::update);
    timer->start(16); // ~60 FPS
}

EcsModel* GameView::model() const {
    return m_model;
}

void GameView::setModel(EcsModel* model) {
    if (m_model == model) return;
    m_model = model;
    emit modelChanged();
    update(); // Request update
}

QQuickFramebufferObject::Renderer* GameView::createRenderer() const {
    return new GameViewRenderer();
}
