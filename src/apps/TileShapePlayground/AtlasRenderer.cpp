#include "AtlasRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

static const char* kTexVsGlsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv;
out vec2 v_uv;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_uv = uv;
}
)";

static const char* kTexFsGlsl = R"(
#version 330
in vec2 v_uv;
out vec4 frag_color;
uniform sampler2D atlas_tex;
void main() {
    frag_color = texture(atlas_tex, v_uv);
    if (frag_color.a < 0.05) discard;
}
)";

static const char* kColorVsGlsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_color = color;
}
)";

static const char* kColorFsGlsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

// Z-buffered raised pass: pos.z is the normalized depth baked CPU-side from
// highground::Vertex::groundY (field y before the lift; monotonic along the
// iso view ray). Same fragment shaders as the painter pipelines.
static const char* kDepthTexVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec2 uv;
out vec2 v_uv;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_uv = uv;
}
)";

static const char* kDepthColorVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_color = color;
}
)";

// Triplanar walls: tcoord = undeformed map coordinates in world px + lift;
// the fragment shader blends the (mapY,lift) and (mapX,lift) projections of
// a tiling rock texture by the contour normal — no UVs, no seams.
static const char* kTriWallVsGlsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color;
layout(location=2) in vec3 tcoord;
layout(location=3) in vec2 normal;
out vec4 v_color;
out vec2 v_uv1;
out vec2 v_uv2;
out vec2 v_normal;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
uniform float tex_scale;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_color = color;
    v_uv1 = tcoord.yz * tex_scale;
    v_uv2 = tcoord.xz * tex_scale;
    v_normal = normal;
}
)";

static const char* kTriDepthWallVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color;
layout(location=2) in vec3 tcoord;
layout(location=3) in vec2 normal;
out vec4 v_color;
out vec2 v_uv1;
out vec2 v_uv2;
out vec2 v_normal;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
uniform float tex_scale;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_color = color;
    v_uv1 = tcoord.yz * tex_scale;
    v_uv2 = tcoord.xz * tex_scale;
    v_normal = normal;
}
)";

static const char* kTriWallFsGlsl = R"(
#version 330
in vec4 v_color;
in vec2 v_uv1;
in vec2 v_uv2;
in vec2 v_normal;
out vec4 frag_color;
uniform sampler2D wall_tex;
void main() {
    vec2 w = abs(v_normal);
    float wsum = max(w.x + w.y, 1e-5);
    vec4 c = (w.x * texture(wall_tex, v_uv1) + w.y * texture(wall_tex, v_uv2)) / wsum;
    frag_color = c * v_color;
}
)";

static const char* kTexVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float2 uv: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.uv = inp.uv;
    return o;
}
)";

static const char* kTexFsHlsl = R"(
Texture2D atlas_tex: register(t0);
SamplerState smp: register(s0);
struct PSIn {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    float4 c = atlas_tex.Sample(smp, inp.uv);
    if (c.a < 0.05) discard;
    return c;
}
)";

static const char* kColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float4 color: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.color = inp.color;
    return o;
}
)";

static const char* kColorFsHlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    return inp.color;
}
)";

static const char* kDepthTexVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float2 uv: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.uv = inp.uv;
    return o;
}
)";

static const char* kDepthColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float3 _pad0;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float4 color: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.color = inp.color;
    return o;
}
)";

static const char* kTriWallVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float tex_scale;
    float2 _pad0;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float4 color: TEXCOORD1;
    float3 tcoord: TEXCOORD2;
    float2 normal: TEXCOORD3;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
    float2 uv1: TEXCOORD1;
    float2 uv2: TEXCOORD2;
    float2 normal: TEXCOORD3;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.color = inp.color;
    o.uv1 = inp.tcoord.yz * tex_scale;
    o.uv2 = inp.tcoord.xz * tex_scale;
    o.normal = inp.normal;
    return o;
}
)";

static const char* kTriDepthWallVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float tex_scale;
    float2 _pad0;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float4 color: TEXCOORD1;
    float3 tcoord: TEXCOORD2;
    float2 normal: TEXCOORD3;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
    float2 uv1: TEXCOORD1;
    float2 uv2: TEXCOORD2;
    float2 normal: TEXCOORD3;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.color = inp.color;
    o.uv1 = inp.tcoord.yz * tex_scale;
    o.uv2 = inp.tcoord.xz * tex_scale;
    o.normal = inp.normal;
    return o;
}
)";

static const char* kTriWallFsHlsl = R"(
Texture2D wall_tex: register(t0);
SamplerState smp: register(s0);
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
    float2 uv1: TEXCOORD1;
    float2 uv2: TEXCOORD2;
    float2 normal: TEXCOORD3;
};
float4 main(PSIn inp): SV_Target {
    float2 w = abs(inp.normal);
    float wsum = max(w.x + w.y, 1e-5);
    float4 c = (w.x * wall_tex.Sample(smp, inp.uv1) + w.y * wall_tex.Sample(smp, inp.uv2)) / wsum;
    return c * inp.color;
}
)";

// MSL shaders (Metal backend, macOS). Entry point "_main" is sokol's default
// for Metal; vertex attributes map by index ([[attribute(N)]]). VsParams field
// layout must match the C++ struct — packed_float3 keeps the tail padding at
// 12 bytes (32 bytes total), float3 would align to 16 and break the layout.
static const char* kTexVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    packed_float3 _pad0;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.uv = in.uv;
    return o;
}
)";

static const char* kTexFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float2 uv;
};

