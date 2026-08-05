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
layout(location=2) in vec4 layers;
layout(location=3) in vec4 weights;
layout(location=4) in float fill;
out vec2 v_uv;
out vec2 v_world;
out vec4 v_layers;
out vec4 v_weights;
out float v_fill;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    v_uv = uv;
    v_world = pos;
    v_layers = layers;
    v_weights = weights;
    v_fill = fill;
}
)";

static const char* kTexFsGlsl = R"(
#version 330
in vec2 v_uv;
in vec2 v_world;
in vec4 v_layers;
in vec4 v_weights;
in float v_fill;
out vec4 frag_color;
uniform sampler2D atlas_tex;
uniform sampler2DArray tiling_array;
uniform vec4 tex_params;
uniform vec4 blend_params;

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

void main() {
    vec4 mask = texture(atlas_tex, v_uv);
    vec3 rgb = mask.rgb;
    float alpha = mask.a;
    if (tex_params.x > 0.5) {
        // World-space tiling: the texture flows continuously across cells
        // (there are no per-tile cuts to mismatch), the atlas tile only
        // clips the shape — no per-cell edge outlines, the terrain reads
        // as one continuous surface. Up to 4 candidate textures blend via
        // the interpolated corner weights; in the blend zone the weights
        // are sharpened and wobbled by fbm noise for an organic edge (the
        // wobble is gated by w*(1-w), so pure interiors stay untouched).
        float n = 0.0;
        if (blend_params.y > 0.0) {
            n = fbm2(v_world * blend_params.z) - 0.5;
        }
        vec4 w = max(v_weights, 0.0);
        float sw = w.x + w.y + w.z + w.w;
        if (sw > 1e-4) {
            vec2 tuv = v_world * tex_params.y;
            w = pow(w, vec4(max(blend_params.x, 0.05)));
            w += n * blend_params.y * w * (1.0 - w);
            w = max(w, 0.0);
            w /= max(w.x + w.y + w.z + w.w, 1e-4);
            rgb = w.x * texture(tiling_array, vec3(tuv, v_layers.x)).rgb
                + w.y * texture(tiling_array, vec3(tuv, v_layers.y)).rgb
                + w.z * texture(tiling_array, vec3(tuv, v_layers.z)).rgb
                + w.w * texture(tiling_array, vec3(tuv, v_layers.w)).rgb;
        }
        // Soft edge into empty space: the fill weight (1 at on-nodes, 0 at
        // off-nodes) fades coverage around the 0.5 iso — the same line the
        // atlas mask used to cut at — with the same noise wobble, so the
        // region contour feathers out instead of stepping hard.
        float feather = max(blend_params.w, 1e-3);
        float fill = clamp(v_fill + n * blend_params.y * v_fill * (1.0 - v_fill), 0.0, 1.0);
        alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
        if (alpha < 0.004) discard;
    } else {
        if (mask.a < 0.05) discard;
    }
    frag_color = vec4(rgb, alpha);
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

static const char* kTexVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
};
struct VSIn {
    float2 pos: TEXCOORD0;
    float2 uv: TEXCOORD1;
    float4 layers: TEXCOORD2;
    float4 weights: TEXCOORD3;
    float fill: TEXCOORD4;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
    float2 world: TEXCOORD1;
    float4 layers: TEXCOORD2;
    float4 weights: TEXCOORD3;
    float fill: TEXCOORD4;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.uv = inp.uv;
    o.world = inp.pos;
    o.layers = inp.layers;
    o.weights = inp.weights;
    o.fill = inp.fill;
    return o;
}
)";

