#include "render_core/cliff_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "atlas_tile_types.h"

namespace render_core {

namespace {

// ---------------------------------------------------------------------------
// Shaders (ported from SDFGeneratedLandscape's AtlasRenderer cliff pass; the VS
// additionally applies the camera and normalizes z from the raw ground fieldY
// via z_range — the raised-pass depth convention).
// ---------------------------------------------------------------------------

static const char* cliff_vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in float groove;
layout(location=3) in vec3 world;
out vec3 v_normal;
out float v_groove;
out vec3 v_world;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform vec2 z_range;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, (z_range.x - pos.z) * z_range.y, 1.0);
    v_normal = normal;
    v_groove = groove;
    v_world = world;
}
)";

static const char* cliff_fs_src_glsl = R"(
#version 330
in vec3 v_normal;
in float v_groove;
in vec3 v_world;
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
uniform vec4 params3;
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
    for (int j = 0; j < 5; j++) {
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
    // Flat tops: tiled texture (slight uv wobble against visible tiling) or
    // the procedural grassA/grassB mix when the asset has no top texture.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    vec2 tuv = p.xz * params1.w + 0.06 * vec2(fbm2(3.1 * p.xz), fbm2(2.7 * p.zx + 5.0));
    vec3 texCol = texture(top_tex, tuv).rgb;
    vec3 grass = (params2.w > 0.5) ? texCol * (0.85 + 0.3 * gm) : mix(grass_a.rgb, grass_b.rgb, gm);
    // Noise-distorted rim: the top creeps down the wall irregularly.
    float rim = 0.22 * fbm2(6.0 * p.xz);
    float topMask = smoothstep(0.7 - rim, 0.9 - rim, n.y);
    vec3 base = mix(rock, grass, topMask);
    // Sediment strata bands on the walls.
    base *= 1.0 - params3.x * (1.0 - topMask) * (0.5 + 0.5 * sin(p.y * 40.0 + 3.0 * fbm3(8.0 * p)));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    vec3 l = normalize(light_dir.xyz);
    vec3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    vec3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, params0.w, shell) * (1.0 - topMask);
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    // Bottom blend: darken + a faint soil-green cast, so the wall foot merges
    // with the underlay instead of hanging in the void.
    float hf = clamp(p.y / max(params2.z, 0.001), 0.0, 1.0);
    float bf = smoothstep(0.0, params2.y, hf);
    col *= mix(1.0 - params2.x, 1.0, bf);
    col = mix(col * vec3(0.85, 1.0, 0.8), col, bf);
    frag_color = vec4(pow(clamp(col, 0.0, 1.0), vec3(params1.y)), 1.0);
}
)";

static const char* cliff_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal: TEXCOORD1;
    float groove: TEXCOORD2;
    float3 world: TEXCOORD3;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal: TEXCOORD0;
    float groove: TEXCOORD1;
    float3 world: TEXCOORD2;
};
VSOut main(VSIn inp) {
    VSOut o;
    float2 screen = (inp.pos.xy * camera_zoom) + camera_offset;
    float2 clip;
    clip.x = (screen.x / view_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / view_size.y) * 2.0;
    o.pos = float4(clip, (z_range.x - inp.pos.z) * z_range.y, 1.0);
    o.normal = inp.normal;
    o.groove = inp.groove;
    o.world = inp.world;
    return o;
}
)";

static const char* cliff_fs_src_hlsl = R"(
cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 view_dir;
    float4 dark_color;
    float4 gold_color;
    float4 grass_a;
    float4 grass_b;
    float4 params0; // x: vein threshold, y: ambient, z: diffuse, w: spec strength
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: tex scale
    float4 params2; // x: bottom darken, y: bottom band, z: plateau top, w: use texture
    float4 params3; // x: strata strength
};
Texture2D top_tex: register(t0);
SamplerState top_tex_smp: register(s0);

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
};

