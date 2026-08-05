#include "render_core/landscape_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include <highground_core/highground.h>
#include <landscape_core/landscape_logic.h>

#include "render_core/image_loader.h"
#include "render_core/texture_blend.h"

#include "atlas_tile_types.h"

namespace render_core {

static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv0;
layout(location=2) in vec4 color0;
out vec2 v_uv;
out vec4 v_color;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_uv = uv0;
    v_color = color0;
}
)";

static const char* fs_src_glsl = R"(
#version 330
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
uniform sampler2D tex;
void main() {
    vec4 t = texture(tex, v_uv);
    frag_color = t * v_color;
}
)";

// Minimal HLSL for D3D11 backend
static const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; };
struct VSIn { float2 pos: TEXCOORD0; float2 uv0: TEXCOORD1; float4 color0: TEXCOORD2; };
struct VSOut { float4 pos: SV_Position; float2 uv0: TEXCOORD0; float4 color0: TEXCOORD1; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.uv0 = inp.uv0;
    o.color0 = inp.color0;
    return o;
}
)";

static const char* fs_src_hlsl = R"(
Texture2D tex0: register(t0);
SamplerState smp0: register(s0);
struct PSIn { float4 pos: SV_Position; float2 uv0: TEXCOORD0; float4 color0: TEXCOORD1; };
float4 main(PSIn inp): SV_Target0 {
    float4 t = tex0.Sample(smp0, inp.uv0);
    return t * inp.color0;
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
    float2 pos [[attribute(0)]];
    float2 uv0 [[attribute(1)]];
    float4 color0 [[attribute(2)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv0;
    float4 color0;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.uv0 = in.uv0;
    o.color0 = in.color0;
    return o;
}
)";

static const char* fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float2 uv0;
    float4 color0;
};

fragment float4 _main(PSIn in [[stage_in]],
                      texture2d<float> tex0 [[texture(0)]],
                      sampler smp0 [[sampler(0)]]) {
    float4 t = tex0.sample(smp0, in.uv0);
    return t * in.color0;
}
)";

// ---------------------------------------------------------------------------
// Stitched flat ground pass (scene stitch port from HighgroundWithEffects):
// the tile quad carries the world (map-cell) position, sits at one constant
// depth behind the whole scene and shades with the shared sun/tone + the
// contact-AO distance field. Tiles tie under LESS_EQUAL and keep resolving
// against each other by painter order; the 3D passes (raised/cliff/stone,
// all closer) always win.
// ---------------------------------------------------------------------------

static const char* ground_vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 uv0;
layout(location=2) in vec2 world;
out vec2 v_uv;
out vec2 v_world;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    // Constant depth behind the whole scene (HighgroundWithEffects'
    // kGroundDepth): the z-range convention maps visible content to ~0.5.
    gl_Position = vec4(clip_pos, 0.999, 1.0);
    v_uv = uv0;
    v_world = world;
}
)";

static const char* ground_fs_src_glsl = R"(
#version 330
in vec2 v_uv;
in vec2 v_world;
out vec4 frag_color;
uniform vec4 sun_dir;
uniform vec4 stitch0; // ground ambient, diffuse, gamma, unused
uniform vec4 stitch1; // unused, unused, AO strength, AO radius (cells)
uniform vec4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
uniform sampler2D tex;
uniform sampler2D ao_tex;
void main() {
    vec4 t = texture(tex, v_uv);
    // One sun, one ambient, one gamma with the cliff pass: the ground drawn
    // as a raw texture next to a lit mesh is what read as a collage. Flat
    // ground faces straight up, so ndl is the sun's y.
    vec3 l = normalize(sun_dir.xyz);
    float light = stitch0.x + stitch0.y * max(l.y, 0.0);
    // Contact AO: the R8 field holds the distance to the highground footprint
    // normalized by 4 cells (kAoMaxDistanceCells).
    vec2 aoUv = (v_world - ao_rect.xy) * ao_rect.zw;
    float aoDist = textureLod(ao_tex, aoUv, 0.0).r * 4.0;
    float ao = mix(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    vec3 col = t.rgb * (light * ao);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(stitch0.z)), t.a);
}
)";

static const char* ground_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; };
struct VSIn { float2 pos: TEXCOORD0; float2 uv0: TEXCOORD1; float2 world: TEXCOORD2; };
struct VSOut { float4 pos: SV_Position; float2 uv0: TEXCOORD0; float2 world: TEXCOORD1; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    // Constant depth behind the whole scene (see the GLSL variant).
    o.pos = float4(clip, 0.999, 1.0);
    o.uv0 = inp.uv0;
    o.world = inp.world;
    return o;
}
)";

static const char* ground_fs_src_hlsl = R"(
cbuffer stitch_params: register(b0) {
    float4 sun_dir;
    float4 stitch0; // ground ambient, diffuse, gamma, unused
    float4 stitch1; // unused, unused, AO strength, AO radius (cells)
    float4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
};
Texture2D tex0: register(t0);
SamplerState smp0: register(s0);
Texture2D ao_tex: register(t1);
SamplerState ao_smp: register(s1);
struct PSIn { float4 pos: SV_Position; float2 uv0: TEXCOORD0; float2 world: TEXCOORD1; };
float4 main(PSIn inp): SV_Target0 {
    float4 t = tex0.Sample(smp0, inp.uv0);
    float3 l = normalize(sun_dir.xyz);
    float light = stitch0.x + stitch0.y * max(l.y, 0.0);
    float2 aoUv = (inp.world - ao_rect.xy) * ao_rect.zw;
    float aoDist = ao_tex.SampleLevel(ao_smp, aoUv, 0).r * 4.0;
    float ao = lerp(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    float3 col = t.rgb * (light * ao);
    return float4(pow(saturate(col), (float3)stitch0.z), t.a);
}
)";

static const char* ground_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float2 uv0 [[attribute(1)]];
    float2 world [[attribute(2)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv0;
    float2 world;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    // Constant depth behind the whole scene (see the GLSL variant).
    o.pos = float4(clip, 0.999, 1.0);
    o.uv0 = in.uv0;
    o.world = in.world;
    return o;
}
)";

static const char* ground_fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct StitchParams {
    float4 sun_dir;
    float4 stitch0; // ground ambient, diffuse, gamma, unused
    float4 stitch1; // unused, unused, AO strength, AO radius (cells)
    float4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
};

struct PSIn {
    float4 pos [[position]];
    float2 uv0;
    float2 world;
};

fragment float4 _main(PSIn in [[stage_in]],
                      constant StitchParams& st [[buffer(1)]],
                      texture2d<float> tex0 [[texture(0)]],
                      sampler smp0 [[sampler(0)]],
                      texture2d<float> ao_tex [[texture(1)]],
                      sampler ao_smp [[sampler(1)]]) {
    float4 t = tex0.sample(smp0, in.uv0);
    float3 l = normalize(st.sun_dir.xyz);
    float light = st.stitch0.x + st.stitch0.y * max(l.y, 0.0);
    float2 aoUv = (in.world - st.ao_rect.xy) * st.ao_rect.zw;
    float aoDist = ao_tex.sample(ao_smp, aoUv, level(0)).r * 4.0;
    float ao = mix(1.0 - st.stitch1.z, 1.0, smoothstep(0.0, max(st.stitch1.w, 1e-3), aoDist));
    float3 col = t.rgb * (light * ao);
    return float4(pow(saturate(col), float3(st.stitch0.z)), t.a);
}
)";

// ---------------------------------------------------------------------------
// Texture-2D pass (SDFGeneratedLandscape "Texture 2D" layer port): a flat
// ground-level pass where every cell is a diamond fan (center + the 4 corner
// nodes). The corners carry one-hot weights over the cell's candidate
// textures (texture_blend.h) and the on/off fill weight, so neighboring
// textures blend smoothly across the shared nodes and the region contour
// feathers into empty space. Textures live in one array and sample in world
// (map-cell) coordinates — continuous, no per-tile cuts. The stitch shading
// (sun/tone + contact AO) matches the flat ground pass.
// ---------------------------------------------------------------------------

static const char* texture_vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec2 world;
layout(location=2) in vec4 layers;
layout(location=3) in vec4 weights;
layout(location=4) in vec4 tiling;
layout(location=5) in float fill;
out vec2 v_world;
out vec4 v_layers;
out vec4 v_weights;
out vec4 v_tiling;
out float v_fill;
uniform vec2 view_size;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    // Same ground-level constant depth as the flat ground pass.
    gl_Position = vec4(clip_pos, 0.999, 1.0);
    v_world = world;
    v_layers = layers;
    v_weights = weights;
    v_tiling = tiling;
    v_fill = fill;
}
)";

static const char* texture_fs_src_glsl = R"(
#version 330
in vec2 v_world;
in vec4 v_layers;
in vec4 v_weights;
in vec4 v_tiling;
in float v_fill;
out vec4 frag_color;
uniform vec4 sun_dir;
uniform vec4 stitch0; // ground ambient, diffuse, gamma, unused
uniform vec4 stitch1; // unused, unused, AO strength, AO radius (cells)
uniform vec4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
uniform vec4 blend_params; // weight sharpness, wobble strength, wobble scale (per cell), edge feather
uniform sampler2DArray tiling_array;
uniform sampler2D ao_tex;

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
    // Up to 4 candidate textures blend via the interpolated corner weights;
    // in the blend zone the weights are sharpened and wobbled by fbm noise
    // for an organic edge (the wobble is gated by w*(1-w), so pure interiors
    // stay untouched). Each texture tiles with its own density.
    float n = 0.0;
    if (blend_params.y > 0.0) {
        n = fbm2(v_world * blend_params.z) - 0.5;
    }
    vec4 w = max(v_weights, 0.0);
    float sw = w.x + w.y + w.z + w.w;
    vec3 rgb = vec3(0.0);
    if (sw > 1e-4) {
        w = pow(w, vec4(max(blend_params.x, 0.05)));
        w += n * blend_params.y * w * (1.0 - w);
        w = max(w, 0.0);
        w /= max(w.x + w.y + w.z + w.w, 1e-4);
        rgb = w.x * texture(tiling_array, vec3(v_world * v_tiling.x, v_layers.x)).rgb
            + w.y * texture(tiling_array, vec3(v_world * v_tiling.y, v_layers.y)).rgb
            + w.z * texture(tiling_array, vec3(v_world * v_tiling.z, v_layers.z)).rgb
            + w.w * texture(tiling_array, vec3(v_world * v_tiling.w, v_layers.w)).rgb;
    }
    // Soft edge into empty space: the fill weight (1 at on-nodes, 0 at
    // off-nodes) fades coverage around the 0.5 iso with the same noise
    // wobble, so the region contour feathers out instead of stepping hard.
    float feather = max(blend_params.w, 1e-3);
    float fill = clamp(v_fill + n * blend_params.y * v_fill * (1.0 - v_fill), 0.0, 1.0);
    float alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
    if (alpha < 0.004) discard;
    // One sun, one ambient, one gamma with the flat ground pass; the contact
    // AO field is shared too (this layer stays ground-level scenery).
    vec3 l = normalize(sun_dir.xyz);
    float light = stitch0.x + stitch0.y * max(l.y, 0.0);
    vec2 aoUv = (v_world - ao_rect.xy) * ao_rect.zw;
    float aoDist = textureLod(ao_tex, aoUv, 0.0).r * 4.0;
    float ao = mix(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    vec3 col = rgb * (light * ao);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(stitch0.z)), alpha);
}
)";

