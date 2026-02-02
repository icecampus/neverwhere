#include "lib.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if defined(__EMSCRIPTEN__)
    #define SOKOL_GLES3
#elif defined(_WIN32)
    // For testing integration with Qt (which we'll force to OpenGL)
    #define SOKOL_GLCORE33
#elif defined(__APPLE__)
    #define SOKOL_GLCORE33
#else
    #define SOKOL_GLCORE33
#endif

#include <sokol_gfx.h>
#include <sokol_log.h>
#include <spdlog/spdlog.h>

namespace Graphics {

bool is_initialized = false;

void init() {
    if (is_initialized) return;

    sg_desc desc = {};
    desc.logger.func = slog_func;
    // When using OpenGL, we don't need to pass device pointers, 
    // but we MUST have the GL context active (which Qt provides in beforeRendering)
    sg_setup(&desc);
    
    if (sg_isvalid()) {
        spdlog::info("Sokol GFX initialized successfully");
        is_initialized = true;
    } else {
        spdlog::error("Failed to initialize Sokol GFX");
    }
}

void render_test_frame() {
    if (!is_initialized) return;

    // Simple pass action to clear the screen (or part of it)
    // We assume the default framebuffer is bound by Qt
    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = { 1.0f, 0.0f, 1.0f, 1.0f }; // Magenta to be visible

    // We can't use sg_begin_default_pass because we don't own the swapchain in the way Sokol expects
    // for a standalone app. But for GL, default pass usually means FBO 0.
    // However, Qt might be rendering to an FBO.
    // Instead of clearing the whole screen (which might wipe Qt UI), let's just draw a viewport.
    
    // For a safe test without blowing up Qt's state:
    // Just try to clear a small scissor rect or similar?
    // Let's rely on sg_apply_viewport if possible.
    
    // Actually, calling sg_begin_default_pass(..., width, height) resets a lot of state.
    // Let's use a custom pass with default framebuffer (0) if possible, or just raw GL calls wrapped?
    // No, we want to use Sokol.
    
    // Let's assume we are drawing to the currently bound framebuffer.
    // sg_begin_pass requires an sg_pass object.
    // sg_begin_default_pass wraps the default backbuffer.
    
    int w = 800; // temporary
    int h = 600; // temporary
    
    // This is risky inside Qt's render loop without restoring state, 
    // but for a "test" to see color, it's fine.
    sg_begin_default_pass(&action, w, h);
    sg_end_pass();
    sg_commit();
}

}