float4 main(PSIn inp): SV_Target {
    float3 n = normalize(inp.normal);
    float3 p = inp.world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, inp.groove);
    float3 gold = gold_color.rgb + float3(1.0, 0.9, 0.4) * step(params0.x, f);
    float3 rock = lerp(dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Flat tops: tiled texture (uv wobble) or the procedural grass mix.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float2 tuv = p.xz * params1.w + 0.06 * float2(fbm2(3.1 * p.xz), fbm2(2.7 * p.zx + 5.0));
    float3 texCol = top_tex.Sample(top_tex_smp, tuv).rgb;
    float3 grass = (params2.w > 0.5) ? texCol * (0.85 + 0.3 * gm) : lerp(grass_a.rgb, grass_b.rgb, gm);
    // Noise-distorted rim: the top creeps down the wall irregularly.
    float rim = 0.22 * fbm2(6.0 * p.xz);
    float topMask = smoothstep(0.7 - rim, 0.9 - rim, n.y);
    float3 base = lerp(rock, grass, topMask);
    // Sediment strata bands on the walls.
    base *= 1.0 - params3.x * (1.0 - topMask) * (0.5 + 0.5 * sin(p.y * 40.0 + 3.0 * fbm3(8.0 * p)));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(light_dir.xyz);
    float3 rd = view_dir.xyz;
    float ndl = dot(n, l);
    float3 col = base * (params0.y + params1.z * max(-ndl, 0.0) +
        params0.z * max(ndl, 0.0));
    float specAmt = lerp(0.05, params0.w, shell) * (1.0 - topMask);
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), params1.x);
    // Bottom blend: darken + a faint soil-green cast toward the underlay.
    float hf = clamp(p.y / max(params2.z, 0.001), 0.0, 1.0);
    float bf = smoothstep(0.0, params2.y, hf);
    col *= lerp(1.0 - params2.x, 1.0, bf);
    col = lerp(col * float3(0.85, 1.0, 0.8), col, bf);
    return float4(pow(clamp(col, 0.0, 1.0), (float3)params1.y), 1.0);
}
)";

static const char* cliff_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
};

struct VSIn {
    float3 pos [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float groove [[attribute(2)]];
    float3 world [[attribute(3)]];
};

struct VSOut {
    float4 pos [[position]];
    float3 normal;
    float groove;
    float3 world;
};

vertex VSOut _main(VSIn in [[stage_in]], constant VsParams& params [[buffer(0)]]) {
    VSOut o;
    float2 screen = (in.pos.xy * params.camera_zoom) + params.camera_offset;
    float2 clip = float2(
        (screen.x / params.view_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / params.view_size.y) * 2.0
    );
    o.pos = float4(clip, (params.z_range.x - in.pos.z) * params.z_range.y, 1.0);
    o.normal = in.normal;
    o.groove = in.groove;
    o.world = in.world;
    return o;
}
)";

static const char* cliff_fs_src_msl = R"(
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
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: tex scale
    float4 params2; // x: bottom darken, y: bottom band, z: plateau top, w: use texture
    float4 params3; // x: strata strength
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
    fp *= fp * (3.0 - 2.0 * fp);
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
};

fragment float4 _main(PSIn in [[stage_in]], constant FsParams& fs [[buffer(1)]],
                      texture2d<float> top_tex [[texture(0)]],
                      sampler top_tex_smp [[sampler(0)]]) {
    float3 n = normalize(in.normal);
    float3 p = in.world;
    // Omphalos stone palette: dark at groove floors, gold + veins on the shell.
    float f = fbm3(32.0 * p);
    float shell = 1.0 - smoothstep(0.005, 0.05, in.groove);
    float3 gold = fs.gold_color.rgb + float3(1.0, 0.9, 0.4) * step(fs.params0.x, f);
    float3 rock = mix(fs.dark_color.rgb, gold, shell) * (1.0 - 0.3 * f);
    // Flat tops: tiled texture (uv wobble) or the procedural grass mix.
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float2 tuv = p.xz * fs.params1.w + 0.06 * float2(fbm2(3.1 * p.xz), fbm2(2.7 * p.zx + 5.0));
    float3 texCol = top_tex.sample(top_tex_smp, tuv).rgb;
    float3 grass = (fs.params2.w > 0.5) ? texCol * (0.85 + 0.3 * gm) : mix(fs.grass_a.rgb, fs.grass_b.rgb, gm);
    // Noise-distorted rim: the top creeps down the wall irregularly.
    float rim = 0.22 * fbm2(6.0 * p.xz);
    float topMask = smoothstep(0.7 - rim, 0.9 - rim, n.y);
    float3 base = mix(rock, grass, topMask);
    // Sediment strata bands on the walls.
    base *= 1.0 - fs.params3.x * (1.0 - topMask) * (0.5 + 0.5 * sin(p.y * 40.0 + 3.0 * fbm3(8.0 * p)));
    // Cheap sun lambert + wrap ambient + spec; the iso view direction is constant.
    float3 l = normalize(fs.light_dir.xyz);
    float3 rd = fs.view_dir.xyz;
    float ndl = dot(n, l);
    float3 col = base * (fs.params0.y + fs.params1.z * max(-ndl, 0.0) +
        fs.params0.z * max(ndl, 0.0));
    float specAmt = mix(0.05, fs.params0.w, shell) * (1.0 - topMask);
    col += specAmt * pow(max(dot(normalize(l - rd), n), 0.0), fs.params1.x);
    // Bottom blend: darken + a faint soil-green cast toward the underlay.
    float hf = clamp(p.y / max(fs.params2.z, 0.001), 0.0, 1.0);
    float bf = smoothstep(0.0, fs.params2.y, hf);
    col *= mix(1.0 - fs.params2.x, 1.0, bf);
    col = mix(col * float3(0.85, 1.0, 0.8), col, bf);
    return float4(pow(clamp(col, 0.0, 1.0), float3(fs.params1.y)), 1.0);
}
)";