static const char* texture_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; };
struct VSIn {
    float2 pos: TEXCOORD0;
    float2 world: TEXCOORD1;
    float4 layers: TEXCOORD2;
    float4 weights: TEXCOORD3;
    float4 tiling: TEXCOORD4;
    float fill: TEXCOORD5;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 world: TEXCOORD0;
    float4 layers: TEXCOORD1;
    float4 weights: TEXCOORD2;
    float4 tiling: TEXCOORD3;
    float fill: TEXCOORD4;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    // Same ground-level constant depth as the flat ground pass.
    o.pos = float4(clip, 0.999, 1.0);
    o.world = inp.world;
    o.layers = inp.layers;
    o.weights = inp.weights;
    o.tiling = inp.tiling;
    o.fill = inp.fill;
    return o;
}
)";

static const char* texture_fs_src_hlsl = R"(
cbuffer stitch_params: register(b0) {
    float4 sun_dir;
    float4 stitch0; // ground ambient, diffuse, gamma, unused
    float4 stitch1; // unused, unused, AO strength, AO radius (cells)
    float4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
};
cbuffer blend_ub: register(b1) {
    float4 blend_params; // weight sharpness, wobble strength, wobble scale (per cell), edge feather
};
Texture2DArray tiling_array: register(t0);
SamplerState tiling_smp: register(s0);
Texture2D ao_tex: register(t1);
SamplerState ao_smp: register(s1);
struct PSIn {
    float4 pos: SV_Position;
    float2 world: TEXCOORD0;
    float4 layers: TEXCOORD1;
    float4 weights: TEXCOORD2;
    float4 tiling: TEXCOORD3;
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

float4 main(PSIn inp): SV_Target0 {
    // World-space tiling + candidate blend + soft empty edge: see the GLSL variant.
    float n = 0.0;
    if (blend_params.y > 0.0) {
        n = fbm2(inp.world * blend_params.z) - 0.5;
    }
    float4 w = max(inp.weights, 0.0);
    float sw = w.x + w.y + w.z + w.w;
    float3 rgb = float3(0.0, 0.0, 0.0);
    if (sw > 1e-4) {
        w = pow(w, (float4)max(blend_params.x, 0.05));
        w += n * blend_params.y * w * (1.0 - w);
        w = max(w, 0.0);
        w /= max(w.x + w.y + w.z + w.w, 1e-4);
        rgb = w.x * tiling_array.Sample(tiling_smp, float3(inp.world * inp.tiling.x, inp.layers.x)).rgb
            + w.y * tiling_array.Sample(tiling_smp, float3(inp.world * inp.tiling.y, inp.layers.y)).rgb
            + w.z * tiling_array.Sample(tiling_smp, float3(inp.world * inp.tiling.z, inp.layers.z)).rgb
            + w.w * tiling_array.Sample(tiling_smp, float3(inp.world * inp.tiling.w, inp.layers.w)).rgb;
    }
    float feather = max(blend_params.w, 1e-3);
    float fill = clamp(inp.fill + n * blend_params.y * inp.fill * (1.0 - inp.fill), 0.0, 1.0);
    float alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
    if (alpha < 0.004) discard;
    float3 l = normalize(sun_dir.xyz);
    float light = stitch0.x + stitch0.y * max(l.y, 0.0);
    float2 aoUv = (inp.world - ao_rect.xy) * ao_rect.zw;
    float aoDist = ao_tex.SampleLevel(ao_smp, aoUv, 0).r * 4.0;
    float ao = lerp(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    float3 col = rgb * (light * ao);
    return float4(pow(saturate(col), (float3)stitch0.z), alpha);
}
)";

static const char* texture_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float2 world [[attribute(1)]];
    float4 layers [[attribute(2)]];
    float4 weights [[attribute(3)]];
    float4 tiling [[attribute(4)]];
    float fill [[attribute(5)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 world;
    float4 layers;
    float4 weights;
    float4 tiling;
    float fill;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    // Same ground-level constant depth as the flat ground pass.
    o.pos = float4(clip, 0.999, 1.0);
    o.world = in.world;
    o.layers = in.layers;
    o.weights = in.weights;
    o.tiling = in.tiling;
    o.fill = in.fill;
    return o;
}
)";

static const char* texture_fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct StitchParams {
    float4 sun_dir;
    float4 stitch0; // ground ambient, diffuse, gamma, unused
    float4 stitch1; // unused, unused, AO strength, AO radius (cells)
    float4 ao_rect; // xy: AO field origin (cells), zw: 1 / field extent
};

struct BlendParams {
    float4 blend_params; // weight sharpness, wobble strength, wobble scale (per cell), edge feather
};

struct PSIn {
    float4 pos [[position]];
    float2 world;
    float4 layers;
    float4 weights;
    float4 tiling;
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
                      constant StitchParams& st [[buffer(1)]],
                      constant BlendParams& bp [[buffer(2)]],
                      texture2d_array<float> tiling_array [[texture(0)]],
                      sampler tiling_smp [[sampler(0)]],
                      texture2d<float> ao_tex [[texture(1)]],
                      sampler ao_smp [[sampler(1)]]) {
    // World-space tiling + candidate blend + soft empty edge: see the GLSL variant.
    float n = 0.0;
    if (bp.blend_params.y > 0.0) {
        n = fbm2(in.world * bp.blend_params.z) - 0.5;
    }
    float4 w = max(in.weights, 0.0);
    float sw = w.x + w.y + w.z + w.w;
    float3 rgb = float3(0.0);
    if (sw > 1e-4) {
        w = pow(w, float4(max(bp.blend_params.x, 0.05)));
        w += n * bp.blend_params.y * w * (1.0 - w);
        w = max(w, 0.0);
        w /= max(w.x + w.y + w.z + w.w, 1e-4);
        rgb = w.x * tiling_array.sample(tiling_smp, in.world * in.tiling.x, uint(in.layers.x)).rgb
            + w.y * tiling_array.sample(tiling_smp, in.world * in.tiling.y, uint(in.layers.y)).rgb
            + w.z * tiling_array.sample(tiling_smp, in.world * in.tiling.z, uint(in.layers.z)).rgb
            + w.w * tiling_array.sample(tiling_smp, in.world * in.tiling.w, uint(in.layers.w)).rgb;
    }
    float feather = max(bp.blend_params.w, 1e-3);
    float fill = clamp(in.fill + n * bp.blend_params.y * in.fill * (1.0 - in.fill), 0.0, 1.0);
    float alpha = smoothstep(0.5 - feather, 0.5 + feather, fill);
    if (alpha < 0.004) {
        discard_fragment();
    }
    float3 l = normalize(st.sun_dir.xyz);
    float light = st.stitch0.x + st.stitch0.y * max(l.y, 0.0);
    float2 aoUv = (in.world - st.ao_rect.xy) * st.ao_rect.zw;
    float aoDist = ao_tex.sample(ao_smp, aoUv, level(0)).r * 4.0;
    float ao = mix(1.0 - st.stitch1.z, 1.0, smoothstep(0.0, max(st.stitch1.w, 1e-3), aoDist));
    float3 col = rgb * (light * ao);
    return float4(pow(saturate(col), float3(st.stitch0.z)), alpha);
}
)";

// Untextured pos+color shaders for the cliff wall pipeline (same vertex
// layout as OverlayRenderer, triangles instead of lines).
static const char* wall_vs_src_glsl = R"(
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

static const char* wall_fs_src_glsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

static const char* wall_vs_src_hlsl = R"(
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

static const char* wall_fs_src_hlsl = R"(
struct PSIn { float4 pos: SV_Position; float4 color0: TEXCOORD0; };
float4 main(PSIn inp): SV_Target0 {
    return inp.color0;
}
)";

static const char* wall_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
};

struct VSIn {
    float2 pos [[attribute(0)]];
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
    o.pos = float4(clip, 0.0, 1.0);
    o.color0 = in.color0;
    return o;
}
)";

static const char* wall_fs_src_msl = R"(
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

// Z-buffered raised pass: pos.z is the ground y (un-lifted field y) of the
// vertex, monotonic with depth along the iso view ray (0,+1,+1). Ground-y
// grows TOWARD the viewer (the painter draws larger ground-y last), so with
// LESS_EQUAL + clear 1.0 the closer fragment must map to the SMALLER z —
// hence (zFar - groundY) * zScale. z_range = {far ground-y, 1/(far-near)}.
// Fragment shaders are shared with the painter pipelines.
static const char* depth_tex_vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec2 uv0;
layout(location=2) in vec4 color0;
out vec2 v_uv;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 z_range;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, (z_range.x - pos.z) * z_range.y, 1.0);
    v_uv = uv0;
    v_color = color0;
}
)";

static const char* depth_tex_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; float2 z_range; };
struct VSIn { float3 pos: TEXCOORD0; float2 uv0: TEXCOORD1; float4 color0: TEXCOORD2; };
struct VSOut { float4 pos: SV_Position; float2 uv0: TEXCOORD0; float4 color0: TEXCOORD1; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, (z_range.x - inp.pos.z) * z_range.y, 1.0);
    o.uv0 = inp.uv0;
    o.color0 = inp.color0;
    return o;
}
)";

static const char* depth_tex_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 z_range;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float2 uv0 [[attribute(1)]];
    float4 color0 [[attribute(2)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv0;
    float4 color0;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, (params.z_range.x - in.pos.z) * params.z_range.y, 1.0);
    o.uv0 = in.uv0;
    o.color0 = in.color0;
    return o;
}
)";

static const char* depth_wall_vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color0;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 z_range;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, (z_range.x - pos.z) * z_range.y, 1.0);
    v_color = color0;
}
)";

static const char* depth_wall_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; float2 z_range; };
struct VSIn { float3 pos: TEXCOORD0; float4 color0: TEXCOORD1; };
struct VSOut { float4 pos: SV_Position; float4 color0: TEXCOORD0; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, (z_range.x - inp.pos.z) * z_range.y, 1.0);
    o.color0 = inp.color0;
    return o;
}
)";

static const char* depth_wall_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 z_range;
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
    o.pos = float4(clip, (params.z_range.x - in.pos.z) * params.z_range.y, 1.0);
    o.color0 = in.color0;
    return o;
}
)";

// Triplanar walls: tcoord = undeformed map coordinates in world px + lift;
// the fragment shader blends the (mapY,lift) and (mapX,lift) projections of
// a tiling rock texture by the contour normal (Vertex::normal from
// highground_core) — no UVs, no seams. tri_params.x = tex scale (px -> uv).
static const char* tri_wall_vs_src_glsl = R"(
#version 330
layout(location=0) in vec2 pos;
layout(location=1) in vec4 color0;
layout(location=2) in vec3 tcoord;
layout(location=3) in vec2 normal;
out vec4 v_color;
out vec2 v_uv1;
out vec2 v_uv2;
out vec2 v_normal;
uniform vec2 view_size;
uniform vec2 tri_params;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_color = color0;
    v_uv1 = tcoord.yz * tri_params.x;
    v_uv2 = tcoord.xz * tri_params.x;
    v_normal = normal;
}
)";

static const char* tri_wall_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; float2 tri_params; };
struct VSIn { float2 pos: TEXCOORD0; float4 color0: TEXCOORD1; float3 tcoord: TEXCOORD2; float2 normal: TEXCOORD3; };
struct VSOut { float4 pos: SV_Position; float4 color0: TEXCOORD0; float2 uv1: TEXCOORD1; float2 uv2: TEXCOORD2; float2 normal: TEXCOORD3; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.color0 = inp.color0;
    o.uv1 = inp.tcoord.yz * tri_params.x;
    o.uv2 = inp.tcoord.xz * tri_params.x;
    o.normal = inp.normal;
    return o;
}
)";

