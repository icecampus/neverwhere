#include "pch.h"

#include "BrepRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

#include <spdlog/spdlog.h>

// One TU per binary owns the stb_image implementation (the screenshot helper
// owns stb_image_write).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

// ---------------------------------------------------------------------------
// B-rep pass shaders: pos (field-space, z pre-baked) + normal + world + baked
// quad color. One source set per backend (GLSL/HLSL/MSL), compile-time pick
// like the neighboring playgrounds.
// ---------------------------------------------------------------------------

static const char* kBrepVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in vec3 world;
layout(location=3) in vec4 color;
out vec3 v_normal;
out vec3 v_world;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_normal = normal;
    v_world = world;
    v_color = color;
}
)";

static const char* kBrepFsGlsl = R"(
#version 330
in vec3 v_normal;
in vec3 v_world;
in vec4 v_color;
out vec4 frag_color;
uniform vec4 light_dir;   // xyz: direction towards the sun
uniform vec4 view_dir;    // xyz: constant iso view direction (viewer -> scene)
uniform vec4 params0;     // x: ambient, y: diffuse, z: spec strength, w: spec power
uniform vec4 params1;     // x: gamma, y: material V tiling factor (map aspect)
uniform vec4 params2;     // x: material tiling, y: albedo, z: normal, w: AO
uniform vec4 params3;     // x: roughness
uniform sampler2D mat_color;
uniform sampler2D mat_normal;
uniform sampler2D mat_ao;
uniform sampler2D mat_rough;

// Material UV: 2:1 sets (Ground061) tile V twice as fast to keep the features
// square in world space; square sets (marble_cliff_01) use the 1x factor. The
// factor arrives in params1.y, measured from the map aspect at load.
vec2 matuv(vec2 q) {
    return q * vec2(params2.x, params2.x * params1.y);
}

void main() {
    vec3 n = normalize(v_normal);
    vec3 p = v_world;
    vec3 base = v_color.rgb;

    // Dominant-axis box projection: the B-rep surface is mostly vertical
    // walls, where a top-only material projection would streak.
    vec3 an = abs(n);
    vec2 duv;
    vec3 tang;
    vec3 bitang;
    if (an.y >= an.x && an.y >= an.z) {
        duv = p.xz; tang = vec3(1.0, 0.0, 0.0); bitang = vec3(0.0, 0.0, 1.0);
    } else if (an.x >= an.z) {
        duv = p.zy; tang = vec3(0.0, 0.0, 1.0); bitang = vec3(0.0, 1.0, 0.0);
    } else {
        duv = p.xy; tang = vec3(1.0, 0.0, 0.0); bitang = vec3(0.0, 1.0, 0.0);
    }

    // --- Material set: each strength at 0 disables its channel — at all
    // zeros the shader is bit-for-bit the plain quad color.
    // Albedo: triplanar over the world position (the mesh has no UVs),
    // weights sharpened to ^4 towards the dominant axis.
    if (params2.y > 0.0) {
        vec3 tw = an * an;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        vec3 alb = tw.x * texture(mat_color, matuv(p.zy)).rgb +
            tw.y * texture(mat_color, matuv(p.xz)).rgb +
            tw.z * texture(mat_color, matuv(p.xy)).rgb;
        base = mix(base, alb, params2.y);
    }
    // Normal detail along the dominant axis, NormalGL (Y+) convention.
    if (params2.z > 0.0) {
        vec3 nts = texture(mat_normal, matuv(duv)).rgb * 2.0 - 1.0;
        vec3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(mix(n, nm, params2.z));
    }
    // Cheap sun lambert + ambient + Blinn spec; the iso view direction is
    // constant (viewer -> scene, so the Blinn half vector is l - rd).
    vec3 l = normalize(light_dir.xyz);
    vec3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    // AO dims the ambient term (the baked cavity of the material).
    float ambient = params0.x;
    if (params2.w > 0.0) {
        ambient *= mix(1.0, texture(mat_ao, matuv(duv)).r, params2.w);
    }
    vec3 col = base * (ambient + params0.y * max(ndl, 0.0));
    // Roughness kills the spec on the material.
    float specAmt = params0.z;
    if (params3.x > 0.0) {
        specAmt *= mix(1.0, 1.0 - texture(mat_rough, matuv(duv)).r, params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params0.w);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(params1.x)), v_color.a);
}
)";