void fillVsUniformDesc(sg_shader_uniform_block* block, std::size_t size) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = size;
    block->hlsl_register_b_n = 0;
    block->msl_buffer_n = 0;
    block->wgsl_group0_binding_n = 0;
    block->spirv_set0_binding_n = 0;
    block->glsl_uniforms[0].glsl_name = "view_size";
    block->glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[1].glsl_name = "camera_offset";
    block->glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[2].glsl_name = "z_range";
    block->glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT2;
    block->glsl_uniforms[3].glsl_name = "camera_zoom";
    block->glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
}

// Cliff fragment block (palette/light): slot 1, after the vertex block.
void fillFsUniformDesc(sg_shader_uniform_block* block, std::size_t size) {
    block->stage = SG_SHADERSTAGE_FRAGMENT;
    block->size = size;
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

// FNV-1a over the content key (tiles + field params); scalar-by-scalar so
// struct padding never leaks into the key.
void hashCombine(std::uint64_t& h, std::uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
}

void hashFloat(std::uint64_t& h, float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    hashCombine(h, bits);
}

std::uint64_t hashFieldParams(const cliff::FieldParams& p) {
    std::uint64_t h = 1469598103934665603ULL;
    hashFloat(h, p.cellSize);
    hashFloat(h, p.padding);
    hashFloat(h, p.plateauHeight);
    hashFloat(h, p.d2Scale);
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.blurRadiusCells)));
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.blurPasses)));
    hashFloat(h, p.edgeRadius);
    hashFloat(h, p.grooveMaskWidth);
    hashFloat(h, p.grooveFadeK);
    hashFloat(h, p.grooveRimFade);
    hashFloat(h, p.fbmAmplitude);
    hashFloat(h, p.fbmFrequency);
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.fbmOctaves)));
    hashFloat(h, p.groundDepth);
    hashFloat(h, p.groundMargin);
    hashFloat(h, p.groundRounding);
    hashCombine(h, p.groundEnabled ? 1ULL : 0ULL);
    hashFloat(h, p.groovePeriod);
    hashFloat(h, p.groovePhase);
    hashFloat(h, p.grooveDepthMax);
    hashFloat(h, p.grooveSmooth);
    for (const auto& anglePair : p.grooveAngles) {
        hashFloat(h, anglePair[0]);
        hashFloat(h, anglePair[1]);
    }
    return h;
}

// One connected piece of an asset's cliffs: its on-nodes and the tiles
// carrying them (tiles drive the prototype silhouette while pending).
struct CliffComponent {
    std::vector<glm::ivec2> nodes;
    std::vector<const LandscapeTile*> tiles;
};

// Content key of a region: the field params + the sorted node set. The same
// node set hashes identically every frame, so an untouched region keeps its
// cache entry; a local edit changes only its own component's key.
std::uint64_t regionKey(std::vector<glm::ivec2> nodes, const cliff::FieldParams& params) {
    std::uint64_t h = hashFieldParams(params);
    std::sort(nodes.begin(), nodes.end(), [](const glm::ivec2& a, const glm::ivec2& b) {
        if (a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    });
    for (const glm::ivec2& n : nodes) {
        hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.x)));
        hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.y)));
    }
    return h;
}

