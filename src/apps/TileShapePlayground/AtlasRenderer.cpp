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

// ---------------------------------------------------------------------------
// Scene stitching (shared by the ground pass and the cliff pass): the sun both
// of them are lit by, plus the orthographic shadow-map lookup. Declared as
// snippets because the two passes assemble them into different shaders; the
// HLSL cbuffer register and the MSL buffer index differ per pass, so only the
// lookup function itself is literally shared.
// ---------------------------------------------------------------------------

static const char* kStitchCoreGlsl = R"(
uniform vec4 sun_dir;
uniform vec4 sh_row0;
uniform vec4 sh_row1;
uniform vec4 sh_row2;
uniform vec4 stitch0; // ambient, diffuse, gamma, shadow strength
uniform vec4 stitch1; // shadow bias, shadow texel, AO strength, AO radius
)";

// Ground-only tail of the block (see SceneStitchParams).
static const char* kStitchAoGlsl = R"(
uniform vec4 ao_rect;
)";

// 3x3 PCF with a slope-scaled bias. stitch0.w == 0 turns shadows off, and
// anything outside the light frustum stays lit.
static const char* kStitchFnGlsl = R"(
float shadowFactor(sampler2D shadowTex, vec3 wp, float ndl) {
    if (stitch0.w <= 0.0) {
        return 1.0;
    }
    vec4 hp = vec4(wp, 1.0);
    vec3 sc = vec3(dot(sh_row0, hp), dot(sh_row1, hp), dot(sh_row2, hp));
    if (sc.x <= 0.0 || sc.x >= 1.0 || sc.y <= 0.0 || sc.y >= 1.0) {
        return 1.0;
    }
    float bias = stitch1.x * (1.0 + 3.0 * (1.0 - clamp(ndl, 0.0, 1.0)));
    float lit = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 uv = sc.xy + vec2(float(i), float(j)) * stitch1.y;
            lit += step(sc.z - bias, textureLod(shadowTex, uv, 0.0).r);
        }
    }
    return mix(1.0, lit * (1.0 / 9.0), stitch0.w);
}
)";

static const char* kStitchFnHlsl = R"(
float shadowFactor(Texture2D shadowTex, SamplerState shadowSmp, float3 wp, float ndl) {
    if (stitch0.w <= 0.0) {
        return 1.0;
    }
    float4 hp = float4(wp, 1.0);
    float3 sc = float3(dot(sh_row0, hp), dot(sh_row1, hp), dot(sh_row2, hp));
    if (sc.x <= 0.0 || sc.x >= 1.0 || sc.y <= 0.0 || sc.y >= 1.0) {
        return 1.0;
    }
    float bias = stitch1.x * (1.0 + 3.0 * (1.0 - saturate(ndl)));
    float lit = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            float2 uv = sc.xy + float2((float)i, (float)j) * stitch1.y;
            lit += step(sc.z - bias, shadowTex.SampleLevel(shadowSmp, uv, 0).r);
        }
    }
    return lerp(1.0, lit * (1.0 / 9.0), stitch0.w);
}
)";

static const char* kStitchStructCoreMsl = R"(
struct StitchParams {
    float4 sun_dir;
    float4 sh_row0;
    float4 sh_row1;
    float4 sh_row2;
    float4 stitch0;
    float4 stitch1;
};
)";

static const char* kStitchStructAoMsl = R"(
struct StitchParams {
    float4 sun_dir;
    float4 sh_row0;
    float4 sh_row1;
    float4 sh_row2;
    float4 stitch0;
    float4 stitch1;
    float4 ao_rect;
};
)";

static const char* kStitchFnMsl = R"(
float shadowFactor(constant StitchParams& st, texture2d<float> shadowTex,
                   sampler shadowSmp, float3 wp, float ndl) {
    if (st.stitch0.w <= 0.0) {
        return 1.0;
    }
    float4 hp = float4(wp, 1.0);
    float3 sc = float3(dot(st.sh_row0, hp), dot(st.sh_row1, hp), dot(st.sh_row2, hp));
    if (sc.x <= 0.0 || sc.x >= 1.0 || sc.y <= 0.0 || sc.y >= 1.0) {
        return 1.0;
    }
    float bias = st.stitch1.x * (1.0 + 3.0 * (1.0 - saturate(ndl)));
    float lit = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            float2 uv = sc.xy + float2(float(i), float(j)) * st.stitch1.y;
            lit += step(sc.z - bias, shadowTex.sample(shadowSmp, uv, level(0)).r);
        }
    }
    return mix(1.0, lit * (1.0 / 9.0), st.stitch0.w);
}
)";

// ---------------------------------------------------------------------------
// Flat tile pass (atlas tiles + the grass underlay). Carries a baked depth and
// the world position so the ground can be lit by the same sun as the
// highground and receive its contact AO and cast shadow.
// ---------------------------------------------------------------------------

static const char* kTexVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec2 uv;
layout(location=2) in vec2 world;
out vec2 v_uv;
out vec2 v_world;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
    v_uv = uv;
    v_world = world;
}
)";

static const char* kTexFsBodyGlsl = R"(
in vec2 v_uv;
in vec2 v_world;
out vec4 frag_color;
uniform sampler2D atlas_tex;
uniform sampler2D ao_tex;
uniform sampler2D shadow_tex;

void main() {
    vec4 texel = texture(atlas_tex, v_uv);
    if (texel.a < 0.05) discard;
    // One sun, one ambient, one gamma with the cliff pass: the ground used to
    // be a raw texture next to a lit mesh, which is what read as a collage.
    vec3 l = normalize(sun_dir.xyz);
    float ndl = max(l.y, 0.0);
    float light = stitch0.x + stitch0.y * ndl;
    // Contact AO: the R8 field holds the distance to the highground footprint
    // normalized by kAoMaxDistanceCells (4 cells).
    vec2 aoUv = (v_world - ao_rect.xy) * ao_rect.zw;
    float aoDist = textureLod(ao_tex, aoUv, 0.0).r * 4.0;
    float ao = mix(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    float sh = shadowFactor(shadow_tex, vec3(v_world.x, 0.0, v_world.y), ndl);
    vec3 col = texel.rgb * (light * ao * sh);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(stitch0.z)), texel.a);
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
    float3 pos: TEXCOORD0;
    float2 uv: TEXCOORD1;
    float2 world: TEXCOORD2;
};
struct VSOut {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
    float2 world: TEXCOORD1;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, inp.pos.z, 1.0);
    o.uv = inp.uv;
    o.world = inp.world;
    return o;
}
)";

static const char* kTexFsHeadHlsl = R"(
cbuffer stitch_params: register(b0) {
    float4 sun_dir;
    float4 sh_row0;
    float4 sh_row1;
    float4 sh_row2;
    float4 stitch0;
    float4 stitch1;
    float4 ao_rect;
};
Texture2D atlas_tex: register(t0);
SamplerState smp: register(s0);
Texture2D ao_tex: register(t1);
SamplerState ao_smp: register(s1);
Texture2D shadow_tex: register(t2);
SamplerState shadow_smp: register(s2);
)";

static const char* kTexFsBodyHlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float2 uv: TEXCOORD0;
    float2 world: TEXCOORD1;
};
float4 main(PSIn inp): SV_Target {
    float4 texel = atlas_tex.Sample(smp, inp.uv);
    if (texel.a < 0.05) discard;
    float3 l = normalize(sun_dir.xyz);
    float ndl = max(l.y, 0.0);
    float light = stitch0.x + stitch0.y * ndl;
    float2 aoUv = (inp.world - ao_rect.xy) * ao_rect.zw;
    float aoDist = ao_tex.SampleLevel(ao_smp, aoUv, 0).r * 4.0;
    float ao = lerp(1.0 - stitch1.z, 1.0, smoothstep(0.0, max(stitch1.w, 1e-3), aoDist));
    float sh = shadowFactor(shadow_tex, shadow_smp, float3(inp.world.x, 0.0, inp.world.y), ndl);
    float3 col = texel.rgb * (light * ao * sh);
    return float4(pow(saturate(col), (float3)stitch0.z), texel.a);
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
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float2 world [[attribute(2)]];
};

