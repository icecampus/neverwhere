#include "pch.h"
#include "water_background.h"

// GLSL 330 only: this background lives in the editor shell, which is always
// GLCORE (the game client has its own clear-color background).
static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv0;
out vec2 v_uv;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_uv = uv0;
}
)";

// Exact port of the old CustomItem fragment shader (shaders/mandelbrot.frag,
// active branch — the mandelbrot part was commented out).
static const char* fs_src_glsl = R"(
#version 330
in vec2 v_uv;
out vec4 frag_color;
uniform float iTime;
void main()
{
    vec2 uv = v_uv;

    // Background color (as in the original)
    vec4 texture_color = vec4(21.0/255.0, 37.0/255.0, 43.0/255.0, 1.0);

    vec4 k = vec4(iTime) * 0.8;
    k.xy = uv * 700.0;

    mat3 m1 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;

    mat3 m2 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.2;

    mat3 m3 = mat3(
        vec3(-2.0, -1.0, 0.0),
        vec3(3.0, -1.0, 1.0),
        vec3(1.0, -1.0, -1.0)
    ) * 0.5;

    float val1 = length(0.5 - fract(k.xyw *= m1));
    float val2 = length(0.5 - fract(k.xyw *= m2));
    float val3 = length(0.5 - fract(k.xyw *= m3));

    vec4 color = vec4(pow(min(min(val1, val2), val3), 7.0) * 3.0) + texture_color;
    frag_color = color;
}
)";

void WaterBackground::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();

    // Static quad: 2 triangles, filled once in render() (single update per frame).
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * (int)sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "water-verts";
    vbuf = sg_make_buffer(&buf_desc);

    bind.vertex_buffers[0] = vbuf;
}

void WaterBackground::shutdown() {
    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    destroyPipeline();
}

void WaterBackground::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    shd_desc.vertex_func.source = vs_src_glsl;
    shd_desc.fragment_func.source = fs_src_glsl;

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(float);
    shd_desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "iTime";
    shd_desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT;

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2; // uv
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // The Qt FBO depth attachment is not wrapped into the pass (see MapRenderItem).
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "water-pipeline";
    pip = sg_make_pipeline(&pip_desc);
}

void WaterBackground::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
}

void WaterBackground::render(
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    float timeSeconds) {

    if (pip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;

    // World-space quad matching the old CustomItem: 20000x20000 anchored at
    // the world origin, uv 0..1 across it (so the caustics stay fixed in the
    // world and follow camera pan/zoom).
    constexpr float kWorldSize = 20000.0f;
    const glm::vec2 tl = camera.worldToScreen({0.0f, 0.0f});
    const glm::vec2 br = camera.worldToScreen({kWorldSize, kWorldSize});

    const Vertex verts[6] = {
        {{tl.x, tl.y}, {0.0f, 0.0f}},
        {{br.x, tl.y}, {1.0f, 0.0f}},
        {{tl.x, br.y}, {0.0f, 1.0f}},

        {{tl.x, br.y}, {0.0f, 1.0f}},
        {{br.x, tl.y}, {1.0f, 0.0f}},
        {{br.x, br.y}, {1.0f, 1.0f}},
    };

    sg_range range = { verts, sizeof(verts) };
    sg_update_buffer(vbuf, &range);

    sg_apply_pipeline(pip);

    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range vs_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &vs_range);

    float fs_params[1] = {timeSeconds};
    sg_range fs_range = { &fs_params, sizeof(fs_params) };
    sg_apply_uniforms(1, &fs_range);

    sg_apply_bindings(&bind);
    sg_draw(0, 6, 1);
}