static const char* kBrepVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal: TEXCOORD1;
    float3 world: TEXCOORD2;
    float4 color: TEXCOORD3;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal: TEXCOORD0;
    float3 world: TEXCOORD1;
    float4 color: TEXCOORD2;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.normal = inp.normal;
    o.world = inp.world;
    o.color = inp.color;
    return o;
}
)";

static const char* kBrepFsHlsl = R"(
cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 view_dir;
    float4 params0; // x: ambient, y: diffuse, z: spec strength, w: spec power
    float4 params1; // x: gamma, y: material V tiling factor (map aspect)
    float4 params2; // x: material tiling, y: albedo, z: normal, w: AO
    float4 params3; // x: roughness
};

Texture2D mat_color: register(t0);
Texture2D mat_normal: register(t1);
Texture2D mat_ao: register(t2);
Texture2D mat_rough: register(t3);
SamplerState mat_smp: register(s0);

// Material UV: 2:1 sets tile V twice as fast; square sets use the 1x factor
// (params1.y, measured from the map aspect at load).
float2 matuv(float2 q) {
    return q * float2(params2.x, params2.x * params1.y);
}

struct PSIn {
    float4 pos: SV_Position;
    float3 normal: TEXCOORD0;
    float3 world: TEXCOORD1;
    float4 color: TEXCOORD2;
};

float4 main(PSIn inp): SV_Target {
    float3 n = normalize(inp.normal);
    float3 p = inp.world;
    float3 base = inp.color.rgb;

    // Dominant-axis box projection (see the GLSL twin).
    float3 an = abs(n);
    float2 duv;
    float3 tang;
    float3 bitang;
    if (an.y >= an.x && an.y >= an.z) {
        duv = p.xz; tang = float3(1.0, 0.0, 0.0); bitang = float3(0.0, 0.0, 1.0);
    } else if (an.x >= an.z) {
        duv = p.zy; tang = float3(0.0, 0.0, 1.0); bitang = float3(0.0, 1.0, 0.0);
    } else {
        duv = p.xy; tang = float3(1.0, 0.0, 0.0); bitang = float3(0.0, 1.0, 0.0);
    }

    if (params2.y > 0.0) {
        float3 tw = an * an;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        float3 alb = tw.x * mat_color.Sample(mat_smp, matuv(p.zy)).rgb +
            tw.y * mat_color.Sample(mat_smp, matuv(p.xz)).rgb +
            tw.z * mat_color.Sample(mat_smp, matuv(p.xy)).rgb;
        base = lerp(base, alb, params2.y);
    }
    if (params2.z > 0.0) {
        float3 nts = mat_normal.Sample(mat_smp, matuv(duv)).rgb * 2.0 - 1.0;
        float3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(lerp(n, nm, params2.z));
    }
    float3 l = normalize(light_dir.xyz);
    float3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    float ambient = params0.x;
    if (params2.w > 0.0) {
        ambient *= lerp(1.0, mat_ao.Sample(mat_smp, matuv(duv)).r, params2.w);
    }
    float3 col = base * (ambient + params0.y * max(ndl, 0.0));
    float specAmt = params0.z;
    if (params3.x > 0.0) {
        specAmt *= lerp(1.0, 1.0 - mat_rough.Sample(mat_smp, matuv(duv)).r, params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params0.w);
    return float4(pow(clamp(col, 0.0, 1.0), (float3)params1.x), inp.color.a);
}
)";

static const char* kBrepVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 world [[attribute(2)]];
    float4 color [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float3 normal;
    float3 world;
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
    o.normal = in.normal;
    o.world = in.world;
    o.color = in.color;
    return o;
}
)";

static const char* kBrepFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float4 light_dir;
    float4 view_dir;
    float4 params0; // x: ambient, y: diffuse, z: spec strength, w: spec power
    float4 params1; // x: gamma, y: material V tiling factor (map aspect)
    float4 params2; // x: material tiling, y: albedo, z: normal, w: AO
    float4 params3; // x: roughness
};