struct VSOut {
    float4 pos [[position]];
    float2 uv;
    float2 world;
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
    o.world = in.world;
    return o;
}
)";

static const char* kTexFsHeadMsl = R"(
#include <metal_stdlib>
using namespace metal;
)";

static const char* kTexFsBodyMsl = R"(
struct PSIn {
    float4 pos [[position]];
    float2 uv;
    float2 world;
};

fragment float4 _main(PSIn in [[stage_in]],
                      constant StitchParams& st [[buffer(1)]],
                      texture2d<float> atlas_tex [[texture(0)]],
                      sampler smp [[sampler(0)]],
                      texture2d<float> ao_tex [[texture(1)]],
                      sampler ao_smp [[sampler(1)]],
                      texture2d<float> shadow_tex [[texture(2)]],
                      sampler shadow_smp [[sampler(2)]]) {
    float4 texel = atlas_tex.sample(smp, in.uv);
    if (texel.a < 0.05) {
        discard_fragment();
    }
    float3 l = normalize(st.sun_dir.xyz);
    float ndl = max(l.y, 0.0);
    float light = st.stitch0.x + st.stitch0.y * ndl;
    float2 aoUv = (in.world - st.ao_rect.xy) * st.ao_rect.zw;
    float aoDist = ao_tex.sample(ao_smp, aoUv, level(0)).r * 4.0;
    float ao = mix(1.0 - st.stitch1.z, 1.0, smoothstep(0.0, max(st.stitch1.w, 1e-3), aoDist));
    float sh = shadowFactor(st, shadow_tex, shadow_smp,
                            float3(in.world.x, 0.0, in.world.y), ndl);
    float3 col = texel.rgb * (light * ao * sh);
    return float4(pow(saturate(col), float3(st.stitch0.z)), texel.a);
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

static const char* kCliffFsHeadGlsl = R"(
in vec3 v_normal;
in float v_groove;
in vec3 v_world;
in float v_rim;
out vec4 frag_color;
uniform vec4 view_dir;
uniform vec4 dark_color;
uniform vec4 gold_color;
uniform vec4 grass_a;
uniform vec4 grass_b;
uniform vec4 params0;
uniform vec4 params1;
uniform vec4 params2;
uniform vec4 params3;
uniform vec4 params4;
uniform vec4 params5;
uniform vec4 params6;
uniform vec4 params7;
uniform sampler2D top_tex;
uniform sampler2D shadow_tex;
)";

static const char* kCliffFsBodyGlsl = R"(
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
    // Scree boulders ride the same vertex stream, tagged with a negative
    // groove: they must keep the wall palette, never the grassy top.
    float scree = step(v_groove, -0.5);
    float groove = max(v_groove, 0.0);
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, groove);
    vec3 gold = gold_color.rgb + vec3(1.0, 0.9, 0.4) * step(params0.x, f);
    vec3 rock = mix(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    // Rotated (params6.w) and tinted (params7) away from the ground: the top
    // shares grass.png with the field below and would otherwise continue it.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    vec3 grass = mix(grass_a.rgb, grass_b.rgb, gm);
    float ca = cos(params6.w);
    float sa = sin(params6.w);
    vec2 topUv = vec2(ca * p.x - sa * p.z, sa * p.x + ca * p.z) * params2.w;
    grass = mix(grass, texture(top_tex, topUv).rgb, params2.z) * params7.rgb;
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
    topMask *= 1.0 - scree;
    vec3 base = mix(rock, grass, topMask);
    // The plateau shares grass.png with the ground; lift its tone so the two
    // do not read as one continuous plane (params4.y).
    base *= mix(1.0, params4.y, topMask);
    // Grass skirt: instead of ending on a clean surface-nets contour, the
    // wall dissolves into the ground material over params3.z above the
    // ground plane (params5.w = the height the mesh is sunk to).
    float ground = params5.w;
    float skirt = 1.0 - smoothstep(0.0, max(params3.z, 1e-4), p.y - ground);
    // The trailing gate is what makes params4.x == 0 mean "no skirt at all":
    // step() against a zero threshold still passes wherever the noise lands on
    // exactly zero.
    float overgrow = step(fbm2(p.xz * params3.w), skirt * params4.x)
                   * step(1e-4, params4.x);
    base = mix(base, grass * 0.8, overgrow * (1.0 - topMask));
    // Bounce off the grass below, cool sky on the upward faces: the stone and
    // the ground palettes need something in common to share a scene.
    float low = 1.0 - smoothstep(0.0, max(params3.z * 4.0, 1e-4), p.y - ground);
    base = mix(base, base * params5.rgb, low * params4.z);
    base = mix(base, base * params6.rgb, clamp(n.y, 0.0, 1.0) * params4.w);
    // Baked wall proximity darkens the plateau edge (params3.x), so the top
    // stops looking pasted onto the walls.
    float rimAo = 1.0 - clamp(v_rim * params3.x, 0.0, 1.0);
    // The wall's own contact AO, sharing the ground's strength (stitch1.z):
    // the foot of the wall sees no more sky than the grass it stands in, and
    // leaving it lit while the ground darkens draws the seam instead of
    // hiding it. params7.w is the reach up the wall, 0 turns it off.
    float footAo = 1.0 - stitch1.z * step(1e-4, params7.w)
        * (1.0 - smoothstep(0.0, max(params7.w, 1e-4), p.y - params5.w));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is
    // constant. The sun comes from the shared scene block — the ground is lit
    // by the very same vector.
    vec3 l = normalize(sun_dir.xyz);
    vec3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    // Self shadow: only the direct terms are occluded, ambient and the wrap
    // backlight stay so the shaded side keeps its volume.
    float sh = shadowFactor(shadow_tex, vec3(p.x, (p.y - ground) * params3.y, p.z), ndl);
    vec3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0) * sh) * rimAo;
    float specAmt = mix(0.05, params0.w, shell) * (1.0 - topMask);
    col += sh * specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    col *= footAo;
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

static const char* kCliffFsHeadHlsl = R"(
cbuffer fs_params: register(b0) {
    float4 view_dir;
    float4 dark_color;
    float4 gold_color;
    float4 grass_a;
    float4 grass_b;
    float4 params0; // x: vein threshold, y: ambient, z: diffuse, w: spec strength
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: boulder plane Y (0 = off)
    float4 params2; // x: grass->stone fade depth, y: rim gradient strength, z: top texture strength, w: tiling
    float4 params3; // x: rim contact AO, y: height->world, z: skirt height, w: skirt frequency
    float4 params4; // x: overgrowth, y: top brightness, z: bounce strength, w: sky strength
    float4 params5; // xyz: bounce tint, w: ground plane Y
    float4 params6; // xyz: sky tint, w: top texture UV rotation
    float4 params7; // xyz: plateau top tint
};
cbuffer stitch_params: register(b1) {
    float4 sun_dir;
    float4 sh_row0;
    float4 sh_row1;
    float4 sh_row2;
    float4 stitch0;
    float4 stitch1;
};
Texture2D top_tex: register(t0);
SamplerState top_smp: register(s0);
Texture2D shadow_tex: register(t1);
SamplerState shadow_smp: register(s1);
)";