// Split one asset's tiles into connected node components (8-connectivity on
// the node grid — nodes are neighbours when they share a map cell). Tiles
// attach to the component of their on corner nodes (a cell's corner nodes are
// mutually connected, so the mapping is unambiguous).
std::vector<CliffComponent> computeComponents(const std::vector<const LandscapeTile*>& group) {
    std::unordered_map<std::uint64_t, glm::ivec2> remaining;
    remaining.reserve(group.size() * 4);
    for (const LandscapeTile* t : group) {
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t->tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
        for (int i = 0; i < 4; ++i) {
            if (mask[i]) {
                remaining.emplace(nodeKey(corners[i]), corners[i]);
            }
        }
    }

    std::vector<CliffComponent> components;
    while (!remaining.empty()) {
        CliffComponent comp;
        std::vector<glm::ivec2> stack{remaining.begin()->second};
        remaining.erase(remaining.begin());
        while (!stack.empty()) {
            const glm::ivec2 node = stack.back();
            stack.pop_back();
            comp.nodes.push_back(node);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const auto it = remaining.find(nodeKey({node.x + dx, node.y + dy}));
                    if (it != remaining.end()) {
                        stack.push_back(it->second);
                        remaining.erase(it);
                    }
                }
            }
        }
        components.push_back(std::move(comp));
    }

    // Tiles -> the component of their first on corner node.
    std::unordered_map<std::uint64_t, std::size_t> nodeToComp;
    for (std::size_t ci = 0; ci < components.size(); ++ci) {
        for (const glm::ivec2& n : components[ci].nodes) {
            nodeToComp.emplace(nodeKey(n), ci);
        }
    }
    for (const LandscapeTile* t : group) {
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t->tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
        for (int i = 0; i < 4; ++i) {
            if (mask[i]) {
                components[nodeToComp[nodeKey(corners[i])]].tiles.push_back(t);
                break;
            }
        }
    }
    return components;
}

} // namespace

void CliffRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        // Surface nets have no sortable primitives — no painter fallback, the
        // pass exists only with a real depth attachment.
        ensurePipeline();
    }
}

void CliffRenderer::shutdown() {
    destroyPipeline();
    for (auto& [uuid, asset] : caches) {
        asset.topTex.destroy();
        for (auto& [key, region] : asset.regions) {
            if (region.vbuf.id != SG_INVALID_ID) {
                sg_destroy_buffer(region.vbuf);
                region.vbuf = {};
            }
        }
    }
    caches.clear();
    if (m_previewVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_previewVbuf);
        m_previewVbuf = {};
        m_previewVbufSize = 0;
    }
}

void CliffRenderer::ensureCliffAsset(const std::string& assetUuid, const CliffParams& params) {
    assets[assetUuid] = params;
}

void CliffRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // One render_core binary serves all shells — pick shader sources by the
    // active backend (same pattern as LandscapeRenderer).
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = cliff_vs_src_hlsl;
        shd_desc.fragment_func.source = cliff_fs_src_hlsl;
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = cliff_vs_src_msl;
        shd_desc.fragment_func.source = cliff_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = cliff_vs_src_glsl;
        shd_desc.fragment_func.source = cliff_fs_src_glsl;
    }
    for (int i = 0; i < 4; ++i) {
        shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[i].hlsl_sem_index = i;
    }
    fillVsUniformDesc(&shd_desc.uniform_blocks[0], sizeof(CliffVsParams));
    fillFsUniformDesc(&shd_desc.uniform_blocks[1], sizeof(CliffFsParams));
    // Top texture (slot 0, fragment stage; same triplet shape as the sprite pass).
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
    shd_desc.texture_sampler_pairs[0].glsl_name = "top_tex";
    shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3; // normal
    pip_desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT;  // groove
    pip_desc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT3; // world
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip_desc.depth.write_enabled = true;
    pip_desc.label = "render-core-cliff-pip";
    pip = sg_make_pipeline(&pip_desc);

    // 1x1 white fallback for assets without a top texture (the FS declares
    // top_tex unconditionally, so the binding must always be complete).
    const std::uint32_t white = 0xFFFFFFFF;
    sg_image_desc img_desc = {};
    img_desc.width = 1;
    img_desc.height = 1;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.data.mip_levels[0].ptr = &white;
    img_desc.data.mip_levels[0].size = sizeof(white);
    img_desc.label = "render-core-cliff-dummy-tex";
    m_dummyImage = sg_make_image(&img_desc);
    sg_view_desc view_desc = {};
    view_desc.texture.image = m_dummyImage;
    view_desc.label = "render-core-cliff-dummy-view";
    m_dummyView = sg_make_view(&view_desc);
    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.wrap_u = SG_WRAP_REPEAT;
    smp_desc.wrap_v = SG_WRAP_REPEAT;
    smp_desc.label = "render-core-cliff-dummy-smp";
    m_dummySampler = sg_make_sampler(&smp_desc);
}

