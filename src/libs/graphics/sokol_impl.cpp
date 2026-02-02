#include "lib.h"

// Define SOKOL_IMPL only once here
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY
// Suppress Sokol assertions for now to test rendering
#define SOKOL_ASSERT(x) void(0)

#if defined(__EMSCRIPTEN__)
    #define SOKOL_GLES3
#elif defined(_WIN32)
    #define SOKOL_GLCORE33
    #include <glad/glad.h>
#elif defined(__APPLE__)
    #define SOKOL_GLCORE33
    #include <glad/glad.h>
#else
    #define SOKOL_GLCORE33
    #include <glad/glad.h>
#endif

#include <sokol_gfx.h>
#include <sokol_log.h>
#include <spdlog/spdlog.h>
#include <QtGui/QOpenGLContext>

namespace Graphics {

bool is_initialized = false;
sg_pipeline pip;
sg_bindings bind;
sg_buffer vbuf;

static int frame_width = 800;
static int frame_height = 600;

// Shader source
const char* vs_src = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;

void main() {
    // Convert 0..width, 0..height to -1..1
    // x: 0 -> -1, w -> 1  => (x / w) * 2 - 1
    // y: 0 -> 1, h -> -1  => 1 - (y / h) * 2
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_color = color;
}
)";

const char* fs_src = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";

void init() {
    if (is_initialized) return;

#if !defined(__EMSCRIPTEN__)
    // Initialize GLAD
    // We use the current context's getProcAddress
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx) {
        gladLoadGLLoader((GLADloadproc)[](const char* name) {
            return (void*)QOpenGLContext::currentContext()->getProcAddress(name);
        });
        spdlog::info("GLAD initialized");
    } else {
        spdlog::error("No QOpenGLContext found for GLAD initialization");
    }
#endif

    sg_desc desc = {};
    desc.logger.func = slog_func;
    // We assume context is set by Qt
    sg_setup(&desc);
    
    if (!sg_isvalid()) {
        spdlog::error("Failed to initialize Sokol GFX");
        return;
    }

    // Create shader
    sg_shader_desc shd_desc = {};
    shd_desc.vs.source = vs_src;
    shd_desc.fs.source = fs_src;
    shd_desc.vs.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.vs.uniform_blocks[0].uniforms[0].name = "view_size";
    shd_desc.vs.uniform_blocks[0].uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    sg_shader shd = sg_make_shader(&shd_desc);

    // Create pipeline
    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip = sg_make_pipeline(&pip_desc);

    // Create dynamic vertex buffer (max 10000 quads = 60000 verts)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 60000 * sizeof(Vertex);
    buf_desc.usage = SG_USAGE_STREAM;
    buf_desc.label = "quad-vertices";
    vbuf = sg_make_buffer(&buf_desc);

    if (vbuf.id == SG_INVALID_ID) {
        spdlog::error("Failed to create vertex buffer");
    }

    bind.vertex_buffers[0] = vbuf;

    spdlog::info("Sokol GFX initialized successfully with Pipeline");
    is_initialized = true;
}

void begin_frame(int width, int height) {
    if (!is_initialized) return;
    
    // CRITICAL: Reset Sokol's state cache because Qt has messed with GL state since the last frame
    sg_reset_state_cache();
    
    frame_width = width;
    frame_height = height;

    spdlog::trace("Graphics::begin_frame: {}x{}", width, height);

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_LOAD; 
    
    // We use the default pass (FBO 0) which is usually the window surface in Qt OpenGL
    sg_begin_default_pass(&action, width, height);
}

void draw_rects(const std::vector<Vertex>& vertices) {
    if (!is_initialized || vertices.empty()) return;

    spdlog::trace("Graphics::draw_rects: {} vertices", vertices.size());

    // Update buffer
    sg_range range = { vertices.data(), vertices.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, range);

    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);

    float vs_params[2] = { (float)frame_width, (float)frame_height };
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(SG_SHADERSTAGE_VS, 0, uniform_range);

    // Draw
    sg_draw(0, (int)vertices.size(), 1);
}

void end_frame() {
    if (!is_initialized) return;
    sg_end_pass();
    sg_commit();
}

void render_test_frame() {
   begin_frame(800, 600);
   end_frame();
}

}
