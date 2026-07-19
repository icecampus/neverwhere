#include "render_core/overlay_renderer.h"

namespace render_core {

static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color0;
out vec4 v_color;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_color = color0;
}
)";

static const char* fs_src_glsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

// Minimal HLSL for D3D11 backend
static const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; };
struct VSIn { float2 pos: TEXCOORD0; float4 color0: TEXCOORD1; };
struct VSOut { float4 pos: SV_Position; float4 color0: TEXCOORD0; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.color0 = inp.color0;
    return o;
}
)";

static const char* fs_src_hlsl = R"(
struct PSIn { float4 pos: SV_Position; float4 color0: TEXCOORD0; };
float4 main(PSIn inp): SV_Target0 {
    return inp.color0;
}
)";

void OverlayRenderer::init() {
    ensurePipeline();

    // Dynamic vertex buffer for many line segments (2 vertices per segment)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 2 * 65536 * (int)sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "overlay-verts";
    vbuf = sg_make_buffer(&buf_desc);

    bind.vertex_buffers[0] = vbuf;
}

void OverlayRenderer::shutdown() {
    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    destroyPipeline();
}

void OverlayRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
#if defined(SOKOL_D3D11)
    shd_desc.vertex_func.source = vs_src_hlsl;
    shd_desc.fragment_func.source = fs_src_hlsl;
    // semantics for D3D11
    shd_desc.attrs[0].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[0].hlsl_sem_index = 0;
    shd_desc.attrs[1].hlsl_sem_name = "TEXCOORD";
    shd_desc.attrs[1].hlsl_sem_index = 1;
#else
    shd_desc.vertex_func.source = vs_src_glsl;
    shd_desc.fragment_func.source = fs_src_glsl;
#endif

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    // We render into the default pass (created by sokol_app), which typically has a depth-stencil attachment.
    // Even if we don't use depth testing, the pipeline depth format must match the pass depth format.
    pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "overlay-pipeline";
    pip = sg_make_pipeline(&pip_desc);
}

void OverlayRenderer::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
}

void OverlayRenderer::render(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight) {
    if (lines.empty()) return;
    if (pip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;

    scratchVerts.clear();
    scratchVerts.reserve(lines.size() * 2);
    for (const LineSegment& l : lines) {
        scratchVerts.push_back({{l.p0.x, l.p0.y}, {l.color.r, l.color.g, l.color.b, l.color.a}});
        scratchVerts.push_back({{l.p1.x, l.p1.y}, {l.color.r, l.color.g, l.color.b, l.color.a}});
    }

    sg_apply_pipeline(pip);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &uniform_range);

    sg_range range = { scratchVerts.data(), scratchVerts.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, &range);

    sg_apply_bindings(&bind);

    sg_draw(0, (int)scratchVerts.size(), 1);
}

} // namespace render_core
