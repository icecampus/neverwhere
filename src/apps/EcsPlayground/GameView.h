#pragma once

#include <QQuickFramebufferObject>
#include <QQuickWindow>
#include <QSGNode>
#include <atomic>

// Sokol definitions must match those in sokol_impl.cpp
#if defined(__EMSCRIPTEN__)
#define SOKOL_GLES3
#elif defined(_WIN32)
#define SOKOL_GLCORE
#elif defined(__APPLE__)
#define SOKOL_GLCORE
#else
#define SOKOL_GLCORE
#endif

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <sokol_gfx.h>


class EcsModel;
namespace SokolGlobal 
{
    extern std::atomic<bool> initialized;
    extern std::atomic<bool> valid;

    // Called from ANY renderer on the render thread
    void lazyInit();
}

//
class GameView : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EcsModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    GameView();
    
    EcsModel* model() const;
    void setModel(EcsModel* model);

    Renderer *createRenderer() const override;

signals:
    void modelChanged();

private:
    EcsModel* m_model = nullptr;
    static bool s_sokolInitialized;
};