fragment float4 _main(PSIn in [[stage_in]],
                      texture2d<float> atlas_tex [[texture(0)]],
                      sampler smp [[sampler(0)]]) {
    float4 c = atlas_tex.sample(smp, in.uv);
    if (c.a < 0.05) {
        discard_fragment();
    }
    return c;
}
)";

static const char* kColorVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    packed_float3 _pad0;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.color = in.color;
    return o;
}
)";

static const char* kColorFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float4 color;
};

fragment float4 _main(PSIn in [[stage_in]]) {
    return in.color;
}
)";

static const char* kDepthTexVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    packed_float3 _pad0;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos.xy * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, in.pos.z, 1.0);
    o.uv = in.uv;
    return o;
}
)";

static const char* kDepthColorVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    packed_float3 _pad0;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos.xy * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, in.pos.z, 1.0);
    o.color = in.color;
    return o;
}
)";

static const char* kTriWallVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float tex_scale;
    packed_float2 _pad0;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float4 color [[attribute(1)]];
    float3 tcoord [[attribute(2)]];
    float2 normal [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.color = in.color;
    o.uv1 = in.tcoord.yz * params.tex_scale;
    o.uv2 = in.tcoord.xz * params.tex_scale;
    o.normal = in.normal;
    return o;
}
)";

static const char* kTriDepthWallVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
    float tex_scale;
    packed_float2 _pad0;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float4 color [[attribute(1)]];
    float3 tcoord [[attribute(2)]];
    float2 normal [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos.xy * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, in.pos.z, 1.0);
    o.color = in.color;
    o.uv1 = in.tcoord.yz * params.tex_scale;
    o.uv2 = in.tcoord.xz * params.tex_scale;
    o.normal = in.normal;
    return o;
}
)";

static const char* kTriWallFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float4 color;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

fragment float4 _main(PSIn in [[stage_in]],
                      texture2d<float> wall_tex [[texture(0)]],
                      sampler smp [[sampler(0)]]) {
    float2 w = abs(in.normal);
    float wsum = max(w.x + w.y, 1e-5);
    float4 c = (w.x * wall_tex.sample(smp, in.uv1) + w.y * wall_tex.sample(smp, in.uv2)) / wsum;
    return c * in.color;
}
)";

void fillVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(AtlasRenderer::VsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 0;
    block->spirv_set0_binding_n = 0;
    block->glsl_uniforms[0].glsl_name = "view_size";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[1].glsl_name = "camera_offset";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[2].glsl_name = "camera_zoom";
    block->glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
}

// Same block with the triplanar scale exposed (the triplanar wall shaders
// declare tex_scale; the other pipelines simply ignore those bytes).
void fillTriVsUniformDesc(sg_shader_uniform_block* block) {
    fillVsUniformDesc(block);
    block->glsl_uniforms[3].glsl_name = "tex_scale";
    block->glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
}

void fillTextureSlotDesc(sg_shader_desc* shd, const char* glslName) {
    shd->views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd->views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd->views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd->views[0].texture.hlsl_register_t_n = 0;
    shd->views[0].texture.msl_texture_n = 0;
    shd->views[0].texture.wgsl_group1_binding_n = 0;
    shd->views[0].texture.spirv_set1_binding_n = 0;

    shd->samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd->samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd->samplers[0].hlsl_register_s_n = 0;
    shd->samplers[0].msl_sampler_n = 0;
    shd->samplers[0].wgsl_group1_binding_n = 1;
    shd->samplers[0].spirv_set1_binding_n = 1;

    shd->texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd->texture_sampler_pairs[0].view_slot = 0;
    shd->texture_sampler_pairs[0].sampler_slot = 0;
    shd->texture_sampler_pairs[0].glsl_name = glslName;
}

} // namespace

void AtlasRenderer::init() {
    ensurePipelines();

    sg_buffer_desc texBuf = {};
    texBuf.size = 6 * 65536 * sizeof(TexVertex);
    texBuf.usage.dynamic_update = true;
    texBuf.label = "tileshape-tex-vbuf";
    m_texVbuf = sg_make_buffer(&texBuf);

    sg_buffer_desc colorBuf = {};
    colorBuf.size = 8 * 65536 * sizeof(ColorVertex);
    colorBuf.usage.dynamic_update = true;
    colorBuf.label = "tileshape-color-vbuf";
    m_colorVbuf = sg_make_buffer(&colorBuf);

    // Separate streams for the z-buffered raised pass: sokol allows only one
    // sg_update_buffer per buffer per frame and the vertex strides differ
    // from the painter streams anyway.
    sg_buffer_desc depthTexBuf = {};
    depthTexBuf.size = 6 * 65536 * sizeof(DepthTexVertex);
    depthTexBuf.usage.dynamic_update = true;
    depthTexBuf.label = "tileshape-depth-tex-vbuf";
    m_depthTexVbuf = sg_make_buffer(&depthTexBuf);

    sg_buffer_desc depthColorBuf = {};
    depthColorBuf.size = 8 * 65536 * sizeof(DepthColorVertex);
    depthColorBuf.usage.dynamic_update = true;
    depthColorBuf.label = "tileshape-depth-color-vbuf";
    m_depthColorVbuf = sg_make_buffer(&depthColorBuf);

    // Triplanar painter walls get their own stream (the color buffer mixes
    // painter walls with the grid lines; the z-buffered triplanar walls reuse
    // m_depthColorVbuf — plain byte storage, the modes are mutually
    // exclusive per frame).
    sg_buffer_desc triWallBuf = {};
    triWallBuf.size = 8 * 65536 * sizeof(TriWallVertex);
    triWallBuf.usage.dynamic_update = true;
    triWallBuf.label = "tileshape-tri-wall-vbuf";
    m_triWallVbuf = sg_make_buffer(&triWallBuf);

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp.label = "tileshape-atlas-smp";
    m_sampler = sg_make_sampler(&smp);

    // Tiled ground texture of the raised top: world-UVs outside [0,1].
    sg_sampler_desc topSmp = {};
    topSmp.min_filter = SG_FILTER_LINEAR;
    topSmp.mag_filter = SG_FILTER_LINEAR;
    topSmp.wrap_u = SG_WRAP_REPEAT;
    topSmp.wrap_v = SG_WRAP_REPEAT;
    topSmp.label = "tileshape-top-smp";
    m_topSampler = sg_make_sampler(&topSmp);

    m_ready = true;
}