static const char* tri_wall_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 tri_params;
};

struct VSIn {
    float2 pos [[attribute(0)]];
    float4 color0 [[attribute(1)]];
    float3 tcoord [[attribute(2)]];
    float2 normal [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color0;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, 0.0, 1.0);
    o.color0 = in.color0;
    o.uv1 = in.tcoord.yz * params.tri_params.x;
    o.uv2 = in.tcoord.xz * params.tri_params.x;
    o.normal = in.normal;
    return o;
}
)";

static const char* tri_depth_wall_vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color0;
layout(location=2) in vec3 tcoord;
layout(location=3) in vec2 normal;
out vec4 v_color;
out vec2 v_uv1;
out vec2 v_uv2;
out vec2 v_normal;
uniform vec2 view_size;
uniform vec2 z_range;
uniform vec2 tri_params;
void main() {
    vec2 clip_pos = vec2(
        (pos.x / view_size.x) * 2.0 - 1.0,
        1.0 - (pos.y / view_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, (z_range.x - pos.z) * z_range.y, 1.0);
    v_color = color0;
    v_uv1 = tcoord.yz * tri_params.x;
    v_uv2 = tcoord.xz * tri_params.x;
    v_normal = normal;
}
)";

static const char* tri_depth_wall_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float2 view_size; float2 z_range; float2 tri_params; };
struct VSIn { float3 pos: TEXCOORD0; float4 color0: TEXCOORD1; float3 tcoord: TEXCOORD2; float2 normal: TEXCOORD3; };
struct VSOut { float4 pos: SV_Position; float4 color0: TEXCOORD0; float2 uv1: TEXCOORD1; float2 uv2: TEXCOORD2; float2 normal: TEXCOORD3; };
VSOut main(VSIn inp) {
    VSOut o;
    float2 clip;
    clip.x = (inp.pos.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (inp.pos.y / view_size.y) * 2.0;
    o.pos = float4(clip, (z_range.x - inp.pos.z) * z_range.y, 1.0);
    o.color0 = inp.color0;
    o.uv1 = inp.tcoord.yz * tri_params.x;
    o.uv2 = inp.tcoord.xz * tri_params.x;
    o.normal = inp.normal;
    return o;
}
)";

static const char* tri_depth_wall_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 z_range;
    float2 tri_params;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float4 color0 [[attribute(1)]];
    float3 tcoord [[attribute(2)]];
    float2 normal [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float4 color0;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 clip = float2(
        (in.pos.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (in.pos.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, (params.z_range.x - in.pos.z) * params.z_range.y, 1.0);
    o.color0 = in.color0;
    o.uv1 = in.tcoord.yz * params.tri_params.x;
    o.uv2 = in.tcoord.xz * params.tri_params.x;
    o.normal = in.normal;
    return o;
}
)";

static const char* tri_wall_fs_src_glsl = R"(
#version 330
in vec4 v_color;
in vec2 v_uv1;
in vec2 v_uv2;
in vec2 v_normal;
out vec4 frag_color;
uniform sampler2D tex;
void main() {
    vec2 w = abs(v_normal);
    float wsum = max(w.x + w.y, 1e-5);
    vec4 c = (w.x * texture(tex, v_uv1) + w.y * texture(tex, v_uv2)) / wsum;
    frag_color = c * v_color;
}
)";

static const char* tri_wall_fs_src_hlsl = R"(
Texture2D tex0: register(t0);
SamplerState smp0: register(s0);
struct PSIn { float4 pos: SV_Position; float4 color0: TEXCOORD0; float2 uv1: TEXCOORD1; float2 uv2: TEXCOORD2; float2 normal: TEXCOORD3; };
float4 main(PSIn inp): SV_Target0 {
    float2 w = abs(inp.normal);
    float wsum = max(w.x + w.y, 1e-5);
    float4 c = (w.x * tex0.Sample(smp0, inp.uv1) + w.y * tex0.Sample(smp0, inp.uv2)) / wsum;
    return c * inp.color0;
}
)";

static const char* tri_wall_fs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float4 color0;
    float2 uv1;
    float2 uv2;
    float2 normal;
};

fragment float4 _main(PSIn in [[stage_in]],
                      texture2d<float> tex0 [[texture(0)]],
                      sampler smp0 [[sampler(0)]]) {
    float2 w = abs(in.normal);
    float wsum = max(w.x + w.y, 1e-5);
    float4 c = (w.x * tex0.sample(smp0, in.uv1) + w.y * tex0.sample(smp0, in.uv2)) / wsum;
    return c * in.color0;
}
)";

namespace {

// Vertex capacities of the dynamic buffers (one sg_update_buffer per buffer
// per frame, so a frame must fit into a single upload).
constexpr std::size_t kRaisedVbufVertices = 6 * 65536;
constexpr std::size_t kWallVbufVertices = 8 * 65536;

// World-space UV tiling of the raised-top ground texture: one texture repeat
// per 256 world pixels (2 cells), continuous across cells (same convention as
// PolygonalGeneratedLandscape's production preview ground UVs).
constexpr float kTopUvPerWorldPx = 1.0f / 256.0f;

// Wall triplanar texture tiling: one rock texture repeat per 256 world px.
constexpr float kWallTexScale = 1.0f / 256.0f;

// Same packing as DiamondIsometry::zOffset.
// nodeKey and tileTypeFromAtlasIndex live in the shared internal header
// src/atlas_tile_types.h (also used by CliffRenderer).

} // namespace

void LandscapeRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();
    ensureGroundPipeline();
    ensureWallPipeline();
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        // A real depth attachment: the raised pass can resolve its internal
        // overlaps on the GPU instead of the CPU painter sort.
        ensureDepthPipelines();
    }
    ensureTriWallPipelines();
    ensureTexturePipeline();

    // Dynamic vertex buffer for many quads (6 vertices per tile) — the flat
    // ground pass streams GroundVertex now.
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * 65536 * (int)sizeof(GroundVertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "landscape-verts";
    vbuf = sg_make_buffer(&buf_desc);

    bind.vertex_buffers[0] = vbuf;

    // Texture-2D pass: own stream (12 fan verts per cell) — the ground pass
    // updates vbuf in the same frame, and sokol allows one update per buffer.
    sg_buffer_desc texture_buf_desc = {};
    texture_buf_desc.size = 12 * 16384 * (int)sizeof(TextureVertex);
    texture_buf_desc.usage.dynamic_update = true;
    texture_buf_desc.label = "landscape-texture-verts";
    textureVbuf = sg_make_buffer(&texture_buf_desc);

    textureBind.vertex_buffers[0] = textureVbuf;

    // Placeholder tiling array (one 1x1 white layer) keeps the texture-pass
    // bindings valid until the first texture asset rebuilds it.
    const std::uint32_t white = 0xFFFFFFFFu;
    sg_image_desc array_desc = {};
    array_desc.type = SG_IMAGETYPE_ARRAY;
    array_desc.width = 1;
    array_desc.height = 1;
    array_desc.num_slices = 1;
    array_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    array_desc.data.mip_levels[0].ptr = &white;
    array_desc.data.mip_levels[0].size = sizeof(white);
    array_desc.label = "landscape-texture-array-placeholder";
    textureArrayImage = sg_make_image(&array_desc);
    sg_view_desc array_view_desc = {};
    array_view_desc.texture.image = textureArrayImage;
    textureArrayView = sg_make_view(&array_view_desc);

    sg_sampler_desc texture_smp_desc = {};
    texture_smp_desc.min_filter = SG_FILTER_LINEAR;
    texture_smp_desc.mag_filter = SG_FILTER_LINEAR;
    texture_smp_desc.wrap_u = SG_WRAP_REPEAT;
    texture_smp_desc.wrap_v = SG_WRAP_REPEAT;
    texture_smp_desc.label = "landscape-texture-array-smp";
    textureArraySampler = sg_make_sampler(&texture_smp_desc);

    // Separate buffers for the raised pass: sokol allows only one
    // sg_update_buffer per buffer per frame, and the flat pass may run in the
    // same frame before renderRaised.
    sg_buffer_desc raised_buf_desc = {};
    raised_buf_desc.size = (int)(kRaisedVbufVertices * sizeof(Vertex));
    raised_buf_desc.usage.dynamic_update = true;
    raised_buf_desc.label = "landscape-raised-verts";
    raisedVbuf = sg_make_buffer(&raised_buf_desc);

    raisedBind.vertex_buffers[0] = raisedVbuf;

    sg_buffer_desc wall_buf_desc = {};
    wall_buf_desc.size = (int)(kWallVbufVertices * sizeof(WallVertex));
    wall_buf_desc.usage.dynamic_update = true;
    wall_buf_desc.label = "landscape-wall-verts";
    wallVbuf = sg_make_buffer(&wall_buf_desc);

    wallBind.vertex_buffers[0] = wallVbuf;

    // Triplanar wall streams (painter and depth variants share this byte
    // buffer — the modes are mutually exclusive per renderer instance).
    sg_buffer_desc tri_wall_buf_desc = {};
    tri_wall_buf_desc.size = (int)(kWallVbufVertices * sizeof(WallVertex));
    tri_wall_buf_desc.usage.dynamic_update = true;
    tri_wall_buf_desc.label = "landscape-tri-wall-verts";
    triWallVbuf = sg_make_buffer(&tri_wall_buf_desc);
}

void LandscapeRenderer::shutdown() {
    for (auto& [_, a] : atlases) {
        a.atlas.destroy();
    }
    atlases.clear();
    for (auto& [_, a] : raisedAtlases) {
        a.gpu.atlas.destroy();
        a.topTex.destroy();
    }
    raisedAtlases.clear();

    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    if (raisedVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(raisedVbuf);
        raisedVbuf.id = SG_INVALID_ID;
    }
    if (wallVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(wallVbuf);
        wallVbuf.id = SG_INVALID_ID;
    }
    if (triWallVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(triWallVbuf);
        triWallVbuf.id = SG_INVALID_ID;
    }
    if (textureVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(textureVbuf);
        textureVbuf.id = SG_INVALID_ID;
    }
    if (textureArrayView.id != SG_INVALID_ID) {
        sg_destroy_view(textureArrayView);
        textureArrayView.id = SG_INVALID_ID;
    }
    if (textureArrayImage.id != SG_INVALID_ID) {
        sg_destroy_image(textureArrayImage);
        textureArrayImage.id = SG_INVALID_ID;
    }
    if (textureArraySampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(textureArraySampler);
        textureArraySampler.id = SG_INVALID_ID;
    }
    textureAssets.clear();
    destroyPipeline();
    destroyGroundPipeline();
    destroyTexturePipeline();
    destroyWallPipeline();
    destroyDepthPipelines();
    destroyTriWallPipelines();
}

void LandscapeRenderer::ensurePipeline() {
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
        shd_desc.attrs[2].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[2].hlsl_sem_index = 2;
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

    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    shd_desc.views[0].texture.msl_texture_n = 0;
    shd_desc.views[0].texture.wgsl_group1_binding_n = 0;
    shd_desc.views[0].texture.spirv_set1_binding_n = 0;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;
    shd_desc.samplers[0].wgsl_group1_binding_n = 1;
    shd_desc.samplers[0].spirv_set1_binding_n = 1;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tex";

    sg_shader shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2; // uv
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // The depth format must match the pass we render into (sokol_app swapchain
    // has depth-stencil; a Qt FBO wrapper has none). We don't depth-test either way.
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "landscape-pipeline";
    pip = sg_make_pipeline(&pip_desc);
}

