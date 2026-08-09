#include "AtlasRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

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
    // Atlas color only: the tile is a colored silhouette under the 3D mesh.
    vec4 mask = texture(atlas_tex, v_uv);
    if (mask.a < 0.05) discard;
    frag_color = mask;
}
)";

static const char* kColorVsGlsl = R"(
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

static const char* kColorFsGlsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

static const char* kTexVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
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
    // Atlas color only: the tile is a colored silhouette under the 3D mesh.
    float4 mask = atlas_tex.Sample(smp, inp.uv);
    if (mask.a < 0.05) discard;
    return mask;
}
)";

static const char* kColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
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

static const char* kColorFsHlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    return inp.color;
}
)";

// MSL shaders (Metal backend, macOS). Entry point "_main" is sokol's default
// for Metal; vertex attributes map by index ([[attribute(N)]]).
static const char* kTexVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
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
    // Atlas color only: the tile is a colored silhouette under the 3D mesh.
    float4 mask = atlas_tex.sample(smp, in.uv);
    if (mask.a < 0.05) {
        discard_fragment();
    }
    return mask;
}
)";

static const char* kColorVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
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

// ---------------------------------------------------------------------------
// Cliff pass (scalar-field surface nets): depth-baked vertices with a
// field-space normal, groove attribute and world position; the fragment
// shader is the CliffFieldPlayground omphalos palette (dark grooves, gold
// shell + veins, grassy tops, lambert + wrap + spec), with the perspective
// camera ray replaced by the constant iso view direction.
// ---------------------------------------------------------------------------

static const char* kCliffVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in float groove;
layout(location=3) in vec3 world;
layout(location=4) in float rim;
out vec3 v_normal;
out float v_groove;
out vec3 v_world;
out float v_rim;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_normal = normal;
    v_groove = groove;
    v_world = world;
    v_rim = rim;
}
)";

static const char* kCliffFsGlsl = R"(
#version 330
in vec3 v_normal;
in float v_groove;
in vec3 v_world;
in float v_rim;
out vec4 frag_color;
uniform vec4 light_dir;
uniform vec4 view_dir;
uniform vec4 dark_color;
uniform vec4 gold_color;
uniform vec4 grass_a;
uniform vec4 grass_b;
uniform vec4 params0;
uniform vec4 params1;
uniform vec4 params2;      // x: material tiling, y: albedo strength, z: normal strength, w: AO strength
uniform vec4 params3;      // x: roughness strength
uniform sampler2D mat_color;
uniform sampler2D mat_normal;
uniform sampler2D mat_ao;
uniform sampler2D mat_rough;

vec4 hashv4v3(vec3 p) {
    vec3 chash = vec3(37.0, 39.0, 41.0);
    return fract(sin(vec4(dot(p, chash), dot(p + vec3(1.0, 0.0, 0.0), chash),
        dot(p + vec3(0.0, 1.0, 0.0), chash), dot(p + vec3(0.0, 0.0, 1.0), chash))) * 43758.54);
}

float noisefv3(vec3 p) {
    vec3 ip = floor(p);
    vec3 fp = fract(p);
    fp *= fp * (3.0 - 2.0 * fp);
    vec4 t = mix(hashv4v3(ip), hashv4v3(ip + vec3(0.0, 0.0, 1.0)), fp.z);
    return mix(mix(t.x, t.y, fp.x), mix(t.z, t.w, fp.x), fp.y);
}