static const char* kTexFsHlsl = R"(
Texture2D atlas_tex: register(t0);
Texture2DArray tiling_array: register(t1);
SamplerState smp: register(s0);
SamplerState tiling_smp: register(s1);
cbuffer fs_params: register(b0) {
    float4 tex_params;
    float4 blend_params;
};
struct PSIn {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
    float2 world: TEXCOORD1;
    float4 layers: TEXCOORD2;
    float4 weights: TEXCOORD3;
    float fill: TEXCOORD4;
};

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
    for (int j = 0; j < 5; ++j) {
        f += a * noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

float4 main(PSIn inp): SV_Target {
    float4 mask = atlas_tex.Sample(smp, inp.uv);
    float3 rgb = mask.rgb;
    float alpha = mask.a;
    if (tex_params.x > 0.5) {
        // World-space tiling + candidate blend + soft empty edge: see GLSL.
        float n = 0.0;
        if (blend_params.y > 0.0) {
            n = fbm2(inp.world * blend_params.z) - 0.5;
        }
        float4 w = max(inp.weights, 0.0);
        float sw = w.x + w.y + w.z + w.w;
        if (sw > 1e-4) {
            float2 tuv = inp.world * tex_params.y;
            w = pow(w, (float4)max(blend_params.x, 0.05));
            w += n * blend_params.y * w * (1.0 - w);
            w = max(w, 0.0);
            w /= max(w.x + w.y + w.z + w.w, 1e-4);
            rgb = w.x * tiling_array.Sample(tiling_smp, float3(tuv, inp.layers.x)).rgb
                + w.y * tiling_array.Sample(tiling_smp, float3(tuv, inp.layers.y)).rgb
                + w.z * tiling_array.Sample(tiling_smp, float3(tuv, inp.layers.z)).rgb
                + w.w * tiling_array.Sample(tiling_smp, float3(tuv, inp.layers.w)).rgb;
        }
        float feather = max(blend_params.w, 1e-3);
        float fill = clamp(inp.fill + n * blend_params.y * inp.fill * (1.0 - inp.fill), 0.0, 1.0);
        alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
        if (alpha < 0.004) discard;
    } else {
        if (mask.a < 0.05) discard;
    }
    return float4(rgb, alpha);
}
)";

static const char* kColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float camera_zoom;
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
    float4 layers [[attribute(2)]];
    float4 weights [[attribute(3)]];
    float fill [[attribute(4)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv;
    float2 world;
    float4 layers;
    float4 weights;
    float fill;
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
    o.world = in.pos;
    o.layers = in.layers;
    o.weights = in.weights;
    o.fill = in.fill;
    return o;
}
)";

static const char* kTexFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float4 tex_params;
    float4 blend_params;
};

struct PSIn {
    float4 pos [[position]];
    float2 uv;
    float2 world;
    float4 layers;
    float4 weights;
    float fill;
};

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
    for (int j = 0; j < 5; ++j) {
        f += a * noisefv2(p);
        a *= 0.5;
        p *= 2.0;
    }
    return f * (1.0 / 1.9375);
}