void LandscapeRenderer::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
}

void LandscapeRenderer::ensureGroundPipeline() {
    if (groundPip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // Same backend split as the other pipelines.
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = ground_vs_src_hlsl;
        shd_desc.fragment_func.source = ground_fs_src_hlsl;
        for (int i = 0; i < 3; ++i) {
            shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd_desc.attrs[i].hlsl_sem_index = i;
        }
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = ground_vs_src_msl;
        shd_desc.fragment_func.source = ground_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = ground_vs_src_glsl;
        shd_desc.fragment_func.source = ground_fs_src_glsl;
    }

    // VS: view size (slot 0).
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    // FS: the FULL scene stitch block (slot 1) — the ground pass is the one
    // that samples the AO field, so unlike the cliff pass it gets ao_rect too.
    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(SceneStitchParams);
    shd_desc.uniform_blocks[1].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[1].msl_buffer_n = 1;
    shd_desc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shd_desc.uniform_blocks[1].spirv_set0_binding_n = 1;
    const char* stitchNames[4] = {"sun_dir", "stitch0", "stitch1", "ao_rect"};
    for (int i = 0; i < 4; ++i) {
        shd_desc.uniform_blocks[1].glsl_uniforms[i].glsl_name = stitchNames[i];
        shd_desc.uniform_blocks[1].glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }

    // Atlas texture (slot 0) + contact-AO field (slot 1, R8 — filterable).
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    shd_desc.views[0].texture.msl_texture_n = 0;
    shd_desc.views[0].texture.wgsl_group1_binding_n = 0;
    shd_desc.views[0].texture.spirv_set1_binding_n = 0;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;
    shd_desc.samplers[0].wgsl_group1_binding_n = 1;
    shd_desc.samplers[0].spirv_set1_binding_n = 1;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tex";

    shd_desc.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[1].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[1].texture.hlsl_register_t_n = 1;
    shd_desc.views[1].texture.msl_texture_n = 1;
    shd_desc.views[1].texture.wgsl_group1_binding_n = 2;
    shd_desc.views[1].texture.spirv_set1_binding_n = 2;
    shd_desc.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[1].hlsl_register_s_n = 1;
    shd_desc.samplers[1].msl_sampler_n = 1;
    shd_desc.samplers[1].wgsl_group1_binding_n = 3;
    shd_desc.samplers[1].spirv_set1_binding_n = 3;
    shd_desc.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[1].view_slot = 1;
    shd_desc.texture_sampler_pairs[1].sampler_slot = 1;
    shd_desc.texture_sampler_pairs[1].glsl_name = "ao_tex";

    groundShd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = groundShd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2; // uv
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2; // world
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        // The ground joins the z-buffer at one constant depth behind
        // everything (see the vertex shader): tiles tie under LESS_EQUAL and
        // keep their painter order, the 3D passes always overdraw.
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
    } else {
        pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        pip_desc.depth.write_enabled = false;
    }
    pip_desc.label = "landscape-ground-pipeline";
    groundPip = sg_make_pipeline(&pip_desc);
}

void LandscapeRenderer::destroyGroundPipeline() {
    if (groundPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(groundPip);
        groundPip.id = SG_INVALID_ID;
    }
    if (groundShd.id != SG_INVALID_ID) {
        sg_destroy_shader(groundShd);
        groundShd.id = SG_INVALID_ID;
    }
}

void LandscapeRenderer::ensureTexturePipeline() {
    if (texturePip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = texture_vs_src_hlsl;
        shd_desc.fragment_func.source = texture_fs_src_hlsl;
        for (int i = 0; i < 6; ++i) {
            shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd_desc.attrs[i].hlsl_sem_index = i;
        }
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = texture_vs_src_msl;
        shd_desc.fragment_func.source = texture_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = texture_vs_src_glsl;
        shd_desc.fragment_func.source = texture_fs_src_glsl;
    }

    // VS: view size (slot 0).
    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    // FS slot 1: the FULL scene stitch block (sun/tone + AO rect), same
    // contract as the flat ground pass.
    shd_desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[1].size = sizeof(SceneStitchParams);
    shd_desc.uniform_blocks[1].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[1].msl_buffer_n = 1;
    shd_desc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shd_desc.uniform_blocks[1].spirv_set0_binding_n = 1;
    const char* stitchNames[4] = {"sun_dir", "stitch0", "stitch1", "ao_rect"};
    for (int i = 0; i < 4; ++i) {
        shd_desc.uniform_blocks[1].glsl_uniforms[i].glsl_name = stitchNames[i];
        shd_desc.uniform_blocks[1].glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }

    // FS slot 2: blend params (TextureBlendParams, one vec4).
    shd_desc.uniform_blocks[2].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.uniform_blocks[2].size = sizeof(TextureBlendParams);
    shd_desc.uniform_blocks[2].hlsl_register_b_n = 1;
    shd_desc.uniform_blocks[2].msl_buffer_n = 2;
    shd_desc.uniform_blocks[2].wgsl_group0_binding_n = 2;
    shd_desc.uniform_blocks[2].spirv_set0_binding_n = 2;
    shd_desc.uniform_blocks[2].glsl_uniforms[0].glsl_name = "blend_params";
    shd_desc.uniform_blocks[2].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;

    // Tiling texture array (slot 0) + contact-AO field (slot 1, R8).
    shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[0].texture.image_type = SG_IMAGETYPE_ARRAY;
    shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[0].texture.hlsl_register_t_n = 0;
    shd_desc.views[0].texture.msl_texture_n = 0;
    shd_desc.views[0].texture.wgsl_group1_binding_n = 0;
    shd_desc.views[0].texture.spirv_set1_binding_n = 0;
    shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[0].hlsl_register_s_n = 0;
    shd_desc.samplers[0].msl_sampler_n = 0;
    shd_desc.samplers[0].wgsl_group1_binding_n = 1;
    shd_desc.samplers[0].spirv_set1_binding_n = 1;
    shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[0].view_slot = 0;
    shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
    shd_desc.texture_sampler_pairs[0].glsl_name = "tiling_array";

    shd_desc.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.views[1].texture.image_type = SG_IMAGETYPE_2D;
    shd_desc.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd_desc.views[1].texture.hlsl_register_t_n = 1;
    shd_desc.views[1].texture.msl_texture_n = 1;
    shd_desc.views[1].texture.wgsl_group1_binding_n = 2;
    shd_desc.views[1].texture.spirv_set1_binding_n = 2;
    shd_desc.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd_desc.samplers[1].hlsl_register_s_n = 1;
    shd_desc.samplers[1].msl_sampler_n = 1;
    shd_desc.samplers[1].wgsl_group1_binding_n = 3;
    shd_desc.samplers[1].spirv_set1_binding_n = 3;
    shd_desc.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd_desc.texture_sampler_pairs[1].view_slot = 1;
    shd_desc.texture_sampler_pairs[1].sampler_slot = 1;
    shd_desc.texture_sampler_pairs[1].glsl_name = "ao_tex";

    textureShd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = textureShd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2; // world
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4; // layers
    pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4; // weights
    pip_desc.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT4; // tiling
    pip_desc.layout.attrs[5].format = SG_VERTEXFORMAT_FLOAT;  // fill
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        // Ground level, same constant depth as the flat ground pass: texture
        // cells tie with it under LESS_EQUAL and win by painter order (the
        // texture pass draws later), the 3D passes stay on top.
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
    } else {
        pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        pip_desc.depth.write_enabled = false;
    }
    pip_desc.label = "landscape-texture-pipeline";
    texturePip = sg_make_pipeline(&pip_desc);
}

void LandscapeRenderer::destroyTexturePipeline() {
    if (texturePip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(texturePip);
        texturePip.id = SG_INVALID_ID;
    }
    if (textureShd.id != SG_INVALID_ID) {
        sg_destroy_shader(textureShd);
        textureShd.id = SG_INVALID_ID;
    }
}

void LandscapeRenderer::ensureWallPipeline() {
    if (wallPip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // Same backend split as the textured pipeline.
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = wall_vs_src_hlsl;
        shd_desc.fragment_func.source = wall_fs_src_hlsl;
        shd_desc.attrs[0].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[0].hlsl_sem_index = 0;
        shd_desc.attrs[1].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[1].hlsl_sem_index = 1;
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = wall_vs_src_msl;
        shd_desc.fragment_func.source = wall_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = wall_vs_src_glsl;
        shd_desc.fragment_func.source = wall_fs_src_glsl;
    }

    shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd_desc.uniform_blocks[0].size = sizeof(float) * 2;
    shd_desc.uniform_blocks[0].hlsl_register_b_n = 0;
    shd_desc.uniform_blocks[0].msl_buffer_n = 0;
    shd_desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd_desc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd_desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd_desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;

    wallShd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = wallShd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // Same depth contract as the textured pipeline (match the pass, no depth test).
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.label = "landscape-wall-pipeline";
    wallPip = sg_make_pipeline(&pip_desc);
}

void LandscapeRenderer::destroyWallPipeline() {
    if (wallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(wallPip);
        wallPip.id = SG_INVALID_ID;
    }
    if (wallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(wallShd);
        wallShd.id = SG_INVALID_ID;
    }
}

void LandscapeRenderer::ensureDepthPipelines() {
    if (depthPip.id != SG_INVALID_ID || depthWallPip.id != SG_INVALID_ID) return;

    // Same backend split as the painter pipelines. The uniform block grows to
    // {view_size, z_range} (16 bytes) for the depth normalization.
    const auto fillDepthShaderDesc = [](sg_shader_desc& desc, bool wall) {
        if (sg_query_backend() == SG_BACKEND_D3D11) {
            desc.vertex_func.source = wall ? depth_wall_vs_src_hlsl : depth_tex_vs_src_hlsl;
            desc.fragment_func.source = wall ? wall_fs_src_hlsl : fs_src_hlsl;
            desc.attrs[0].hlsl_sem_name = "TEXCOORD";
            desc.attrs[0].hlsl_sem_index = 0;
            desc.attrs[1].hlsl_sem_name = "TEXCOORD";
            desc.attrs[1].hlsl_sem_index = 1;
            if (!wall) {
                desc.attrs[2].hlsl_sem_name = "TEXCOORD";
                desc.attrs[2].hlsl_sem_index = 2;
            }
        } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
            desc.vertex_func.source = wall ? depth_wall_vs_src_msl : depth_tex_vs_src_msl;
            desc.fragment_func.source = wall ? wall_fs_src_msl : fs_src_msl;
        } else {
            desc.vertex_func.source = wall ? depth_wall_vs_src_glsl : depth_tex_vs_src_glsl;
            desc.fragment_func.source = wall ? wall_fs_src_glsl : fs_src_glsl;
        }

        desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
        desc.uniform_blocks[0].size = sizeof(float) * 4;
        desc.uniform_blocks[0].hlsl_register_b_n = 0;
        desc.uniform_blocks[0].msl_buffer_n = 0;
        desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
        desc.uniform_blocks[0].spirv_set0_binding_n = 0;
        desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
        desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
        desc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "z_range";
        desc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    };

    {
        sg_shader_desc shd_desc = {};
        fillDepthShaderDesc(shd_desc, false);

        shd_desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd_desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd_desc.views[0].texture.hlsl_register_t_n = 0;
        shd_desc.views[0].texture.msl_texture_n = 0;
        shd_desc.views[0].texture.wgsl_group1_binding_n = 0;
        shd_desc.views[0].texture.spirv_set1_binding_n = 0;
        shd_desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd_desc.samplers[0].hlsl_register_s_n = 0;
        shd_desc.samplers[0].msl_sampler_n = 0;
        shd_desc.samplers[0].wgsl_group1_binding_n = 1;
        shd_desc.samplers[0].spirv_set1_binding_n = 1;
        shd_desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.texture_sampler_pairs[0].view_slot = 0;
        shd_desc.texture_sampler_pairs[0].sampler_slot = 0;
        shd_desc.texture_sampler_pairs[0].glsl_name = "tex";

        depthTexShd = sg_make_shader(&shd_desc);

        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = depthTexShd;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos + ground-y z
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2; // uv
        pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4; // color
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip_desc.depth.pixel_format = depthFormat;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
        pip_desc.label = "landscape-depth-pipeline";
        depthPip = sg_make_pipeline(&pip_desc);
    }

    {
        sg_shader_desc shd_desc = {};
        fillDepthShaderDesc(shd_desc, true);
        depthWallShd = sg_make_shader(&shd_desc);

        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = depthWallShd;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos + ground-y z
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip_desc.depth.pixel_format = depthFormat;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
        pip_desc.label = "landscape-depth-wall-pipeline";
        depthWallPip = sg_make_pipeline(&pip_desc);
    }
}