void AtlasRenderer::shutdown() {
    destroyPipelines();
    if (m_texVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_texVbuf);
        m_texVbuf = {};
    }
    if (m_colorVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_colorVbuf);
        m_colorVbuf = {};
    }
    if (m_depthTexVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_depthTexVbuf);
        m_depthTexVbuf = {};
    }
    if (m_depthColorVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_depthColorVbuf);
        m_depthColorVbuf = {};
    }
    if (m_triWallVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_triWallVbuf);
        m_triWallVbuf = {};
    }
    if (m_sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_sampler);
        m_sampler = {};
    }
    if (m_topSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_topSampler);
        m_topSampler = {};
    }
    destroySlot(m_slots[0]);
    destroySlot(m_slots[1]);
    destroySlot(m_topSlots[0]);
    destroySlot(m_topSlots[1]);
    destroySlot(m_wallTexSlot);
    m_ready = false;
}

void AtlasRenderer::destroySlot(AtlasSlot& slot) {
    if (slot.view.id != SG_INVALID_ID) {
        sg_destroy_view(slot.view);
        slot.view = {};
    }
    if (slot.image.id != SG_INVALID_ID) {
        sg_destroy_image(slot.image);
        slot.image = {};
    }
}

bool AtlasRenderer::uploadSlot(
    AtlasSlot& slot,
    const void* rgba,
    int width,
    int height,
    const char* label) {

    destroySlot(slot);

    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = rgba;
    desc.data.mip_levels[0].size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    desc.label = label;
    slot.image = sg_make_image(&desc);
    if (slot.image.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_image failed ({})", label ? label : "?");
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = slot.image;
    slot.view = sg_make_view(&viewDesc);
    return slot.view.id != SG_INVALID_ID;
}

bool AtlasRenderer::loadAtlasFromFile(AtlasKind kind, const std::string& path, int cols, int rows) {
    m_atlasCols = cols;
    m_atlasRows = rows;

    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::error("AtlasRenderer: failed to load '{}': {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const char* label = (kind == AtlasKind::Grass) ? "grass-atlas" : "flat-atlas-file";
    const bool ok = uploadSlot(m_slots[static_cast<int>(kind)], pixels, w, h, label);
    stbi_image_free(pixels);

    if (ok) {
        spdlog::info("AtlasRenderer: loaded {} into slot {} ({}x{}, {}x{} tiles)",
            path,
            static_cast<int>(kind),
            w,
            h,
            cols,
            rows);
    }
    return ok;
}

bool AtlasRenderer::loadAtlasFromRgba(
    AtlasKind kind,
    const std::uint8_t* rgba,
    int width,
    int height,
    int cols,
    int rows) {

    if (!rgba || width <= 0 || height <= 0) {
        return false;
    }

    m_atlasCols = cols;
    m_atlasRows = rows;
    const char* label = (kind == AtlasKind::Flat) ? "flat-atlas" : "rgba-atlas";
    const bool ok = uploadSlot(m_slots[static_cast<int>(kind)], rgba, width, height, label);
    if (ok) {
        spdlog::info("AtlasRenderer: uploaded RGBA slot {} ({}x{}, {}x{} tiles)",
            static_cast<int>(kind),
            width,
            height,
            cols,
            rows);
    }
    return ok;
}

bool AtlasRenderer::loadTopTextureFromFile(AtlasKind kind, const std::string& path) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::error("AtlasRenderer: failed to load top texture '{}': {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const bool ok = uploadSlot(m_topSlots[static_cast<int>(kind)], pixels, w, h, "raised-top-tex");
    stbi_image_free(pixels);
    if (ok) {
        spdlog::info("AtlasRenderer: loaded raised-top texture {} into slot {} ({}x{})",
            path,
            static_cast<int>(kind),
            w,
            h);
    }
    return ok;
}

bool AtlasRenderer::loadTopTextureFromRgba(AtlasKind kind, const std::uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) {
        return false;
    }
    return uploadSlot(m_topSlots[static_cast<int>(kind)], rgba, width, height, "raised-top-rgba");
}

bool AtlasRenderer::loadWallTextureFromFile(const std::string& path) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::warn("AtlasRenderer: failed to load wall texture '{}': {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const bool ok = uploadSlot(m_wallTexSlot, pixels, w, h, "wall-rock-tex");
    stbi_image_free(pixels);
    if (ok) {
        spdlog::info("AtlasRenderer: loaded wall texture {} ({}x{})", path, w, h);
    }
    return ok;
}

glm::vec4 AtlasRenderer::atlasUvRect(int tileIndex) const {
    if (tileIndex < 0) {
        return {};
    }
    const int col = tileIndex % m_atlasCols;
    const int row = tileIndex / m_atlasCols;
    const float u0 = static_cast<float>(col) / static_cast<float>(m_atlasCols);
    const float v0 = static_cast<float>(row) / static_cast<float>(m_atlasRows);
    const float u1 = static_cast<float>(col + 1) / static_cast<float>(m_atlasCols);
    const float v1 = static_cast<float>(row + 1) / static_cast<float>(m_atlasRows);
    return {u0, v0, u1, v1};
}

void AtlasRenderer::appendTileQuad(
    std::vector<TexVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    int tileIndex,
    float yOffset) {

    // Atlas tiles are square frames containing a 2:1 isometric diamond base
    // (plus grass sticking up). Display as a SQUARE of side cellWidth so the
    // embedded 2:1 base matches the cell diamond (height = cellWidth/2).
    // Using cellSize (cellWidth x cellHeight) squashes that base 2x on Y.
    const glm::vec4 uv = atlasUvRect(tileIndex);
    const glm::vec2 center = iso.mapToField(cell);
    const float side = iso.dims.cellWidth;
    const float half = side * 0.5f;

    const float x0 = center.x - half;
    const float x1 = center.x + half;
    const float y0 = center.y - half + yOffset;
    const float y1 = center.y + half + yOffset;

    const TexVertex tl{x0, y0, uv.x, uv.y};
    const TexVertex tr{x1, y0, uv.z, uv.y};
    const TexVertex br{x1, y1, uv.z, uv.w};
    const TexVertex bl{x0, y1, uv.x, uv.w};

    out.push_back(tl);
    out.push_back(tr);
    out.push_back(br);
    out.push_back(tl);
    out.push_back(br);
    out.push_back(bl);
}

void AtlasRenderer::appendDiamondOutline(
    std::vector<ColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a = corners[i];
        const glm::vec2 b = corners[(i + 1) % 4];
        out.push_back({a.x, a.y, color.r, color.g, color.b, color.a});
        out.push_back({b.x, b.y, color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::appendNodeMarker(
    std::vector<ColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 node,
    glm::vec4 color,
    float yOffset) {

    const glm::vec2 p = iso.nodeToField(node);
    const float s = 6.0f;
    const glm::vec2 pts[4] = {
        {p.x - s, p.y + yOffset},
        {p.x, p.y - s * 0.5f + yOffset},
        {p.x + s, p.y + yOffset},
        {p.x, p.y + s * 0.5f + yOffset},
    };
    for (int i = 0; i < 4; ++i) {
        out.push_back({pts[i].x, pts[i].y, color.r, color.g, color.b, color.a});
        out.push_back({pts[(i + 1) % 4].x, pts[(i + 1) % 4].y, color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::render(
    const PaintLayerView* layers,
    int layerCount,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    glm::ivec2 hoverNode,
    bool hasHover,
    bool hoverRaised,
    const highground::Params* raisedParams,
    bool triplanarWalls,
    float wallTexScale) {

    if (!m_ready || m_texPip.id == SG_INVALID_ID || !layers || layerCount <= 0 || !layers[0].brush) {
        return;
    }

    const int mapW = layers[0].brush->width();
    const int mapH = layers[0].brush->height();

    std::vector<std::pair<std::uint64_t, glm::ivec2>> drawOrder;
    drawOrder.reserve(static_cast<std::size_t>(mapW * mapH));
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            drawOrder.emplace_back(iso.zOffset({x, y}), glm::ivec2{x, y});
        }
    }
    std::sort(drawOrder.begin(), drawOrder.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    // Flat palette layers paint into the shared textured buffer; each range
    // is drawn with the layer's own atlas.
    struct TexRange {
        int base = 0;
        int count = 0;
        AtlasKind atlas = AtlasKind::Grass;
    };
    std::vector<TexRange> flatRanges;
    std::vector<TexVertex> texVerts;
    texVerts.reserve(static_cast<std::size_t>(mapW * mapH * 6 * layerCount));

    std::vector<ColorVertex> wallVerts;
    wallVerts.reserve(static_cast<std::size_t>(mapW * mapH * 12));

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || layer.raised) {
            continue;
        }
        TexRange range;
        range.base = static_cast<int>(texVerts.size());
        range.atlas = layer.atlas;
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto type = layer.brush->cellTypeAt(cell);
            if (!landscape_core::tileTypeHasSurface(type)) {
                continue;
            }
            const int idx = LandBrush::atlasIndexByType(type);
            if (idx >= 0) {
                appendTileQuad(texVerts, iso, cell, idx);
            }
        }
        range.count = static_cast<int>(texVerts.size()) - range.base;
        if (range.count > 0) {
            flatRanges.push_back(range);
        }
    }

    // Raised layers: geometry from highground_core (walls + top surface from
    // the vertex-node grid). Painter mode: primitives arrive depth-sorted
    // (walls and tops interleaved) and are emitted as batches that switch
    // pipeline/texture only on change. Z-buffer mode (layer.zbuf): the lib
    // sort is skipped and vertices carry a normalized depth baked from
    // Vertex::groundY, so batches merge by texture only. The top surface is
    // textured with the tiled ground texture matching the layer's atlas kind.
    struct RaisedBatch {
        bool wall = false;
        int base = 0;
        int count = 0;
        AtlasKind atlas = AtlasKind::Grass;
        bool depth = false; // draw from the depth streams with the z-buffer pipelines
        bool tri = false;   // triplanar wall batch (rock texture, world-space UVs)
    };
    std::vector<RaisedBatch> raisedBatches;

    // Normalized depth along the iso view ray, anchored at the visible
    // ground-y center with generous margins (monotonicity is what matters;
    // the real scene stays far from the clip planes). Ground-y grows TOWARD
    // the viewer (the painter draws larger ground-y last), so with LESS_EQUAL
    // + clear 1.0 the closer fragment must map to the SMALLER z.
    const float groundCenterY = camera.screenToWorld({viewW * 0.5f, viewH * 0.5f}).y;
    const float zFar = groundCenterY + 100000.0f;
    const float zScale = 1.0f / 200000.0f;

    // Triplanar wall texture space: recover the undeformed map coordinates
    // from the field position (inverse of mapToFieldPx) and scale to world
    // px; the iso cell diagonals have equal length on both map axes.
    const float halfW = iso.dims.cellWidth * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    const float mapDiagPx = std::sqrt(halfW * halfW + halfH * halfH);
    const bool tri = triplanarWalls && m_wallTexSlot.view.id != SG_INVALID_ID &&
        m_triWallPip.id != SG_INVALID_ID && m_triDepthWallPip.id != SG_INVALID_ID;

    std::vector<DepthTexVertex> depthTexVerts;
    std::vector<DepthColorVertex> depthWallVerts;
    std::vector<TriWallVertex> triWallVerts;
    std::vector<TriDepthWallVertex> triDepthWallVerts;

    const auto pushWallVertex = [&](const highground::Vertex& v, bool depthMode) {
        if (tri) {
            const float sx = (v.pos.x - halfW) / halfW; // map x - map y
            const float sy = v.pos.y / halfH;           // map x + map y
            const float tx = (sy + sx) * 0.5f * mapDiagPx;
            const float ty = (sy - sx) * 0.5f * mapDiagPx;
            const float lift = v.groundY - v.pos.y;
            if (depthMode) {
                const float z = (zFar - v.groundY) * zScale;
                triDepthWallVerts.push_back(
                    {v.pos.x, v.pos.y, z, v.color.r, v.color.g, v.color.b, v.color.a, tx, ty, lift, v.normal.x, v.normal.y});
            } else {
                triWallVerts.push_back(
                    {v.pos.x, v.pos.y, v.color.r, v.color.g, v.color.b, v.color.a, tx, ty, lift, v.normal.x, v.normal.y});
            }
        } else if (depthMode) {
            const float z = (zFar - v.groundY) * zScale;
            depthWallVerts.push_back({v.pos.x, v.pos.y, z, v.color.r, v.color.g, v.color.b, v.color.a});
        } else {
            wallVerts.push_back({v.pos.x, v.pos.y, v.color.r, v.color.g, v.color.b, v.color.a});
        }
    };

    // Current size of the wall stream for the given mode (for batch bases).
    const auto wallStreamSize = [&](bool depthMode) -> int {
        if (tri) {
            return static_cast<int>(depthMode ? triDepthWallVerts.size() : triWallVerts.size());
        }
        return static_cast<int>(depthMode ? depthWallVerts.size() : wallVerts.size());
    };

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || !layer.raised) {
            continue;
        }

        std::vector<glm::ivec2> onNodes;
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto mask = layer.brush->nodeMaskAt(cell);
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes(cell);
            for (int i = 0; i < 4; ++i) {
                if (mask[i]) {
                    onNodes.push_back(corners[i]);
                }
            }
        }
        if (onNodes.empty()) {
            continue;
        }

        const bool zbuf = layer.zbuf;
        highground::Params gp = raisedParams ? *raisedParams : highground::Params{};
        gp.sortPrimitives = !zbuf; // the GPU depth buffer resolves ordering
        const highground::Grid grid = highground::makeGrid(onNodes.data(), onNodes.size());
        // CGAL region: exact boundary union + CDT top instead of per-cell
        // beveled tops (falls back to generate() when CGAL is not built in).
        const highground::Mesh mesh = (layer.cgal && highground::cgalAvailable())
            ? highground::generateCgal(grid, gp)
            : highground::generate(grid, gp);

        for (const highground::Primitive& prim : mesh.primitives) {
            if (prim.material == highground::Material::Wall) {
                if (raisedBatches.empty() || !raisedBatches.back().wall ||
                    raisedBatches.back().depth != zbuf || raisedBatches.back().tri != tri) {
                    raisedBatches.push_back({true, wallStreamSize(zbuf), 0, layer.atlas, zbuf, tri});
                }
                for (std::uint32_t k = 0; k < prim.count; ++k) {
                    pushWallVertex(mesh.vertices[prim.first + k], zbuf);
                }
                raisedBatches.back().count += static_cast<int>(prim.count);
            } else {
                if (zbuf) {
                    if (raisedBatches.empty() || raisedBatches.back().wall || !raisedBatches.back().depth ||
                        raisedBatches.back().atlas != layer.atlas) {
                        raisedBatches.push_back({false, static_cast<int>(depthTexVerts.size()), 0, layer.atlas, true});
                    }
                    for (std::uint32_t k = 0; k < prim.count; ++k) {
                        const highground::Vertex& v = mesh.vertices[prim.first + k];
                        const float z = (zFar - v.groundY) * zScale;
                        depthTexVerts.push_back({v.pos.x, v.pos.y, z, v.uv.x, v.uv.y});
                    }
                } else {
                    if (raisedBatches.empty() || raisedBatches.back().wall || raisedBatches.back().depth ||
                        raisedBatches.back().atlas != layer.atlas) {
                        raisedBatches.push_back({false, static_cast<int>(texVerts.size()), 0, layer.atlas});
                    }
                    for (std::uint32_t k = 0; k < prim.count; ++k) {
                        const highground::Vertex& v = mesh.vertices[prim.first + k];
                        texVerts.push_back({v.pos.x, v.pos.y, v.uv.x, v.uv.y});
                    }
                }
                raisedBatches.back().count += static_cast<int>(prim.count);
            }
        }
    }

    // Prism demo figure (boundary-first): simplified boundary loops from
    // highground_core::boundaryLoops extruded into flat rectangular walls;
    // the top comes from highground_core::generate (beveled per-cell tops).
    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || !layer.prism) {
            continue;
        }

        std::vector<glm::ivec2> onNodes;
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto mask = layer.brush->nodeMaskAt(cell);
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes(cell);
            for (int i = 0; i < 4; ++i) {
                if (mask[i]) {
                    onNodes.push_back(corners[i]);
                }
            }
        }
        if (onNodes.empty()) {
            continue;
        }

        const highground::Params gp = [&] {
            highground::Params p = raisedParams ? *raisedParams : highground::Params{};
            p.bevel = 0.0f; // the prism is pure rectangles: sharp corners everywhere
            return p;
        }();
        const highground::Grid grid = highground::makeGrid(onNodes.data(), onNodes.size());
        const auto loops = highground::boundaryLoops(grid, iso.dims.cellWidth, iso.dims.cellSize().y);
        const highground::Mesh mesh = highground::generate(grid, gp);
        const float height = gp.height;

        struct Prim {
            float depth;
            bool wall;
            int first;
            int count;
        };
        std::vector<Prim> prims;

        // Rectangular wall per loop edge; two brightness levels by iso axis
        // fake iso lighting, bottom darker (same scheme as the flat walls).
        for (const auto& loop : loops) {
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const glm::vec2& a = loop[i];
                const glm::vec2& b = loop[(i + 1) % loop.size()];
                const glm::vec2 d = b - a;
                const float alongX = std::abs(d.x * 64.0f + d.y * 32.0f);
                const float alongY = std::abs(d.x * -64.0f + d.y * 32.0f);
                const glm::vec3 base = (alongX >= alongY)
                    ? glm::vec3(0.62f, 0.45f, 0.22f)
                    : glm::vec3(0.45f, 0.32f, 0.16f);
                const glm::vec4 top{base, 1.0f};
                const glm::vec4 bottom{base * 0.7f, 1.0f};
                const int first = static_cast<int>(wallVerts.size());
                const auto push = [&](const glm::vec2& p, float lift, const glm::vec4& c) {
                    wallVerts.push_back({p.x, p.y - lift, c.r, c.g, c.b, c.a});
                };
                push(a, height, top);
                push(a, 0.0f, bottom);
                push(b, 0.0f, bottom);
                push(a, height, top);
                push(b, 0.0f, bottom);
                push(b, height, top);
                prims.push_back({std::max(a.y, b.y), true, first, 6});
            }
        }
        // Tops from the lib (beveled per-cell), textured with the layer's
        // raised-top texture.
        for (const highground::Primitive& prim : mesh.primitives) {
            if (prim.material != highground::Material::Top) {
                continue;
            }
            const int first = static_cast<int>(texVerts.size());
            for (std::uint32_t k = 0; k < prim.count; ++k) {
                const highground::Vertex& v = mesh.vertices[prim.first + k];
                texVerts.push_back({v.pos.x, v.pos.y, v.uv.x, v.uv.y});
            }
            prims.push_back({prim.depth, false, first, static_cast<int>(prim.count)});
        }

        // Depth-sorted emission into the shared raised batches (ranges merge
        // only when consecutive prims are contiguous in the vertex streams).
        std::stable_sort(prims.begin(), prims.end(), [](const Prim& a, const Prim& b) {
            if (a.depth != b.depth) return a.depth < b.depth;
            return a.wall > b.wall;
        });
        for (const Prim& prim : prims) {
            if (prim.wall) {
                if (raisedBatches.empty() || !raisedBatches.back().wall || raisedBatches.back().depth ||
                    raisedBatches.back().tri ||
                    raisedBatches.back().base + raisedBatches.back().count != prim.first) {
                    raisedBatches.push_back({true, prim.first, 0, layer.atlas});
                }
                raisedBatches.back().count += prim.count;
            } else {
                if (raisedBatches.empty() || raisedBatches.back().wall || raisedBatches.back().depth ||
                    raisedBatches.back().atlas != layer.atlas ||
                    raisedBatches.back().base + raisedBatches.back().count != prim.first) {
                    raisedBatches.push_back({false, prim.first, 0, layer.atlas});
                }
                raisedBatches.back().count += prim.count;
            }
        }
    }

    std::vector<ColorVertex> lineVerts;
    lineVerts.reserve(static_cast<std::size_t>(mapW * mapH * 8 + 16));

    const glm::vec4 gridColor{0.45f, 0.48f, 0.52f, 0.55f};
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            appendDiamondOutline(lineVerts, iso, {x, y}, gridColor);
        }
    }

    if (hasHover) {
        const float hoverLift = raisedParams ? raisedParams->height : 32.0f;
        appendNodeMarker(
            lineVerts,
            iso,
            hoverNode,
            {1.0f, 0.25f, 0.2f, 1.0f},
            hoverRaised ? -hoverLift : 0.0f);
    }

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;
    vsParams.tex_scale = wallTexScale;

    if (!texVerts.empty()) {
        sg_update_buffer(m_texVbuf, sg_range{texVerts.data(), texVerts.size() * sizeof(TexVertex)});
    }
    if (!depthTexVerts.empty()) {
        sg_update_buffer(m_depthTexVbuf, sg_range{depthTexVerts.data(), depthTexVerts.size() * sizeof(DepthTexVertex)});
    }
    // depthWallVerts and triDepthWallVerts are mutually exclusive (the
    // triplanar mode is global per frame), so the shared buffer gets at most
    // one upload per frame.
    if (!depthWallVerts.empty()) {
        sg_update_buffer(m_depthColorVbuf, sg_range{depthWallVerts.data(), depthWallVerts.size() * sizeof(DepthColorVertex)});
    }
    if (!triDepthWallVerts.empty()) {
        sg_update_buffer(m_depthColorVbuf, sg_range{triDepthWallVerts.data(), triDepthWallVerts.size() * sizeof(TriDepthWallVertex)});
    }
    if (!triWallVerts.empty()) {
        sg_update_buffer(m_triWallVbuf, sg_range{triWallVerts.data(), triWallVerts.size() * sizeof(TriWallVertex)});
    }

    // Painter order: flat ground layers, then the raised batches (painter:
    // depth-sorted by the generator; z-buffer: texture-batched, depth-tested),
    // then the grid lines.
    const auto drawAtlasRange = [&](const TexRange& range) {
        const AtlasSlot& slot = m_slots[static_cast<int>(range.atlas)];
        if (slot.view.id == SG_INVALID_ID) {
            return;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_texVbuf;
        bind.views[0] = slot.view;
        bind.samplers[0] = m_sampler;

        sg_apply_pipeline(m_texPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(range.base, range.count, 1);
    };
    const auto drawTopRange = [&](const RaisedBatch& batch) {
        const AtlasSlot& slot = m_topSlots[static_cast<int>(batch.atlas)];
        if (slot.view.id == SG_INVALID_ID) {
            return;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = batch.depth ? m_depthTexVbuf : m_texVbuf;
        bind.views[0] = slot.view;
        bind.samplers[0] = m_topSampler;

        sg_apply_pipeline(batch.depth ? m_depthTexPip : m_texPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(batch.base, batch.count, 1);
    };

    for (const TexRange& range : flatRanges) {
        drawAtlasRange(range);
    }

    std::vector<ColorVertex> colorVerts;
    colorVerts.reserve(wallVerts.size() + lineVerts.size());
    colorVerts.insert(colorVerts.end(), wallVerts.begin(), wallVerts.end());
    colorVerts.insert(colorVerts.end(), lineVerts.begin(), lineVerts.end());
    if (!colorVerts.empty()) {
        sg_update_buffer(m_colorVbuf, sg_range{colorVerts.data(), colorVerts.size() * sizeof(ColorVertex)});
    }

    for (const RaisedBatch& batch : raisedBatches) {
        if (batch.wall) {
            sg_bindings bind{};
            if (batch.tri) {
                bind.vertex_buffers[0] = batch.depth ? m_depthColorVbuf : m_triWallVbuf;
                bind.views[0] = m_wallTexSlot.view;
                bind.samplers[0] = m_topSampler; // WRAP_REPEAT tiling
            } else {
                bind.vertex_buffers[0] = batch.depth ? m_depthColorVbuf : m_colorVbuf;
            }

            sg_apply_pipeline(batch.tri ? (batch.depth ? m_triDepthWallPip : m_triWallPip)
                                        : (batch.depth ? m_depthWallPip : m_wallPip));
            sg_apply_bindings(&bind);
            sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
            sg_draw(batch.base, batch.count, 1);
        } else {
            drawTopRange(batch);
        }
    }

    if (!lineVerts.empty()) {
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_colorVbuf;

        sg_apply_pipeline(m_colorPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(static_cast<int>(wallVerts.size()), static_cast<int>(lineVerts.size()), 1);
    }
}

void AtlasRenderer::ensurePipelines() {
    if (m_texPip.id != SG_INVALID_ID) {
        return;
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kTexVsHlsl;
        shd.fragment_func.source = kTexFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kTexVsMsl;
        shd.fragment_func.source = kTexFsMsl;
#else
        shd.vertex_func.source = kTexVsGlsl;
        shd.fragment_func.source = kTexFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);

        shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[0].texture.hlsl_register_t_n = 0;
        shd.views[0].texture.msl_texture_n = 0;
        shd.views[0].texture.wgsl_group1_binding_n = 0;
        shd.views[0].texture.spirv_set1_binding_n = 0;

        shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.samplers[0].hlsl_register_s_n = 0;
        shd.samplers[0].msl_sampler_n = 0;
        shd.samplers[0].wgsl_group1_binding_n = 1;
        shd.samplers[0].spirv_set1_binding_n = 1;

        shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[0].view_slot = 0;
        shd.texture_sampler_pairs[0].sampler_slot = 0;
        shd.texture_sampler_pairs[0].glsl_name = "atlas_tex";

        m_texShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_texShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.label = "tileshape-tex-pip";
        m_texPip = sg_make_pipeline(&pip);
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kColorVsHlsl;
        shd.fragment_func.source = kColorFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kColorVsMsl;
        shd.fragment_func.source = kColorFsMsl;
#else
        shd.vertex_func.source = kColorVsGlsl;
        shd.fragment_func.source = kColorFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        m_colorShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_colorShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.primitive_type = SG_PRIMITIVETYPE_LINES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.label = "tileshape-color-pip";
        m_colorPip = sg_make_pipeline(&pip);

        // Same vertex format as the line pipeline, but triangles — cliff walls.
        sg_pipeline_desc wallPip = {};
        wallPip.shader = m_colorShd;
        wallPip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        wallPip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        wallPip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        wallPip.label = "tileshape-wall-pip";
        m_wallPip = sg_make_pipeline(&wallPip);
    }

    // Z-buffered raised pass: same shading as the painter pipelines, but the
    // vertex carries a normalized depth and overlaps resolve via the depth
    // buffer (LEQUAL + write) instead of the CPU painter sort.
    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kDepthTexVsHlsl;
        shd.fragment_func.source = kTexFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kDepthTexVsMsl;
        shd.fragment_func.source = kTexFsMsl;
#else
        shd.vertex_func.source = kDepthTexVsGlsl;
        shd.fragment_func.source = kTexFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);

        shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[0].texture.hlsl_register_t_n = 0;
        shd.views[0].texture.msl_texture_n = 0;
        shd.views[0].texture.wgsl_group1_binding_n = 0;
        shd.views[0].texture.spirv_set1_binding_n = 0;

        shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.samplers[0].hlsl_register_s_n = 0;
        shd.samplers[0].msl_sampler_n = 0;
        shd.samplers[0].wgsl_group1_binding_n = 1;
        shd.samplers[0].spirv_set1_binding_n = 1;

        shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[0].view_slot = 0;
        shd.texture_sampler_pairs[0].sampler_slot = 0;
        shd.texture_sampler_pairs[0].glsl_name = "atlas_tex";

        m_depthTexShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_depthTexShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
        pip.label = "tileshape-depth-tex-pip";
        m_depthTexPip = sg_make_pipeline(&pip);
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kDepthColorVsHlsl;
        shd.fragment_func.source = kColorFsHlsl;
        shd.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd.attrs[0].hlsl_sem_index = 0;
        shd.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd.attrs[1].hlsl_sem_index = 1;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kDepthColorVsMsl;
        shd.fragment_func.source = kColorFsMsl;
#else
        shd.vertex_func.source = kDepthColorVsGlsl;
        shd.fragment_func.source = kColorFsGlsl;
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        m_depthWallShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_depthWallShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
        pip.label = "tileshape-depth-wall-pip";
        m_depthWallPip = sg_make_pipeline(&pip);
    }

    // Triplanar walls: the wall fragment samples a tiling rock texture in
    // world-space projections instead of using the baked color alone.
    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kTriWallVsHlsl;
        shd.fragment_func.source = kTriWallFsHlsl;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kTriWallVsMsl;
        shd.fragment_func.source = kTriWallFsMsl;
#else
        shd.vertex_func.source = kTriWallVsGlsl;
        shd.fragment_func.source = kTriWallFsGlsl;
#endif
        for (int i = 0; i < 4; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
        fillTriVsUniformDesc(&shd.uniform_blocks[0]);
        fillTextureSlotDesc(&shd, "wall_tex");
        m_triWallShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_triWallShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.label = "tileshape-tri-wall-pip";
        m_triWallPip = sg_make_pipeline(&pip);
    }

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kTriDepthWallVsHlsl;
        shd.fragment_func.source = kTriWallFsHlsl;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kTriDepthWallVsMsl;
        shd.fragment_func.source = kTriWallFsMsl;
#else
        shd.vertex_func.source = kTriDepthWallVsGlsl;
        shd.fragment_func.source = kTriWallFsGlsl;
#endif
        for (int i = 0; i < 4; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
        fillTriVsUniformDesc(&shd.uniform_blocks[0]);
        fillTextureSlotDesc(&shd, "wall_tex");
        m_triDepthWallShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_triDepthWallShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
        pip.label = "tileshape-tri-depth-wall-pip";
        m_triDepthWallPip = sg_make_pipeline(&pip);
    }
}

void AtlasRenderer::destroyPipelines() {
    if (m_texPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_texPip);
        m_texPip = {};
    }
    if (m_colorPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_colorPip);
        m_colorPip = {};
    }
    if (m_wallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_wallPip);
        m_wallPip = {};
    }
    if (m_depthTexPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_depthTexPip);
        m_depthTexPip = {};
    }
    if (m_depthWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_depthWallPip);
        m_depthWallPip = {};
    }
    if (m_triWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_triWallPip);
        m_triWallPip = {};
    }
    if (m_triDepthWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_triDepthWallPip);
        m_triDepthWallPip = {};
    }
    if (m_texShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_texShd);
        m_texShd = {};
    }
    if (m_colorShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_colorShd);
        m_colorShd = {};
    }
    if (m_depthTexShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_depthTexShd);
        m_depthTexShd = {};
    }
    if (m_depthWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_depthWallShd);
        m_depthWallShd = {};
    }
    if (m_triWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_triWallShd);
        m_triWallShd = {};
    }
    if (m_triDepthWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_triDepthWallShd);
        m_triDepthWallShd = {};
    }
}
