#include "lib.h"

// Define SOKOL_IMPL only once here
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if defined(__EMSCRIPTEN__)
    #define SOKOL_GLES3
#elif defined(_WIN32)
    #define SOKOL_GLCORE
    #include <glad/glad.h>
#elif defined(__APPLE__)
    // No glad here: sokol_gfx.h (SOKOL_IMPL) pulls in the system <OpenGL/gl3.h>,
    // which conflicts with glad's gl* macro redefinitions.
    #define SOKOL_GLCORE
#else
    // No glad on Linux either: sokol_gfx.h (SOKOL_IMPL) pulls <GL/gl.h>, which
    // glad's include guard blocks — sokol would call GL via glad's unloaded
    // (NULL) pointers and crash in sg_setup.
    #define SOKOL_GLCORE
#endif

// Qt's `slots` macro breaks Sokol internals which use a field with that name.
#ifdef slots
#undef slots
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

    sg_desc desc = {};
    desc.logger.func = slog_func;
    // Important: We don't want Sokol to manage the context creation or swap chain,
    // as Qt handles that.
    sg_setup(&desc);

    if (!sg_isvalid()) {
        spdlog::error("Failed to initialize Sokol GFX");
        return;
    }

    // Create shader
    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source = vs_src;
    shd_desc.fragment_func.source = fs_src;
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
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

    // Explicitly disable depth format expectation for the pipeline
    // because we will render to an FBO pass that might not have depth wrapped.
    pip_desc.depth.pixel_format = SG_PIXELFORMAT_NONE;

    pip_desc.label = "quad-pipeline";
    pip = sg_make_pipeline(&pip_desc);

    // Create dynamic vertex buffer (max 10000 quads = 60000 verts)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 60000 * sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "quad-vertices";
    vbuf = sg_make_buffer(&buf_desc);

    if (vbuf.id == SG_INVALID_ID) {
        spdlog::error("Failed to create vertex buffer");
    }

    bind.vertex_buffers[0] = vbuf;

    spdlog::info("Sokol GFX initialized successfully with Pipeline");
    is_initialized = true;
}

void draw_rects(const std::vector<Vertex>& vertices, int view_width, int view_height) {
    if (!is_initialized || vertices.empty()) return;

    // spdlog::trace("Graphics::draw_rects: {} vertices", vertices.size());

    // Update buffer
    sg_range range = { vertices.data(), vertices.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, range);

    sg_apply_pipeline(pip);
    sg_apply_bindings(&bind);

    float vs_params[2] = { (float)view_width, (float)view_height };
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, uniform_range);

    // Draw
    sg_draw(0, (int)vertices.size(), 1);
}

}
