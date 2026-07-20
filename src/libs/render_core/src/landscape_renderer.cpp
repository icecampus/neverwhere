#include "render_core/landscape_renderer.h"

#include <algorithm>

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

void LandscapeRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    ensurePipeline();

    // Dynamic vertex buffer for many quads (6 vertices per tile)
    sg_buffer_desc buf_desc = {};
    buf_desc.size = 6 * 65536 * (int)sizeof(Vertex);
    buf_desc.usage.dynamic_update = true;
    buf_desc.label = "landscape-verts";
    vbuf = sg_make_buffer(&buf_desc);

    bind.vertex_buffers[0] = vbuf;
}

void LandscapeRenderer::shutdown() {
    for (auto& [_, a] : atlases) {
        a.atlas.destroy();
    }
    atlases.clear();

    if (vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(vbuf);
        vbuf.id = SG_INVALID_ID;
    }
    destroyPipeline();
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

void LandscapeRenderer::ensureAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows) {
    auto it = atlases.find(assetUuid);
    if (it != atlases.end() && it->second.atlas.valid()) {
        return;
    }

    AtlasGpu gpu;
    gpu.atlas.createFromFile(atlasPath, cols, rows);

    // Match editor behaviour: scale tile to cellWidth
    const float cellWidth = 128.0f;
    const float tileW = (float)gpu.atlas.tileWidth();
    const float tileH = (float)gpu.atlas.tileHeight();
    gpu.scale = (tileW > 0.0f) ? (cellWidth / tileW) : 1.0f;
    gpu.tileSize = glm::vec2(tileW * gpu.scale, tileH * gpu.scale);

    atlases[assetUuid] = std::move(gpu);
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
            const glm::vec2 fieldPos = iso.mapToField(t->cell);
            const glm::vec2 screenCenter = camera.worldToScreen(fieldPos);

            // Editor parity: the QML Scale transform scales tile size together
            // with positions, so the quad must grow/shrink with camera zoom.
            const glm::vec2 size = atlas.tileSize * camera.zoom;
            const glm::vec2 tl = screenCenter - size * 0.5f;
            const glm::vec2 br = screenCenter + size * 0.5f;

            const TileUV uv = atlas.atlas.tileUv(t->tileIndex);

            const float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

            // 2 triangles (TL, TR, BL) (BL, TR, BR)
            scratchVerts.push_back({{tl.x, tl.y}, {uv.uv0.x, uv.uv0.y}, {r, g, b, a}});
            scratchVerts.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {r, g, b, a}});
            scratchVerts.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {r, g, b, a}});

            scratchVerts.push_back({{tl.x, br.y}, {uv.uv0.x, uv.uv1.y}, {r, g, b, a}});
            scratchVerts.push_back({{br.x, tl.y}, {uv.uv1.x, uv.uv0.y}, {r, g, b, a}});
            scratchVerts.push_back({{br.x, br.y}, {uv.uv1.x, uv.uv1.y}, {r, g, b, a}});
        }

        scratchDraws.push_back({&atlas, baseVertex, (int)scratchVerts.size() - baseVertex});
    }

    if (scratchVerts.empty()) return;

    sg_range range = { scratchVerts.data(), scratchVerts.size() * sizeof(Vertex) };
    sg_update_buffer(vbuf, &range);

    sg_apply_pipeline(pip);
    float vs_params[2] = {(float)viewWidth, (float)viewHeight};
    sg_range uniform_range = { &vs_params, sizeof(vs_params) };
    sg_apply_uniforms(0, &uniform_range);

    for (const DrawGroup& draw : scratchDraws) {
        bind.views[0] = draw.atlas->atlas.sgView();
        bind.samplers[0] = draw.atlas->atlas.sgSampler();
        sg_apply_bindings(&bind);

        sg_draw(draw.baseVertex, draw.vertexCount, 1);
    }
}

} // namespace render_core