fragment float4 _main(PSIn in [[stage_in]],
                      constant FsParams& params [[buffer(1)]],
                      texture2d<float> atlas_tex [[texture(0)]],
                      texture2d_array<float> tiling_array [[texture(1)]],
                      sampler smp [[sampler(0)]],
                      sampler tiling_smp [[sampler(1)]]) {
    float4 mask = atlas_tex.sample(smp, in.uv);
    float3 rgb = mask.rgb;
    float alpha = mask.a;
    if (params.tex_params.x > 0.5) {
        // World-space tiling + candidate blend + soft empty edge: see GLSL.
        float n = 0.0;
        if (params.blend_params.y > 0.0) {
            n = fbm2(in.world * params.blend_params.z) - 0.5;
        }
        float4 w = max(in.weights, 0.0);
        float sw = w.x + w.y + w.z + w.w;
        if (sw > 1e-4) {
            float2 tuv = in.world * params.tex_params.y;
            w = pow(w, float4(max(params.blend_params.x, 0.05)));
            w += n * params.blend_params.y * w * (1.0 - w);
            w = max(w, 0.0);
            w /= max(w.x + w.y + w.z + w.w, 1e-4);
            rgb = w.x * tiling_array.sample(tiling_smp, tuv, uint(in.layers.x)).rgb
                + w.y * tiling_array.sample(tiling_smp, tuv, uint(in.layers.y)).rgb
                + w.z * tiling_array.sample(tiling_smp, tuv, uint(in.layers.z)).rgb
                + w.w * tiling_array.sample(tiling_smp, tuv, uint(in.layers.w)).rgb;
        }
        float feather = max(params.blend_params.w, 1e-3);
        float fill = clamp(in.fill + n * params.blend_params.y * in.fill * (1.0 - in.fill), 0.0, 1.0);
        alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
        if (alpha < 0.004) {
            discard_fragment();
        }
    } else {
        if (mask.a < 0.05) {
            discard_fragment();
        }
    }
    return float4(rgb, alpha);
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
uniform vec4 params2;
uniform sampler2D top_tex;

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

void main() {
    vec3 n = normalize(v_normal);
    vec3 p = v_world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, v_groove);
    vec3 gold = gold_color.rgb + vec3(1.0, 0.9, 0.4) * step(params0.x, f);
    vec3 rock = mix(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    vec3 grass = mix(grass_a.rgb, grass_b.rgb, gm);
    grass = mix(grass, texture(top_tex, p.xz * params2.w).rgb, params2.z);
    float topMask = smoothstep(0.7, 0.9, n.y);
    // Rim stitch shading (stone layers only, params1.w = top plane Y):
    // boulders above the plane keep the wall palette; below the plane the
    // grass yields to stone gradually over params2.x (not at once) — the
    // flat top and the shallow rim scoops stay grassy; the baked rim weight
    // turns the top stony towards the wall (params2.y = strength).
    if (params1.w > 0.0) {
        float stone = max(step(params1.w + 0.01, p.y),
            smoothstep(0.0, max(params2.x, 1e-4), params1.w - p.y));
        stone = max(stone, clamp(v_rim * params2.y, 0.0, 1.0));
        topMask *= 1.0 - stone;
    }
    vec3 base = mix(rock, grass, topMask);
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    vec3 l = normalize(light_dir.xyz);
    vec3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    vec3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, params0.w, shell) * (1.0 - topMask);
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
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: boulder plane Y (0 = off)
    float4 params2; // x: grass->stone fade depth, y: rim gradient strength, z: top texture strength, w: tiling
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

Texture2D top_tex: register(t0);
SamplerState top_smp: register(s0);
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
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = lerp(grass_a.rgb, grass_b.rgb, gm);
    grass = lerp(grass, top_tex.Sample(top_smp, p.xz * params2.w).rgb, params2.z);
    float topMask = smoothstep(0.7, 0.9, n.y);
    // Rim stitch shading (stone layers only, params1.w = top plane Y):
    // boulders above the plane keep the wall palette; below the plane the
    // grass yields to stone gradually over params2.x (not at once) — the
    // flat top and the shallow rim scoops stay grassy; the baked rim weight
    // turns the top stony towards the wall (params2.y = strength).
    if (params1.w > 0.0) {
        float stone = max(step(params1.w + 0.01, p.y),
            smoothstep(0.0, max(params2.x, 1e-4), params1.w - p.y));
        stone = max(stone, clamp(inp.rim * params2.y, 0.0, 1.0));
        topMask *= 1.0 - stone;
    }
    float3 base = lerp(rock, grass, topMask);
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(light_dir.xyz);
    float3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    float3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = lerp(0.05, params0.w, shell) * (1.0 - topMask);
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
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: boulder plane Y (0 = off)
    float4 params2; // x: grass->stone fade depth, y: rim gradient strength, z: top texture strength, w: tiling
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

fragment float4 _main(PSIn in [[stage_in]], constant FsParams& fs [[buffer(1)]],
                      texture2d<float> top_tex [[texture(0)]],
                      sampler top_smp [[sampler(0)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, in.groove);
    float3 gold = fs.gold_color.rgb + float3(1.0, 0.9, 0.4) * step(fs.params0.x, f);
    float3 rock = mix(fs.dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = mix(fs.grass_a.rgb, fs.grass_b.rgb, gm);
    grass = mix(grass, top_tex.sample(top_smp, p.xz * fs.params2.w).rgb, fs.params2.z);
    float topMask = smoothstep(0.7, 0.9, n.y);
    // Rim stitch shading (stone layers only, params1.w = top plane Y):
    // boulders above the plane keep the wall palette; below the plane the
    // grass yields to stone gradually over params2.x (not at once) — the
    // flat top and the shallow rim scoops stay grassy; the baked rim weight
    // turns the top stony towards the wall (params2.y = strength).
    if (fs.params1.w > 0.0) {
        float stone = max(step(fs.params1.w + 0.01, p.y),
            smoothstep(0.0, max(fs.params2.x, 1e-4), fs.params1.w - p.y));
        stone = max(stone, clamp(in.rim * fs.params2.y, 0.0, 1.0));
        topMask *= 1.0 - stone;
    }
    float3 base = mix(rock, grass, topMask);
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(fs.light_dir.xyz);
    float3 rd = fs.view_dir.xyz;
    float ndl = dot(n, l);
    float3 col = base * (fs.params0.y + fs.params1.z * max(-ndl, 0.0) +
        fs.params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, fs.params0.w, shell) * (1.0 - topMask);
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), fs.params1.x);
    return float4(pow(clamp(col, 0.0, 1.0), float3(fs.params1.y)), 1.0);
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

// Cliff fragment block (palette/light): slot 1, after the vertex block.
void fillCliffFsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = sizeof(CliffFsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 1;
    block->wgsl_group0_binding_n = 1;
    block->spirv_set0_binding_n = 1;
    const char* names[9] = {"light_dir", "view_dir", "dark_color", "gold_color",
        "grass_a", "grass_b", "params0", "params1", "params2"};
    for (int i = 0; i < 9; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
}

void fillTexFsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = sizeof(AtlasRenderer::TexFsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 1;
    block->wgsl_group0_binding_n = 1;
    block->spirv_set0_binding_n = 1;
    block->glsl_uniforms[0].glsl_name = "tex_params";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    block->glsl_uniforms[1].glsl_name = "blend_params";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
}

// Bilinear RGBA resample (texture array slices must share one size).
void resampleRgbaBilinear(
    const std::uint8_t* src,
    int sw,
    int sh,
    std::uint8_t* dst,
    int dw,
    int dh) {

    for (int y = 0; y < dh; ++y) {
        const float gy = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dh) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, sh - 1);
        const int y1 = std::min(y0 + 1, sh - 1);
        const float fy = std::clamp(gy - static_cast<float>(y0), 0.0f, 1.0f);
        for (int x = 0; x < dw; ++x) {
            const float gx = (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dw) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, sw - 1);
            const int x1 = std::min(x0 + 1, sw - 1);
            const float fx = std::clamp(gx - static_cast<float>(x0), 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                const float s00 = static_cast<float>(src[(static_cast<size_t>(y0) * sw + x0) * 4 + c]);
                const float s10 = static_cast<float>(src[(static_cast<size_t>(y0) * sw + x1) * 4 + c]);
                const float s01 = static_cast<float>(src[(static_cast<size_t>(y1) * sw + x0) * 4 + c]);
                const float s11 = static_cast<float>(src[(static_cast<size_t>(y1) * sw + x1) * 4 + c]);
                const float v = (s00 * (1.0f - fx) + s10 * fx) * (1.0f - fy) + (s01 * (1.0f - fx) + s11 * fx) * fy;
                dst[(static_cast<size_t>(y) * dw + x) * 4 + c] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(v), 0l, 255l));
            }
        }
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

    sg_sampler_desc topSmp = {};
    topSmp.min_filter = SG_FILTER_LINEAR;
    topSmp.mag_filter = SG_FILTER_LINEAR;
    topSmp.wrap_u = SG_WRAP_REPEAT;
    topSmp.wrap_v = SG_WRAP_REPEAT;
    topSmp.label = "tileshape-top-tex-smp";
    m_topTexSampler = sg_make_sampler(&topSmp);

    // Placeholder until a real top texture loads: keeps the cliff bindings
    // valid (the shader always samples, strength lives in params2.z).
    const std::uint32_t white = 0xFFFFFFFFu;
    sg_image_desc topImg = {};
    topImg.width = 1;
    topImg.height = 1;
    topImg.pixel_format = SG_PIXELFORMAT_RGBA8;
    topImg.data.mip_levels[0].ptr = &white;
    topImg.data.mip_levels[0].size = sizeof(white);
    topImg.label = "tileshape-top-tex-placeholder";
    m_topTexImage = sg_make_image(&topImg);
    sg_view_desc topView = {};
    topView.texture.image = m_topTexImage;
    m_topTexView = sg_make_view(&topView);

    // Placeholder tiling array (one 1x1 white layer): keeps the flat-pass
    // bindings valid until buildTilingTextureArray runs.
    sg_image_desc arrImg = {};
    arrImg.type = SG_IMAGETYPE_ARRAY;
    arrImg.width = 1;
    arrImg.height = 1;
    arrImg.num_slices = 1;
    arrImg.pixel_format = SG_PIXELFORMAT_RGBA8;
    arrImg.data.mip_levels[0].ptr = &white;
    arrImg.data.mip_levels[0].size = sizeof(white);
    arrImg.label = "tileshape-tiling-array-placeholder";
    m_tilingArray.image = sg_make_image(&arrImg);
    sg_view_desc arrView = {};
    arrView.texture.image = m_tilingArray.image;
    m_tilingArray.view = sg_make_view(&arrView);
    m_tilingArrayLayers = 1;

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
    if (m_topTexView.id != SG_INVALID_ID) {
        sg_destroy_view(m_topTexView);
        m_topTexView = {};
    }
    if (m_topTexImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_topTexImage);
        m_topTexImage = {};
    }
    if (m_topTexSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_topTexSampler);
        m_topTexSampler = {};
    }
    destroySlot(m_slots[0]);
    destroySlot(m_slots[1]);
    destroySlot(m_tilingArray);
    m_tilingArrayLayers = 0;
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

