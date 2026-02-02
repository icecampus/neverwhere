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
        // If we want to manage resources per view, we should destroy pass/images here.
        if (m_pass.id != SG_INVALID_ID) 
        {
            sg_destroy_pass(m_pass);
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

        SokolGlobal::lazyInit();
        if (!SokolGlobal::valid.load(std::memory_order_acquire)) 
        {
            //spdlog::error("Sokol not valid, skipping render");
            return;
        }
        sg_reset_state_cache();

        QOpenGLFramebufferObject* fbo = framebufferObject();
        if (!fbo) return;

        QSize size = fbo->size();
        if (m_width != size.width() || m_height != size.height() || m_pass.id == SG_INVALID_ID) {
            updatePass(fbo);
            if (m_pass.id == SG_INVALID_ID) return;
        }

        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value = { 0.2f, 0.2f, 0.2f, 1.0f };

        sg_begin_pass(m_pass, &action);
        Graphics::draw_rects(m_vertices, m_width, m_height);
        sg_end_pass();
        // ❌ sg_commit() REMOVED!

        // Resetting the state for Qt
        auto gl = QOpenGLContext::currentContext()->functions();
        gl->glUseProgram(0);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);

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
        if (m_pass.id != SG_INVALID_ID) {
            sg_destroy_pass(m_pass);
            m_pass.id = SG_INVALID_ID;
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
        color_desc.render_target = true;
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
        
        sg_pass_desc pass_desc = {};
        pass_desc.color_attachments[0].image = m_pass_img;
        // pass_desc.depth_stencil_attachment.image = ...; // Skip for 2D
        
        m_pass = sg_make_pass(&pass_desc);
        
        if (m_pass.id == SG_INVALID_ID) 
        {
             spdlog::error("Failed to create Sokol pass");
             return;
        }
        
        spdlog::info("Updated Sokol Pass for FBO: Size {}x{}, TexID {}", m_width, m_height, fbo->texture());
    }

    std::vector<Graphics::Vertex> m_vertices;
    QQuickWindow* m_window = nullptr;
    sg_pass m_pass = { SG_INVALID_ID };
    sg_image m_pass_img = { SG_INVALID_ID };
    sg_image m_depth_img = { SG_INVALID_ID };
    int m_width = 0;
    int m_height = 0;
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