void LandscapeRenderer::destroyDepthPipelines() {
    if (depthPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(depthPip);
        depthPip.id = SG_INVALID_ID;
    }
    if (depthWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(depthWallPip);
        depthWallPip.id = SG_INVALID_ID;
    }
    if (depthTexShd.id != SG_INVALID_ID) {
        sg_destroy_shader(depthTexShd);
        depthTexShd.id = SG_INVALID_ID;
    }
    if (depthWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(depthWallShd);
        depthWallShd.id = SG_INVALID_ID;
    }
}

void LandscapeRenderer::ensureTriWallPipelines() {
    if (triWallPip.id != SG_INVALID_ID) return;

    // Same backend split as the other pipelines. The painter variant uses a
    // {view_size, tri_params} block (16 bytes), the depth variant grows to
    // {view_size, z_range, tri_params} (24 bytes).
    const auto fillTriShaderDesc = [](sg_shader_desc& desc, bool depth) {
        if (sg_query_backend() == SG_BACKEND_D3D11) {
            desc.vertex_func.source = depth ? tri_depth_wall_vs_src_hlsl : tri_wall_vs_src_hlsl;
            desc.fragment_func.source = tri_wall_fs_src_hlsl;
            for (int i = 0; i < 4; ++i) {
                desc.attrs[i].hlsl_sem_name = "TEXCOORD";
                desc.attrs[i].hlsl_sem_index = i;
            }
        } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
            desc.vertex_func.source = depth ? tri_depth_wall_vs_src_msl : tri_wall_vs_src_msl;
            desc.fragment_func.source = tri_wall_fs_src_msl;
        } else {
            desc.vertex_func.source = depth ? tri_depth_wall_vs_src_glsl : tri_wall_vs_src_glsl;
            desc.fragment_func.source = tri_wall_fs_src_glsl;
        }

        desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
        desc.uniform_blocks[0].size = depth ? sizeof(float) * 6 : sizeof(float) * 4;
        desc.uniform_blocks[0].hlsl_register_b_n = 0;
        desc.uniform_blocks[0].msl_buffer_n = 0;
        desc.uniform_blocks[0].wgsl_group0_binding_n = 0;
        desc.uniform_blocks[0].spirv_set0_binding_n = 0;
        desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
        desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
        int slot = 1;
        if (depth) {
            desc.uniform_blocks[0].glsl_uniforms[slot].glsl_name = "z_range";
            desc.uniform_blocks[0].glsl_uniforms[slot].type = SG_UNIFORMTYPE_FLOAT2;
            ++slot;
        }
        desc.uniform_blocks[0].glsl_uniforms[slot].glsl_name = "tri_params";
        desc.uniform_blocks[0].glsl_uniforms[slot].type = SG_UNIFORMTYPE_FLOAT2;

        desc.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        desc.views[0].texture.image_type = SG_IMAGETYPE_2D;
        desc.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        desc.views[0].texture.hlsl_register_t_n = 0;
        desc.views[0].texture.msl_texture_n = 0;
        desc.views[0].texture.wgsl_group1_binding_n = 0;
        desc.views[0].texture.spirv_set1_binding_n = 0;
        desc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        desc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        desc.samplers[0].hlsl_register_s_n = 0;
        desc.samplers[0].msl_sampler_n = 0;
        desc.samplers[0].wgsl_group1_binding_n = 1;
        desc.samplers[0].spirv_set1_binding_n = 1;
        desc.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        desc.texture_sampler_pairs[0].view_slot = 0;
        desc.texture_sampler_pairs[0].sampler_slot = 0;
        desc.texture_sampler_pairs[0].glsl_name = "tex";
    };

    {
        sg_shader_desc shd_desc = {};
        fillTriShaderDesc(shd_desc, false);
        triWallShd = sg_make_shader(&shd_desc);

        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = triWallShd;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; // pos
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
        pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3; // tcoord
        pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2; // normal
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip_desc.depth.pixel_format = depthFormat;
        pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        pip_desc.depth.write_enabled = false;
        pip_desc.label = "landscape-tri-wall-pipeline";
        triWallPip = sg_make_pipeline(&pip_desc);
    }

    if (depthFormat != SG_PIXELFORMAT_NONE && triDepthWallPip.id == SG_INVALID_ID) {
        sg_shader_desc shd_desc = {};
        fillTriShaderDesc(shd_desc, true);
        triDepthWallShd = sg_make_shader(&shd_desc);

        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = triDepthWallShd;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos + ground-y z
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
        pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT3; // tcoord
        pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2; // normal
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip_desc.depth.pixel_format = depthFormat;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.write_enabled = true;
        pip_desc.label = "landscape-tri-depth-wall-pipeline";
        triDepthWallPip = sg_make_pipeline(&pip_desc);
    }
}

void LandscapeRenderer::destroyTriWallPipelines() {
    if (triWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(triWallPip);
        triWallPip.id = SG_INVALID_ID;
    }
    if (triDepthWallPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(triDepthWallPip);
        triDepthWallPip.id = SG_INVALID_ID;
    }
    if (triWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(triWallShd);
        triWallShd.id = SG_INVALID_ID;
    }
    if (triDepthWallShd.id != SG_INVALID_ID) {
        sg_destroy_shader(triDepthWallShd);
        triDepthWallShd.id = SG_INVALID_ID;
    }
}

LandscapeRenderer::AtlasGpu LandscapeRenderer::createAtlasGpu(const std::filesystem::path& atlasPath, int cols, int rows) {
    AtlasGpu gpu;
    gpu.atlas.createFromFile(atlasPath, cols, rows);

    // Match editor behaviour: scale tile to cellWidth
    const float cellWidth = 128.0f;
    const float tileW = (float)gpu.atlas.tileWidth();
    const float tileH = (float)gpu.atlas.tileHeight();
    gpu.scale = (tileW > 0.0f) ? (cellWidth / tileW) : 1.0f;
    gpu.tileSize = glm::vec2(tileW * gpu.scale, tileH * gpu.scale);
    return gpu;
}

void LandscapeRenderer::ensureAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows) {
    auto it = atlases.find(assetUuid);
    if (it != atlases.end() && it->second.atlas.valid()) {
        return;
    }

    atlases[assetUuid] = createAtlasGpu(atlasPath, cols, rows);
}

void LandscapeRenderer::ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath, const std::filesystem::path& wallTexturePath) {
    auto it = raisedAtlases.find(assetUuid);
    if (it != raisedAtlases.end()) {
        it->second.params = params; // cheap: presentation can be tweaked at runtime
        if (!topTexturePath.empty() && !it->second.topTex.valid()) {
            it->second.topTex.createFromFile(topTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
        }
        if (!wallTexturePath.empty() && !it->second.wallTex.valid()) {
            it->second.wallTex.createFromFile(wallTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
        }
        if (it->second.gpu.atlas.valid()) {
            return;
        }
    }

    RaisedAtlasGpu raised;
    raised.gpu = createAtlasGpu(atlasPath, cols, rows);
    raised.params = params;
    if (!topTexturePath.empty()) {
        raised.topTex.createFromFile(topTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
    }
    if (!wallTexturePath.empty()) {
        raised.wallTex.createFromFile(wallTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
    }
    raisedAtlases[assetUuid] = std::move(raised);
}

void LandscapeRenderer::appendAtlasQuad(
    std::vector<Vertex>& out,
    const AtlasGpu& atlas,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    std::size_t tileIndex,
    float yOffset) const {

    // Field-space quad centered on the cell; the camera transform (zoom scales
    // positions and sizes together) happens at emission into the frame buffer.
    const glm::vec2 center = iso.mapToField(cell) + glm::vec2(0.0f, yOffset);
    const glm::vec2 tl = center - atlas.tileSize * 0.5f;
    const glm::vec2 br = center + atlas.tileSize * 0.5f;

    const TileUV uv = atlas.atlas.tileUv(tileIndex);

    const float a = 1.0f;

    // 2 triangles (TL, TR, BL) (BL, TR, BR)
    out.push_back({{tl.x, tl.y}, {uv.uv0.x, uv.uv0.y}, {1.0f, 1.0f, 1.0f, a}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {1.0f, 1.0f, 1.0f, a}});
    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {1.0f, 1.0f, 1.0f, a}});

    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {1.0f, 1.0f, 1.0f, a}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {1.0f, 1.0f, 1.0f, a}});
    out.push_back({{br.x, br.y}, {uv.uv1.x, uv.uv1.y}, {1.0f, 1.0f, 1.0f, a}});
}

void LandscapeRenderer::appendGroundQuad(
    std::vector<GroundVertex>& out,
    const AtlasGpu& atlas,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    std::size_t tileIndex) const {

    // Same field-space quad as the legacy path; the camera transform happens
    // at emission into the frame buffer.
    const glm::vec2 center = iso.mapToField(cell);
    const glm::vec2 tl = center - atlas.tileSize * 0.5f;
    const glm::vec2 br = center + atlas.tileSize * 0.5f;

    const TileUV uv = atlas.atlas.tileUv(tileIndex);

    // World (map-cell) coords of the quad corners: the inverse of the cliff
    // stream's placement (nodeToField — no +halfH on y), so the ground shades
    // in the very frame the highground mesh lives in.
    const float halfW = iso.dims.cellWidth * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    const auto worldFromField = [halfW, halfH](const glm::vec2& f) {
        const float diff = (f.x - halfW) / halfW; // mapX - mapZ
        const float sum = f.y / halfH;            // mapX + mapZ
        return glm::vec2{0.5f * (sum + diff), 0.5f * (sum - diff)};
    };
    const glm::vec2 wTL = worldFromField(tl);
    const glm::vec2 wTR = worldFromField({br.x, tl.y});
    const glm::vec2 wBL = worldFromField({tl.x, br.y});
    const glm::vec2 wBR = worldFromField(br);

    // 2 triangles (TL, TR, BL) (BL, TR, BR)
    out.push_back({{tl.x, tl.y}, {uv.uv0.x, uv.uv0.y}, {wTL.x, wTL.y}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {wTR.x, wTR.y}});
    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {wBL.x, wBL.y}});

    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {wBL.x, wBL.y}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {wTR.x, wTR.y}});
    out.push_back({{br.x, br.y}, {uv.uv1.x, uv.uv1.y}, {wBR.x, wBR.y}});
}

