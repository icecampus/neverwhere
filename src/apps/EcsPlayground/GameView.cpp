#include "GameView.h"
#include "EcsModel.h"
#include "graphics/lib.h"
#include <spdlog/spdlog.h>
#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLContext>
#include <QOpenGLFramebufferObject>

// Sokol definitions must match those in sokol_impl.cpp
#if defined(__EMSCRIPTEN__)
    #define SOKOL_GLES3
#elif defined(_WIN32)
    #define SOKOL_GLCORE33
#elif defined(__APPLE__)
    #define SOKOL_GLCORE33
#else
    #define SOKOL_GLCORE33
#endif

#include <sokol_gfx.h>

class GameViewRenderer : public QQuickFramebufferObject::Renderer
{
public:
    GameViewRenderer() {
        // Initialize Sokol if not already done.
        // Graphics::init() handles the global sg_setup.
        // We ensure it's called on the render thread.
        Graphics::init();
    }

    ~GameViewRenderer() override {
        // We don't shutdown Sokol here because it might be used by other views.
        // sg_shutdown(); 
        // If we want to manage resources per view, we should destroy pass/images here.
        if (m_pass.id != SG_INVALID_ID) {
            sg_destroy_pass(m_pass);
        }
        if (m_pass_img.id != SG_INVALID_ID) {
            sg_destroy_image(m_pass_img);
        }
        if (m_depth_img.id != SG_INVALID_ID) {
            sg_destroy_image(m_depth_img);
        }
    }

    void synchronize(QQuickFramebufferObject *item) override {
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
        
        viewPos.each([&](const Position& pos) {
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

        if (m_vertices.empty()) {
            // Debug triangle
            m_vertices.push_back({50, 50, 1, 0, 0, 1});
            m_vertices.push_back({150, 50, 1, 0, 0, 1});
            m_vertices.push_back({50, 150, 1, 0, 0, 1});
        }
    }

    void render() override {
        // 1. Reset Qt OpenGL state (important before Sokol)
        // QQuickFramebufferObject::Renderer automatically handles FBO binding,
        // but we need to ensure Sokol knows about it or we wrap it.
        
        // Since we are in QQuickFramebufferObject, 'render()' is called when the FBO is bound.
        // We need to create an sg_pass that wraps this FBO's textures.
        
        QOpenGLFramebufferObject* fbo = framebufferObject();
        if (!fbo) return;

        QSize size = fbo->size();
        int width = size.width();
        int height = size.height();

        // Check if we need to recreate the pass (resize)
        if (m_width != width || m_height != height || m_pass.id == SG_INVALID_ID) {
            updatePass(fbo);
        }

        // 2. Begin Pass
        // We render INTO the FBO provided by Qt.
        // Since we wrapped it in 'm_pass', we use sg_begin_pass.
        
        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_CLEAR;
        action.colors[0].clear_value = { 0.2f, 0.2f, 0.2f, 1.0f }; // Dark background for the View
        
        sg_begin_pass(m_pass, &action);
        
        // 3. Draw using shared Graphics logic
        Graphics::draw_rects(m_vertices, width, height);
        
        // 4. End Pass
        sg_end_pass();
        sg_commit();
        
        // 5. Reset OpenGL state for Qt
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        if (gl) {
            gl->glUseProgram(0);
            gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
            gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            gl->glDisable(GL_DEPTH_TEST);
            gl->glDisable(GL_CULL_FACE);
            gl->glDisable(GL_SCISSOR_TEST);
            gl->glDisable(GL_STENCIL_TEST);
            gl->glEnable(GL_BLEND);
            gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        
        update(); // Request continuous update
    }

    QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        // format.setSamples(4); // MSAA if needed
        return new QOpenGLFramebufferObject(size, format);
    }

private:
    void updatePass(QOpenGLFramebufferObject* fbo) {
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
        color_desc.gl_textures[0] = fbo->texture();
        m_pass_img = sg_make_image(&color_desc);

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
    setMirrorVertically(true); // FBOs are usually flipped in Qt Quick
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