static const char* kCliffFsBodyHlsl = R"(
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
    // Scree boulders ride the same vertex stream, tagged with a negative
    // groove: they must keep the wall palette, never the grassy top.
    float scree = step(inp.groove, -0.5);
    float groove = max(inp.groove, 0.0);
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, groove);
    float3 gold = gold_color.rgb + float3(1.0, 0.9, 0.4) * step(params0.x, f);
    float3 rock = lerp(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = lerp(grass_a.rgb, grass_b.rgb, gm);
    float ca = cos(params6.w);
    float sa = sin(params6.w);
    float2 topUv = float2(ca * p.x - sa * p.z, sa * p.x + ca * p.z) * params2.w;
    grass = lerp(grass, top_tex.Sample(top_smp, topUv).rgb, params2.z) * params7.rgb;
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
    topMask *= 1.0 - scree;
    float3 base = lerp(rock, grass, topMask);
    // The plateau shares grass.png with the ground; lift its tone so the two
    // do not read as one continuous plane (params4.y).
    base *= lerp(1.0, params4.y, topMask);
    // Grass skirt: instead of ending on a clean surface-nets contour, the
    // wall dissolves into the ground material over params3.z above the
    // ground plane (params5.w = the height the mesh is sunk to).
    float ground = params5.w;
    float skirt = 1.0 - smoothstep(0.0, max(params3.z, 1e-4), p.y - ground);
    // See the GLSL body: the gate is what makes params4.x == 0 mean "no skirt".
    float overgrow = step(fbm2(p.xz * params3.w), skirt * params4.x)
                   * step(1e-4, params4.x);
    base = lerp(base, grass * 0.8, overgrow * (1.0 - topMask));
    // Bounce off the grass below, cool sky on the upward faces: the stone and
    // the ground palettes need something in common to share a scene.
    float low = 1.0 - smoothstep(0.0, max(params3.z * 4.0, 1e-4), p.y - ground);
    base = lerp(base, base * params5.rgb, low * params4.z);
    base = lerp(base, base * params6.rgb, saturate(n.y) * params4.w);
    // Baked wall proximity darkens the plateau edge (params3.x), so the top
    // stops looking pasted onto the walls.
    float rimAo = 1.0 - clamp(inp.rim * params3.x, 0.0, 1.0);
    // See the GLSL body: the wall's own contact AO, sharing the ground's
    // strength (stitch1.z); params7.w is its reach up the wall.
    float footAo = 1.0 - stitch1.z * step(1e-4, params7.w)
        * (1.0 - smoothstep(0.0, max(params7.w, 1e-4), p.y - params5.w));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is
    // constant. The sun comes from the shared scene block — the ground is lit
    // by the very same vector.
    float3 l = normalize(sun_dir.xyz);
    float3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    // Self shadow: only the direct terms are occluded, ambient and the wrap
    // backlight stay so the shaded side keeps its volume.
    float sh = shadowFactor(shadow_tex, shadow_smp,
        float3(p.x, (p.y - ground) * params3.y, p.z), ndl);
    float3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0) * sh) * rimAo;
    float specAmt = lerp(0.05, params0.w, shell) * (1.0 - topMask);
    col += sh * specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    col *= footAo;
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

static const char* kCliffFsHeadMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct FsParams {
    float4 view_dir;
    float4 dark_color;
    float4 gold_color;
    float4 grass_a;
    float4 grass_b;
    float4 params0; // x: vein threshold, y: ambient, z: diffuse, w: spec strength
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: boulder plane Y (0 = off)
    float4 params2; // x: grass->stone fade depth, y: rim gradient strength, z: top texture strength, w: tiling
    float4 params3; // x: rim contact AO, y: height->world, z: skirt height, w: skirt frequency
    float4 params4; // x: overgrowth, y: top brightness, z: bounce strength, w: sky strength
    float4 params5; // xyz: bounce tint, w: ground plane Y
    float4 params6; // xyz: sky tint, w: top texture UV rotation
    float4 params7; // xyz: plateau top tint
};
)";

static const char* kCliffFsBodyMsl = R"(
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
                      constant StitchParams& st [[buffer(2)]],
                      texture2d<float> top_tex [[texture(0)]],
                      sampler top_smp [[sampler(0)]],
                      texture2d<float> shadow_tex [[texture(1)]],
                      sampler shadow_smp [[sampler(1)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world;
    // Scree boulders ride the same vertex stream, tagged with a negative
    // groove: they must keep the wall palette, never the grassy top.
    float scree = step(in.groove, -0.5);
    float groove = max(in.groove, 0.0);
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, groove);
    float3 gold = fs.gold_color.rgb + float3(1.0, 0.9, 0.4) * step(fs.params0.x, f);
    float3 rock = mix(fs.dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Grassy flat tops (omphalos idObj==2 style), optionally textured:
    // world-space tiling, params2.z = strength, params2.w = tiles per unit.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = mix(fs.grass_a.rgb, fs.grass_b.rgb, gm);
    float ca = cos(fs.params6.w);
    float sa = sin(fs.params6.w);
    float2 topUv = float2(ca * p.x - sa * p.z, sa * p.x + ca * p.z) * fs.params2.w;
    grass = mix(grass, top_tex.sample(top_smp, topUv).rgb, fs.params2.z) * fs.params7.rgb;
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
    topMask *= 1.0 - scree;
    float3 base = mix(rock, grass, topMask);
    // The plateau shares grass.png with the ground; lift its tone so the two
    // do not read as one continuous plane (params4.y).
    base *= mix(1.0, fs.params4.y, topMask);
    // Grass skirt: instead of ending on a clean surface-nets contour, the
    // wall dissolves into the ground material over params3.z above the
    // ground plane (params5.w = the height the mesh is sunk to).
    float ground = fs.params5.w;
    float skirt = 1.0 - smoothstep(0.0, max(fs.params3.z, 1e-4), p.y - ground);
    // See the GLSL body: the gate is what makes params4.x == 0 mean "no skirt".
    float overgrow = step(fbm2(p.xz * fs.params3.w), skirt * fs.params4.x)
                   * step(1e-4, fs.params4.x);
    base = mix(base, grass * 0.8, overgrow * (1.0 - topMask));
    // Bounce off the grass below, cool sky on the upward faces: the stone and
    // the ground palettes need something in common to share a scene.
    float low = 1.0 - smoothstep(0.0, max(fs.params3.z * 4.0, 1e-4), p.y - ground);
    base = mix(base, base * fs.params5.rgb, low * fs.params4.z);
    base = mix(base, base * fs.params6.rgb, saturate(n.y) * fs.params4.w);
    // Baked wall proximity darkens the plateau edge (params3.x), so the top
    // stops looking pasted onto the walls.
    float rimAo = 1.0 - clamp(in.rim * fs.params3.x, 0.0, 1.0);
    // See the GLSL body: the wall's own contact AO, sharing the ground's
    // strength (stitch1.z); params7.w is its reach up the wall.
    float footAo = 1.0 - st.stitch1.z * step(1e-4, fs.params7.w)
        * (1.0 - smoothstep(0.0, max(fs.params7.w, 1e-4), p.y - fs.params5.w));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is
    // constant. The sun comes from the shared scene block — the ground is lit
    // by the very same vector.
    float3 l = normalize(st.sun_dir.xyz);
    float3 rd = fs.view_dir.xyz;
    float ndl = dot(n, l);
    // Self shadow: only the direct terms are occluded, ambient and the wrap
    // backlight stay so the shaded side keeps its volume.
    float sh = shadowFactor(st, shadow_tex, shadow_smp,
        float3(p.x, (p.y - ground) * fs.params3.y, p.z), ndl);
    float3 col = base * (fs.params0.y + fs.params1.z * max(-ndl, 0.0) +
        fs.params0.z * max(ndl, 0.0) * sh) * rimAo;
    float specAmt = mix(0.05, fs.params0.w, shell) * (1.0 - topMask);
    col += sh * specAmt * pow(max(dot(normalize(l - rd), n), 0.0), fs.params1.x);
    col *= footAo;
    return float4(pow(clamp(col, 0.0, 1.0), float3(fs.params1.y)), 1.0);
}
)";

// ---------------------------------------------------------------------------
// Shadow pass: the cliff/stone stream re-projected into the sun's orthographic
// frame. The normalized light distance goes into a float COLOR attachment (the
// repo has no depth-texture sampling path); the depth attachment only resolves
// which surface is nearest to the sun.
// ---------------------------------------------------------------------------

static const char* kShadowVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in float groove;
layout(location=3) in vec3 world;
layout(location=4) in float rim;
out float v_depth;
uniform vec4 sm_row0;
uniform vec4 sm_row1;
uniform vec4 sm_row2;
uniform vec4 sm_params; // x: height -> world scale, y: ground plane Y
void main() {
    vec4 hp = vec4(world.x, (world.y - sm_params.y) * sm_params.x, world.z, 1.0);
    vec3 sc = vec3(dot(sm_row0, hp), dot(sm_row1, hp), dot(sm_row2, hp));
    gl_Position = vec4(sc.xy * 2.0 - 1.0, sc.z, 1.0);
    v_depth = sc.z;
}
)";

