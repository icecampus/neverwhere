#include "render_core/overlay_renderer.h"

#include <spdlog/spdlog.h>

namespace render_core {

static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color0;
out vec4 v_color;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, pos.z, 1.0);
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
struct VSIn { float3 pos: TEXCOORD0; float4 color0: TEXCOORD1; };
struct VSOut { float4 pos: SV_Position; float4 color0: TEXCOORD0; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
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

// MSL for the Metal backend (sokol_app on macOS). Vertex attributes map by
// index ([[attribute(N)]]); sokol's default entry point for Metal is "_main".
static const char* vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float4 color0 [[attribute(1)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color0;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, in.pos.z, 1.0);
    o.color0 = in.color0;
    return o;
}
)";

static const char* fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float4 color0;
};

fragment float4 _main(PSIn in [[stage_in]]) {
    return in.color0;
}
)";

void OverlayRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();

    // Dynamic vertex buffers for many line segments (2 vertices per segment);
    // they grow on demand in render() (a heavily zoomed-out grid can exceed
    // the initial capacity — an oversized sg_update_buffer is a hard sokol
    // error). Two channels: grid and the always-on-top cursor.
    for (Channel* channel : {&channelA, &channelB}) {
        sg_buffer_desc buf_desc = {};
        buf_desc.size = 2 * 65536 * (int)sizeof(Vertex);
        buf_desc.usage.dynamic_update = true;
        buf_desc.label = "overlay-verts";
        channel->vbuf = sg_make_buffer(&buf_desc);
        channel->vbufSize = buf_desc.size;
        channel->bind.vertex_buffers[0] = channel->vbuf;
    }
}

void OverlayRenderer::shutdown() {
    for (Channel* channel : {&channelA, &channelB}) {
        if (channel->vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(channel->vbuf);
            channel->vbuf.id = SG_INVALID_ID;
            channel->vbufSize = 0;
        }
    }
    destroyPipeline();
}

void OverlayRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // One render_core binary serves all shells: D3D11 (game client, sokol_app on
    // Windows), METAL (sokol_app on macOS) and GLCORE (editor, Qt FBO) — pick
    // shader sources by the active backend.
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = vs_src_hlsl;
        shd_desc.fragment_func.source = fs_src_hlsl;
        // semantics for D3D11
        shd_desc.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[0].hlsl_sem_index = 0;
        shd_desc.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[1].hlsl_sem_index = 1;
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = vs_src_msl;
        shd_desc.fragment_func.source = fs_src_msl;
    } else {
        shd_desc.vertex_func.source = vs_src_glsl;
        shd_desc.fragment_func.source = fs_src_glsl;
    }

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
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos (z = depth)
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_LINES;
    // The depth format must match the pass we render into (sokol_app swapchain
    // has depth-stencil; a Qt FBO wrapper has none).
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "overlay-pipeline";
    pip = sg_make_pipeline(&pip_desc);

    if (depthFormat != SG_PIXELFORMAT_NONE) {
        // Grid variant: the lines sit on the scene's ground plane, so they both
        // test and write depth. The 3D passes run after them and overdraw only
        // where they are nearer than the plane; anything hanging below it (base
        // slabs, the underwater foot of a shoreline) loses to the grid.
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
        pip_desc.label = "overlay-depth-pipeline";
        pipDepth = sg_make_pipeline(&pip_desc);
    }
}

void OverlayRenderer::destroyPipeline() {
    for (sg_pipeline* p : {&pip, &pipDepth}) {
        if (p->id != SG_INVALID_ID) {
            sg_destroy_pipeline(*p);
            p->id = SG_INVALID_ID;
        }
    }
}

void OverlayRenderer::render(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight) {
    renderLines(channelA, pipDepth.id != SG_INVALID_ID ? pipDepth : pip, lines, viewWidth, viewHeight);
}

void OverlayRenderer::renderTop(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight) {
    renderLines(channelB, pip, lines, viewWidth, viewHeight);
}

void OverlayRenderer::renderLines(
    Channel& channel,
    sg_pipeline pipeline,
    const std::vector<LineSegment>& lines,
    int viewWidth,
    int viewHeight) {
    if (lines.empty()) return;
    if (pipeline.id == SG_INVALID_ID) return;

    scratchVerts.clear();
    scratchVerts.reserve(lines.size() * 2);
    for (const LineSegment& l : lines) {
        scratchVerts.push_back({{l.p0.x, l.p0.y, l.depth0}, {l.color.r, l.color.g, l.color.b, l.color.a}});
        scratchVerts.push_back({{l.p1.x, l.p1.y, l.depth1}, {l.color.r, l.color.g, l.color.b, l.color.a}});
    }

    sg_apply_pipeline(pipeline);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &uniform_range);

    const std::size_t bytes = scratchVerts.size() * sizeof(Vertex);
    if (channel.vbufSize < bytes) {
        if (channel.vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(channel.vbuf);
        }
        sg_buffer_desc buf_desc = {};
        buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
        buf_desc.usage.dynamic_update = true;
        buf_desc.label = "overlay-verts";
        channel.vbuf = sg_make_buffer(&buf_desc);
        channel.vbufSize = buf_desc.size;
        channel.bind.vertex_buffers[0] = channel.vbuf;
    }
    if (channel.vbuf.id == SG_INVALID_ID) {
        spdlog::error("OverlayRenderer: failed to grow the vertex buffer to {} bytes", bytes);
        return;
    }

    sg_range range = { scratchVerts.data(), bytes };
    sg_update_buffer(channel.vbuf, &range);

    sg_apply_bindings(&channel.bind);

    sg_draw(0, (int)scratchVerts.size(), 1);
}

} // namespace render_core