bool AtlasRenderer::loadTopTextureFromFile(const std::string& path) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        spdlog::error("AtlasRenderer: failed to load top texture '{}': {}", path, stbi_failure_reason());
        if (pixels) {
            stbi_image_free(pixels);
        }
        return false;
    }

    if (m_topTexView.id != SG_INVALID_ID) {
        sg_destroy_view(m_topTexView);
        m_topTexView = {};
    }
    if (m_topTexImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_topTexImage);
        m_topTexImage = {};
    }

    sg_image_desc desc = {};
    desc.width = w;
    desc.height = h;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = pixels;
    desc.data.mip_levels[0].size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    desc.label = "tileshape-top-tex";
    m_topTexImage = sg_make_image(&desc);
    stbi_image_free(pixels);
    if (m_topTexImage.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_image failed (top texture)");
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = m_topTexImage;
    m_topTexView = sg_make_view(&viewDesc);
    if (m_topTexView.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_view failed (top texture)");
        return false;
    }
    spdlog::info("AtlasRenderer: loaded top texture {} ({}x{})", path, w, h);
    return true;
}

int AtlasRenderer::buildTilingTextureArray(const std::vector<std::string>& paths, std::vector<int>* outLayers) {
    struct CpuTex {
        int w = 0;
        int h = 0;
        std::vector<std::uint8_t> rgba;
    };
    std::vector<CpuTex> loaded;
    std::vector<int> fileLayer(paths.size(), -1);
    int maxDim = 64;
    for (size_t i = 0; i < paths.size(); ++i) {
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load(paths[i].c_str(), &w, &h, &comp, 4);
        if (!pixels || w <= 0 || h <= 0) {
            spdlog::error("AtlasRenderer: failed to load tiling texture '{}': {}", paths[i], stbi_failure_reason());
            if (pixels) {
                stbi_image_free(pixels);
            }
            continue;
        }
        CpuTex tex;
        tex.w = w;
        tex.h = h;
        tex.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
        fileLayer[i] = static_cast<int>(loaded.size());
        maxDim = std::max(maxDim, std::max(w, h));
        loaded.push_back(std::move(tex));
    }
    if (outLayers) {
        *outLayers = fileLayer;
    }
    if (loaded.empty()) {
        return 0;
    }

    // All slices share one size (array constraint): the largest source
    // dimension, clamped to a sane range for a paint texture.
    const int size = std::clamp(maxDim, 64, 1024);
    std::vector<std::uint8_t> slices(static_cast<size_t>(size) * size * 4 * loaded.size());
    for (size_t i = 0; i < loaded.size(); ++i) {
        resampleRgbaBilinear(
            loaded[i].rgba.data(),
            loaded[i].w,
            loaded[i].h,
            slices.data() + i * static_cast<size_t>(size) * size * 4,
            size,
            size);
    }

    sg_image_desc desc = {};
    desc.type = SG_IMAGETYPE_ARRAY;
    desc.width = size;
    desc.height = size;
    desc.num_slices = static_cast<int>(loaded.size());
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = slices.data();
    desc.data.mip_levels[0].size = slices.size();
    desc.label = "tileshape-tiling-array";
    sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_image failed (tiling array)");
        return 0;
    }
    sg_view_desc viewDesc = {};
    viewDesc.texture.image = image;
    sg_view view = sg_make_view(&viewDesc);
    if (view.id == SG_INVALID_ID) {
        spdlog::error("AtlasRenderer: sg_make_view failed (tiling array)");
        sg_destroy_image(image);
        return 0;
    }

    destroySlot(m_tilingArray);
    m_tilingArray.image = image;
    m_tilingArray.view = view;
    m_tilingArrayLayers = static_cast<int>(loaded.size());
    spdlog::info("AtlasRenderer: tiling array {}x{} x{} layers from {} files", size, size, loaded.size(), paths.size());
    return m_tilingArrayLayers;
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
    float yOffset,
    float baseLayer) {

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

    const float L[4] = {baseLayer, 0.0f, 0.0f, 0.0f};
    const float W[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    const TexVertex tl{x0, y0, uv.x, uv.y, {L[0], L[1], L[2], L[3]}, {W[0], W[1], W[2], W[3]}, 1.0f};
    const TexVertex tr{x1, y0, uv.z, uv.y, {L[0], L[1], L[2], L[3]}, {W[0], W[1], W[2], W[3]}, 1.0f};
    const TexVertex br{x1, y1, uv.z, uv.w, {L[0], L[1], L[2], L[3]}, {W[0], W[1], W[2], W[3]}, 1.0f};
    const TexVertex bl{x0, y1, uv.x, uv.w, {L[0], L[1], L[2], L[3]}, {W[0], W[1], W[2], W[3]}, 1.0f};

    out.push_back(tl);
    out.push_back(tr);
    out.push_back(br);
    out.push_back(tl);
    out.push_back(br);
    out.push_back(bl);
}

void AtlasRenderer::appendTileFan(
    std::vector<TexVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    int tileIndex,
    const float layers[4],
    const glm::vec4 cornerWeights[4],
    const float cornerFill[4]) {

    // Same square-frame UV mapping as appendTileQuad, but tessellated as a
    // fan over the cell diamond (center + the 4 corner node positions), so
    // the texture weights sit exactly on the nodes and interpolate
    // continuously across shared edges of neighboring cells. The fill
    // weight rides the same interpolation and fades the coverage into
    // empty space in the FS.
    const glm::vec4 uv = atlasUvRect(tileIndex);
    const glm::vec2 center = iso.mapToField(cell);
    const float side = iso.dims.cellWidth;
    const float half = side * 0.5f;

    const auto uvAt = [&](glm::vec2 p) -> glm::vec2 {
        return {
            uv.x + (p.x - (center.x - half)) / side * (uv.z - uv.x),
            uv.y + (p.y - (center.y - half)) / side * (uv.w - uv.y)};
    };
    const auto makeVert = [&](glm::vec2 p, glm::vec4 w, float fill) -> TexVertex {
        const glm::vec2 t = uvAt(p);
        return TexVertex{
            p.x, p.y, t.x, t.y,
            {layers[0], layers[1], layers[2], layers[3]},
            {w.x, w.y, w.z, w.w},
            fill};
    };

    const auto corners = iso.cellDiamondCorners(cell); // Left, Up, Right, Down
    const glm::vec4 centerWeights =
        (cornerWeights[0] + cornerWeights[1] + cornerWeights[2] + cornerWeights[3]) * 0.25f;
    const float centerFill = (cornerFill[0] + cornerFill[1] + cornerFill[2] + cornerFill[3]) * 0.25f;

    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        out.push_back(makeVert(center, centerWeights, centerFill));
        out.push_back(makeVert(corners[i], cornerWeights[i], cornerFill[i]));
        out.push_back(makeVert(corners[next], cornerWeights[next], cornerFill[next]));
    }
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

void AtlasRenderer::appendDiamondFill(
    std::vector<ColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell); // Left, Up, Right, Down
    const int tris[6] = {0, 1, 2, 0, 2, 3};
    for (const int idx : tris) {
        const glm::vec2 p = corners[idx];
        out.push_back({p.x, p.y, color.r, color.g, color.b, color.a});
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
    // is drawn with the layer's own atlas (and, for textured layers, with
    // the tiling array sampled in world coordinates under the mask).
    struct TexRange {
        int base = 0;
        int count = 0;
        AtlasKind atlas = AtlasKind::Grass;
        bool textured = false;
        float baseLayer = 0.0f;   // single-texture mode: array layer for the whole range
        float tilingScale = 0.0f; // repeats per field unit
        float blendSharpness = 1.0f;
        float blendNoise = 0.0f;
        float blendNoiseScale = 0.0f; // per field unit
        float edgeFade = 0.0f;        // feather width around the fill = 0.5 iso
    };
    std::vector<TexRange> flatRanges;
    std::vector<TexVertex> texVerts;
    texVerts.reserve(static_cast<std::size_t>(mapW * mapH * 12 * layerCount));

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        // The cliff layer also paints its flat tiles: they show the painted
        // silhouette instantly while the field mesh rebuilds (debounced), and
        // the finished mesh covers them by depth afterwards.
        if (!layer.brush) {
            continue;
        }
        if (layer.multiTexture) {
            // Multi-texture layer: ONE range for the whole layer. Each cell
            // is a diamond fan whose corner vertices carry one-hot weights
            // over the cell's candidate textures (LandBrush::cellTextureBlend)
            // — neighboring textures blend smoothly across the shared nodes.
            // The corner fill weights (on/off) ride along and feather the
            // region contour into empty space in the FS.
            TexRange range;
            range.base = static_cast<int>(texVerts.size());
            range.atlas = layer.atlas;
            range.textured = true;
            range.tilingScale = layer.tilingRepeats / std::max(iso.dims.cellWidth, 1.0f);
            range.blendSharpness = layer.blendSharpness;
            range.blendNoise = layer.blendNoise;
            range.blendNoiseScale = layer.blendNoiseScale / std::max(iso.dims.cellWidth, 1.0f);
            range.edgeFade = layer.edgeFade;
            for (const auto& [z, cell] : drawOrder) {
                (void)z;
                const auto type = layer.brush->cellTypeAt(cell);
                if (!landscape_core::tileTypeHasSurface(type)) {
                    continue;
                }
                const int idx = LandBrush::atlasIndexByType(type);
                if (idx < 0) {
                    continue;
                }
                float cellLayers[4];
                glm::vec4 cornerWeights[4];
                layer.brush->cellTextureBlend(cell, cellLayers, cornerWeights);
                const std::array<bool, 4> mask = layer.brush->nodeMaskAt(cell);
                const float cornerFill[4] = {
                    mask[0] ? 1.0f : 0.0f, mask[1] ? 1.0f : 0.0f,
                    mask[2] ? 1.0f : 0.0f, mask[3] ? 1.0f : 0.0f};
                appendTileFan(texVerts, iso, cell, idx, cellLayers, cornerWeights, cornerFill);
            }
            range.count = static_cast<int>(texVerts.size()) - range.base;
            if (range.count > 0) {
                flatRanges.push_back(range);
            }
            continue;
        }
        TexRange range;
        range.base = static_cast<int>(texVerts.size());
        range.atlas = layer.atlas;
        if (layer.tilingTex >= 0 && layer.tilingTex < m_tilingArrayLayers) {
            range.textured = true;
            range.baseLayer = static_cast<float>(layer.tilingTex);
            range.tilingScale = layer.tilingRepeats / std::max(iso.dims.cellWidth, 1.0f);
        }
        for (const auto& [z, cell] : drawOrder) {
            (void)z;
            const auto type = layer.brush->cellTypeAt(cell);
            if (!landscape_core::tileTypeHasSurface(type)) {
                continue;
            }
            const int idx = LandBrush::atlasIndexByType(type);
            if (idx >= 0) {
                appendTileQuad(texVerts, iso, cell, idx, 0.0f, range.baseLayer);
            }
        }
        range.count = static_cast<int>(texVerts.size()) - range.base;
        if (range.count > 0) {
            flatRanges.push_back(range);
        }
    }

    // Normalized depth along the iso view ray, anchored at the visible
    // ground-y center with generous margins (monotonicity is what matters;
    // the real scene stays far from the clip planes). Ground-y grows TOWARD
    // the viewer, so with LESS_EQUAL + clear 1.0 the closer fragment must map
    // to the SMALLER z.
    const float groundCenterY = camera.screenToWorld({viewW * 0.5f, viewH * 0.5f}).y;
    const float zFar = groundCenterY + 100000.0f;
    const float zScale = 1.0f / 200000.0f;

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

    // Painter order: flat ground layers, then the z-buffered cliff meshes,
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
        bind.views[1] = m_tilingArray.view;
        bind.samplers[1] = m_topTexSampler;

        TexFsParams texFs{};
        texFs.values[0] = range.textured ? 1.0f : 0.0f;
        texFs.values[1] = range.tilingScale;
        texFs.blend[0] = range.blendSharpness;
        texFs.blend[1] = range.blendNoise;
        texFs.blend[2] = range.blendNoiseScale;
        texFs.blend[3] = range.edgeFade;

        sg_apply_pipeline(m_texPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_apply_uniforms(1, sg_range{&texFs, sizeof(texFs)});
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

    // Cliff/stone layers: scalar-field surface nets from the same nodes, drawn
    // with the z-buffer and per-pixel shading. The mesh is cached per brush;
    // the heavy field rebuild runs at most once per edit burst (0.3 s
    // debounce), a heightScale edit only re-projects the cached mesh.
    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || (!layer.cliff && !layer.stone)) {
            continue;
        }
        if ((layer.cliff && !layer.cliffParams) || (layer.stone && !layer.stoneParams)) {
            continue;
        }
        CliffCache& cache = cliffCacheFor(layer.brush);
        const bool paramsChanged = layer.stone
            ? std::memcmp(&cache.stoneParams, layer.stoneParams, sizeof(stone_gen::StoneFieldParams)) != 0
            : std::memcmp(&cache.params, layer.cliffParams, sizeof(cliff::FieldParams)) != 0;
        const bool contentChanged = !cache.contentValid ||
            cache.brushVersion != layer.brush->version() ||
            cache.stone != layer.stone || paramsChanged;
        const bool scaleChanged = cache.heightScale != layer.cliffHeightScale;
        if (contentChanged || scaleChanged) {
            cache.brushVersion = layer.brush->version();
            cache.stone = layer.stone;
            if (layer.stone) {
                std::memcpy(&cache.stoneParams, layer.stoneParams, sizeof(stone_gen::StoneFieldParams));
            } else {
                std::memcpy(&cache.params, layer.cliffParams, sizeof(cliff::FieldParams));
            }
            cache.heightScale = layer.cliffHeightScale;
            cache.contentValid = true;
            cache.lastEditSec = nowSec;
            cache.meshDirty = cache.meshDirty || contentChanged;
            cache.streamDirty = true;
            cache.stats.pending = true;
        }
        if ((cache.meshDirty || cache.streamDirty) && (nowSec - cache.lastEditSec) > 0.3) {
            rebuildCliffCache(cache, iso, zFar, zScale);
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
            bind.views[0] = m_topTexView;
            bind.samplers[0] = m_topTexSampler;

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
    const topology_core::DiamondIsometry& iso,
    float zFar,
    float zScale) {

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
            minX -= 1;
            minY -= 1;
            maxX += 1;
            maxY += 1;
            const int nodesX = maxX - minX + 1;
            const int nodesY = maxY - minY + 1;
            std::vector<std::uint8_t> nodes(static_cast<size_t>(nodesX) * nodesY, 0);
            for (const glm::ivec2& n : onNodes) {
                nodes[static_cast<size_t>(n.y - minY) * nodesX + (n.x - minX)] = 1;
            }

            cliff::RegularizeStats regStats;
            if (cache.stone) {
                // StoneCube voronoi stones over the same slab:
                // StoneField -> generic ScalarFieldView -> surface nets.
                stone_gen::StoneField field(cache.stoneParams, nodes.data(), nodesX, nodesY);
                cliff::ScalarFieldView view = field.view();
                std::vector<float> samples;
                field.sample(samples);
                cliff::regularizeSigns(view, samples, &regStats);
                cache.mesh = cliff::extractSurfaceNets(view, samples, nullptr);
                cache.stats.voxelCount = view.nx * view.ny * view.nz;
            } else {
                cliff::CliffField field(cache.params, nodes.data(), nodesX, nodesY);
                std::vector<float> samples;
                field.sample(samples);
                cliff::regularizeSigns(field, samples, &regStats);
                cache.mesh = cliff::extractSurfaceNets(field, samples, nullptr);
                cache.stats.voxelCount = field.sizeX() * field.sizeY() * field.sizeZ();
            }
            const cliff::WatertightReport report = cliff::checkWatertight(cache.mesh);
            cache.stats.watertight = report.ok();
            cache.stats.vertexCount = static_cast<int>(cache.mesh.vertices.size());
            cache.stats.triangleCount = static_cast<int>(cache.mesh.indices.size() / 3);
            cache.origin = {minX, minY};
            if (!report.ok()) {
                spdlog::warn("AtlasRenderer: {} mesh not watertight ({} bad of {} edges, {} saddles left)",
                    cache.stone ? "stone" : "cliff",
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
        const float z = (zFar - fieldY) * zScale;
        cache.stream.push_back(CliffVertex{
            fieldX, fieldY - v.py * cache.heightScale, z,
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
        for (int i = 0; i < 5; ++i) {
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
        fillTexFsUniformDesc(&shd.uniform_blocks[1]);

        shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[0].texture.hlsl_register_t_n = 0;
        shd.views[0].texture.msl_texture_n = 0;
        shd.views[0].texture.wgsl_group1_binding_n = 0;
        shd.views[0].texture.spirv_set1_binding_n = 0;

        shd.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[1].texture.image_type = SG_IMAGETYPE_ARRAY;
        shd.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.views[1].texture.hlsl_register_t_n = 1;
        shd.views[1].texture.msl_texture_n = 1;
        shd.views[1].texture.wgsl_group1_binding_n = 2;
        shd.views[1].texture.spirv_set1_binding_n = 2;

        shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.samplers[0].hlsl_register_s_n = 0;
        shd.samplers[0].msl_sampler_n = 0;
        shd.samplers[0].wgsl_group1_binding_n = 1;
        shd.samplers[0].spirv_set1_binding_n = 1;

        shd.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.samplers[1].hlsl_register_s_n = 1;
        shd.samplers[1].msl_sampler_n = 1;
        shd.samplers[1].wgsl_group1_binding_n = 3;
        shd.samplers[1].spirv_set1_binding_n = 3;

        shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[0].view_slot = 0;
        shd.texture_sampler_pairs[0].sampler_slot = 0;
        shd.texture_sampler_pairs[0].glsl_name = "atlas_tex";

        shd.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[1].view_slot = 1;
        shd.texture_sampler_pairs[1].sampler_slot = 1;
        shd.texture_sampler_pairs[1].glsl_name = "tiling_array";

        m_texShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_texShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
        pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4;
        pip.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
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
        shd.texture_sampler_pairs[0].glsl_name = "top_tex";

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