void LandscapeRenderer::render(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    const GroundStitchContext& groundStitch) {

    if (tiles.empty()) return;
    if (groundPip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;

    // Group tiles by atlas uuid (bind texture per group)
    std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
    groups.reserve(atlases.size() + 4);
    for (const auto& t : tiles) {
        groups[t.assetUuid].push_back(&t);
    }

    // Build one merged vertex stream: sokol allows only ONE sg_update_buffer
    // per buffer per frame, so all atlas groups go into a single update and
    // are drawn as per-group vertex ranges with their own texture binding.
    scratchGroundVerts.clear();
    scratchDraws.clear();

    for (auto& [uuid, group] : groups) {
        auto it = atlases.find(uuid);
        if (it == atlases.end() || !it->second.atlas.valid()) {
            continue;
        }
        const AtlasGpu& atlas = it->second;

        // Stable-ish draw order
        std::sort(group.begin(), group.end(), [&](const LandscapeTile* a, const LandscapeTile* b) {
            const std::uint64_t za = iso.zOffset(a->cell);
            const std::uint64_t zb = iso.zOffset(b->cell);
            return za < zb;
        });

        const int baseVertex = (int)scratchGroundVerts.size();

        for (const LandscapeTile* t : group) {
            appendGroundQuad(scratchGroundVerts, atlas, iso, t->cell, t->tileIndex);
        }

        scratchDraws.push_back({&atlas.atlas, baseVertex, (int)scratchGroundVerts.size() - baseVertex});
    }

    if (scratchGroundVerts.empty()) return;

    // Field -> screen (camera zoom scales positions and quad sizes together);
    // the world (map-cell) attribute is screen-independent and stays.
    for (GroundVertex& v : scratchGroundVerts) {
        const glm::vec2 s = camera.worldToScreen({v.pos[0], v.pos[1]});
        v.pos[0] = s.x;
        v.pos[1] = s.y;
    }

    sg_range range = { scratchGroundVerts.data(), scratchGroundVerts.size() * sizeof(GroundVertex) };
    sg_update_buffer(vbuf, &range);

    sg_apply_pipeline(groundPip);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &uniform_range);
    // Scene stitch block (sun/tone + AO rect), shared with the cliff pass.
    sg_range stitch_range = { &groundStitch.params, sizeof(SceneStitchParams) };
    sg_apply_uniforms(1, &stitch_range);

    // The AO field binding is constant across the atlas groups.
    bind.views[1] = groundStitch.aoView;
    bind.samplers[1] = groundStitch.aoSampler;

    for (const DrawGroup& draw : scratchDraws) {
        bind.views[0] = draw.texture->sgView();
        bind.samplers[0] = draw.texture->sgSampler();
        sg_apply_bindings(&bind);

        sg_draw(draw.baseVertex, draw.vertexCount, 1);
    }
}

void LandscapeRenderer::ensureTextureAsset(
    const std::string& assetUuid,
    const std::filesystem::path& texturePath,
    float tilingRepeats) {

    auto it = textureAssets.find(assetUuid);
    if (it != textureAssets.end()) {
        // Live retuning: tiling rides in the vertex stream (no array rebuild);
        // a texture file swap has to land in the array though.
        it->second.tilingRepeats = tilingRepeats;
        if (it->second.path != texturePath) {
            it->second.path = texturePath;
            textureArrayDirty = true;
        }
        return;
    }
    TextureAssetEntry entry;
    entry.path = texturePath;
    entry.tilingRepeats = tilingRepeats;
    entry.slice = static_cast<int>(textureAssets.size());
    textureAssets.emplace(assetUuid, entry);
    textureArrayDirty = true;
}

void LandscapeRenderer::rebuildTextureArray() {
    textureArrayDirty = false;

    // All array slices share one size (the array constraint): the largest
    // source dimension, clamped to a sane range for a paint texture.
    struct Slice {
        std::string uuid;
        ImageRGBA8 image;
    };
    std::vector<Slice> slices(textureAssets.size());
    int maxDim = 64;
    for (const auto& [uuid, entry] : textureAssets) {
        if (entry.slice < 0 || entry.slice >= static_cast<int>(slices.size())) {
            continue;
        }
        Slice& slot = slices[static_cast<std::size_t>(entry.slice)];
        slot.uuid = uuid;
        slot.image = loadImageRGBA8(entry.path);
        if (slot.image.width <= 0 || slot.image.height <= 0) {
            spdlog::error("LandscapeRenderer: failed to load tiling texture '{}' — white slice", entry.path.string());
            slot.image.width = 1;
            slot.image.height = 1;
            slot.image.pixels = {255, 255, 255, 255};
        }
        maxDim = std::max(maxDim, std::max(slot.image.width, slot.image.height));
    }
    if (slices.empty()) {
        return;
    }

    const int size = std::clamp(maxDim, 64, 1024);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size) * size * 4 * slices.size());
    for (std::size_t i = 0; i < slices.size(); ++i) {
        std::uint8_t* dst = data.data() + i * static_cast<std::size_t>(size) * size * 4;
        const ImageRGBA8& src = slices[i].image;
        for (int y = 0; y < size; ++y) {
            // Bilinear resample into the shared slice size.
            const float gy = (static_cast<float>(y) + 0.5f) * src.height / size - 0.5f;
            const int y0 = std::clamp(static_cast<int>(std::floor(gy)), 0, src.height - 1);
            const int y1 = std::min(y0 + 1, src.height - 1);
            const float fy = std::clamp(gy - static_cast<float>(y0), 0.0f, 1.0f);
            for (int x = 0; x < size; ++x) {
                const float gx = (static_cast<float>(x) + 0.5f) * src.width / size - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(gx)), 0, src.width - 1);
                const int x1 = std::min(x0 + 1, src.width - 1);
                const float fx = std::clamp(gx - static_cast<float>(x0), 0.0f, 1.0f);
                for (int c = 0; c < 4; ++c) {
                    const float s00 = static_cast<float>(src.pixels[(static_cast<std::size_t>(y0) * src.width + x0) * 4 + c]);
                    const float s10 = static_cast<float>(src.pixels[(static_cast<std::size_t>(y0) * src.width + x1) * 4 + c]);
                    const float s01 = static_cast<float>(src.pixels[(static_cast<std::size_t>(y1) * src.width + x0) * 4 + c]);
                    const float s11 = static_cast<float>(src.pixels[(static_cast<std::size_t>(y1) * src.width + x1) * 4 + c]);
                    const float v = (s00 * (1.0f - fx) + s10 * fx) * (1.0f - fy) + (s01 * (1.0f - fx) + s11 * fx) * fy;
                    dst[(static_cast<std::size_t>(y) * size + x) * 4 + c] =
                        static_cast<std::uint8_t>(std::clamp(std::lround(v), 0l, 255l));
                }
            }
        }
    }

    sg_image_desc desc = {};
    desc.type = SG_IMAGETYPE_ARRAY;
    desc.width = size;
    desc.height = size;
    desc.num_slices = static_cast<int>(slices.size());
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = data.data();
    desc.data.mip_levels[0].size = data.size();
    desc.label = "landscape-texture-array";
    sg_image image = sg_make_image(&desc);
    if (image.id == SG_INVALID_ID) {
        spdlog::error("LandscapeRenderer: sg_make_image failed (texture array)");
        return;
    }
    sg_view_desc view_desc = {};
    view_desc.texture.image = image;
    sg_view view = sg_make_view(&view_desc);
    if (view.id == SG_INVALID_ID) {
        spdlog::error("LandscapeRenderer: sg_make_view failed (texture array)");
        sg_destroy_image(image);
        return;
    }

    if (textureArrayView.id != SG_INVALID_ID) {
        sg_destroy_view(textureArrayView);
    }
    if (textureArrayImage.id != SG_INVALID_ID) {
        sg_destroy_image(textureArrayImage);
    }
    textureArrayImage = image;
    textureArrayView = view;
    spdlog::info("LandscapeRenderer: texture array {}x{} x{} slices", size, size, slices.size());
}

