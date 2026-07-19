#include "pch.h"

// The single Sokol implementation TU for the editor.
// The editor renders through sokol inside a Qt OpenGL context
// (QQuickFramebufferObject), so the backend is GLCORE on every desktop OS
// (SOKOL_GLCORE is also set target-wide in CMakeLists.txt).
#define SOKOL_IMPL
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_GLES3)
    #if defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <glad/glad.h>

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <sokol_gfx.h>
#include <sokol_log.h>
