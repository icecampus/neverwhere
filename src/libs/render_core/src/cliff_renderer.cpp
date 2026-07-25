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
// Shaders (ported from TileShapePlayground's AtlasRenderer cliff pass; the VS
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
    vec2 t = mix(hashv2v2(ip), hashv2v2(ip + vec2(1.0, 0.0), chash)), fp.y);
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
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    vec3 grass = mix(grass_a.rgb, grass_b.rgb, gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
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

static const char* cliff_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
    float _pad0;
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
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: unused
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
    return frac(sin(float2(dot(p, chash), dot(p + vec2(1.0, 0.0), chash))) * 43758.54);
}

float noisefv2(float2 p) {
    float2 ip = floor(p);
    float2 fp = frac(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    float2 t = lerp(hashv2v2(ip), hashv2v2(ip + float2(1.0, 0.0), chash)), fp.y);
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
    // Grassy flat tops (omphalos idObj==2 style).
    float gm = smoothstep(0.4, 0.6, fbm2(2.0 * p.xz));
    float3 grass = lerp(grass_a.rgb, grass_b.rgb, gm);
    float topMask = smoothstep(0.7, 0.9, n.y);
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

static const char* cliff_vs_src_msl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
    float camera_zoom;
    float _pad0;
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
    float4 params1; // x: spec power, y: gamma, z: wrap backlight, w: unused
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
    return fract(sin(float2(dot(p, chash), dot(p + vec2(1.0, 0.0), chash))) * 43758.54);
}

float noisefv2(float2 p) {
    float2 ip = floor(p);
    float2 fp = fract(p);
    fp *= fp * (3.0 - 2.0 * fp);
    float2 t = mix(hashv2v2(ip), hashv2v2(ip + float2(1.0, 0.0), chash)), fp.y);
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

fragment float4 _main(PSIn in [[stage_in]], constant FsParams& fs [[buffer(1)]]) {
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
    const char* names[8] = {"light_dir", "view_dir", "dark_color", "gold_color",
        "grass_a", "grass_b", "params0", "params1"};
    for (int i = 0; i < 8; ++i) {
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
    for (auto& [uuid, cache] : caches) {
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(cache.vbuf);
            cache.vbuf = {};
        }
    }
    caches.clear();
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
}

void CliffRenderer::destroyPipeline() {
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

    for (auto& [uuid, group] : groups) {
        const CliffParams& params = assets.find(uuid)->second;
        CliffCache& cache = caches[uuid];

        // Content key: the sorted (cell, tileIndex) set + the field params.
        std::sort(group.begin(), group.end(), [](const LandscapeTile* a, const LandscapeTile* b) {
            if (a->cell.y != b->cell.y) return a->cell.y < b->cell.y;
            if (a->cell.x != b->cell.x) return a->cell.x < b->cell.x;
            return a->tileIndex < b->tileIndex;
        });
        std::uint64_t hash = hashFieldParams(params.field);
        for (const LandscapeTile* t : group) {
            hashCombine(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(t->cell.x)));
            hashCombine(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(t->cell.y)));
            hashCombine(hash, static_cast<std::uint64_t>(t->tileIndex));
        }

        const bool contentChanged = !cache.contentValid || cache.contentHash != hash;
        const bool scaleChanged = cache.heightScale != params.heightScale;
        if (contentChanged || scaleChanged) {
            cache.contentHash = hash;
            cache.heightScale = params.heightScale;
            cache.fieldParams = params.field;
            cache.contentValid = true;
            cache.lastEditSec = nowSec;
            cache.meshDirty = cache.meshDirty || contentChanged;
            cache.streamDirty = true;
        }
        if ((cache.meshDirty || cache.streamDirty) && (nowSec - cache.lastEditSec) > 0.3) {
            rebuildCliffCache(cache, group, iso);
            cache.meshDirty = false;
            cache.streamDirty = false;
            cache.gpuDirty = true;
        }
        if (cache.gpuDirty) {
            const std::size_t bytes = cache.stream.size() * sizeof(CliffVertex);
            if (bytes > 0) {
                if (cache.vbufSize < bytes) {
                    if (cache.vbuf.id != SG_INVALID_ID) {
                        sg_destroy_buffer(cache.vbuf);
                    }
                    sg_buffer_desc buf_desc = {};
                    buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
                    buf_desc.usage.dynamic_update = true;
                    buf_desc.label = "render-core-cliff-vbuf";
                    cache.vbuf = sg_make_buffer(&buf_desc);
                    cache.vbufSize = buf_desc.size;
                }
                if (cache.vbuf.id != SG_INVALID_ID) {
                    sg_update_buffer(cache.vbuf, sg_range{cache.stream.data(), bytes});
                }
            }
            cache.gpuDirty = false;
        }
        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID) continue;

        // Shading uniforms from the *current* params — palette edits are
        // instant and never touch the cache.
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

        sg_bindings bind = {};
        bind.vertex_buffers[0] = cache.vbuf;
        sg_apply_pipeline(pip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
        sg_apply_uniforms(1, sg_range{&fs, sizeof(fs)});
        sg_draw(0, static_cast<int>(cache.stream.size()), 1);
    }
}

void CliffRenderer::rebuildCliffCache(
    CliffCache& cache,
    const std::vector<const LandscapeTile*>& group,
    const topology_core::DiamondIsometry& iso) {

    if (cache.meshDirty) {
        // Full path: tiles -> corner nodes -> bbox grid -> scalar field ->
        // regularize -> surface nets (mirrors the playground's rebuild).
        std::unordered_set<std::uint64_t> seen;
        std::vector<glm::ivec2> onNodes;
        for (const LandscapeTile* t : group) {
            const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t->tileIndex));
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
            for (int i = 0; i < 4; ++i) {
                if (mask[i] && seen.insert(nodeKey(corners[i])).second) {
                    onNodes.push_back(corners[i]);
                }
            }
        }

        if (onNodes.empty()) {
            cache.mesh = {};
            cache.watertight = true;
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
            // border — the brush keeps border nodes off).
            minX -= 1;
            minY -= 1;
            maxX += 1;
            maxY += 1;
            const int nodesX = maxX - minX + 1;
            const int nodesY = maxY - minY + 1;
            std::vector<std::uint8_t> nodes(static_cast<std::size_t>(nodesX) * nodesY, 0);
            for (const glm::ivec2& n : onNodes) {
                nodes[static_cast<std::size_t>(n.y - minY) * nodesX + (n.x - minX)] = 1;
            }

            cliff::CliffField field(cache.fieldParams, nodes.data(), nodesX, nodesY);
            std::vector<float> samples;
            field.sample(samples);
            cliff::RegularizeStats regStats;
            cliff::regularizeSigns(field, samples, &regStats);
            cache.mesh = cliff::extractSurfaceNets(field, samples, nullptr);
            const cliff::WatertightReport report = cliff::checkWatertight(cache.mesh);
            cache.watertight = report.ok();
            cache.origin = {minX, minY};
            if (!report.ok()) {
                spdlog::warn("CliffRenderer: cliff mesh not watertight ({} bad of {} edges, {} saddles left)",
                    report.badEdges, report.undirectedEdges, regStats.remaining);
            }
        }
    }

    // Projection path (also after a full rebuild): mesh -> field-space vertex
    // stream. Placement matches DiamondIsometry::nodeToField (no +halfH on y),
    // z carries the raw ground fieldY (normalized in the VS via z_range).
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
            {fieldX, fieldY - v.py * cache.heightScale, fieldY},
            {v.nx, v.ny, v.nz},
            v.groove,
            {mapX, v.py, mapZ}});
    }
}

} // namespace render_core