struct PSIn {
    float4 pos [[position]];
    float3 normal;
    float3 world;
    float4 color;
};

// Material UV: 2:1 sets tile V twice as fast; square sets use the 1x factor.
// MSL helpers cannot see the uniform block, so tiling and the V factor arrive
// as arguments.
float2 matuv(float2 q, float tiling, float vTile) {
    return q * float2(tiling, tiling * vTile);
}

fragment float4 _main(PSIn in [[stage_in]], constant FsParams& fs [[buffer(1)]],
                      texture2d<float> mat_color [[texture(0)]],
                      texture2d<float> mat_normal [[texture(1)]],
                      texture2d<float> mat_ao [[texture(2)]],
                      texture2d<float> mat_rough [[texture(3)]],
                      sampler mat_smp [[sampler(0)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world;
    float3 base = in.color.rgb;

    // Dominant-axis box projection (see the GLSL twin).
    float3 an = abs(n);
    float2 duv;
    float3 tang;
    float3 bitang;
    if (an.y >= an.x && an.y >= an.z) {
        duv = p.xz; tang = float3(1.0, 0.0, 0.0); bitang = float3(0.0, 0.0, 1.0);
    } else if (an.x >= an.z) {
        duv = p.zy; tang = float3(0.0, 0.0, 1.0); bitang = float3(0.0, 1.0, 0.0);
    } else {
        duv = p.xy; tang = float3(1.0, 0.0, 0.0); bitang = float3(0.0, 1.0, 0.0);
    }

    if (fs.params2.y > 0.0) {
        float3 tw = an * an;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        float3 alb = tw.x * mat_color.sample(mat_smp, matuv(p.zy, fs.params2.x, fs.params1.y)).rgb +
            tw.y * mat_color.sample(mat_smp, matuv(p.xz, fs.params2.x, fs.params1.y)).rgb +
            tw.z * mat_color.sample(mat_smp, matuv(p.xy, fs.params2.x, fs.params1.y)).rgb;
        base = mix(base, alb, fs.params2.y);
    }
    if (fs.params2.z > 0.0) {
        float3 nts = mat_normal.sample(mat_smp, matuv(duv, fs.params2.x, fs.params1.y)).rgb * 2.0 - 1.0;
        float3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(mix(n, nm, fs.params2.z));
    }
    float3 l = normalize(fs.light_dir.xyz);
    float3 rd = fs.view_dir.xyz;
    float ndl = dot(n, l);
    float ambient = fs.params0.x;
    if (fs.params2.w > 0.0) {
        ambient *= mix(1.0, mat_ao.sample(mat_smp, matuv(duv, fs.params2.x, fs.params1.y)).r, fs.params2.w);
    }
    float3 col = base * (ambient + fs.params0.y * max(ndl, 0.0));
    float specAmt = fs.params0.z;
    if (fs.params3.x > 0.0) {
        specAmt *= mix(1.0, 1.0 - mat_rough.sample(mat_smp, matuv(duv, fs.params2.x, fs.params1.y)).r, fs.params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), fs.params0.w);
    return float4(pow(clamp(col, 0.0, 1.0), float3(fs.params1.x)), in.color.a);
}
)";

void fillVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(BrepRenderer::VsParams);
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

// Fragment block (light + material): uniform slot 1, after the vertex block.
void fillFsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = sizeof(BrepFsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 1;
    block->wgsl_group0_binding_n = 1;
    block->spirv_set0_binding_n = 1;
    const char* names[6] = {"light_dir", "view_dir", "params0", "params1", "params2", "params3"};
    for (int i = 0; i < 6; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
}

// Full RGBA8 mip chain via 2x2 box filter, halving until 1x1 (odd sizes
// clamp the second tap). Material maps only — never atlases, whose tiles
// would bleed into each other in the low levels.
std::vector<std::vector<std::uint8_t>> buildMipChain(const std::uint8_t* rgba, int w, int h) {
    std::vector<std::vector<std::uint8_t>> levels;
    levels.emplace_back(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2);
        const int nh = std::max(1, h / 2);
        const std::vector<std::uint8_t>& src = levels.back();
        std::vector<std::uint8_t> dst(static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4);
        for (int y = 0; y < nh; ++y) {
            const int y0 = std::min(2 * y, h - 1);
            const int y1 = std::min(2 * y + 1, h - 1);
            for (int x = 0; x < nw; ++x) {
                const int x0 = std::min(2 * x, w - 1);
                const int x1 = std::min(2 * x + 1, w - 1);
                for (int c = 0; c < 4; ++c) {
                    const int sum = static_cast<int>(src[(static_cast<size_t>(y0) * w + x0) * 4 + c]) +
                        static_cast<int>(src[(static_cast<size_t>(y0) * w + x1) * 4 + c]) +
                        static_cast<int>(src[(static_cast<size_t>(y1) * w + x0) * 4 + c]) +
                        static_cast<int>(src[(static_cast<size_t>(y1) * w + x1) * 4 + c]);
                    dst[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    return levels;
}

} // namespace

void appendBrepQuadVertices(
    const landscape_mesh::MeshQuad& quad,
    glm::ivec2 origin,
    float halfW,
    float halfH,
    float heightScale,
    std::vector<BrepVertex>& out) {

    // Wall panels take the mesh-authoritative outward normal (the displaced
    // facet normal would shade the rock displacement twice); flat parts use
    // their own facet normal.
    const landscape_mesh::Vec3 n = quad.cliffWall ? landscape_mesh::litWallNormal(quad) : quad.normal;
    const auto push = [&](const landscape_mesh::Vec3& v) {
        // The composer runs at cellSize 1, so mesh units ARE map cells; the
        // projection matches DiamondIsometry::nodeToField (no +halfH on y).
        const float mapX = static_cast<float>(origin.x) + v.x;
        const float mapZ = static_cast<float>(origin.y) + v.z;
        const float fieldX = (mapX - mapZ) * halfW + halfW;
        const float fieldY = (mapX + mapZ) * halfH;
        const float liftPx = v.y * heightScale;
        out.push_back(BrepVertex{
            fieldX,
            fieldY - liftPx,
            brepBakedDepth(fieldY, liftPx),
            n.x,
            n.y,
            n.z,
            mapX,
            v.y,
            mapZ,
            quad.color.r / 255.0f,
            quad.color.g / 255.0f,
            quad.color.b / 255.0f,
            quad.color.a / 255.0f});
    };
    push(quad.a);
    push(quad.b);
    push(quad.c);
    push(quad.a);
    push(quad.c);
    push(quad.d);
}

void BrepRenderer::init() {
    sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
    shd.vertex_func.source = kBrepVsHlsl;
    shd.fragment_func.source = kBrepFsHlsl;
#elif defined(SOKOL_METAL)
    shd.vertex_func.source = kBrepVsMsl;
    shd.fragment_func.source = kBrepFsMsl;
#else
    shd.vertex_func.source = kBrepVsGlsl;
    shd.fragment_func.source = kBrepFsGlsl;
#endif
    for (int i = 0; i < 4; ++i) {
        shd.attrs[i].hlsl_sem_name = "TEXCOORD";
        shd.attrs[i].hlsl_sem_index = i;
    }
    fillVsUniformDesc(&shd.uniform_blocks[0]);
    fillFsUniformDesc(&shd.uniform_blocks[1]);

    // Material maps: color/normal/AO/roughness as texture views 0..3, all
    // sampled with the one shared REPEAT sampler (slot 0).
    const char* matGlslNames[4] = {"mat_color", "mat_normal", "mat_ao", "mat_rough"};
    for (int i = 0; i < 4; ++i) {
        shd.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[i].texture.hlsl_register_t_n = i;
        shd.views[i].texture.msl_texture_n = i;
        shd.views[i].texture.wgsl_group1_binding_n = i * 2;
        shd.views[i].texture.spirv_set1_binding_n = i * 2;

        shd.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[i].view_slot = i;
        shd.texture_sampler_pairs[i].sampler_slot = 0;
        shd.texture_sampler_pairs[i].glsl_name = matGlslNames[i];
    }

    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.samplers[0].hlsl_register_s_n = 0;
    shd.samplers[0].msl_sampler_n = 0;
    shd.samplers[0].wgsl_group1_binding_n = 1;
    shd.samplers[0].spirv_set1_binding_n = 1;

    shd.label = "brep-shd";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3; // normal
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3; // world
    pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // The only depth-writing pass here: the mesh must self-occlude (displaced
    // wall panels fold over themselves). Quads arrive in either winding, so
    // no culling (default SG_CULLMODE_NONE).
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "brep-pip";
    m_pip = sg_make_pipeline(&pip);

    sg_sampler_desc matSmp = {};
    matSmp.min_filter = SG_FILTER_LINEAR;
    matSmp.mag_filter = SG_FILTER_LINEAR;
    // The material maps carry a full CPU-built mip chain (see
    // uploadSlotMipmapped): without it the minified REPEAT texture aliases
    // into noise a few cells out. Anisotropy keeps the grazing iso angle
    // crisp; sokol clamps/ignores it when the extension is missing.
    matSmp.mipmap_filter = SG_FILTER_LINEAR;
    matSmp.max_anisotropy = 8;
    matSmp.wrap_u = SG_WRAP_REPEAT;
    matSmp.wrap_v = SG_WRAP_REPEAT;
    matSmp.label = "brep-mat-smp";
    m_matSampler = sg_make_sampler(&matSmp);

    // Placeholders until a material set loads: keeps the bindings valid (the
    // shader always samples; strengths live in params2/params3). White color,
    // flat tangent normal (128,128,255), white AO/roughness.
    const std::uint32_t placeholderPx[4] = {
        0xFFFFFFFFu, // color: white
        0xFFFF8080u, // normal: flat (little-endian RGBA bytes 128,128,255,255)
        0xFFFFFFFFu, // AO: white
        0xFFFFFFFFu, // roughness: white
    };
    for (int i = 0; i < 4; ++i) {
        sg_image_desc img = {};
        img.width = 1;
        img.height = 1;
        img.pixel_format = SG_PIXELFORMAT_RGBA8;
        img.data.mip_levels[0].ptr = &placeholderPx[i];
        img.data.mip_levels[0].size = sizeof(placeholderPx[i]);
        img.label = "brep-mat-placeholder";
        m_matMaps[i].image = sg_make_image(&img);
        sg_view_desc view = {};
        view.texture.image = m_matMaps[i].image;
        m_matMaps[i].view = sg_make_view(&view);
    }

    m_ready = true;
}

void BrepRenderer::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vbuf);
        m_vbuf = {};
    }
    m_vbufSize = 0;
    if (m_pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_pip);
        m_pip = {};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {};
    }
    if (m_matSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_matSampler);
        m_matSampler = {};
    }
    for (int i = 0; i < 4; ++i) {
        destroySlot(m_matMaps[i]);
    }
    m_ready = false;
}