void CliffRenderer::destroyPipeline() {
    if (m_dummyView.id != SG_INVALID_ID) {
        sg_destroy_view(m_dummyView);
        m_dummyView = {};
    }
    if (m_dummySampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_dummySampler);
        m_dummySampler = {};
    }
    if (m_dummyImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_dummyImage);
        m_dummyImage = {};
    }
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
    if (shd.id != SG_INVALID_ID) {
        sg_destroy_shader(shd);
        shd.id = SG_INVALID_ID;
    }
}

void CliffRenderer::render(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    double nowSec) {

    if (pip.id == SG_INVALID_ID || tiles.empty()) return;

    // Group tiles by asset uuid (only registered cliff3d assets).
    std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
    for (const LandscapeTile& t : tiles) {
        if (assets.find(t.assetUuid) == assets.end()) continue;
        groups[t.assetUuid].push_back(&t);
    }
    if (groups.empty()) return;

    // Depth range along the iso view ray (same anchor/formula as the raised
    // pass, so both passes share the depth buffer consistently).
    const float groundCenterY = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y;
    CliffVsParams vs{};
    vs.view_size[0] = static_cast<float>(viewWidth);
    vs.view_size[1] = static_cast<float>(viewHeight);
    vs.camera_offset[0] = camera.offset.x;
    vs.camera_offset[1] = camera.offset.y;
    vs.z_range[0] = groundCenterY + 100000.0f;
    vs.z_range[1] = 1.0f / 200000.0f;
    vs.camera_zoom = camera.zoom;

    // Shading uniforms from the *current* asset params — palette edits are
    // instant and never touch the caches.
    const auto buildFs = [&iso](const CliffParams& params, bool useTexture) {
        const CliffShading& s = params.shading;
        CliffFsParams fs{};
        const float lightCe = std::cos(s.lightElevation);
        fs.lightDir[0] = lightCe * std::sin(s.lightAzimuth);
        fs.lightDir[1] = std::sin(s.lightElevation);
        fs.lightDir[2] = lightCe * std::cos(s.lightAzimuth);
        // Constant iso view direction (viewer -> scene), mirrors the playground.
        const float halfH = iso.dims.cellSize().y * 0.5f;
        const float viewY = 2.0f * halfH / std::max(params.heightScale, 1.0f);
        const float viewLen = std::sqrt(2.0f + viewY * viewY);
        fs.viewDir[0] = -1.0f / viewLen;
        fs.viewDir[1] = -viewY / viewLen;
        fs.viewDir[2] = -1.0f / viewLen;
        std::memcpy(fs.darkColor, s.darkColor.data(), sizeof(s.darkColor));
        std::memcpy(fs.goldColor, s.goldColor.data(), sizeof(s.goldColor));
        std::memcpy(fs.grassA, s.grassA.data(), sizeof(s.grassA));
        std::memcpy(fs.grassB, s.grassB.data(), sizeof(s.grassB));
        fs.params0[0] = s.veinThreshold;
        fs.params0[1] = s.ambient;
        fs.params0[2] = s.diffuse;
        fs.params0[3] = s.specStrength;
        fs.params1[0] = s.specPower;
        fs.params1[1] = s.gamma;
        fs.params1[2] = s.backLight;
        fs.params1[3] = s.texScale;
        fs.params2[0] = s.bottomDarken;
        fs.params2[1] = s.bottomBand;
        fs.params2[2] = params.field.plateauHeight;
        fs.params2[3] = useTexture ? 1.0f : 0.0f;
        fs.params3[0] = s.strataStrength;
        return fs;
    };

    // Prototype silhouette for pending groups: a flat tile diamond at ground
    // level, shaded by the same FS palette (normal up, no carve -> grass top).
    const auto appendPreviewDiamond = [&iso](std::vector<CliffVertex>& out, const glm::ivec2& cell) {
        const glm::vec2 center = iso.mapToField(cell);
        const float halfW = iso.dims.cellWidth * 0.5f;
        const float halfH = iso.dims.cellSize().y * 0.5f;
        const auto v = [](float px, float py, float wx, float wz) {
            return CliffVertex{{px, py, py}, {0.0f, 1.0f, 0.0f}, 0.0f, {wx, 0.0f, wz}};
        };
        const float cx = static_cast<float>(cell.x);
        const float cy = static_cast<float>(cell.y);
        const CliffVertex vL = v(center.x - halfW, center.y, cx, cy + 0.5f);
        const CliffVertex vU = v(center.x, center.y - halfH, cx + 0.5f, cy);
        const CliffVertex vR = v(center.x + halfW, center.y, cx + 1.0f, cy + 0.5f);
        const CliffVertex vD = v(center.x, center.y + halfH, cx + 0.5f, cy + 1.0f);
        out.push_back(vL);
        out.push_back(vU);
        out.push_back(vR);
        out.push_back(vL);
        out.push_back(vR);
        out.push_back(vD);
    };

    m_previewVerts.clear();
    m_previewRanges.clear();

    for (auto& [uuid, group] : groups) {
        const CliffParams& params = assets.find(uuid)->second;
        AssetCache& asset = caches[uuid];

        // Lazy (re)load of the optional tiled top texture; the FS falls back
        // to the procedural grass mix when the asset has none (or it failed).
        if (!asset.topTexTried || asset.topTexPath != params.topTexturePath) {
            asset.topTex.destroy();
            asset.topTexPath = params.topTexturePath;
            asset.topTexTried = true;
            if (!params.topTexturePath.empty()) {
                asset.topTex.createFromFile(params.topTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
                if (!asset.topTex.valid()) {
                    spdlog::warn("CliffRenderer: top texture failed to load: {}", params.topTexturePath.string());
                }
            }
        }
        const bool useTex = asset.topTex.valid();
        const sg_view texView = useTex ? asset.topTex.sgView() : m_dummyView;
        const sg_sampler texSmp = useTex ? asset.topTex.sgSampler() : m_dummySampler;

        // Split into connected regions and key them by content: an untouched
        // region keeps its cache entry, a local edit rehashes only its own.
        const std::vector<CliffComponent> components = computeComponents(group);
        std::vector<std::uint64_t> keys;
        keys.reserve(components.size());
        std::unordered_set<std::uint64_t> liveKeys;
        for (const CliffComponent& comp : components) {
            const std::uint64_t key = regionKey(comp.nodes, params.field);
            keys.push_back(key);
            liveKeys.insert(key);
        }

        // Sweep dead regions (vbufs included) — keys that vanished by edits.
        for (auto it = asset.regions.begin(); it != asset.regions.end();) {
            if (liveKeys.find(it->first) == liveKeys.end()) {
                if (it->second.vbuf.id != SG_INVALID_ID) {
                    sg_destroy_buffer(it->second.vbuf);
                }
                it = asset.regions.erase(it);
            } else {
                ++it;
            }
        }

        // heightScale/flare edits only re-project the built regions (no field work).
        const bool scaleChanged = !asset.heightScaleValid || asset.heightScale != params.heightScale;
        const bool flareChanged = asset.flareAmount != params.flareAmount || asset.flareBand != params.flareBand;
        if (scaleChanged || flareChanged) {
            for (auto& [key, region] : asset.regions) {
                if (!region.pending) {
                    projectRegionStream(region, iso, params.heightScale, params.flareAmount,
                        params.flareBand, params.field.plateauHeight);
                    const std::size_t bytes = region.stream.size() * sizeof(CliffVertex);
                    if (bytes > 0) {
                        if (region.vbufSize < bytes) {
                            if (region.vbuf.id != SG_INVALID_ID) {
                                sg_destroy_buffer(region.vbuf);
                            }
                            sg_buffer_desc buf_desc = {};
                            buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
                            buf_desc.usage.dynamic_update = true;
                            buf_desc.label = "render-core-cliff-vbuf";
                            region.vbuf = sg_make_buffer(&buf_desc);
                            region.vbufSize = buf_desc.size;
                        }
                        if (region.vbuf.id != SG_INVALID_ID) {
                            sg_update_buffer(region.vbuf, sg_range{region.stream.data(), bytes});
                        }
                    }
                }
            }
            asset.heightScale = params.heightScale;
            asset.heightScaleValid = true;
            asset.flareAmount = params.flareAmount;
            asset.flareBand = params.flareBand;
        }

        for (std::size_t ci = 0; ci < components.size(); ++ci) {
            const CliffComponent& comp = components[ci];
            RegionCache& region = asset.regions[keys[ci]]; // pending entry on miss
            if (region.pending) {
                if (region.pendingSince < 0.0) {
                    region.pendingSince = nowSec;
                }
                if ((nowSec - region.pendingSince) > 0.3) {
                    rebuildRegion(region, comp.nodes, iso, params.field, params.heightScale,
                        params.flareAmount, params.flareBand);
                    region.pending = false;
                }
            }

            if (region.pending) {
                // Prototype silhouette of THIS region's tiles while it rebuilds.
                PreviewRange range;
                range.base = static_cast<int>(m_previewVerts.size());
                range.params = &params;
                range.texView = texView;
                range.texSampler = texSmp;
                range.useTexture = useTex;
                for (const LandscapeTile* t : comp.tiles) {
                    appendPreviewDiamond(m_previewVerts, t->cell);
                }
                range.count = static_cast<int>(m_previewVerts.size()) - range.base;
                if (range.count > 0) {
                    m_previewRanges.push_back(range);
                }
                continue;
            }

            if (region.stream.empty() || region.vbuf.id == SG_INVALID_ID) continue;

            const CliffFsParams fs = buildFs(params, useTex);

            sg_bindings bind = {};
            bind.vertex_buffers[0] = region.vbuf;
            bind.views[0] = texView;
            bind.samplers[0] = texSmp;
            sg_apply_pipeline(pip);
            sg_apply_bindings(&bind);
            sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
            sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
            sg_draw(0, static_cast<int>(region.stream.size()), 1);
        }
    }

    // Upload and draw the prototype silhouettes (one buffer update per frame).
    if (!m_previewVerts.empty()) {
        const std::size_t bytes = m_previewVerts.size() * sizeof(CliffVertex);
        if (m_previewVbufSize < bytes) {
            if (m_previewVbuf.id != SG_INVALID_ID) {
                sg_destroy_buffer(m_previewVbuf);
            }
            sg_buffer_desc buf_desc = {};
            buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
            buf_desc.usage.dynamic_update = true;
            buf_desc.label = "render-core-cliff-preview-vbuf";
            m_previewVbuf = sg_make_buffer(&buf_desc);
            m_previewVbufSize = buf_desc.size;
        }
        if (m_previewVbuf.id != SG_INVALID_ID) {
            sg_update_buffer(m_previewVbuf, sg_range{m_previewVerts.data(), bytes});
            for (const PreviewRange& range : m_previewRanges) {
                const CliffFsParams fs = buildFs(*range.params, range.useTexture);
                sg_bindings bind = {};
                bind.vertex_buffers[0] = m_previewVbuf;
                bind.views[0] = range.texView;
                bind.samplers[0] = range.texSampler;
                sg_apply_pipeline(pip);
                sg_apply_bindings(&bind);
                sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
                sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
                sg_draw(range.base, range.count, 1);
            }
        }
    }
}

void CliffRenderer::rebuildRegion(
    RegionCache& region,
    const std::vector<glm::ivec2>& componentNodes,
    const topology_core::DiamondIsometry& iso,
    const cliff::FieldParams& fieldParams,
    float heightScale,
    float flareAmount,
    float flareBand) {

    // Full path for ONE connected region: nodes -> bbox field -> regularize
    // -> surface nets (the other regions of the same asset stay untouched).
    if (!componentNodes.empty()) {
        // Field over the component bbox + a one-cell margin (the blurred
        // outline must not cross the field border).
        int minX = componentNodes[0].x;
        int minY = componentNodes[0].y;
        int maxX = componentNodes[0].x;
        int maxY = componentNodes[0].y;
        for (const glm::ivec2& n : componentNodes) {
            minX = std::min(minX, n.x);
            minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x);
            maxY = std::max(maxY, n.y);
        }
        minX -= 1;
        minY -= 1;
        maxX += 1;
        maxY += 1;
        const int nodesX = maxX - minX + 1;
        const int nodesY = maxY - minY + 1;
        std::vector<std::uint8_t> nodes(static_cast<std::size_t>(nodesX) * nodesY, 0);
        for (const glm::ivec2& n : componentNodes) {
            nodes[static_cast<std::size_t>(n.y - minY) * nodesX + (n.x - minX)] = 1;
        }

        cliff::CliffField field(fieldParams, nodes.data(), nodesX, nodesY);
        std::vector<float> samples;
        field.sample(samples);
        cliff::RegularizeStats regStats;
        cliff::regularizeSigns(field, samples, &regStats);
        region.mesh = cliff::extractSurfaceNets(field, samples, nullptr);
        region.origin = {minX, minY};
        const cliff::WatertightReport report = cliff::checkWatertight(region.mesh);
        region.watertight = report.ok();
        if (!report.ok()) {
            spdlog::warn("CliffRenderer: cliff mesh not watertight ({} bad of {} edges, {} saddles left)",
                report.badEdges, report.undirectedEdges, regStats.remaining);
        }
        spdlog::info("CliffRenderer: rebuilt region at ({}, {}) — {} nodes, {} tris",
            minX, minY, componentNodes.size(), region.mesh.indices.size() / 3);
    } else {
        region.mesh = {};
        region.watertight = true;
    }

    projectRegionStream(region, iso, heightScale, flareAmount, flareBand, fieldParams.plateauHeight);

    // Upload the fresh stream.
    const std::size_t bytes = region.stream.size() * sizeof(CliffVertex);
    if (bytes > 0) {
        if (region.vbufSize < bytes) {
            if (region.vbuf.id != SG_INVALID_ID) {
                sg_destroy_buffer(region.vbuf);
            }
            sg_buffer_desc buf_desc = {};
            buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
            buf_desc.usage.dynamic_update = true;
            buf_desc.label = "render-core-cliff-vbuf";
            region.vbuf = sg_make_buffer(&buf_desc);
            region.vbufSize = buf_desc.size;
        }
        if (region.vbuf.id != SG_INVALID_ID) {
            sg_update_buffer(region.vbuf, sg_range{region.stream.data(), bytes});
        }
    }
}

