#include "GameView.h"
#include "graphics/lib.h"
#include <spdlog/spdlog.h>
#include <QtQuick/qquickwindow.h>

GameView::GameView()
{
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, &GameView::handleWindowChanged);
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
    if (m_sokolInitialized && isVisible()) {
        spdlog::trace("Rendering frame with Sokol...");
        
        // Ensure the QQuickWindow's OpenGL context is bound (it should be in beforeRendering)
        // Draw something
        Graphics::render_test_frame();
        
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
