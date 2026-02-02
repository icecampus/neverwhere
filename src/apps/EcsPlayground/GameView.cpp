#include "GameView.h"
#include "EcsModel.h"
#include "graphics/lib.h"
#include <spdlog/spdlog.h>
#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLContext>

GameView::GameView()
{
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, &GameView::handleWindowChanged);
}

EcsModel* GameView::model() const {
    return m_model;
}

void GameView::setModel(EcsModel* model) {
    if (m_model == model) return;
    m_model = model;
    emit modelChanged();
}

void GameView::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        // Render after Qt Scene Graph (Overlay) to be visible over the background
        connect(win, &QQuickWindow::afterRendering, this, &GameView::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &GameView::cleanup, Qt::DirectConnection);
    }
}

void GameView::sync()
{
    if (!m_sokolInitialized) {
        setupSokol();
    }
    
    // Only render if we are visible and initialized
    if (m_sokolInitialized && isVisible() && m_model) {
        // spdlog::trace("Rendering frame with Sokol...");
        
        // Prepare vertices from ECS
        std::vector<Graphics::Vertex> vertices;
        const auto& reg = m_model->registry();
        auto view = reg.view<Position>();
        
        // Reserve memory (6 vertices per quad)
        vertices.reserve(view.size() * 6);
        
        float w = 40.0f;
        float h = 40.0f;
        
        // Iterate entities
        view.each([&](const Position& pos) {
            float x = pos.x;
            float y = pos.y;
            
            // Color (Cyan: 0, 1, 1, 1)
            float r = 0.0f, g = 1.0f, b = 1.0f, a = 1.0f;
            
            // 2 triangles per quad
            // Top-Left, Top-Right, Bottom-Left
            // Bottom-Left, Top-Right, Bottom-Right
            
            // TL
            vertices.push_back({x, y, r, g, b, a});
            // TR
            vertices.push_back({x + w, y, r, g, b, a});
            // BL
            vertices.push_back({x, y + h, r, g, b, a});
            
            // BL
            vertices.push_back({x, y + h, r, g, b, a});
            // TR
            vertices.push_back({x + w, y, r, g, b, a});
            // BR
            vertices.push_back({x + w, y + h, r, g, b, a});
        });

        if (vertices.empty()) {
             // Debug draw even if no entities
             vertices.push_back({100, 100, 1, 0, 0, 1});
             vertices.push_back({200, 100, 1, 0, 0, 1});
             vertices.push_back({100, 200, 1, 0, 0, 1});
             spdlog::debug("Drawing DEBUG triangle (no entities found)");
        } else {
             // spdlog::trace("Drawing {} vertices for {} entities. First pos: {},{}", vertices.size(), view.size(), vertices[0].x, vertices[0].y);
        }

        // Draw
        // Use window size for projection
        
        // 1. Get current FBO
        QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
        GLint currentFbo = 0;
        gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFbo);
        
        // spdlog::trace("GameView::sync: Size {}x{}, DPR {}, FBO {}", window()->width(), window()->height(), window()->devicePixelRatio(), currentFbo);
        static bool fboLogged = false;
        if (!fboLogged) {
            spdlog::info("GameView::sync: Size {}x{}, DPR {}, FBO {}", window()->width(), window()->height(), window()->devicePixelRatio(), currentFbo);
            fboLogged = true;
        }
        
        // 2. Begin Frame (Sokol binds FBO 0)
        Graphics::begin_frame(window()->width() * window()->devicePixelRatio(), window()->height() * window()->devicePixelRatio());
        
        // 3. Restore FBO if needed
        if (currentFbo != 0) {
             gl->glBindFramebuffer(GL_FRAMEBUFFER, currentFbo);
        }
        
        Graphics::draw_rects(vertices);
        Graphics::end_frame();
        
        // Force continuous update for animation/test
        window()->update();
    }
}

void GameView::setupSokol()
{
    spdlog::info("Setting up Sokol in GameView context...");
    Graphics::init();
    m_sokolInitialized = true;
}

void GameView::cleanup()
{
    spdlog::info("Cleaning up GameView...");
    // graphics cleanup if needed
}