static const char* kShadowFsGlsl = R"(
#version 330
in float v_depth;
layout(location=0) out float frag_depth;
void main() {
    frag_depth = v_depth;
}
)";

static const char* kShadowVsHlsl = R"(
cbuffer sm_params_block: register(b0) {
    float4 sm_row0;
    float4 sm_row1;
    float4 sm_row2;
    float4 sm_params;
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
    float depth: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    float4 hp = float4(inp.world.x, (inp.world.y - sm_params.y) * sm_params.x, inp.world.z, 1.0);
    float3 sc = float3(dot(sm_row0, hp), dot(sm_row1, hp), dot(sm_row2, hp));
    o.pos = float4(sc.xy * 2.0 - 1.0, sc.z, 1.0);
    o.depth = sc.z;
    return o;
}
)";

static const char* kShadowFsHlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float depth: TEXCOORD0;
};
float main(PSIn inp): SV_Target {
    return inp.depth;
}
)";

static const char* kShadowVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct SmParams {
    float4 sm_row0;
    float4 sm_row1;
    float4 sm_row2;
    float4 sm_params;
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
    float depth;
};

vertex VSOut _main(VSIn in [[stage_in]], constant SmParams& sm [[buffer(0)]]) {
    VSOut o;
    float4 hp = float4(in.world.x, (in.world.y - sm.sm_params.y) * sm.sm_params.x, in.world.z, 1.0);
    float3 sc = float3(dot(sm.sm_row0, hp), dot(sm.sm_row1, hp), dot(sm.sm_row2, hp));
    o.pos = float4(sc.xy * 2.0 - 1.0, sc.z, 1.0);
    o.depth = sc.z;
    return o;
}
)";

static const char* kShadowFsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct PSIn {
    float4 pos [[position]];
    float depth;
};

fragment float _main(PSIn in [[stage_in]]) {
    return in.depth;
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
    const char* names[13] = {"view_dir", "dark_color", "gold_color",
        "grass_a", "grass_b", "params0", "params1", "params2", "params3",
        "params4", "params5", "params6", "params7"};
    for (int i = 0; i < 13; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
}

// Scene stitching block, shared by the ground pass (its only fragment block,
// slot 1 / b0) and the cliff pass (slot 2 / b1, next to the palette block).
// The cliff pass takes the core only — see kStitchCoreBytes.
void fillStitchUniformDesc(sg_shader_uniform_block* block, int slot, int hlslRegister, bool withAo) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = withAo ? sizeof(SceneStitchParams) : kStitchCoreBytes;
    block->hlsl_register_b_n = hlslRegister;
    block->msl_buffer_n = slot;
    block->wgsl_group0_binding_n = slot;
    block->spirv_set0_binding_n = slot;
    const char* names[7] = {"sun_dir", "sh_row0", "sh_row1", "sh_row2",
        "stitch0", "stitch1", "ao_rect"};
    const int count = withAo ? 7 : 6;
    for (int i = 0; i < count; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
}

void fillShadowVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(AtlasRenderer::ShadowVsParams);
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 0;
    block->spirv_set0_binding_n = 0;
    const char* names[4] = {"sm_row0", "sm_row1", "sm_row2", "sm_params"};
    for (int i = 0; i < 4; ++i) {
        block->glsl_uniforms[i].glsl_name = names[i];
        block->glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
    }
}

// Fragment texture/sampler slot wiring: the shadow map is sampled with
// NEAREST only (float render targets are not guaranteed filterable), so it
// must be declared unfilterable or sokol's validation rejects the pair.
// Normalized depth along the iso view ray. Field-y grows TOWARD the viewer, so
// with LESS_EQUAL + clear 1.0 the closer fragment needs the SMALLER z. The
// anchor is a constant rather than camera-derived: the cliff stream bakes its
// z once at rebuild time while the ground computes it every frame, and a
// camera-derived anchor would drift between the two as the view pans.
constexpr float kZFar = 100000.0f;
constexpr float kZScale = 1.0f / 200000.0f;

// Every ground fragment shares one depth behind the whole scene (see the tile
// pipeline comment in ensurePipelines).
constexpr float kGroundDepth = 0.999f;

// 8 texels per cell: the contact AO falloff is a fraction of a cell wide, and
// at 4 the bilinear ramp spans under two texels and shows the chamfer
// transform's diagonal staircase.
constexpr int kAoTexelsPerCell = 8;
constexpr int kAoMarginCells = 4;

float bakedDepth(float fieldY) {
    return (kZFar - fieldY) * kZScale;
}

// Field position -> world (map) coordinates: the inverse of the placement the
// cliff stream uses (DiamondIsometry::nodeToField, no +halfH on y), so the
// ground samples the AO field and the shadow map in the mesh's own frame.
glm::vec2 worldFromField(float fieldX, float fieldY, float halfW, float halfH) {
    const float diff = (fieldX - halfW) / halfW; // mapX - mapZ
    const float sum = fieldY / halfH;            // mapX + mapZ
    return {0.5f * (sum + diff), 0.5f * (sum - diff)};
}

bool sameScree(const ScreeParams& a, const ScreeParams& b) {
    return a.enabled == b.enabled && a.perCell == b.perCell && a.band == b.band &&
        a.sizeMin == b.sizeMin && a.sizeMax == b.sizeMax && a.buried == b.buried &&
        a.seed == b.seed;
}