void CliffRenderer::projectRegionStream(
    RegionCache& region,
    const topology_core::DiamondIsometry& iso,
    float heightScale,
    float flareAmount,
    float flareBand,
    float plateauHeight) {

    // Projection path: region mesh -> field-space vertex stream. Placement
    // matches DiamondIsometry::nodeToField (no +halfH on y), z carries the raw
    // ground fieldY (normalized in the VS via z_range).
    //
    // Wall flare: near ground level the vertices bulge outward along the
    // horizontal normal (quadratic falloff over flareBand * plateauHeight), so
    // the wall foot overlaps the underlay instead of ending in a hard line.
    // Positions shift consistently (screen pos, ground anchor and the world
    // attribute), the mesh topology/normals stay untouched.
    region.stream.clear();
    region.stream.reserve(region.mesh.indices.size());
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    const float flareTop = std::max(flareBand, 0.0f) * plateauHeight;
    for (const std::uint32_t index : region.mesh.indices) {
        const cliff::MeshVertex& v = region.mesh.vertices[index];
        float px = v.px;
        float pz = v.pz;
        if (flareAmount > 0.0f && flareTop > 1e-6f && v.py < flareTop) {
            const float t = 1.0f - v.py / flareTop;
            const float off = flareAmount * t * t;
            const float hl = std::sqrt(v.nx * v.nx + v.nz * v.nz);
            if (hl > 1e-4f) {
                px += v.nx / hl * off;
                pz += v.nz / hl * off;
            }
        }
        const float mapX = static_cast<float>(region.origin.x) + px;
        const float mapZ = static_cast<float>(region.origin.y) + pz;
        const float fieldX = (mapX - mapZ) * halfW + halfW;
        const float fieldY = (mapX + mapZ) * halfH;
        region.stream.push_back(CliffVertex{
            {fieldX, fieldY - v.py * heightScale, fieldY},
            {v.nx, v.ny, v.nz},
            v.groove,
            {mapX, v.py, mapZ}});
    }
}

} // namespace render_core
