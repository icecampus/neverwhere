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

namespace {

// Vertex capacities of the dynamic buffers (one sg_update_buffer per buffer
// per frame, so a frame must fit into a single upload).
constexpr std::size_t kRaisedVbufVertices = 6 * 65536;
constexpr std::size_t kWallVbufVertices = 8 * 65536;

// World-space UV tiling of the raised-top ground texture: one texture repeat
// per 256 world pixels (2 cells), continuous across cells (same convention as
// MeshGenerationPlayground's production preview ground UVs).
constexpr float kTopUvPerWorldPx = 1.0f / 256.0f;

// Same packing as DiamondIsometry::zOffset.
std::uint64_t nodeKey(const glm::ivec2& node) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(node.y)) << 32)
        | static_cast<std::uint32_t>(node.x);
}

// Recover the LandscapeTileType from the stored atlas tileIndex. Mirrors
// SliceAsset::subTileTypeByIndex (Grass 4x6 slice atlas layout) — the same
// table the vertex-centric pencil wrote the index with.
landscape_core::LandscapeTileType tileTypeFromAtlasIndex(std::size_t tileIndex) {
    using T = landscape_core::LandscapeTileType;
    switch (tileIndex) {
    case 0:
    case 1:
    case 2:
    case 3:
        return T::Full;
    case 4:
        return T::DownLack;
    case 5:
        return T::LeftLack;
    case 6:
        return T::UpLack;
    case 7:
        return T::RightLack;
    case 8:
        return T::UpCorner;
    case 9:
        return T::RightCorner;
    case 10:
        return T::DownCorner;
    case 11:
        return T::LeftCorner;
    case 12:
    case 16:
        return T::RightUpLine;
    case 13:
    case 17:
        return T::RightDownLine;
    case 14:
    case 18:
        return T::LeftDownLine;
    case 15:
    case 19:
        return T::LeftUpLine;
    case 20:
        return T::UpAndDownCorners;
    case 21:
        return T::LeftRightCorners;
    default:
        return T::Unknown;
    }
}

} // namespace

void LandscapeRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();
    ensureWallPipeline();

    // Dynamic vertex buffer for many quads (6 vertices per tile)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * 65536 * (int)sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "landscape-verts";
    vbuf = sg_make_buffer(&buf_desc);

    bind.vertex_buffers[0] = vbuf;

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
    destroyPipeline();
    destroyWallPipeline();
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

void LandscapeRenderer::ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath) {
    auto it = raisedAtlases.find(assetUuid);
    if (it != raisedAtlases.end()) {
        it->second.params = params; // cheap: presentation can be tweaked at runtime
        if (!topTexturePath.empty() && !it->second.topTex.valid()) {
            it->second.topTex.createFromFile(topTexturePath, 1, 1, SG_FILTER_LINEAR, SG_WRAP_REPEAT);
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

    const float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    // 2 triangles (TL, TR, BL) (BL, TR, BR)
    out.push_back({{tl.x, tl.y}, {uv.uv0.x, uv.uv0.y}, {r, g, b, a}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {r, g, b, a}});
    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {r, g, b, a}});

    out.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {r, g, b, a}});
    out.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {r, g, b, a}});
    out.push_back({{br.x, br.y}, {uv.uv1.x, uv.uv1.y}, {r, g, b, a}});
}

void LandscapeRenderer::render(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight) {

    if (tiles.empty()) return;
    if (pip.id == SG_INVALID_ID || vbuf.id == SG_INVALID_ID) return;

    // Group tiles by atlas uuid (bind texture per group)
    std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
    groups.reserve(atlases.size() + 4);
    for (const auto& t : tiles) {
        groups[t.assetUuid].push_back(&t);
    }

    // Build one merged vertex stream: sokol allows only ONE sg_update_buffer
    // per buffer per frame, so all atlas groups go into a single update and
    // are drawn as per-group vertex ranges with their own texture binding.
    scratchVerts.clear();
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

        const int baseVertex = (int)scratchVerts.size();

        for (const LandscapeTile* t : group) {
            appendAtlasQuad(scratchVerts, atlas, iso, t->cell, t->tileIndex, 0.0f);
        }

        scratchDraws.push_back({&atlas.atlas, baseVertex, (int)scratchVerts.size() - baseVertex});
    }

    if (scratchVerts.empty()) return;

    // Field -> screen (camera zoom scales positions and quad sizes together).
    for (Vertex& v : scratchVerts) {
        const glm::vec2 s = camera.worldToScreen({v.pos[0], v.pos[1]});
        v.pos[0] = s.x;
        v.pos[1] = s.y;
    }

    sg_range range = { scratchVerts.data(), scratchVerts.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, &range);

    sg_apply_pipeline(pip);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &uniform_range);

    for (const DrawGroup& draw : scratchDraws) {
        bind.views[0] = draw.texture->sgView();
        bind.samplers[0] = draw.texture->sgSampler();
        sg_apply_bindings(&bind);

        sg_draw(draw.baseVertex, draw.vertexCount, 1);
    }
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

        meshes.push_back(highground::generate(
            highground::makeGrid(assetNodes.data(), assetNodes.size()), gp));
        const highground::Mesh& mesh = meshes.back();

        const bool texturedTop = raised.topTex.valid();
        for (const highground::Primitive& prim : mesh.primitives) {
            if (prim.material == highground::Material::Wall) {
                order.push_back({prim.depth, true, &mesh, prim.first, prim.count, nullptr, nullptr});
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

    bool emitTruncated = false;
    for (const PrimRef& ref : order) {
        if (ref.isWall) {
            if (scratchWallVerts.size() + ref.count > kWallVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || !batches.back().wall) {
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

    if (!scratchWallVerts.empty()) {
        sg_range range = { scratchWallVerts.data(), scratchWallVerts.size() * sizeof(WallVertex) };
        sg_update_buffer(wallVbuf, &range);
    }
    if (!scratchRaisedVerts.empty()) {
        sg_range range = { scratchRaisedVerts.data(), scratchRaisedVerts.size() * sizeof(Vertex) };
        sg_update_buffer(raisedVbuf, &range);
    }

    for (const Batch& batch : batches) {
        if (batch.wall) {
            sg_apply_pipeline(wallPip);
            sg_apply_uniforms(0, &uniform_range);
            sg_apply_bindings(&wallBind);
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
