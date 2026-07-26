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

// glad conflicts with the GL headers that sokol_gfx.h pulls in under SOKOL_IMPL
// (glad redefines gl* names as macros pointing at its own function pointers,
// which nobody loads here):
// - on macOS that's the system <OpenGL/gl3.h> (compile-time conflict);
// - on Linux that's <GL/gl.h> — glad's __gl_h_ guard silently blocks it and
//   sokol ends up calling GL through glad's NULL pointers (crash in sg_setup).
// On both platforms sokol must use the system GL headers directly, so glad is
// only needed on Windows (where sokol's embedded loader shadows it anyway).
#if !defined(__APPLE__) && !defined(__linux__)
#include <glad/glad.h>
#endif

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
#endif

#include <sokol_gfx.h>
#include <sokol_log.h>