void BrepRenderer::destroySlot(MatSlot& slot) {
    if (slot.view.id != SG_INVALID_ID) {
        sg_destroy_view(slot.view);
        slot.view = {};
    }
    if (slot.image.id != SG_INVALID_ID) {
        sg_destroy_image(slot.image);
        slot.image = {};
    }
}

bool BrepRenderer::uploadSlotMipmapped(
    MatSlot& slot,
    const void* rgba,
    int width,
    int height,
    const char* label) {

    destroySlot(slot);

    // sg_make_image copies the levels synchronously, so a local chain is fine.
    const std::vector<std::vector<std::uint8_t>> levels =
        buildMipChain(static_cast<const std::uint8_t*>(rgba), width, height);
    sg_image_desc desc = {};
    desc.width = width;
    desc.height = height;
    desc.num_mipmaps = static_cast<int>(levels.size());
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    for (size_t i = 0; i < levels.size(); ++i) {
        desc.data.mip_levels[i].ptr = levels[i].data();
        desc.data.mip_levels[i].size = levels[i].size();
    }
    desc.label = label;
    slot.image = sg_make_image(&desc);
    if (slot.image.id == SG_INVALID_ID) {
        spdlog::error("BrepRenderer: sg_make_image failed ({})", label ? label : "?");
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = slot.image;
    slot.view = sg_make_view(&viewDesc);
    return slot.view.id != SG_INVALID_ID;
}

namespace {

// Naming conventions probed per channel (stb-readable formats only — EXR
// sources must be converted to PNG first, tmp/convert_exr.py): ambientCG
// "<set>_Color.jpg" style and Poly Haven "<set>_diff_4k.jpg" style, plus the
// `_2k` variants of the latter (e.g. marble_cliff_03 ships 2k maps).
constexpr const char* kMatCandidates[4][8] = {
    {"_Color.jpg", "_Color.png", "_diff_4k.jpg", "_diff_4k.png", "_diff_2k.jpg", "_diff_2k.png", "_diff.jpg", "_diff.png"},
    {"_NormalGL.jpg", "_NormalGL.png", "_nor_gl_4k.jpg", "_nor_gl_4k.png", "_nor_gl_2k.jpg", "_nor_gl_2k.png", "_nor_gl.jpg", "_nor_gl.png"},
    {"_AmbientOcclusion.jpg", "_AmbientOcclusion.png", "_ao_4k.jpg", "_ao_4k.png", "_ao_2k.jpg", "_ao_2k.png", "_ao.jpg", "_ao.png"},
    {"_Roughness.jpg", "_Roughness.png", "_rough_4k.jpg", "_rough_4k.png", "_rough_2k.jpg", "_rough_2k.png", "_rough.jpg", "_rough.png"},
};
constexpr const char* kMatLabels[4] = {"mat-color", "mat-normal", "mat-ao", "mat-rough"};

// First existing candidate path for a channel, or "".
std::string findMatCandidate(const std::string& dir, const std::string& setName, int channel) {
    std::error_code ec;
    for (const char* suffix : kMatCandidates[channel]) {
        const std::string path = dir + "/" + setName + suffix;
        if (std::filesystem::exists(path, ec)) {
            return path;
        }
    }
    return {};
}

} // namespace

int probeMaterialMaps(const std::string& dir, const std::string& setName) {
    int mask = 0;
    for (int i = 0; i < 4; ++i) {
        if (!findMatCandidate(dir, setName, i).empty()) {
            mask |= 1 << i;
        }
    }
    return mask;
}

int BrepRenderer::loadMaterialMaps(const std::string& dir, const std::string& setName) {
    int loaded = 0;
    for (int i = 0; i < 4; ++i) {
        const std::string path = findMatCandidate(dir, setName, i);
        if (!path.empty()) {
            int w = 0, h = 0, comp = 0;
            stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
            if (pixels && w > 0 && h > 0) {
                // V tiling factor from the map aspect: 2 for 2:1 sets
                // (Ground061), 1 for square ones (Poly Haven cliffs) — keeps
                // features square in world space. All maps of a set share the
                // aspect; first wins.
                if (loaded == 0) {
                    m_matVTile = static_cast<float>(std::max(1, static_cast<int>(std::lround(
                        static_cast<double>(w) / static_cast<double>(h)))));
                }
                const bool ok = uploadSlotMipmapped(m_matMaps[i], pixels, w, h, kMatLabels[i]);
                stbi_image_free(pixels);
                if (!ok) {
                    spdlog::error("BrepRenderer: material map '{}' upload failed", path);
                } else {
                    spdlog::info("BrepRenderer: {} <- {} ({}x{})", kMatLabels[i], path, w, h);
                    loaded |= 1 << i;
                }
            } else if (pixels) {
                stbi_image_free(pixels);
            }
        }
        // Missing map: the placeholder stays bound and the shader falls back
        // towards the quad color for that channel.
    }
    spdlog::info("BrepRenderer: material '{}' from '{}': mask={:#x}, vTile={}",
        setName, dir, loaded, m_matVTile);
    return loaded;
}

void BrepRenderer::setContent(
    const std::uint8_t* nodes,
    int nodesX,
    int nodesY,
    const BrepGenParams& params) {

    const std::size_t count = static_cast<std::size_t>(std::max(nodesX, 0)) * static_cast<std::size_t>(std::max(nodesY, 0));
    const bool nodesChanged = nodesX != m_nodesX || nodesY != m_nodesY ||
        (count > 0 &&
            (!nodes || m_nodes.size() != count || std::memcmp(m_nodes.data(), nodes, count) != 0));
    const bool meshParamsChanged =
        params.raisedHeight != m_params.raisedHeight ||
        params.rockSeed != m_params.rockSeed ||
        params.rockAmplitude != m_params.rockAmplitude ||
        params.cornerBevel != m_params.cornerBevel ||
        params.rockEnabled != m_params.rockEnabled ||
        params.wallSubdivH != m_params.wallSubdivH ||
        params.wallSubdivV != m_params.wallSubdivV ||
        params.wallStyle != m_params.wallStyle;
    const bool streamParamsChanged = params.heightScale != m_params.heightScale;

    if (nodesChanged || meshParamsChanged) {
        if (count > 0 && nodes) {
            m_nodes.assign(nodes, nodes + count);
        } else {
            m_nodes.clear();
        }
        m_nodesX = nodesX;
        m_nodesY = nodesY;
        m_params = params;
        m_meshDirty = true;
        m_streamDirty = false;
        m_pendingSince = -1.0;
        m_stats.pending = true;
    } else if (streamParamsChanged) {
        m_params.heightScale = params.heightScale;
        if (!m_meshDirty) {
            // Cheap path: re-bake the cached quads with the new lift, no
            // composer run and no debounce (the slider stays live).
            m_streamDirty = true;
            m_stats.pending = true;
        }
    }
}

void BrepRenderer::update(const topology_core::DiamondIsometry& iso, double nowSec) {
    if (m_meshDirty) {
        if (m_pendingSince < 0.0) {
            m_pendingSince = nowSec;
        }
        if (nowSec - m_pendingSince >= 0.3) {
            rebuildMesh(iso);
            m_meshDirty = false;
            m_pendingSince = -1.0;
            m_stats.pending = false;
        }
    } else if (m_streamDirty) {
        rebakeStream(iso);
        m_streamDirty = false;
        m_stats.pending = false;
    }
}

void BrepRenderer::rebuildMesh(const topology_core::DiamondIsometry& iso) {
    const auto t0 = std::chrono::steady_clock::now();

    // Full path for the whole node field: on-nodes -> bbox node grid (1-cell
    // margin, the boundary walls/bevels must not cross the grid border) ->
    // cell solid-mask -> landscape_mesh compose -> quad cache. The editor's
    // CyclopeanRenderer::rebuildMesh runs this same pipeline.
    m_quads.clear();
    m_stats.quadCount = 0;
    m_stats.topQuadCount = 0;
    m_stats.wallQuadCount = 0;
    m_stats.seamsPassed = true;
    m_stats.seamMismatches = 0;
    m_stats.seamCheckedEdges = 0;

    int minX = 0;
    int minY = 0;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < m_nodesY; ++y) {
        for (int x = 0; x < m_nodesX; ++x) {
            if (m_nodes[static_cast<std::size_t>(y) * m_nodesX + x] == 0) {
                continue;
            }
            if (maxX < 0) {
                minX = maxX = x;
                minY = maxY = y;
            } else {
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    if (maxX >= 0) {
        minX -= 1;
        minY -= 1;
        maxX += 1;
        maxY += 1;
        const int bboxNodesX = maxX - minX + 1;
        const int bboxNodesY = maxY - minY + 1;
        std::vector<std::uint8_t> bboxNodes(static_cast<std::size_t>(bboxNodesX) * bboxNodesY, 0);
        for (int y = 0; y < m_nodesY; ++y) {
            for (int x = 0; x < m_nodesX; ++x) {
                if (m_nodes[static_cast<std::size_t>(y) * m_nodesX + x] != 0) {
                    bboxNodes[static_cast<std::size_t>(y - minY) * bboxNodesX + (x - minX)] = 1;
                }
            }
        }

        landscape_mesh::SolidMeshBuildRequest request;
        request.mask = landscape_mesh::solidMaskFromNodes(bboxNodes.data(), bboxNodesX, bboxNodesY);
        request.baseHeight = 0.0f;
        request.topHeight = m_params.raisedHeight;
        request.level = 1;
        request.maxLevel = 1;
        request.includeWalls = true;
        request.fadeWallDisplacementAtBottom = false;

        // Single-level plateau: the level height equals the plateau top.
        landscape_mesh::MeshBuildSettings settings;
        settings.cellSize = 1.0f;
        settings.levelHeight = m_params.raisedHeight;
        settings.cornerBevel = m_params.cornerBevel;
        settings.rockEnabled = m_params.rockEnabled;
        settings.rockSeed = m_params.rockSeed;
        settings.rockAmplitude = m_params.rockAmplitude;
        settings.wallHorizontalSubdivisions = m_params.wallSubdivH;
        settings.wallVerticalSubdivisions = m_params.wallSubdivV;
        settings.wallStyle = m_params.wallStyle;

        const landscape_mesh::CompositionResult result =
            landscape_mesh::composeSolidMaskMesh(request, settings);
        if (!result.seams.passed) {
            spdlog::warn("BrepRenderer: wall mesh seam validation failed ({} of {} edges)",
                result.seams.mismatches, result.seams.checkedEdges);
        }

        m_origin = {minX, minY};
        m_stats.quadCount = static_cast<int>(result.quads.size());
        m_stats.topQuadCount = result.stats.topQuadCount;
        m_stats.wallQuadCount = result.stats.cliffWallQuadCount;
        m_stats.seamsPassed = result.seams.passed;
        m_stats.seamMismatches = result.seams.mismatches;
        m_stats.seamCheckedEdges = result.seams.checkedEdges;
        m_quads = result.quads;
    }

    rebakeStream(iso);

    const auto t1 = std::chrono::steady_clock::now();
    m_stats.rebuildMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    spdlog::info("BrepRenderer: rebuilt mesh at ({}, {}) — {} quads, {:.1f} ms",
        m_origin.x, m_origin.y, m_stats.quadCount, m_stats.rebuildMs);
}

void BrepRenderer::rebakeStream(const topology_core::DiamondIsometry& iso) {
    m_stream.clear();
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    m_stream.reserve(m_quads.size() * 6);
    for (const landscape_mesh::MeshQuad& quad : m_quads) {
        appendBrepQuadVertices(quad, m_origin, halfW, halfH, m_params.heightScale, m_stream);
    }
    m_stats.vertexCount = static_cast<int>(m_stream.size());
    uploadStream();
}

void BrepRenderer::uploadStream() {
    const std::size_t bytes = m_stream.size() * sizeof(BrepVertex);
    if (bytes == 0) {
        return;
    }
    if (m_vbufSize < bytes) {
        if (m_vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(m_vbuf);
        }
        sg_buffer_desc bufDesc = {};
        bufDesc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
        bufDesc.usage.dynamic_update = true;
        bufDesc.label = "brep-vbuf";
        m_vbuf = sg_make_buffer(&bufDesc);
        m_vbufSize = bufDesc.size;
    }
    if (m_vbuf.id != SG_INVALID_ID) {
        // One sg_update_buffer per buffer per frame: rebuilds/re-bakes run at
        // most once per frame from update(), so this is the only upload.
        sg_update_buffer(m_vbuf, sg_range{m_stream.data(), bytes});
    }
}

void BrepRenderer::render(
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    const BrepFsParams& fs) {

    if (!m_ready || m_stream.empty() || m_vbuf.id == SG_INVALID_ID) {
        return;
    }

    VsParams vs{};
    vs.view_size[0] = static_cast<float>(viewW);
    vs.view_size[1] = static_cast<float>(viewH);
    vs.camera_offset[0] = camera.offset.x;
    vs.camera_offset[1] = camera.offset.y;
    vs.camera_zoom = camera.zoom;

    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_vbuf;
    for (int i = 0; i < 4; ++i) {
        bind.views[i] = m_matMaps[i].view;
    }
    bind.samplers[0] = m_matSampler;

    sg_apply_pipeline(m_pip);
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
    sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
    sg_draw(0, static_cast<int>(m_stream.size()), 1);
}