void LandscapeRenderer::renderTexture(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    const GroundStitchContext& groundStitch,
    const TextureBlendParams& blendParams) {

    if (tiles.empty()) return;
    if (texturePip.id == SG_INVALID_ID || textureVbuf.id == SG_INVALID_ID) return;

    if (textureArrayDirty) {
        rebuildTextureArray();
    }

    // Per-cell blend data: the candidate textures of the cell's on corner
    // nodes (by vote — see texture_blend.h) and the one-hot corner weights.
    const std::vector<TextureBlendCell> cells = buildTextureBlendCells(tiles);

    // Fan emission: center + 4 corner nodes per cell, in painter order.
    scratchTextureVerts.clear();
    std::vector<const TextureBlendCell*> sorted;
    sorted.reserve(cells.size());
    for (const TextureBlendCell& cell : cells) {
        sorted.push_back(&cell);
    }
    std::sort(sorted.begin(), sorted.end(), [&](const TextureBlendCell* a, const TextureBlendCell* b) {
        return iso.zOffset(a->cell) < iso.zOffset(b->cell);
    });

    const float halfW = iso.dims.cellWidth * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    // Field -> world (map-cell) coords, the very frame the flat ground pass
    // shades in (inverse of nodeToField).
    const auto worldFromField = [halfW, halfH](const glm::vec2& f) {
        const float diff = (f.x - halfW) / halfW; // mapX - mapZ
        const float sum = f.y / halfH;            // mapX + mapZ
        return glm::vec2{0.5f * (sum + diff), 0.5f * (sum - diff)};
    };

    const std::size_t maxVerts = 12 * 16384;
    for (const TextureBlendCell* cellPtr : sorted) {
        const TextureBlendCell& cell = *cellPtr;
        // Resolve candidates to array slices/tiling densities; a cell naming
        // an unregistered asset is skipped (the frame source ensures assets).
        float layers[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float tiling[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        bool resolved = true;
        for (int k = 0; k < cell.candidateCount; ++k) {
            const auto it = textureAssets.find(cell.candidateUuids[static_cast<std::size_t>(k)]);
            if (it == textureAssets.end() || it->second.slice < 0) {
                resolved = false;
                break;
            }
            layers[k] = static_cast<float>(it->second.slice);
            tiling[k] = it->second.tilingRepeats;
        }
        if (!resolved) {
            continue;
        }
        for (int k = cell.candidateCount; k < 4; ++k) {
            layers[k] = layers[0];
            tiling[k] = tiling[0];
        }

        const auto corners = iso.cellDiamondCorners(cell.cell); // Left, Up, Right, Down
        const glm::vec2 center = iso.mapToField(cell.cell);
        const glm::vec4 centerWeights =
            (cell.cornerWeights[0] + cell.cornerWeights[1] + cell.cornerWeights[2] + cell.cornerWeights[3]) * 0.25f;
        const float centerFill =
            (cell.cornerFill[0] + cell.cornerFill[1] + cell.cornerFill[2] + cell.cornerFill[3]) * 0.25f;

        const auto makeVert = [&](const glm::vec2& p, const glm::vec4& w, float fill) {
            const glm::vec2 world = worldFromField(p);
            return TextureVertex{
                {p.x, p.y},
                {world.x, world.y},
                {layers[0], layers[1], layers[2], layers[3]},
                {w.x, w.y, w.z, w.w},
                {tiling[0], tiling[1], tiling[2], tiling[3]},
                fill};
        };

        if (scratchTextureVerts.size() + 12 > maxVerts) {
            spdlog::warn("LandscapeRenderer: texture vertex buffer full, dropping cells");
            break;
        }
        for (int i = 0; i < 4; ++i) {
            const int next = (i + 1) % 4;
            scratchTextureVerts.push_back(makeVert(center, centerWeights, centerFill));
            scratchTextureVerts.push_back(makeVert(corners[i], cell.cornerWeights[i], cell.cornerFill[i]));
            scratchTextureVerts.push_back(makeVert(corners[next], cell.cornerWeights[next], cell.cornerFill[next]));
        }
    }

    if (scratchTextureVerts.empty()) return;

    // Field -> screen (camera zoom scales positions); the world (map-cell)
    // attribute is screen-independent and stays.
    for (TextureVertex& v : scratchTextureVerts) {
        const glm::vec2 s = camera.worldToScreen({v.pos[0], v.pos[1]});
        v.pos[0] = s.x;
        v.pos[1] = s.y;
    }

    sg_range range = { scratchTextureVerts.data(), scratchTextureVerts.size() * sizeof(TextureVertex) };
    sg_update_buffer(textureVbuf, &range);

    sg_apply_pipeline(texturePip);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range vs_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &vs_range);
    sg_range stitch_range = { &groundStitch.params, sizeof(SceneStitchParams) };
    sg_apply_uniforms(1, &stitch_range);
    sg_range blend_range = { &blendParams, sizeof(TextureBlendParams) };
    sg_apply_uniforms(2, &blend_range);

    textureBind.views[0] = textureArrayView;
    textureBind.samplers[0] = textureArraySampler;
    textureBind.views[1] = groundStitch.aoView;
    textureBind.samplers[1] = groundStitch.aoSampler;
    sg_apply_bindings(&textureBind);

    sg_draw(0, static_cast<int>(scratchTextureVerts.size()), 1);
}

void LandscapeRenderer::renderRaised(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight) {

    if (tiles.empty()) return;
    if (pip.id == SG_INVALID_ID || wallPip.id == SG_INVALID_ID) return;
    if (raisedVbuf.id == SG_INVALID_ID || wallVbuf.id == SG_INVALID_ID) return;

    // Z-buffer mode is available only with a real depth attachment (init).
    const bool useDepth = depthPip.id != SG_INVALID_ID && depthWallPip.id != SG_INVALID_ID;

    // Keep only tiles whose raised atlas is known, sorted back-to-front.
    std::vector<const LandscapeTile*> cells;
    cells.reserve(tiles.size());
    for (const LandscapeTile& t : tiles) {
        auto it = raisedAtlases.find(t.assetUuid);
        if (it == raisedAtlases.end() || !it->second.gpu.atlas.valid()) {
            continue;
        }
        cells.push_back(&t);
    }
    if (cells.empty()) return;
    std::sort(cells.begin(), cells.end(), [&](const LandscapeTile* a, const LandscapeTile* b) {
        return iso.zOffset(a->cell) < iso.zOffset(b->cell);
    });

    // Reconstruct the shared corner-node grid from the tiles themselves: the
    // stored tileIndex encodes the cell's LandscapeTileType, which encodes the
    // 4 corner nodes (corner nodes are shared between adjacent cells). A node
    // is "on" when any tile touching it carries it — for pencil-written data
    // this reproduces the original node grid exactly, so no separate node
    // store is needed.
    std::unordered_set<std::uint64_t> onNodes;
    onNodes.reserve(cells.size() * 4);
    for (const LandscapeTile* t : cells) {
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t->tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
        for (int i = 0; i < 4; ++i) {
            if (mask[i]) {
                onNodes.insert(nodeKey(corners[i]));
            }
        }
    }

    // Per-cell corner-node masks (aligned with `cells`).
    std::vector<std::array<bool, 4>> cellMasks;
    cellMasks.reserve(cells.size());
    for (const LandscapeTile* t : cells) {
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
        cellMasks.push_back(std::array<bool, 4>{
            onNodes.find(nodeKey(corners[0])) != onNodes.end(),
            onNodes.find(nodeKey(corners[1])) != onNodes.end(),
            onNodes.find(nodeKey(corners[2])) != onNodes.end(),
            onNodes.find(nodeKey(corners[3])) != onNodes.end(),
        });
    }
    std::unordered_map<const LandscapeTile*, const std::array<bool, 4>*> maskByTile;
    maskByTile.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        maskByTile.emplace(cells[i], &cellMasks[i]);
    }

    // ------------------------------------------------------------------
    // Geometry generation (highground_core): walls + tops per asset.
    // Assets WITHOUT a topTexture keep the legacy atlas-quad tops; their
    // walls still come from the lib (tops of the generated mesh are dropped).
    // ------------------------------------------------------------------
    std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
    groups.reserve(raisedAtlases.size() + 4);
    for (const LandscapeTile* t : cells) {
        groups[t->assetUuid].push_back(t);
    }

    // Meshes outlive the emission (deque for pointer stability).
    std::deque<highground::Mesh> meshes;

    struct PrimRef {
        float depth;
        bool isWall;                  // wall pipeline when true
        const highground::Mesh* mesh; // lib geometry (null for legacy atlas tops)
        std::uint32_t first;
        std::uint32_t count;
        const TextureAtlas* tex;      // Top prims: topTex or the atlas
        const LandscapeTile* tile;    // legacy atlas tops only
    };
    std::vector<PrimRef> order;

    for (auto& [uuid, group] : groups) {
        const RaisedAtlasGpu& raised = raisedAtlases.find(uuid)->second;

        // Node list of this asset's landmass -> the lib's grid.
        std::vector<glm::ivec2> assetNodes;
        assetNodes.reserve(group.size() * 4);
        for (const LandscapeTile* t : group) {
            const std::array<bool, 4>& mask = *maskByTile[t];
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
            for (int i = 0; i < 4; ++i) {
                if (mask[i]) {
                    assetNodes.push_back(corners[i]);
                }
            }
        }
        highground::Params gp;
        gp.cellWidth = iso.dims.cellWidth;
        gp.cellHeight = iso.dims.cellSize().y;
        gp.height = raised.params.height;
        gp.rockWalls = raised.params.rockWalls;
        gp.amplitude = raised.params.amplitude;
        gp.bevel = raised.params.bevel;
        gp.topUvPerWorldPx = kTopUvPerWorldPx;
        gp.sortPrimitives = !useDepth; // the GPU depth buffer resolves ordering

        meshes.push_back(highground::generate(
            highground::makeGrid(assetNodes.data(), assetNodes.size()), gp));
        const highground::Mesh& mesh = meshes.back();

        const bool texturedTop = raised.topTex.valid();
        for (const highground::Primitive& prim : mesh.primitives) {
            if (prim.material == highground::Material::Wall) {
                // tex != nullptr on wall prims selects the triplanar wall
                // pipeline (the raised atlas has a rock texture).
                const TextureAtlas* wallTex = raised.wallTex.valid() ? &raised.wallTex : nullptr;
                order.push_back({prim.depth, true, &mesh, prim.first, prim.count, wallTex, nullptr});
            } else if (texturedTop) {
                order.push_back({prim.depth, false, &mesh, prim.first, prim.count, &raised.topTex, nullptr});
            }
            // without topTexture the lib's tops are dropped in favor of
            // legacy atlas quads (below)
        }
        if (!texturedTop) {
            // Legacy path: per-cell atlas quads, same as the flat pass,
            // lifted by height (depth parity: center + half tile height).
            for (const LandscapeTile* t : group) {
                const float depth = iso.mapToField(t->cell).y + raised.gpu.tileSize.y * 0.5f;
                order.push_back({depth, false, nullptr, 0, 0, &raised.gpu.atlas, t});
            }
        }
    }

    // Triplanar wall texture space: recover the undeformed map coordinates
    // from the field position (inverse of mapToFieldPx) and scale to world
    // px; the iso cell diagonals have equal length on both map axes. tcoord =
    // {mapX px, mapY px, lift px}; lift = ground y minus the lifted y.
    const float triHalfW = iso.dims.cellWidth * 0.5f;
    const float triHalfH = iso.dims.cellSize().y * 0.5f;
    const float triMapDiagPx = std::sqrt(triHalfW * triHalfW + triHalfH * triHalfH);
    const auto wallTcoord = [&](const highground::Vertex& v, float out[3]) {
        const float sx = (v.pos.x - triHalfW) / triHalfW; // map x - map y
        const float sy = v.pos.y / triHalfH;              // map x + map y
        out[0] = (sy + sx) * 0.5f * triMapDiagPx;
        out[1] = (sy - sx) * 0.5f * triMapDiagPx;
        out[2] = v.groundY - v.pos.y;
    };

    if (useDepth) {
        // ------------------------------------------------------------------
        // Z-buffer emission: no CPU sort — vertices carry the raw ground y as
        // z (normalized in the vertex shader) and internal overlaps resolve
        // via the depth buffer, so batches merge by pipeline/texture only.
        // ------------------------------------------------------------------
        struct DepthBatch {
            bool wall;
            const TextureAtlas* tex;
            int base;
            int count;
        };
        std::vector<DepthBatch> batches;
        scratchDepthWallVerts.clear();
        scratchDepthVerts.clear();
        scratchTriDepthWallVerts.clear();

        // The depth vertex streams reuse the shared buffers (plain byte
        // storage) — capacities in depth-vertex units.
        constexpr std::size_t kDepthWallCap = (kWallVbufVertices * sizeof(WallVertex)) / sizeof(DepthWallVertex);
        constexpr std::size_t kDepthRaisedCap = (kRaisedVbufVertices * sizeof(Vertex)) / sizeof(DepthVertex);
        constexpr std::size_t kTriDepthWallCap = (kWallVbufVertices * sizeof(WallVertex)) / sizeof(TriDepthWallVertex);

        bool emitTruncated = false;
        for (const PrimRef& ref : order) {
            if (ref.isWall) {
                if (ref.tex != nullptr) {
                    // Triplanar wall (rock texture on this raised atlas).
                    if (scratchTriDepthWallVerts.size() + ref.count > kTriDepthWallCap) {
                        emitTruncated = true;
                        continue;
                    }
                    if (batches.empty() || !batches.back().wall || batches.back().tex != ref.tex) {
                        batches.push_back({true, ref.tex, (int)scratchTriDepthWallVerts.size(), 0});
                    }
                    for (std::uint32_t k = 0; k < ref.count; ++k) {
                        const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                        const glm::vec2 p = camera.worldToScreen(v.pos);
                        float tcoord[3];
                        wallTcoord(v, tcoord);
                        scratchTriDepthWallVerts.push_back(
                            {{p.x, p.y, v.groundY},
                             {v.color.r, v.color.g, v.color.b, v.color.a},
                             {tcoord[0], tcoord[1], tcoord[2]},
                             {v.normal.x, v.normal.y}});
                    }
                    batches.back().count += (int)ref.count;
                    continue;
                }
                if (scratchDepthWallVerts.size() + ref.count > kDepthWallCap) {
                    emitTruncated = true;
                    continue;
                }
                if (batches.empty() || !batches.back().wall || batches.back().tex != nullptr) {
                    batches.push_back({true, nullptr, (int)scratchDepthWallVerts.size(), 0});
                }
                for (std::uint32_t k = 0; k < ref.count; ++k) {
                    const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                    const glm::vec2 p = camera.worldToScreen(v.pos);
                    scratchDepthWallVerts.push_back({{p.x, p.y, v.groundY}, {v.color.r, v.color.g, v.color.b, v.color.a}});
                }
                batches.back().count += (int)ref.count;
            } else if (ref.mesh != nullptr) {
                if (scratchDepthVerts.size() + ref.count > kDepthRaisedCap) {
                    emitTruncated = true;
                    continue;
                }
                if (batches.empty() || batches.back().wall || batches.back().tex != ref.tex) {
                    batches.push_back({false, ref.tex, (int)scratchDepthVerts.size(), 0});
                }
                for (std::uint32_t k = 0; k < ref.count; ++k) {
                    const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                    const glm::vec2 p = camera.worldToScreen(v.pos);
                    scratchDepthVerts.push_back({{p.x, p.y, v.groundY}, {v.uv.x, v.uv.y}, {v.color.r, v.color.g, v.color.b, v.color.a}});
                }
                batches.back().count += (int)ref.count;
            } else {
                // Legacy atlas-quad top (6 verts per cell): the quad is lifted
                // by height, so the per-vertex ground y is its un-lifted y.
                if (scratchDepthVerts.size() + 6 > kDepthRaisedCap) {
                    emitTruncated = true;
                    continue;
                }
                if (batches.empty() || batches.back().wall || batches.back().tex != ref.tex) {
                    batches.push_back({false, ref.tex, (int)scratchDepthVerts.size(), 0});
                }
                const RaisedAtlasGpu& raised = raisedAtlases.find(ref.tile->assetUuid)->second;
                std::vector<Vertex> quad;
                quad.reserve(6);
                appendAtlasQuad(quad, raised.gpu, iso, ref.tile->cell, ref.tile->tileIndex, -raised.params.height);
                for (const Vertex& v : quad) {
                    const glm::vec2 p = camera.worldToScreen({v.pos[0], v.pos[1]});
                    scratchDepthVerts.push_back(
                        {{p.x, p.y, v.pos[1] + raised.params.height},
                         {v.uv[0], v.uv[1]},
                         {v.color[0], v.color[1], v.color[2], v.color[3]}});
                }
                batches.back().count += 6;
            }
        }
        if (emitTruncated) {
            spdlog::error("LandscapeRenderer::renderRaised: vertex overflow during z-buffer emission, geometry truncated");
        }

        // Depth range along the iso view ray, anchored at the visible
        // ground-y center with generous margins (see the shader comment).
        const float groundCenterY = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y;
        float vs_params[4] = {
            (float)viewWidth,
            (float)viewHeight,
            groundCenterY + 100000.0f, // far ground-y
            1.0f / 200000.0f,          // 1 / (far - near)
        };
        sg_range uniform_range = { &vs_params, sizeof(vs_params) };

        // Triplanar walls: same block + tex scale (24 bytes total).
        float tri_vs_params[6] = {
            (float)viewWidth,
            (float)viewHeight,
            groundCenterY + 100000.0f,
            1.0f / 200000.0f,
            kWallTexScale,
            0.0f,
        };
        sg_range tri_uniform_range = { &tri_vs_params, sizeof(tri_vs_params) };

        if (!scratchDepthWallVerts.empty()) {
            sg_range range = { scratchDepthWallVerts.data(), scratchDepthWallVerts.size() * sizeof(DepthWallVertex) };
            sg_update_buffer(wallVbuf, &range);
        }
        if (!scratchDepthVerts.empty()) {
            sg_range range = { scratchDepthVerts.data(), scratchDepthVerts.size() * sizeof(DepthVertex) };
            sg_update_buffer(raisedVbuf, &range);
        }
        if (!scratchTriDepthWallVerts.empty()) {
            sg_range range = { scratchTriDepthWallVerts.data(), scratchTriDepthWallVerts.size() * sizeof(TriDepthWallVertex) };
            sg_update_buffer(triWallVbuf, &range);
        }

        for (const DepthBatch& batch : batches) {
            if (batch.wall) {
                if (batch.tex != nullptr) {
                    sg_bindings triBind{};
                    triBind.vertex_buffers[0] = triWallVbuf;
                    triBind.views[0] = batch.tex->sgView();
                    triBind.samplers[0] = batch.tex->sgSampler();
                    sg_apply_pipeline(triDepthWallPip);
                    sg_apply_uniforms(0, &tri_uniform_range);
                    sg_apply_bindings(&triBind);
                } else {
                    sg_apply_pipeline(depthWallPip);
                    sg_apply_uniforms(0, &uniform_range);
                    sg_apply_bindings(&wallBind);
                }
            } else {
                sg_apply_pipeline(depthPip);
                sg_apply_uniforms(0, &uniform_range);
                raisedBind.views[0] = batch.tex->sgView();
                raisedBind.samplers[0] = batch.tex->sgSampler();
                sg_apply_bindings(&raisedBind);
            }
            sg_draw(batch.base, batch.count, 1);
        }
        return;
    }

    // ------------------------------------------------------------------
    // Depth-sorted emission: walls and tops merge into one painter's-algorithm
    // sequence by ground y (walls first on ties), so an arm of a landmass
    // standing in front of another arm's walls (L-shapes, diagonal joins,
    // separate islands) occludes correctly. Batches switch pipeline/texture
    // only when it changes between consecutive primitives.
    // ------------------------------------------------------------------
    std::stable_sort(order.begin(), order.end(), [](const PrimRef& a, const PrimRef& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        return a.isWall > b.isWall; // walls before tops at equal depth
    });

    struct Batch {
        bool wall;
        const TextureAtlas* tex;
        int base;
        int count;
    };
    std::vector<Batch> batches;
    scratchWallVerts.clear();
    scratchRaisedVerts.clear();
    scratchTriWallVerts.clear();

    // Byte capacity of the triplanar buffer in painter tri-wall vertices.
    constexpr std::size_t kTriWallCap = (kWallVbufVertices * sizeof(WallVertex)) / sizeof(TriWallVertex);

    bool emitTruncated = false;
    for (const PrimRef& ref : order) {
        if (ref.isWall) {
            if (ref.tex != nullptr) {
                // Triplanar wall (rock texture on this raised atlas).
                if (scratchTriWallVerts.size() + ref.count > kTriWallCap) {
                    emitTruncated = true;
                    continue;
                }
                if (batches.empty() || !batches.back().wall || batches.back().tex != ref.tex) {
                    batches.push_back({true, ref.tex, (int)scratchTriWallVerts.size(), 0});
                }
                for (std::uint32_t k = 0; k < ref.count; ++k) {
                    const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                    const glm::vec2 p = camera.worldToScreen(v.pos);
                    float tcoord[3];
                    wallTcoord(v, tcoord);
                    scratchTriWallVerts.push_back(
                        {{p.x, p.y},
                         {v.color.r, v.color.g, v.color.b, v.color.a},
                         {tcoord[0], tcoord[1], tcoord[2]},
                         {v.normal.x, v.normal.y}});
                }
                batches.back().count += (int)ref.count;
                continue;
            }
            if (scratchWallVerts.size() + ref.count > kWallVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || !batches.back().wall || batches.back().tex != nullptr) {
                batches.push_back({true, nullptr, (int)scratchWallVerts.size(), 0});
            }
            for (std::uint32_t k = 0; k < ref.count; ++k) {
                const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                const glm::vec2 p = camera.worldToScreen(v.pos);
                scratchWallVerts.push_back({{p.x, p.y}, {v.color.r, v.color.g, v.color.b, v.color.a}});
            }
            batches.back().count += (int)ref.count;
        } else if (ref.mesh != nullptr) {
            if (scratchRaisedVerts.size() + ref.count > kRaisedVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || batches.back().wall || batches.back().tex != ref.tex) {
                batches.push_back({false, ref.tex, (int)scratchRaisedVerts.size(), 0});
            }
            for (std::uint32_t k = 0; k < ref.count; ++k) {
                const highground::Vertex& v = ref.mesh->vertices[ref.first + k];
                const glm::vec2 p = camera.worldToScreen(v.pos);
                scratchRaisedVerts.push_back({{p.x, p.y}, {v.uv.x, v.uv.y}, {v.color.r, v.color.g, v.color.b, v.color.a}});
            }
            batches.back().count += (int)ref.count;
        } else {
            // Legacy atlas-quad top (6 verts per cell).
            if (scratchRaisedVerts.size() + 6 > kRaisedVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || batches.back().wall || batches.back().tex != ref.tex) {
                batches.push_back({false, ref.tex, (int)scratchRaisedVerts.size(), 0});
            }
            const RaisedAtlasGpu& raised = raisedAtlases.find(ref.tile->assetUuid)->second;
            std::vector<Vertex> quad;
            quad.reserve(6);
            appendAtlasQuad(quad, raised.gpu, iso, ref.tile->cell, ref.tile->tileIndex, -raised.params.height);
            for (const Vertex& v : quad) {
                const glm::vec2 p = camera.worldToScreen({v.pos[0], v.pos[1]});
                scratchRaisedVerts.push_back({{p.x, p.y}, {v.uv[0], v.uv[1]}, {v.color[0], v.color[1], v.color[2], v.color[3]}});
            }
            batches.back().count += 6;
        }
    }
    if (emitTruncated) {
        spdlog::error("LandscapeRenderer::renderRaised: vertex overflow during depth-sorted emission, geometry truncated");
    }

    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };

    // Triplanar walls: {view_size, tex scale} (16 bytes).
    float tri_vs_params[4] = {(float)viewWidth, (float)viewHeight, kWallTexScale, 0.0f};
    sg_range tri_uniform_range = { &tri_vs_params, sizeof(tri_vs_params) };

    if (!scratchWallVerts.empty()) {
        sg_range range = { scratchWallVerts.data(), scratchWallVerts.size() * sizeof(WallVertex) };
        sg_update_buffer(wallVbuf, &range);
    }
    if (!scratchRaisedVerts.empty()) {
        sg_range range = { scratchRaisedVerts.data(), scratchRaisedVerts.size() * sizeof(Vertex) };
        sg_update_buffer(raisedVbuf, &range);
    }
    if (!scratchTriWallVerts.empty()) {
        sg_range range = { scratchTriWallVerts.data(), scratchTriWallVerts.size() * sizeof(TriWallVertex) };
        sg_update_buffer(triWallVbuf, &range);
    }

    for (const Batch& batch : batches) {
        if (batch.wall) {
            if (batch.tex != nullptr) {
                sg_bindings triBind{};
                triBind.vertex_buffers[0] = triWallVbuf;
                triBind.views[0] = batch.tex->sgView();
                triBind.samplers[0] = batch.tex->sgSampler();
                sg_apply_pipeline(triWallPip);
                sg_apply_uniforms(0, &tri_uniform_range);
                sg_apply_bindings(&triBind);
            } else {
                sg_apply_pipeline(wallPip);
                sg_apply_uniforms(0, &uniform_range);
                sg_apply_bindings(&wallBind);
            }
        } else {
            sg_apply_pipeline(pip);
            sg_apply_uniforms(0, &uniform_range);
            raisedBind.views[0] = batch.tex->sgView();
            raisedBind.samplers[0] = batch.tex->sgSampler();
            sg_apply_bindings(&raisedBind);
        }
        sg_draw(batch.base, batch.count, 1);
    }
}

} // namespace render_core