// Deterministic scatter hash (integer mix, not fract(sin(...)) — that streaks
// on large arguments, see the StoneCubePlayground notes).
float hash01(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * 747796405u + b * 2891336453u + c * 668265263u + 1442695040u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

void fillTextureSlot(sg_shader_desc* shd, int slot, const char* glslName, bool filtering) {
    shd->views[slot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd->views[slot].texture.image_type = SG_IMAGETYPE_2D;
    shd->views[slot].texture.sample_type =
        filtering ? SG_IMAGESAMPLETYPE_FLOAT : SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT;
    shd->views[slot].texture.hlsl_register_t_n = slot;
    shd->views[slot].texture.msl_texture_n = slot;
    shd->views[slot].texture.wgsl_group1_binding_n = slot * 2;
    shd->views[slot].texture.spirv_set1_binding_n = slot * 2;

    shd->samplers[slot].stage = SG_SHADERSTAGE_FRAGMENT;
    shd->samplers[slot].sampler_type =
        filtering ? SG_SAMPLERTYPE_FILTERING : SG_SAMPLERTYPE_NONFILTERING;
    shd->samplers[slot].hlsl_register_s_n = slot;
    shd->samplers[slot].msl_sampler_n = slot;
    shd->samplers[slot].wgsl_group1_binding_n = slot * 2 + 1;
    shd->samplers[slot].spirv_set1_binding_n = slot * 2 + 1;

    shd->texture_sampler_pairs[slot].stage = SG_SHADERSTAGE_FRAGMENT;
    shd->texture_sampler_pairs[slot].view_slot = slot;
    shd->texture_sampler_pairs[slot].sampler_slot = slot;
    shd->texture_sampler_pairs[slot].glsl_name = glslName;
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

    // Contact AO field sampler + a 1x1 "nothing nearby" placeholder, so the
    // ground bindings stay valid before anything is painted.
    sg_sampler_desc aoSmp = {};
    aoSmp.min_filter = SG_FILTER_LINEAR;
    aoSmp.mag_filter = SG_FILTER_LINEAR;
    aoSmp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    aoSmp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    aoSmp.label = "tileshape-ao-smp";
    m_aoSampler = sg_make_sampler(&aoSmp);

    const std::uint8_t aoFar = 255;
    sg_image_desc aoImg = {};
    aoImg.width = 1;
    aoImg.height = 1;
    aoImg.pixel_format = SG_PIXELFORMAT_R8;
    aoImg.data.mip_levels[0].ptr = &aoFar;
    aoImg.data.mip_levels[0].size = sizeof(aoFar);
    aoImg.label = "tileshape-ao-placeholder";
    m_aoImage = sg_make_image(&aoImg);
    sg_view_desc aoView = {};
    aoView.texture.image = m_aoImage;
    m_aoView = sg_make_view(&aoView);

    // The shadow map is a float render target: NEAREST only, PCF is done by
    // hand in the shader.
    sg_sampler_desc shadowSmp = {};
    shadowSmp.min_filter = SG_FILTER_NEAREST;
    shadowSmp.mag_filter = SG_FILTER_NEAREST;
    shadowSmp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    shadowSmp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    shadowSmp.label = "tileshape-shadow-smp";
    m_shadowSampler = sg_make_sampler(&shadowSmp);

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
    if (m_aoView.id != SG_INVALID_ID) {
        sg_destroy_view(m_aoView);
        m_aoView = {};
    }
    if (m_aoImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_aoImage);
        m_aoImage = {};
    }
    if (m_aoSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_aoSampler);
        m_aoSampler = {};
    }
    if (m_shadowSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_shadowSampler);
        m_shadowSampler = {};
    }
    if (m_shadow.colorTexture.id != SG_INVALID_ID) {
        sg_destroy_view(m_shadow.colorTexture);
    }
    if (m_shadow.colorAttachment.id != SG_INVALID_ID) {
        sg_destroy_view(m_shadow.colorAttachment);
    }
    if (m_shadow.depthAttachment.id != SG_INVALID_ID) {
        sg_destroy_view(m_shadow.depthAttachment);
    }
    if (m_shadow.colorImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_shadow.colorImage);
    }
    if (m_shadow.depthImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_shadow.depthImage);
    }
    m_shadow = ShadowTarget{};
    m_aoField = ContactAoField{};
    m_aoKey = 0;
    destroySlot(m_slots[0]);
    destroySlot(m_slots[1]);
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
    float yOffset) {

    // Atlas tiles are square frames containing a 2:1 isometric diamond base
    // (plus grass sticking up). Display as a SQUARE of side cellWidth so the
    // embedded 2:1 base matches the cell diamond (height = cellWidth/2).
    // Using cellSize (cellWidth x cellHeight) squashes that base 2x on Y.
    const glm::vec4 uv = atlasUvRect(tileIndex);
    const glm::vec2 center = iso.mapToField(cell);
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    const float side = iso.dims.cellWidth;
    const float half = side * 0.5f;

    const float x0 = center.x - half;
    const float x1 = center.x + half;
    const float y0 = center.y - half + yOffset;
    const float y1 = center.y + half + yOffset;

    const auto vert = [&](float x, float y, float u, float v) {
        const glm::vec2 w = worldFromField(x, y, halfW, halfH);
        return TexVertex{x, y, kGroundDepth, u, v, w.x, w.y};
    };

    const TexVertex tl = vert(x0, y0, uv.x, uv.y);
    const TexVertex tr = vert(x1, y0, uv.z, uv.y);
    const TexVertex br = vert(x1, y1, uv.z, uv.w);
    const TexVertex bl = vert(x0, y1, uv.x, uv.w);

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

void AtlasRenderer::prepare(const SceneFrame& frame) {
    if (!m_ready || !frame.layers || frame.layerCount <= 0 || !frame.layers[0].brush ||
        !frame.iso) {
        return;
    }
    syncCliffCaches(frame);
    refreshAoField(frame);
    renderShadowMap(frame);
}

void AtlasRenderer::render(const SceneFrame& frame) {
    if (!m_ready || m_texPip.id == SG_INVALID_ID || !frame.layers || frame.layerCount <= 0 ||
        !frame.layers[0].brush || !frame.iso || !frame.camera) {
        return;
    }

    const PaintLayerView* layers = frame.layers;
    const int layerCount = frame.layerCount;
    const topology_core::DiamondIsometry& iso = *frame.iso;
    const topology_core::Camera2D& camera = *frame.camera;
    const int viewW = frame.viewW;
    const int viewH = frame.viewH;
    const UnderlayParams* underlay = frame.underlay;
    const CliffFsParams* cliffShading = frame.cliffShading;

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

    // Grass underlay: the bottom-most range of the shared textured buffer
    // (base 0, drawn first), bound to the tiling top texture instead of an
    // atlas. One field-space quad over the map bbox plus a one-cell margin
    // (edge cliff walls lean past the map border and must stand on it);
    // UV tiles the texture per cell width, aligned to the field origin.
    int underlayCount = 0;
    if (underlay && underlay->enabled && m_topTexView.id != SG_INVALID_ID) {
        const glm::vec2 cellSize = iso.dims.cellSize();
        const float halfW = cellSize.x * 0.5f;
        const float halfH = cellSize.y * 0.5f;
        const glm::vec2 c00 = iso.mapToField({0, 0});
        const glm::vec2 c10 = iso.mapToField({mapW - 1, 0});
        const glm::vec2 c01 = iso.mapToField({0, mapH - 1});
        const glm::vec2 c11 = iso.mapToField({mapW - 1, mapH - 1});
        const float x0 = std::min({c00.x, c10.x, c01.x, c11.x}) - cellSize.x;
        const float x1 = std::max({c00.x, c10.x, c01.x, c11.x}) + cellSize.x;
        const float y0 = std::min({c00.y, c10.y, c01.y, c11.y}) - cellSize.y;
        const float y1 = std::max({c00.y, c10.y, c01.y, c11.y}) + cellSize.y;
        const float s = underlay->tilesPerCell / cellSize.x;
        const auto vert = [&](float x, float y) {
            const glm::vec2 w = worldFromField(x, y, halfW, halfH);
            return TexVertex{x, y, kGroundDepth, x * s, y * s, w.x, w.y};
        };
        const TexVertex tl = vert(x0, y0);
        const TexVertex tr = vert(x1, y0);
        const TexVertex br = vert(x1, y1);
        const TexVertex bl = vert(x0, y1);
        texVerts.push_back(tl);
        texVerts.push_back(tr);
        texVerts.push_back(br);
        texVerts.push_back(tl);
        texVerts.push_back(br);
        texVerts.push_back(bl);
        underlayCount = 6;
    }

    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        // The cliff layer also paints its flat tiles: they show the painted
        // silhouette instantly while the field mesh rebuilds (debounced), and
        // the finished mesh covers them by depth afterwards.
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

    std::vector<ColorVertex> lineVerts;
    lineVerts.reserve(static_cast<std::size_t>(mapW * mapH * 8 + 16));

    const glm::vec4 gridColor{0.45f, 0.48f, 0.52f, 0.55f};
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            appendDiamondOutline(lineVerts, iso, {x, y}, gridColor);
        }
    }

    if (frame.hasHover) {
        appendNodeMarker(lineVerts, iso, frame.hoverNode, {1.0f, 0.25f, 0.2f, 1.0f});
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

    const SceneStitchParams stitch = buildStitchParams(frame);
    const sg_view shadowView =
        m_shadow.valid ? m_shadow.colorTexture : m_aoView; // never sampled when off

    // Painter order: flat ground layers, then the z-buffered cliff meshes,
    // then the grid lines on top.
    const auto drawGroundRange = [&](sg_view albedo, sg_sampler sampler, int base, int count) {
        if (albedo.id == SG_INVALID_ID || count <= 0) {
            return;
        }
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_texVbuf;
        bind.views[0] = albedo;
        bind.samplers[0] = sampler;
        bind.views[1] = m_aoView;
        bind.samplers[1] = m_aoSampler;
        bind.views[2] = shadowView;
        bind.samplers[2] = m_shadowSampler;

        sg_apply_pipeline(m_texPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_apply_uniforms(1, sg_range{&stitch, sizeof(stitch)});
        sg_draw(base, count, 1);
    };

    if (underlayCount > 0) {
        drawGroundRange(m_topTexView, m_topTexSampler, 0, underlayCount);
    }

    for (const TexRange& range : flatRanges) {
        drawGroundRange(
            m_slots[static_cast<int>(range.atlas)].view, m_sampler, range.base, range.count);
    }

    if (!lineVerts.empty()) {
        sg_update_buffer(m_colorVbuf, sg_range{lineVerts.data(), lineVerts.size() * sizeof(ColorVertex)});
    }

    // Cliff/stone layers: scalar-field surface nets from the same nodes, drawn
    // with the z-buffer and per-pixel shading. Cache upkeep already ran in
    // prepare(); here we only draw what it produced.
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    for (int li = 0; li < layerCount; ++li) {
        const PaintLayerView& layer = layers[li];
        if (!layer.brush || (!layer.cliff && !layer.stone)) {
            continue;
        }
        const CliffCache& cache = cliffCacheFor(layer.brush);
        const CliffFsParams* shading = layer.shadingOverride ? layer.shadingOverride : cliffShading;
        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID || shading == nullptr) {
            continue;
        }
        // The shadow lookup and the skirt need this layer's own height scale
        // and sink, which the palette struct does not know about.
        CliffFsParams fs = *shading;
        fs.params3[1] = isoHeightToWorld(halfW, halfH, cache.heightScale);
        fs.params5[3] = cache.sink;

        sg_bindings bind{};
        bind.vertex_buffers[0] = cache.vbuf;
        bind.views[0] = m_topTexView;
        bind.samplers[0] = m_topTexSampler;
        bind.views[1] = shadowView;
        bind.samplers[1] = m_shadowSampler;

        sg_apply_pipeline(m_cliffPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
        sg_apply_uniforms(2, sg_range{&stitch, kStitchCoreBytes});
        sg_draw(0, static_cast<int>(cache.stream.size()), 1);
    }

    if (!lineVerts.empty()) {
        sg_bindings bind{};
        bind.vertex_buffers[0] = m_colorVbuf;

        sg_apply_pipeline(m_colorPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(0, static_cast<int>(lineVerts.size()), 1);
    }
}

void AtlasRenderer::syncCliffCaches(const SceneFrame& frame) {
    static const ScreeParams kNoScree{false, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    for (int li = 0; li < frame.layerCount; ++li) {
        const PaintLayerView& layer = frame.layers[li];
        if (!layer.brush || (!layer.cliff && !layer.stone)) {
            continue;
        }
        if ((layer.cliff && !layer.cliffParams) || (layer.stone && !layer.stoneParams)) {
            continue;
        }
        CliffCache& cache = cliffCacheFor(layer.brush);
        const ScreeParams& scree = layer.scree ? *layer.scree : kNoScree;
        const bool paramsChanged = layer.stone
            ? std::memcmp(&cache.stoneParams, layer.stoneParams, sizeof(stone_gen::StoneFieldParams)) != 0
            : std::memcmp(&cache.params, layer.cliffParams, sizeof(cliff::FieldParams)) != 0;
        const bool contentChanged = !cache.contentValid ||
            cache.brushVersion != layer.brush->version() ||
            cache.stone != layer.stone || paramsChanged;
        // Height, sink and the scree ring only need the cheap re-projection.
        const bool streamChanged = cache.heightScale != layer.cliffHeightScale ||
            cache.sink != layer.sink || !sameScree(cache.scree, scree);
        if (contentChanged || streamChanged) {
            cache.brushVersion = layer.brush->version();
            cache.stone = layer.stone;
            if (layer.stone) {
                std::memcpy(&cache.stoneParams, layer.stoneParams, sizeof(stone_gen::StoneFieldParams));
            } else {
                std::memcpy(&cache.params, layer.cliffParams, sizeof(cliff::FieldParams));
            }
            cache.heightScale = layer.cliffHeightScale;
            cache.sink = layer.sink;
            cache.scree = scree;
            cache.contentValid = true;
            cache.lastEditSec = frame.nowSec;
            cache.meshDirty = cache.meshDirty || contentChanged;
            cache.streamDirty = true;
            cache.stats.pending = true;
        }
        if ((cache.meshDirty || cache.streamDirty) && (frame.nowSec - cache.lastEditSec) > 0.3) {
            rebuildCliffCache(cache, *frame.iso);
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
    }
}

void AtlasRenderer::refreshAoField(const SceneFrame& frame) {
    // Only the layers that actually produce height cast contact AO.
    const LandBrush* brushes[8] = {};
    int count = 0;
    std::uint64_t key = 1;
    for (int li = 0; li < frame.layerCount && count < 8; ++li) {
        const PaintLayerView& layer = frame.layers[li];
        if (!layer.brush || (!layer.cliff && !layer.stone)) {
            continue;
        }
        brushes[count++] = layer.brush;
        key = key * 1000003u + layer.brush->version() + 1u;
    }
    if (key == m_aoKey) {
        return;
    }
    m_aoKey = key;
    m_aoField = buildContactAoField(brushes, count, kAoTexelsPerCell, kAoMarginCells);

    if (m_aoView.id != SG_INVALID_ID) {
        sg_destroy_view(m_aoView);
        m_aoView = {};
    }
    if (m_aoImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_aoImage);
        m_aoImage = {};
    }

    const std::uint8_t far = 255;
    sg_image_desc desc = {};
    desc.pixel_format = SG_PIXELFORMAT_R8;
    if (m_aoField.empty()) {
        desc.width = 1;
        desc.height = 1;
        desc.data.mip_levels[0].ptr = &far;
        desc.data.mip_levels[0].size = sizeof(far);
        desc.label = "tileshape-ao-placeholder";
    } else {
        desc.width = m_aoField.width;
        desc.height = m_aoField.height;
        desc.data.mip_levels[0].ptr = m_aoField.texels.data();
        desc.data.mip_levels[0].size = m_aoField.texels.size();
        desc.label = "tileshape-ao-field";
    }
    m_aoImage = sg_make_image(&desc);
    sg_view_desc viewDesc = {};
    viewDesc.texture.image = m_aoImage;
    m_aoView = sg_make_view(&viewDesc);
}

void AtlasRenderer::renderShadowMap(const SceneFrame& frame) {
    // Reset first: an invalid basis is what tells buildStitchParams to keep
    // everything lit, so a disabled/empty frame cannot reuse a stale map.
    m_sunBasis = SunBasis{};
    if (!m_shadow.valid || m_shadowPip.id == SG_INVALID_ID || !frame.stitch ||
        !frame.stitch->shadowsEnabled) {
        return;
    }

    const glm::vec2 cellSz = frame.iso->dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    int mapW = 0;
    int mapH = 0;
    float maxWorldY = 0.0f;
    bool anyGeometry = false;
    for (int li = 0; li < frame.layerCount; ++li) {
        const PaintLayerView& layer = frame.layers[li];
        if (!layer.brush) {
            continue;
        }
        mapW = std::max(mapW, layer.brush->width());
        mapH = std::max(mapH, layer.brush->height());
        if (!layer.cliff && !layer.stone) {
            continue;
        }
        const CliffCache& cache = cliffCacheFor(layer.brush);
        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID) {
            continue;
        }
        anyGeometry = true;
        maxWorldY = std::max(
            maxWorldY, cache.maxHeight * isoHeightToWorld(halfW, halfH, cache.heightScale));
    }
    if (!anyGeometry) {
        return;
    }

    // Light volume: the map bbox with a margin (walls lean past the border),
    // up to the tallest mesh in world units.
    constexpr float kBoxMargin = 3.0f;
    const glm::vec3 boxMin{-kBoxMargin, 0.0f, -kBoxMargin};
    const glm::vec3 boxMax{
        static_cast<float>(mapW) + kBoxMargin,
        std::max(maxWorldY, 1.0f),
        static_cast<float>(mapH) + kBoxMargin};
    m_sunBasis = buildSunBasis(frame.stitch->sunDirection(), boxMin, boxMax);
    if (!m_sunBasis.valid) {
        return;
    }

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {1.0f, 1.0f, 1.0f, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.attachments.colors[0] = m_shadow.colorAttachment;
    pass.attachments.depth_stencil = m_shadow.depthAttachment;
    pass.label = "tileshape-shadow-pass";
    sg_begin_pass(&pass);
    sg_apply_pipeline(m_shadowPip);
    for (int li = 0; li < frame.layerCount; ++li) {
        const PaintLayerView& layer = frame.layers[li];
        if (!layer.brush || (!layer.cliff && !layer.stone)) {
            continue;
        }
        const CliffCache& cache = cliffCacheFor(layer.brush);
        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID) {
            continue;
        }
        ShadowVsParams vs{};
        std::memcpy(vs.row0, &m_sunBasis.row0, sizeof(vs.row0));
        std::memcpy(vs.row1, &m_sunBasis.row1, sizeof(vs.row1));
        std::memcpy(vs.row2, &m_sunBasis.row2, sizeof(vs.row2));
        vs.heightToWorld[0] = isoHeightToWorld(halfW, halfH, cache.heightScale);
        vs.heightToWorld[1] = cache.sink;

        sg_bindings bind{};
        bind.vertex_buffers[0] = cache.vbuf;
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
        sg_draw(0, static_cast<int>(cache.stream.size()), 1);
    }
    sg_end_pass();
}

SceneStitchParams AtlasRenderer::buildStitchParams(const SceneFrame& frame) const {
    static const SceneStitchSettings kDefaults{};
    const SceneStitchSettings& s = frame.stitch ? *frame.stitch : kDefaults;

    SceneStitchParams p{};
    const glm::vec3 sun = s.sunDirection();
    p.sunDir[0] = sun.x;
    p.sunDir[1] = sun.y;
    p.sunDir[2] = sun.z;
    std::memcpy(p.shadowRow0, &m_sunBasis.row0, sizeof(p.shadowRow0));
    std::memcpy(p.shadowRow1, &m_sunBasis.row1, sizeof(p.shadowRow1));
    std::memcpy(p.shadowRow2, &m_sunBasis.row2, sizeof(p.shadowRow2));

    if (m_aoField.empty()) {
        // 1x1 "far" placeholder: any uv clamps to "nothing nearby".
        p.aoRect[2] = 1.0f;
        p.aoRect[3] = 1.0f;
    } else {
        p.aoRect[0] = m_aoField.originX;
        p.aoRect[1] = m_aoField.originZ;
        p.aoRect[2] = 1.0f / m_aoField.extentX();
        p.aoRect[3] = 1.0f / m_aoField.extentZ();
    }

    // The ground half of the block; the cliff pass keeps its own palette
    // ambient/diffuse/gamma and only shares the shadow terms.
    p.params0[0] = s.groundLit ? s.ambient : 1.0f;
    p.params0[1] = s.groundLit ? s.diffuse : 0.0f;
    p.params0[2] = s.groundLit ? s.gamma : 1.0f;
    p.params0[3] = (s.shadowsEnabled && m_sunBasis.valid) ? s.shadowStrength : 0.0f;
    p.params1[0] = s.shadowBias;
    p.params1[1] = m_shadowTexel;
    p.params1[2] = s.aoEnabled ? s.aoStrength : 0.0f;
    p.params1[3] = s.aoRadius;
    return p;
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
                // StoneCubePlayground voronoi stones over the same slab:
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
    cache.maxHeight = 0.0f;
    for (const std::uint32_t index : cache.mesh.indices) {
        const cliff::MeshVertex& v = cache.mesh.vertices[index];
        const float mapX = static_cast<float>(cache.origin.x) + v.px;
        const float mapZ = static_cast<float>(cache.origin.y) + v.pz;
        const float fieldX = (mapX - mapZ) * halfW + halfW;
        const float fieldY = (mapX + mapZ) * halfH;
        cache.maxHeight = std::max(cache.maxHeight, v.py);
        // The sink pushes the mesh below the ground plane so its base stops
        // reading as a shape parked on top of the grass.
        cache.stream.push_back(CliffVertex{
            fieldX, fieldY - (v.py - cache.sink) * cache.heightScale, bakedDepth(fieldY),
            v.nx, v.ny, v.nz, v.groove,
            mapX, v.py, mapZ, v.rim});
    }
    appendScreeRing(cache, iso);
    cache.stats.rebuildMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void AtlasRenderer::appendScreeRing(CliffCache& cache, const topology_core::DiamondIsometry& iso) {
    cache.stats.screeCount = 0;
    const ScreeParams& s = cache.scree;
    if (!s.enabled || s.perCell <= 0 || s.band <= 0.0f || !cache.brush || cache.mesh.indices.empty()) {
        return;
    }

    // Distance to this layer's own footprint tells us where the ring goes and
    // how big the boulders get; the same transform the ground AO uses.
    const LandBrush* brush = cache.brush;
    const ContactAoField field = buildContactAoField(&brush, 1, kAoTexelsPerCell, kAoMarginCells);
    if (field.empty()) {
        return;
    }

    // Octahedron subdivided once: 32 triangles is enough for a pebble at this
    // zoom and keeps a few hundred of them cheap.
    static const std::vector<glm::vec3> kUnitTris = [] {
        const glm::vec3 poles[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        const int faces[8][3] = {
            {0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4},
            {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
        std::vector<glm::vec3> out;
        out.reserve(8 * 4 * 3);
        for (const auto& f : faces) {
            const glm::vec3 a = poles[f[0]];
            const glm::vec3 b = poles[f[1]];
            const glm::vec3 c = poles[f[2]];
            const glm::vec3 ab = glm::normalize(a + b);
            const glm::vec3 bc = glm::normalize(b + c);
            const glm::vec3 ca = glm::normalize(c + a);
            const glm::vec3 tris[4][3] = {{a, ab, ca}, {ab, b, bc}, {ca, bc, c}, {ab, bc, ca}};
            for (const auto& t : tris) {
                out.push_back(t[0]);
                out.push_back(t[1]);
                out.push_back(t[2]);
            }
        }
        return out;
    }();

    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    const std::uint32_t seed = static_cast<std::uint32_t>(s.seed * 977.0f) + 1u;

    const int minCell = -kAoMarginCells;
    const int maxCellX = brush->width() + kAoMarginCells;
    const int maxCellZ = brush->height() + kAoMarginCells;
    for (int cz = minCell; cz < maxCellZ; ++cz) {
        for (int cx = minCell; cx < maxCellX; ++cx) {
            for (int k = 0; k < s.perCell; ++k) {
                const std::uint32_t hx = static_cast<std::uint32_t>(cx + 4096);
                const std::uint32_t hz = static_cast<std::uint32_t>(cz + 4096);
                const float jx = hash01(hx, hz, seed + static_cast<std::uint32_t>(k) * 7u);
                const float jz = hash01(hz, hx, seed + static_cast<std::uint32_t>(k) * 13u + 5u);
                const float pick = hash01(hx * 3u + 1u, hz * 5u + 2u, seed + static_cast<std::uint32_t>(k));
                const float wx = static_cast<float>(cx) + jx;
                const float wz = static_cast<float>(cz) + jz;
                const float dist = field.distanceAt(wx, wz);
                if (dist <= 0.0f || dist >= s.band) {
                    continue;
                }
                // Thin the ring out towards its outer edge, and shrink the
                // boulders with it: a hard rim of debris looks like a fence.
                const float falloff = 1.0f - dist / s.band;
                if (pick > falloff) {
                    continue;
                }
                const float radius = s.sizeMin + (s.sizeMax - s.sizeMin) * falloff *
                    hash01(hx, hz, seed + static_cast<std::uint32_t>(k) * 31u + 11u);
                if (radius <= 1e-3f) {
                    continue;
                }
                // Height units, not cells: the mesh's y lives in scalar-field
                // units and the projection scales it by heightScale.
                const float toHeight = halfW / std::max(cache.heightScale, 1e-3f);
                const float squash = 0.65f;
                const float centerY = cache.sink + radius * (1.0f - 2.0f * s.buried) * toHeight * squash;

                // The boulder is an ellipsoid in field space, so its normal is
                // the gradient there: y divided by the y radius ratio.
                const float nyScale = 1.0f / std::max(squash * toHeight, 1e-3f);
                for (const glm::vec3& u : kUnitTris) {
                    const float mapX = wx + u.x * radius;
                    const float mapZ = wz + u.z * radius;
                    const float py = centerY + u.y * radius * squash * toHeight;
                    const float fieldX = (mapX - mapZ) * halfW + halfW;
                    const float fieldY = (mapX + mapZ) * halfH;
                    cache.stream.push_back(CliffVertex{
                        fieldX, fieldY - (py - cache.sink) * cache.heightScale, bakedDepth(fieldY),
                        u.x, u.y * nyScale, u.z,
                        -1.0f, // scree marker: keeps the wall palette in the FS
                        mapX, py, mapZ, 0.0f});
                }
                cache.stats.screeCount++;
            }
        }
    }
}

glm::vec3 SceneStitchSettings::sunDirection() const {
    const float ce = std::cos(lightElevation);
    return glm::normalize(glm::vec3{
        ce * std::sin(lightAzimuth), std::sin(lightElevation), ce * std::cos(lightAzimuth)});
}

void AtlasRenderer::ensurePipelines() {
    if (m_texPip.id != SG_INVALID_ID) {
        return;
    }

    ensureShadowTarget();

    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        const std::string texFs = std::string(kTexFsHeadHlsl) + kStitchFnHlsl + kTexFsBodyHlsl;
        shd.vertex_func.source = kTexVsHlsl;
        shd.fragment_func.source = texFs.c_str();
        for (int i = 0; i < 3; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
#elif defined(SOKOL_METAL)
        const std::string texFs =
            std::string(kTexFsHeadMsl) + kStitchStructAoMsl + kStitchFnMsl + kTexFsBodyMsl;
        shd.vertex_func.source = kTexVsMsl;
        shd.fragment_func.source = texFs.c_str();
#else
        const std::string texFs = std::string("#version 330\n") + kStitchCoreGlsl + kStitchAoGlsl +
            kStitchFnGlsl + kTexFsBodyGlsl;
        shd.vertex_func.source = kTexVsGlsl;
        shd.fragment_func.source = texFs.c_str();
#endif
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        fillStitchUniformDesc(&shd.uniform_blocks[1], 1, 0, true);
        fillTextureSlot(&shd, 0, "atlas_tex", true);
        fillTextureSlot(&shd, 1, "ao_tex", true);
        fillTextureSlot(&shd, 2, "shadow_tex", false);

        m_texShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_texShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        // The ground joins the z-buffer, but at ONE constant depth behind
        // everything else: tiles keep resolving against each other by painter
        // order (they all tie under LESS_EQUAL) and can never eat into the
        // highground that stands on them.
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
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
    }

    // Cliff pass (scalar-field surface nets): baked depth + field-space
    // normal/groove/world attributes, per-pixel omphalos palette.
    {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        const std::string cliffFs =
            std::string(kCliffFsHeadHlsl) + kStitchFnHlsl + kCliffFsBodyHlsl;
        shd.vertex_func.source = kCliffVsHlsl;
        shd.fragment_func.source = cliffFs.c_str();
#elif defined(SOKOL_METAL)
        const std::string cliffFs =
            std::string(kCliffFsHeadMsl) + kStitchStructCoreMsl + kStitchFnMsl + kCliffFsBodyMsl;
        shd.vertex_func.source = kCliffVsMsl;
        shd.fragment_func.source = cliffFs.c_str();
#else
        const std::string cliffFs = std::string("#version 330\n") + kCliffFsHeadGlsl +
            kStitchCoreGlsl + kStitchFnGlsl + kCliffFsBodyGlsl;
        shd.vertex_func.source = kCliffVsGlsl;
        shd.fragment_func.source = cliffFs.c_str();
#endif
        for (int i = 0; i < 5; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
        fillVsUniformDesc(&shd.uniform_blocks[0]);
        fillCliffFsUniformDesc(&shd.uniform_blocks[1]);
        fillStitchUniformDesc(&shd.uniform_blocks[2], 2, 1, false);
        fillTextureSlot(&shd, 0, "top_tex", true);
        fillTextureSlot(&shd, 1, "shadow_tex", false);

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

    // Shadow pass: same vertex stream, projected into the sun's frame.
    if (m_shadow.valid) {
        sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
        shd.vertex_func.source = kShadowVsHlsl;
        shd.fragment_func.source = kShadowFsHlsl;
#elif defined(SOKOL_METAL)
        shd.vertex_func.source = kShadowVsMsl;
        shd.fragment_func.source = kShadowFsMsl;
#else
        shd.vertex_func.source = kShadowVsGlsl;
        shd.fragment_func.source = kShadowFsGlsl;
#endif
        for (int i = 0; i < 5; ++i) {
            shd.attrs[i].hlsl_sem_name = "TEXCOORD";
            shd.attrs[i].hlsl_sem_index = i;
        }
        fillShadowVsUniformDesc(&shd.uniform_blocks[0]);
        m_shadowShd = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_shadowShd;
        pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;
        pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3;
        pip.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip.depth.write_enabled = true;
        pip.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
        pip.colors[0].pixel_format = m_shadow.colorFormat;
        pip.color_count = 1;
        pip.label = "tileshape-shadow-pip";
        m_shadowPip = sg_make_pipeline(&pip);
    }
}

void AtlasRenderer::ensureShadowTarget() {
    if (m_shadow.size != 0) {
        return;
    }

    // Depth rides in a float COLOR attachment: this sokol build has no
    // depth-texture sampling path anywhere in the repo, and R32F is the
    // cheapest renderable float format (RGBA16F is the portable fallback).
    sg_pixel_format format = SG_PIXELFORMAT_R32F;
    if (!sg_query_pixelformat(format).render) {
        format = SG_PIXELFORMAT_RGBA16F;
    }
    if (!sg_query_pixelformat(format).render) {
        spdlog::warn("AtlasRenderer: no renderable float format, shadows disabled");
        m_shadow.size = -1;
        return;
    }

    constexpr int kShadowSize = 1024;
    m_shadow.colorFormat = format;
    m_shadow.size = kShadowSize;

    sg_image_desc colorDesc = {};
    colorDesc.usage.color_attachment = true;
    colorDesc.width = kShadowSize;
    colorDesc.height = kShadowSize;
    colorDesc.pixel_format = format;
    colorDesc.label = "tileshape-shadow-color";
    m_shadow.colorImage = sg_make_image(&colorDesc);

    sg_image_desc depthDesc = {};
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.width = kShadowSize;
    depthDesc.height = kShadowSize;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH;
    depthDesc.label = "tileshape-shadow-depth";
    m_shadow.depthImage = sg_make_image(&depthDesc);

    sg_view_desc colorAtt = {};
    colorAtt.color_attachment.image = m_shadow.colorImage;
    colorAtt.label = "tileshape-shadow-color-att";
    m_shadow.colorAttachment = sg_make_view(&colorAtt);

    sg_view_desc depthAtt = {};
    depthAtt.depth_stencil_attachment.image = m_shadow.depthImage;
    depthAtt.label = "tileshape-shadow-depth-att";
    m_shadow.depthAttachment = sg_make_view(&depthAtt);

    sg_view_desc colorTex = {};
    colorTex.texture.image = m_shadow.colorImage;
    colorTex.label = "tileshape-shadow-tex";
    m_shadow.colorTexture = sg_make_view(&colorTex);

    m_shadow.valid = m_shadow.colorAttachment.id != SG_INVALID_ID &&
        m_shadow.depthAttachment.id != SG_INVALID_ID &&
        m_shadow.colorTexture.id != SG_INVALID_ID;
    m_shadowTexel = 1.0f / static_cast<float>(kShadowSize);
    if (!m_shadow.valid) {
        spdlog::warn("AtlasRenderer: shadow target creation failed, shadows disabled");
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
    if (m_cliffPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_cliffPip);
        m_cliffPip = {};
    }
    if (m_shadowPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_shadowPip);
        m_shadowPip = {};
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
    if (m_shadowShd.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shadowShd);
        m_shadowShd = {};
    }
}