float fbm3(vec3 p) {
    float f = 0.0;
    float a = 1.0;
    for (int i = 0; i < 5; i++) {
        f += a * noisefv3(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

vec2 hashv2v2(vec2 p) {
    vec2 chash = vec2(37.0, 39.0);
    return fract(sin(vec2(dot(p, chash), dot(p + vec2(1.0, 0.0), chash))) * 43758.54);
}

float noisefv2(vec2 p) {
    vec2 ip = floor(p);
    vec2 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    vec2 t = mix(hashv2v2(ip), hashv2v2(ip + vec2(1.0, 0.0)), fp.y);
    return mix(t.x, t.y, fp.x);
}

float fbm2(vec2 p) {
    float f = 0.0;
    float a = 1.0;
    for (int j = 0; j < 5; ++j) {
        f += a * noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

// Material UV: the Ground061 maps are 2:1, so V tiles twice as fast to
// keep the features square in world space.
vec2 matuv(vec2 q) {
    return q * vec2(params2.x, params2.x * 2.0);
}

void main() {
    vec3 n = normalize(v_normal);
    vec3 p = v_world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, v_groove);
    vec3 gold = gold_color.rgb + vec3(1.0, 0.9, 0.4) * step(params0.x, f);
    vec3 rock = mix(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    vec3 grass = mix(grass_a.rgb, grass_b.rgb, gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
    vec3 base = mix(rock, grass, topMask);
    // --- Material (ambientCG Ground061): each strength at 0 disables its
    // channel — at all zeros the shader is bit-for-bit the palette one.
    // Albedo: triplanar over the world position (the mesh has no UVs),
    // weights sharpened to ^4 towards the dominant axis.
    if (params2.y > 0.0) {
        vec3 tw = abs(n);
        tw = tw * tw;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        vec3 alb = tw.x * texture(mat_color, matuv(p.zy)).rgb +
            tw.y * texture(mat_color, matuv(p.xz)).rgb +
            tw.z * texture(mat_color, matuv(p.xy)).rgb;
        base = mix(base, alb, params2.y);
    }
    // Normal detail: top projection (the beach reads mostly horizontal,
    // skirt included), tangent frame around the geometric normal,
    // NormalGL (Y+) convention. Remaps n before the lighting below.
    if (params2.z > 0.0) {
        vec3 nts = texture(mat_normal, matuv(p.xz)).rgb * 2.0 - 1.0;
        vec3 tang = abs(n.y) < 0.99 ? normalize(cross(vec3(0.0, 1.0, 0.0), n)) : vec3(1.0, 0.0, 0.0);
        vec3 bitang = cross(n, tang);
        vec3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(mix(n, nm, params2.z));
    }
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    vec3 l = normalize(light_dir.xyz);
    vec3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    // AO dims the ambient term (the baked cavity of the material).
    float ambient = params0.y;
    if (params2.w > 0.0) {
        ambient *= mix(1.0, texture(mat_ao, matuv(p.xz)).r, params2.w);
    }
    vec3 col = base * (ambient + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, params0.w, shell) * (1.0 - topMask);
    // Roughness kills the spec on the material (sand is rough).
    if (params3.x > 0.0) {
        specAmt *= mix(1.0, 1.0 - texture(mat_rough, matuv(p.xz)).r, params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(params1.y)), 1.0);
}
)";

static const char* kCliffVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal: TEXCOORD1;
    float groove: TEXCOORD2;
    float3 world: TEXCOORD3;
    float rim: TEXCOORD4;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal: TEXCOORD0;
    float groove: TEXCOORD1;
    float3 world: TEXCOORD2;
    float rim: TEXCOORD3;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.normal = inp.normal;
    o.groove = inp.groove;
    o.world = inp.world;
    o.rim = inp.rim;
    return o;
}
)";

static const char* kCliffFsHlsl = R"(
cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 view_dir;
    float4 dark_color;
    float4 gold_color;
    float4 grass_a;
    float4 grass_b;
    float4 params0; // x: vein threshold, y: ambient, z: diffuse, w: spec strength
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: unused
    float4 params2; // x: material tiling, y: albedo strength, z: normal strength, w: AO strength
    float4 params3; // x: roughness strength
};

float4 hashv4v3(float3 p) {
    float3 chash = float3(37.0, 39.0, 41.0);
    return frac(sin(float4(dot(p, chash), dot(p + float3(1.0, 0.0, 0.0), chash),
        dot(p + float3(0.0, 1.0, 0.0), chash), dot(p + float3(0.0, 0.0, 1.0), chash))) * 43758.54);
}

float noisefv3(float3 p) {
    float3 ip = floor(p);
    float3 fp = frac(p);
    fp *= fp * (3.0 - 2.0 * fp);
    float4 t = lerp(hashv4v3(ip), hashv4v3(ip + float3(0.0, 0.0, 1.0)), fp.z);
    return lerp(lerp(t.x, t.y, fp.x), lerp(t.z, t.w, fp.x), fp.y);
}

float fbm3(float3 p) {
    float f = 0.0;
    float a = 1.0;
    for (int i = 0; i < 5; i++) {
        f += a * noisefv3(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

float2 hashv2v2(float2 p) {
    float2 chash = float2(37.0, 39.0);
    return frac(sin(float2(dot(p, chash), dot(p + float2(1.0, 0.0), chash))) * 43758.54);
}

float noisefv2(float2 p) {
    float2 ip = floor(p);
    float2 fp = frac(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    float2 t = lerp(hashv2v2(ip), hashv2v2(ip + float2(1.0, 0.0)), fp.y);
    return lerp(t.x, t.y, fp.x);
}

float fbm2(float2 p) {
    float f = 0.0;
    float a = 1.0;
    for (int j = 0; j < 5; j++) {
        f += a * noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

Texture2D mat_color: register(t0);
Texture2D mat_normal: register(t1);
Texture2D mat_ao: register(t2);
Texture2D mat_rough: register(t3);
SamplerState mat_smp: register(s0);

// Material UV: the Ground061 maps are 2:1, so V tiles twice as fast to
// keep the features square in world space.
float2 matuv(float2 q) {
    return q * float2(params2.x, params2.x * 2.0);
}

struct PSIn {
    float4 pos: SV_Position;
    float3 normal: TEXCOORD0;
    float groove: TEXCOORD1;
    float3 world: TEXCOORD2;
    float rim: TEXCOORD3;
};

float4 main(PSIn inp): SV_Target {
    float3 n = normalize(inp.normal);
    float3 p = inp.world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, inp.groove);
    float3 gold = gold_color.rgb + float3(1.0, 0.9, 0.4) * step(params0.x, f);
    float3 rock = lerp(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = lerp(grass_a.rgb, grass_b.rgb, gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
    float3 base = lerp(rock, grass, topMask);
    // --- Material (ambientCG Ground061): each strength at 0 disables its
    // channel — at all zeros the shader is bit-for-bit the palette one.
    // Albedo: triplanar over the world position (the mesh has no UVs),
    // weights sharpened to ^4 towards the dominant axis.
    if (params2.y > 0.0) {
        float3 tw = abs(n);
        tw = tw * tw;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        float3 alb = tw.x * mat_color.Sample(mat_smp, matuv(p.zy)).rgb +
            tw.y * mat_color.Sample(mat_smp, matuv(p.xz)).rgb +
            tw.z * mat_color.Sample(mat_smp, matuv(p.xy)).rgb;
        base = lerp(base, alb, params2.y);
    }
    // Normal detail: top projection (the beach reads mostly horizontal,
    // skirt included), tangent frame around the geometric normal,
    // NormalGL (Y+) convention. Remaps n before the lighting below.
    if (params2.z > 0.0) {
        float3 nts = mat_normal.Sample(mat_smp, matuv(p.xz)).rgb * 2.0 - 1.0;
        float3 tang = abs(n.y) < 0.99 ? normalize(cross(float3(0.0, 1.0, 0.0), n)) : float3(1.0, 0.0, 0.0);
        float3 bitang = cross(n, tang);
        float3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(lerp(n, nm, params2.z));
    }
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(light_dir.xyz);
    float3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    // AO dims the ambient term (the baked cavity of the material).
    float ambient = params0.y;
    if (params2.w > 0.0) {
        ambient *= lerp(1.0, mat_ao.Sample(mat_smp, matuv(p.xz)).r, params2.w);
    }
    float3 col = base * (ambient + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = lerp(0.05, params0.w, shell) * (1.0 - topMask);
    // Roughness kills the spec on the material (sand is rough).
    if (params3.x > 0.0) {
        specAmt *= lerp(1.0, 1.0 - mat_rough.Sample(mat_smp, matuv(p.xz)).r, params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    return float4(pow(clamp(col, 0.0, 1.0), (float3)params1.y), 1.0);
}
)";

static const char* kCliffVsMsl = R"(
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
    float groove [[attribute(2)]];
    float3 world [[attribute(3)]];
    float rim [[attribute(4)]];
};

struct VSOut {
    float4 pos [[position]];
    float3 normal;
    float groove;
    float3 world;
    float rim;
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
    o.groove = in.groove;
    o.world = in.world;
    o.rim = in.rim;
    return o;
}
)";

static const char* kCliffFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float4 light_dir;
    float4 view_dir;
    float4 dark_color;
    float4 gold_color;
    float4 grass_a;
    float4 grass_b;
    float4 params0; // x: vein threshold, y: ambient, z: diffuse, w: spec strength
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: unused
    float4 params2; // x: material tiling, y: albedo strength, z: normal strength, w: AO strength
    float4 params3; // x: roughness strength
};

float4 hashv4v3(float3 p) {
    float3 chash = float3(37.0, 39.0, 41.0);
    return fract(sin(float4(dot(p, chash), dot(p + float3(1.0, 0.0, 0.0), chash),
        dot(p + float3(0.0, 1.0, 0.0), chash), dot(p + float3(0.0, 0.0, 1.0), chash))) * 43758.54);
}

float noisefv3(float3 p) {
    float3 ip = floor(p);
    float3 fp = fract(p);
    fp *= fp * (3.0 - 2.0 * fp);
    float4 t = mix(hashv4v3(ip), hashv4v3(ip + float3(0.0, 0.0, 1.0)), fp.z);
    return mix(mix(t.x, t.y, fp.x), mix(t.z, t.w, fp.x), fp.y);
}

float fbm3(float3 p) {
    float f = 0.0;
    float a = 1.0;
    for (int i = 0; i < 5; i++) {
        f += a * noisefv3(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

float2 hashv2v2(float2 p) {
    float2 chash = float2(37.0, 39.0);
    return fract(sin(float2(dot(p, chash), dot(p + float2(1.0, 0.0), chash))) * 43758.54);
}

float noisefv2(float2 p) {
    float2 ip = floor(p);
    float2 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    float2 t = mix(hashv2v2(ip), hashv2v2(ip + float2(1.0, 0.0)), fp.y);
    return mix(t.x, t.y, fp.x);
}

float fbm2(float2 p) {
    float f = 0.0;
    float a = 1.0;
    for (int j = 0; j < 5; j++) {
        f += a * noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

struct PSIn {
    float4 pos [[position]];
    float3 normal;
    float groove;
    float3 world;
    float rim;
};

// Material UV: the Ground061 maps are 2:1, so V tiles twice as fast to
// keep the features square in world space.
float2 matuv(float2 q, float tiling) {
    return q * float2(tiling, tiling * 2.0);
}

fragment float4 _main(PSIn in [[stage_in]], constant FsParams& fs [[buffer(1)]],
                      texture2d<float> mat_color [[texture(0)]],
                      texture2d<float> mat_normal [[texture(1)]],
                      texture2d<float> mat_ao [[texture(2)]],
                      texture2d<float> mat_rough [[texture(3)]],
                      sampler mat_smp [[sampler(0)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, in.groove);
    float3 gold = fs.gold_color.rgb + float3(1.0, 0.9, 0.4) * step(fs.params0.x, f);
    float3 rock = mix(fs.dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = mix(fs.grass_a.rgb, fs.grass_b.rgb, gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
    float3 base = mix(rock, grass, topMask);
    // --- Material (ambientCG Ground061): each strength at 0 disables its
    // channel — at all zeros the shader is bit-for-bit the palette one.
    // Albedo: triplanar over the world position (the mesh has no UVs),
    // weights sharpened to ^4 towards the dominant axis.
    if (fs.params2.y > 0.0) {
        float3 tw = abs(n);
        tw = tw * tw;
        tw = tw * tw;
        tw /= (tw.x + tw.y + tw.z);
        float3 alb = tw.x * mat_color.sample(mat_smp, matuv(p.zy, fs.params2.x)).rgb +
            tw.y * mat_color.sample(mat_smp, matuv(p.xz, fs.params2.x)).rgb +
            tw.z * mat_color.sample(mat_smp, matuv(p.xy, fs.params2.x)).rgb;
        base = mix(base, alb, fs.params2.y);
    }
    // Normal detail: top projection (the beach reads mostly horizontal,
    // skirt included), tangent frame around the geometric normal,
    // NormalGL (Y+) convention. Remaps n before the lighting below.
    if (fs.params2.z > 0.0) {
        float3 nts = mat_normal.sample(mat_smp, matuv(p.xz, fs.params2.x)).rgb * 2.0 - 1.0;
        float3 tang = abs(n.y) < 0.99 ? normalize(cross(float3(0.0, 1.0, 0.0), n)) : float3(1.0, 0.0, 0.0);
        float3 bitang = cross(n, tang);
        float3 nm = normalize(tang * nts.x + bitang * nts.y + n * nts.z);
        n = normalize(mix(n, nm, fs.params2.z));
    }
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(fs.light_dir.xyz);
    float3 rd = fs.view_dir.xyz;
    float ndl = dot(n, l);
    // AO dims the ambient term (the baked cavity of the material).
    float ambient = fs.params0.y;
    if (fs.params2.w > 0.0) {
        ambient *= mix(1.0, mat_ao.sample(mat_smp, matuv(p.xz, fs.params2.x)).r, fs.params2.w);
    }
    float3 col = base * (ambient + fs.params1.z * max(-ndl, 0.0) +
        fs.params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, fs.params0.w, shell) * (1.0 - topMask);
    // Roughness kills the spec on the material (sand is rough).
    if (fs.params3.x > 0.0) {
        specAmt *= mix(1.0, 1.0 - mat_rough.sample(mat_smp, matuv(p.xz, fs.params2.x)).r, fs.params3.x);
    }
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), fs.params1.x);
    return float4(pow(clamp(col, 0.0, 1.0), float3(fs.params1.y)), 1.0);
}
)";

// Normalized depth along the iso view ray. Field-y grows TOWARD the viewer, so
// with LESS_EQUAL + clear 1.0 the closer fragment needs the SMALLER z. The
// anchor is a constant rather than camera-derived: the cliff/stone/tech streams
// bake their z once at rebuild time while the overlay computes it every frame,
// and a camera-derived anchor would drift between the two as the view pans (a
// pan of one cell was enough to flip the grid from under the mesh to over it).
constexpr float kZFar = 100000.0f;
constexpr float kZScale = 1.0f / 200000.0f;

float bakedDepth(float fieldY) {
    return (kZFar - fieldY) * kZScale;
}

// The overlay (grid, hover footprint) rides the ground plane itself: every
// vertex takes the depth of its own field row, out of the same bakedDepth the
// mesh streams use. That is the water-plane reading of the grid, and it needs
// no height term to get right. A mesh fragment carries the depth of its own
// row while its height only shifts it UP the screen, i.e. over rows FARTHER
// than its own — so raised geometry always wins against the grid lines it
// covers, and geometry hanging BELOW the plane (the base slab, the underwater
// foot of the tech shoreline) shifts DOWN into nearer rows and loses to their
// grid lines, which is what reads as "under water".

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

// Cliff fragment block (palette/light): slot 1, after the vertex block.
void fillCliffFsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = sizeof(CliffFsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 1;
    block->wgsl_group0_binding_n = 1;
    block->spirv_set0_binding_n = 1;
    const char* names[10] = {"light_dir", "view_dir", "dark_color", "gold_color",
        "grass_a", "grass_b", "params0", "params1", "params2", "params3"};
    for (int i = 0; i < 10; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
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

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp.label = "tileshape-atlas-smp";
    m_sampler = sg_make_sampler(&smp);

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
    matSmp.label = "tileshape-mat-smp";
    m_matSampler = sg_make_sampler(&matSmp);

    // Placeholders until a material set loads: keeps the cliff bindings
    // valid (the shader always samples; strengths live in params3/params4).
    // White color, flat tangent normal (128,128,255), white AO/roughness.
    const std::uint32_t placeholderPx[kMatCount] = {
        0xFFFFFFFFu, // color: white
        0xFFFF8080u, // normal: flat (little-endian RGBA bytes 128,128,255,255)
        0xFFFFFFFFu, // AO: white
        0xFFFFFFFFu, // roughness: white
    };
    for (int i = 0; i < kMatCount; ++i) {
        sg_image_desc img = {};
        img.width = 1;
        img.height = 1;
        img.pixel_format = SG_PIXELFORMAT_RGBA8;
        img.data.mip_levels[0].ptr = &placeholderPx[i];
        img.data.mip_levels[0].size = sizeof(placeholderPx[i]);
        img.label = "tileshape-mat-placeholder";
        m_matMaps[i].image = sg_make_image(&img);
        sg_view_desc view = {};
        view.texture.image = m_matMaps[i].image;
        m_matMaps[i].view = sg_make_view(&view);
    }

    m_ready = true;
}

void AtlasRenderer::shutdown() {
    destroyPipelines();
    for (CliffCache& cache : m_cliffCaches) {
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(cache.vbuf);
            cache.vbuf = {};
        }
    }
    m_cliffCaches.clear();
    if (m_texVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_texVbuf);
        m_texVbuf = {};
    }
    if (m_colorVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_colorVbuf);
        m_colorVbuf = {};
    }
    if (m_sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_sampler);
        m_sampler = {};
    }
    if (m_matSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_matSampler);
        m_matSampler = {};
    }
    for (int i = 0; i < kMatCount; ++i) {
        destroySlot(m_matMaps[i]);
    }
    destroySlot(m_slots[0]);
    destroySlot(m_slots[1]);
    destroySlot(m_slots[2]);
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

namespace {

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

bool AtlasRenderer::uploadSlotMipmapped(
    AtlasSlot& slot,
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
        spdlog::error("AtlasRenderer: sg_make_image failed ({})", label ? label : "?");
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = slot.image;
    slot.view = sg_make_view(&viewDesc);
    return slot.view.id != SG_INVALID_ID;
}

int AtlasRenderer::loadMaterialMaps(const std::string& dir, const std::string& setName) {
    const char* suffixes[kMatCount] = {"_Color.jpg", "_NormalGL.jpg", "_AmbientOcclusion.jpg", "_Roughness.jpg"};
    const char* labels[kMatCount] = {"mat-color", "mat-normal", "mat-ao", "mat-rough"};
    int loaded = 0;
    for (int i = 0; i < kMatCount; ++i) {
        const std::string path = dir + "/" + setName + suffixes[i];
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!pixels || w <= 0 || h <= 0) {
            // Missing map: the placeholder stays bound and the shader falls
            // back towards the palette for that channel.
            spdlog::warn("AtlasRenderer: material map '{}' not loaded: {}", path, stbi_failure_reason());
            if (pixels) {
                stbi_image_free(pixels);
            }
            continue;
        }
        const bool ok = uploadSlotMipmapped(m_matMaps[i], pixels, w, h, labels[i]);
        stbi_image_free(pixels);
        if (!ok) {
            spdlog::error("AtlasRenderer: material map '{}' upload failed", path);
            continue;
        }
        ++loaded;
    }
    spdlog::info("AtlasRenderer: material '{}' from '{}': {}/{} maps loaded",
        setName, dir, loaded, static_cast<int>(kMatCount));
    return loaded;
}

bool AtlasRenderer::loadReliefMap(const std::string& path, maskfield::ReliefMap& out) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 1);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::warn("AtlasRenderer: relief map '{}' not loaded: {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }
    // The library helper does the dune-scale low-pass (see mask_field).
    out = mask::reliefMapFromImage(pixels, w, h, 1);
    stbi_image_free(pixels);
    if (out.gray.empty()) {
        return false;
    }
    spdlog::info("AtlasRenderer: relief map '{}' ({}x{}, low-passed to {}x{})", path, w, h, out.w, out.h);
    return true;
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
    const char* label =
        (kind == AtlasKind::Flat) ? "flat-atlas" : (kind == AtlasKind::FlatGreen) ? "flat-green-atlas" : "rgba-atlas";
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
        out.push_back({a.x, a.y, bakedDepth(a.y), color.r, color.g, color.b, color.a});
        out.push_back({b.x, b.y, bakedDepth(b.y), color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::appendDiamondFill(
    std::vector<ColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell); // Left, Up, Right, Down
    const int tris[6] = {0, 1, 2, 0, 2, 3};
    for (const int idx : tris) {
        const glm::vec2 p = corners[idx];
        out.push_back({p.x, p.y, bakedDepth(p.y), color.r, color.g, color.b, color.a});
    }
}

void AtlasRenderer::appendNodeMarker(
    std::vector<ColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 node,
    glm::vec4 color) {

    const glm::vec2 p = iso.nodeToField(node);
    const float s = 6.0f;
    const glm::vec2 pts[4] = {
        {p.x - s, p.y},
        {p.x, p.y - s * 0.5f},
        {p.x + s, p.y},
        {p.x, p.y + s * 0.5f},
    };
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a = pts[i];
        const glm::vec2 b = pts[(i + 1) % 4];
        out.push_back({a.x, a.y, bakedDepth(a.y), color.r, color.g, color.b, color.a});
        out.push_back({b.x, b.y, bakedDepth(b.y), color.r, color.g, color.b, color.a});
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
    const CliffFsParams* cliffShading,
    double nowSec) {

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
    // is drawn with the layer's own atlas. The mask layer also paints its
    // flat tiles: they show the painted silhouette instantly while the field
    // mesh rebuilds (debounced), and the finished mesh covers them by depth
    // afterwards.
    struct TexRange {
        int base = 0;
        int count = 0;
        AtlasKind atlas = AtlasKind::Grass;
    };
    std::vector<TexRange> flatRanges;
    std::vector<TexVertex> texVerts;
    texVerts.reserve(static_cast<std::size_t>(mapW * mapH * 12 * layerCount));

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush) {
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

    // Overlay stream: hover fill triangles first (own draw call on the
    // triangle pipeline), then all the line vertices — one buffer upload
    // feeds both draws.
    std::vector<ColorVertex> overlayVerts;
    overlayVerts.reserve(static_cast<std::size_t>(mapW * mapH * 8 + 64));

    int fillVertCount = 0;
    if (hasHover) {
        // Brush footprint preview: a click rewrites exactly the four cells
        // touching the hovered node (LandBrush::affectedCells), so tint those
        // diamonds. Cells outside the map (edge/corner nodes) simply drop out.
        const glm::vec4 cellFill{1.0f, 0.75f, 0.25f, 0.15f};
        for (const glm::ivec2 cell : topology_core::DiamondIsometry::nodeNeighbourCells(hoverNode)) {
            if (cell.x < 0 || cell.y < 0 || cell.x >= mapW || cell.y >= mapH) {
                continue;
            }
            appendDiamondFill(overlayVerts, iso, cell, cellFill);
        }
        fillVertCount = static_cast<int>(overlayVerts.size());
    }

    const glm::vec4 gridColor{0.45f, 0.48f, 0.52f, 0.55f};
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            appendDiamondOutline(overlayVerts, iso, {x, y}, gridColor);
        }
    }

    if (hasHover) {
        // Footprint outlines go over the grid, the node marker on top.
        const glm::vec4 cellEdge{1.0f, 0.75f, 0.25f, 0.85f};
        for (const glm::ivec2 cell : topology_core::DiamondIsometry::nodeNeighbourCells(hoverNode)) {
            if (cell.x < 0 || cell.y < 0 || cell.x >= mapW || cell.y >= mapH) {
                continue;
            }
            appendDiamondOutline(overlayVerts, iso, cell, cellEdge);
        }
        appendNodeMarker(overlayVerts, iso, hoverNode, {1.0f, 0.25f, 0.2f, 1.0f});
    }

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;

    if (!texVerts.empty()) {
        sg_update_buffer(m_texVbuf, sg_range{texVerts.data(), texVerts.size() * sizeof(TexVertex)});
    }

    // Painter order: flat ground layers, then the z-buffered mask meshes,
    // then the grid lines on top.
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

    for (const TexRange& range : flatRanges) {
        drawAtlasRange(range);
    }

    // One upload for both overlay draws (sokol allows a single buffer update
    // per frame): hover fill triangles first, grid/outline lines after them.
    if (!overlayVerts.empty()) {
        sg_update_buffer(m_colorVbuf, sg_range{overlayVerts.data(), overlayVerts.size() * sizeof(ColorVertex)});
    }

    // Mask layers: scalar-field surface nets from the same nodes, drawn with
    // the z-buffer and per-pixel shading. The mesh is cached per brush; the
    // heavy field rebuild runs at most once per edit burst (0.3 s debounce),
    // a heightScale edit only re-projects the cached mesh.
    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || !layer.mask || !layer.maskParams) {
            continue;
        }
        CliffCache& cache = cliffCacheFor(layer.brush);
        const bool paramsChanged =
            std::memcmp(&cache.maskParams, layer.maskParams, sizeof(maskfield::MaskFieldParams)) != 0;
        const bool contentChanged = !cache.contentValid ||
            cache.brushVersion != layer.brush->version() ||
            cache.mask != layer.mask || paramsChanged;
        const bool scaleChanged = cache.heightScale != layer.cliffHeightScale;
        if (contentChanged || scaleChanged) {
            cache.brushVersion = layer.brush->version();
            cache.mask = layer.mask;
            std::memcpy(&cache.maskParams, layer.maskParams, sizeof(maskfield::MaskFieldParams));
            cache.heightScale = layer.cliffHeightScale;
            cache.contentValid = true;
            cache.lastEditSec = nowSec;
            cache.meshDirty = cache.meshDirty || contentChanged;
            cache.streamDirty = true;
            cache.stats.pending = true;
        }
        if ((cache.meshDirty || cache.streamDirty) && (nowSec - cache.lastEditSec) > 0.3) {
            rebuildCliffCache(cache, iso);
            cache.meshDirty = false;
            cache.streamDirty = false;
            cache.gpuDirty = true;
            cache.stats.pending = false;
        }
        if (cache.gpuDirty) {
            const size_t bytes = cache.stream.size() * sizeof(CliffVertex);
            if (bytes > 0) {
                if (cache.vbufSize < bytes) {
                    if (cache.vbuf.id != SG_INVALID_ID) {
                        sg_destroy_buffer(cache.vbuf);
                    }
                    sg_buffer_desc bufDesc = {};
                    bufDesc.size = ((bytes / (size_t{1} << 20)) + 1) * (size_t{1} << 20);
                    bufDesc.usage.dynamic_update = true;
                    bufDesc.label = "tileshape-cliff-vbuf";
                    cache.vbuf = sg_make_buffer(&bufDesc);
                    cache.vbufSize = bufDesc.size;
                }
                if (cache.vbuf.id != SG_INVALID_ID) {
                    sg_update_buffer(cache.vbuf, sg_range{cache.stream.data(), bytes});
                }
            }
            cache.gpuDirty = false;
        }
        const CliffFsParams* shading = layer.shadingOverride ? layer.shadingOverride : cliffShading;
        if (!cache.stream.empty() && cache.vbuf.id != SG_INVALID_ID && shading != nullptr) {
            sg_bindings bind{};
            bind.vertex_buffers[0] = cache.vbuf;
            for (int i = 0; i < kMatCount; ++i) {
                bind.views[i] = m_matMaps[i].view;
            }
            bind.samplers[0] = m_matSampler;

            sg_apply_pipeline(m_cliffPip);
            sg_apply_bindings(&bind);
            sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
            sg_apply_uniforms(1, sg_range{shading, sizeof(CliffFsParams)});
            sg_draw(0, static_cast<int>(cache.stream.size()), 1);
        }
    }

    if (!overlayVerts.empty()) {
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_colorVbuf;

        if (fillVertCount > 0) {
            sg_apply_pipeline(m_colorTriPip);
            sg_apply_bindings(&bind);
            sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
            sg_draw(0, fillVertCount, 1);
        }
        const int lineVertCount = static_cast<int>(overlayVerts.size()) - fillVertCount;
        if (lineVertCount > 0) {
            sg_apply_pipeline(m_colorPip);
            sg_apply_bindings(&bind);
            sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
            sg_draw(fillVertCount, lineVertCount, 1);
        }
    }
}

AtlasRenderer::CliffCache& AtlasRenderer::cliffCacheFor(const LandBrush* brush) {
    for (CliffCache& cache : m_cliffCaches) {
        if (cache.brush == brush) {
            return cache;
        }
    }
    m_cliffCaches.push_back({});
    m_cliffCaches.back().brush = brush;
    return m_cliffCaches.back();
}

const CliffStats& AtlasRenderer::cliffStatsFor(const LandBrush* brush) const {
    static const CliffStats kEmpty{};
    for (const CliffCache& cache : m_cliffCaches) {
        if (cache.brush == brush) {
            return cache.stats;
        }
    }
    return kEmpty;
}

void AtlasRenderer::rebuildCliffCache(
    CliffCache& cache,
    const topology_core::DiamondIsometry& iso) {

    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();

    if (cache.meshDirty) {
        // Full path: brush nodes -> bbox grid -> scalar field -> surface nets.
        const LandBrush& brush = *cache.brush;
        std::vector<glm::ivec2> onNodes;
        for (int y = 0; y <= brush.height(); ++y) {
            for (int x = 0; x <= brush.width(); ++x) {
                if (brush.nodeIsOn({x, y})) {
                    onNodes.emplace_back(x, y);
                }
            }
        }

        if (onNodes.empty()) {
            cache.mesh = {};
            cache.stats.voxelCount = 0;
            cache.stats.vertexCount = 0;
            cache.stats.triangleCount = 0;
            cache.stats.watertight = true;
        } else {
            int minX = onNodes[0].x;
            int minY = onNodes[0].y;
            int maxX = onNodes[0].x;
            int maxY = onNodes[0].y;
            for (const glm::ivec2& n : onNodes) {
                minX = std::min(minX, n.x);
                minY = std::min(minY, n.y);
                maxX = std::max(maxX, n.x);
                maxY = std::max(maxY, n.y);
            }
            // One-cell margin (the blurred outline must not cross the field
            // border, same contract as the demo shape's zero border rows).
            // Mask with a spread drops its skirt onto the ground past the
            // painted nodes: the zero ring must stay wider than the spread
            // so the skirt foot still closes inside the field.
            const int margin = (cache.maskParams.spreadDistance > 0.0f)
                ? 1 + static_cast<int>(std::ceil(cache.maskParams.spreadDistance))
                : 1;
            minX -= margin;
            minY -= margin;
            maxX += margin;
            maxY += margin;
            const int nodesX = maxX - minX + 1;
            const int nodesY = maxY - minY + 1;
            std::vector<std::uint8_t> nodes(static_cast<size_t>(nodesX) * nodesY, 0);
            for (const glm::ivec2& n : onNodes) {
                nodes[static_cast<size_t>(n.y - minY) * nodesX + (n.x - minX)] = 1;
            }

            cliff::RegularizeStats regStats;
            // The Texture 2D mask silhouette (interpolated node fill, iso
            // 0.5) extruded into a plate with a sloped skirt:
            // MaskField -> generic ScalarFieldView -> surface nets.
            maskfield::MaskField field(cache.maskParams, nodes.data(), nodesX, nodesY);
            cliff::ScalarFieldView view = field.view();
            std::vector<float> samples;
            field.sample(samples);
            cliff::regularizeSigns(view, samples, &regStats);
            cache.mesh = cliff::extractSurfaceNets(view, samples, nullptr);
            cache.stats.voxelCount = view.nx * view.ny * view.nz;
            const cliff::WatertightReport report = cliff::checkWatertight(cache.mesh);
            cache.stats.watertight = report.ok();
            cache.stats.vertexCount = static_cast<int>(cache.mesh.vertices.size());
            cache.stats.triangleCount = static_cast<int>(cache.mesh.indices.size() / 3);
            cache.origin = {minX, minY};
            if (!report.ok()) {
                spdlog::warn("AtlasRenderer: mask mesh not watertight ({} bad of {} edges, {} saddles left)",
                    report.badEdges, report.undirectedEdges, regStats.remaining);
            }
        }
    }

    // Projection path (also after a full rebuild): mesh -> projected stream.
    // Placement matches DiamondIsometry::nodeToField (no +halfH on y), so the
    // blob stays registered with the node markers of the other layers.
    cache.stream.clear();
    cache.stream.reserve(cache.mesh.indices.size());
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    for (const std::uint32_t index : cache.mesh.indices) {
        const cliff::MeshVertex& v = cache.mesh.vertices[index];
        const float mapX = static_cast<float>(cache.origin.x) + v.px;
        const float mapZ = static_cast<float>(cache.origin.y) + v.pz;
        const float fieldX = (mapX - mapZ) * halfW + halfW;
        const float fieldY = (mapX + mapZ) * halfH;
        cache.stream.push_back(CliffVertex{
            fieldX, fieldY - v.py * cache.heightScale, bakedDepth(fieldY),
            v.nx, v.ny, v.nz, v.groove,
            mapX, v.py, mapZ, v.rim});
    }
    cache.stats.rebuildMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
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
        for (int i = 0; i < 2; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
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
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        pip.primitive_type = SG_PRIMITIVETYPE_LINES;
        pip.colors[0].blend.enabled = true;
        pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        // The overlay (grid / hover) shares the z-buffer with the 3D meshes:
        // depth-tested against their baked depth, but not written, so overlay
        // pieces never occlude each other (or the flat tiles under them).
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = false;
        pip.label = "tileshape-color-pip";
        m_colorPip = sg_make_pipeline(&pip);

        // Same color stream as triangles: translucent hover footprint fill.
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.label = "tileshape-color-tri-pip";
        m_colorTriPip = sg_make_pipeline(&pip);
    }

    // Cliff pass (scalar-field surface nets): baked depth + field-space
    // normal/groove/world attributes, per-pixel omphalos palette.
    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kCliffVsHlsl;
        shd.fragment_func.source = kCliffFsHlsl;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kCliffVsMsl;
        shd.fragment_func.source = kCliffFsMsl;
#else
        shd.vertex_func.source = kCliffVsGlsl;
        shd.fragment_func.source = kCliffFsGlsl;
#endif
        for (int i = 0; i < 5; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        fillCliffFsUniformDesc(&shd.uniform_blocks[1]);

        // Material maps: color/normal/AO/roughness as texture views 0..3,
        // all sampled with the one shared REPEAT sampler (slot 0).
        const char* matGlslNames[kMatCount] = {"mat_color", "mat_normal", "mat_ao", "mat_rough"};
        for (int i = 0; i < kMatCount; ++i) {
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

        m_cliffShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_cliffShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
        pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
        pip.label = "tileshape-cliff-pip";
        m_cliffPip = sg_make_pipeline(&pip);
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
    if (m_colorTriPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_colorTriPip);
        m_colorTriPip = {};
    }
    if (m_cliffPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_cliffPip);
        m_cliffPip = {};
    }
    if (m_texShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_texShd);
        m_texShd = {};
    }
    if (m_colorShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_colorShd);
        m_colorShd = {};
    }
    if (m_cliffShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_cliffShd);
        m_cliffShd = {};
    }
}
