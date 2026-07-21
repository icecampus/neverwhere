#include "render_core/landscape_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include <landscape_core/landscape_logic.h>

#include "raised_geometry.h"
#include "rock_walls.h"

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

// Simple flat cliff walls (TileShapePlayground port): a vertical quad under
// each contour segment — top edge follows the land contour lifted by `height`,
// bottom edge is the same segment at ground. Two brightness levels by grid
// axis fake iso lighting; bottom is darker. Emits world-space (field px).
void appendWallTriangles(
    std::vector<RockWallVertex>& out,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask,
    float height) {

    const auto segments = cellContourSegments(iso, cell, mask);
    for (const ContourSegment& seg : segments) {
        const glm::vec3 base = (seg.axis == 0)
            ? glm::vec3(0.62f, 0.45f, 0.22f)
            : glm::vec3(0.45f, 0.32f, 0.16f);
        const glm::vec4 top{base, 1.0f};
        const glm::vec4 bottom{base * 0.7f, 1.0f};

        const RockWallVertex t0{seg.edgeMid.x, seg.edgeMid.y - height, top.r, top.g, top.b, top.a};
        const RockWallVertex t1{seg.center.x, seg.center.y - height, top.r, top.g, top.b, top.a};
        const RockWallVertex b0{seg.edgeMid.x, seg.edgeMid.y, bottom.r, bottom.g, bottom.b, bottom.a};
        const RockWallVertex b1{seg.center.x, seg.center.y, bottom.r, bottom.g, bottom.b, bottom.a};

        out.push_back(t0);
        out.push_back(b0);
        out.push_back(b1);
        out.push_back(t0);
        out.push_back(b1);
        out.push_back(t1);
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

void LandscapeRenderer::appendTexturedTop(
    std::vector<Vertex>& out,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask,
    float yOffset) const {

    const auto corners = iso.cellDiamondCorners(cell); // [Left, Up, Right, Down]
    const glm::vec2 center = iso.mapToField(cell);
    const glm::vec2 lift{0.0f, yOffset};

    // Field space (screen transform happens at batch emission); UVs come from
    // the un-lifted field position, so the ground pattern stays continuous.
    const auto emit = [&](const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
        out.push_back({{a.x + lift.x, a.y + lift.y}, {a.x * kTopUvPerWorldPx, a.y * kTopUvPerWorldPx}, {1.0f, 1.0f, 1.0f, 1.0f}});
        out.push_back({{b.x + lift.x, b.y + lift.y}, {b.x * kTopUvPerWorldPx, b.y * kTopUvPerWorldPx}, {1.0f, 1.0f, 1.0f, 1.0f}});
        out.push_back({{c.x + lift.x, c.y + lift.y}, {c.x * kTopUvPerWorldPx, c.y * kTopUvPerWorldPx}, {1.0f, 1.0f, 1.0f, 1.0f}});
    };

    // Diamond edges as index pairs into the [Left, Up, Right, Down] mask —
    // same table as cellContourSegments, so the top matches the wall contour.
    static constexpr int kEdges[4][2] = {
        {0, 1}, // Left-Up
        {1, 2}, // Up-Right
        {2, 3}, // Right-Down
        {3, 0}, // Down-Left
    };

    for (const auto& edge : kEdges) {
        const int a = edge[0];
        const int b = edge[1];
        if (mask[a] && mask[b]) {
            emit(center, corners[a], corners[b]); // full quadrant
        } else if (mask[a]) {
            emit(center, corners[a], (corners[a] + corners[b]) * 0.5f); // half at corner a
        } else if (mask[b]) {
            emit(center, (corners[a] + corners[b]) * 0.5f, corners[b]); // half at corner b
        }
    }
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

    // Per-cell corner-node masks (aligned with `cells`), shared by the wall
    // contour and the raised-top shape so they always match.
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

    // Walls in world (field) space: simple per-cell quads, or the global
    // rock-wall chains collected per asset (a chain must span the whole
    // landmass, so rock segments of all cells of one asset go into a single
    // build). wallDepths holds one iso depth key per wall QUAD (6 verts):
    // the ground y of its bottom edge — the painter's-algorithm sort key.
    std::vector<RockWallVertex> worldWalls;
    worldWalls.reserve(cells.size() * 12);
    std::vector<float> wallDepths;
    wallDepths.reserve(cells.size() * 2);
    std::unordered_map<std::string, std::vector<RockContourSegment>> rockSegments;
    std::unordered_map<std::string, std::vector<std::vector<glm::vec2>>> topChainsByAsset;

    const auto quadGroundY = [&](std::size_t quadIndex) {
        const std::size_t base = quadIndex * 6;
        float y = worldWalls[base].y;
        for (std::size_t k = 1; k < 6; ++k) {
            y = std::max(y, worldWalls[base + k].y);
        }
        return y;
    };

    for (std::size_t i = 0; i < cells.size(); ++i) {
        const LandscapeTile* t = cells[i];
        const std::array<bool, 4>& mask = cellMasks[i];
        const RaisedParams& params = raisedAtlases.find(t->assetUuid)->second.params;
        if (params.rockWalls) {
            std::vector<RockContourSegment> segs = cellRockContourSegments(t->cell, mask);
            std::vector<RockContourSegment>& bucket = rockSegments[t->assetUuid];
            bucket.insert(bucket.end(), segs.begin(), segs.end());
        } else {
            const std::size_t firstQuad = worldWalls.size() / 6;
            appendWallTriangles(worldWalls, iso, t->cell, mask, params.height);
            for (std::size_t q = firstQuad; q < worldWalls.size() / 6; ++q) {
                wallDepths.push_back(quadGroundY(q));
            }
        }
    }

    for (const auto& [uuid, segments] : rockSegments) {
        const RaisedParams& params = raisedAtlases.find(uuid)->second.params;
        RockWallParams rockParams;
        rockParams.cornerBevel = params.bevel;
        rockParams.amplitude = params.amplitude;
        const std::size_t firstQuad = worldWalls.size() / 6;
        RockWallBuild rockBuild = buildRockWalls(segments, params.height, rockParams, iso);
        worldWalls.insert(worldWalls.end(), rockBuild.verts.begin(), rockBuild.verts.end());
        for (std::size_t q = firstQuad; q < worldWalls.size() / 6; ++q) {
            wallDepths.push_back(quadGroundY(q));
        }
        // Beveled wall-top boundary chains -> the raised top surface is formed
        // from the walls' upper contour (triangulated below).
        topChainsByAsset[uuid] = std::move(rockBuild.topChains);
    }

    if (worldWalls.size() > kWallVbufVertices) {
        spdlog::error("LandscapeRenderer::renderRaised: wall vertex overflow ({} > {}), geometry truncated",
            worldWalls.size(),
            kWallVbufVertices);
        worldWalls.resize((kWallVbufVertices / 6) * 6); // cut at a quad boundary
        wallDepths.resize(worldWalls.size() / 6);
    }

    // Raised tops, built as field-space primitives with iso depth keys.
    // Cells of an asset with a topTexture are drawn as ground-textured
    // triangles; otherwise the same atlas quads as the flat pass, lifted by
    // params.height. The textured top is formed from the walls' upper contour
    // (the beveled boundary chains exported by the rock-wall build,
    // ear-clipped into triangles), so the top surface and the wall tops match
    // exactly — no "lid" overhang at chamfered corners. Contours with holes
    // (mixed chain winding), non-rock walls or missing chains fall back to
    // per-cell mask-shaped triangles.
    std::vector<Vertex> topFieldVerts;
    struct TopPrim {
        float depth = 0.0f;          // ground y of the primitive's lowest vertex
        const TextureAtlas* tex = nullptr;
        int first = 0;               // into topFieldVerts
        int count = 0;
    };
    std::vector<TopPrim> topPrims;
    {
        std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
        groups.reserve(raisedAtlases.size() + 4);
        for (const LandscapeTile* t : cells) {
            groups[t->assetUuid].push_back(t);
        }
        // Mask lookup aligned with `cells` (built above).
        std::unordered_map<const LandscapeTile*, const std::array<bool, 4>*> maskByTile;
        maskByTile.reserve(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            maskByTile.emplace(cells[i], &cellMasks[i]);
        }

        // Ear-clipped top triangles per asset (field space, unlifted), empty
        // when the contour construction is not applicable -> per-cell fallback.
        // Hole chains (opposite winding to the largest-area chain) are merged
        // into their containing outer chain with a zero-width bridge, so the
        // top surface keeps the pit open without falling back to per-cell tops.
        std::unordered_map<std::string, std::vector<glm::vec2>> chainTopTris;
        for (const auto& [uuid, chains] : topChainsByAsset) {
            const RaisedAtlasGpu& raised = raisedAtlases.find(uuid)->second;
            if (!raised.topTex.valid() || chains.empty()) {
                continue;
            }
            // All chains of one landmass are walked with land on the left, so
            // outer chains share the winding of the largest-area chain; an
            // opposite winding means a hole.
            float refArea = 0.0f;
            for (const auto& poly : chains) {
                const float area = polygonSignedArea(poly);
                if (std::abs(area) > std::abs(refArea)) {
                    refArea = area;
                }
            }
            std::vector<const std::vector<glm::vec2>*> outers;
            std::vector<const std::vector<glm::vec2>*> holes;
            for (const auto& poly : chains) {
                if (polygonSignedArea(poly) * refArea > 0.0f) {
                    outers.push_back(&poly);
                } else {
                    holes.push_back(&poly);
                }
            }

            std::vector<glm::vec2> tris;
            bool ok = true;
            for (const auto* outer : outers) {
                std::vector<glm::vec2> merged = *outer;
                for (const auto* hole : holes) {
                    if (pointInPolygon(*outer, hole->front())) {
                        merged = mergeHoleIntoOuter(merged, *hole);
                        if (merged.empty()) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (!ok) {
                    break;
                }
                std::vector<glm::vec2> part = triangulateSimplePolygon(merged);
                if (part.empty()) {
                    ok = false;
                    break;
                }
                tris.insert(tris.end(), part.begin(), part.end());
            }
            // Every hole must land inside some outer chain.
            for (const auto* hole : holes) {
                bool paired = false;
                for (const auto* outer : outers) {
                    if (pointInPolygon(*outer, hole->front())) {
                        paired = true;
                        break;
                    }
                }
                if (!paired) {
                    ok = false;
                }
            }
            if (ok) {
                chainTopTris[uuid] = std::move(tris);
            }
        }

        bool topsTruncated = false;
        for (auto& [uuid, group] : groups) {
            const RaisedAtlasGpu& raised = raisedAtlases.find(uuid)->second;
            const bool texturedTop = raised.topTex.valid();
            const float yOffset = -raised.params.height;
            const TextureAtlas* tex = texturedTop ? &raised.topTex : &raised.gpu.atlas;

            // Emit helpers: append field-space verts and register one prim per
            // primitive (3 verts per textured triangle, 6 per atlas quad).
            const auto pushPrims = [&](std::size_t firstVert, int vertsPerPrim) {
                for (std::size_t base = firstVert; base < topFieldVerts.size(); base += vertsPerPrim) {
                    float depth = topFieldVerts[base].pos[1];
                    for (int k = 1; k < vertsPerPrim; ++k) {
                        depth = std::max(depth, topFieldVerts[base + k].pos[1]);
                    }
                    topPrims.push_back({depth - yOffset, tex, (int)base, vertsPerPrim});
                }
            };

            auto chainIt = chainTopTris.find(uuid);
            if (chainIt != chainTopTris.end()) {
                // Contour-formed top: world UVs, depth from ground positions.
                const std::vector<glm::vec2>& tris = chainIt->second;
                if (topFieldVerts.size() + tris.size() > kRaisedVbufVertices) {
                    topsTruncated = true;
                } else {
                    const glm::vec2 lift{0.0f, yOffset};
                    for (const glm::vec2& p : tris) {
                        topFieldVerts.push_back({{p.x + lift.x, p.y + lift.y}, {p.x * kTopUvPerWorldPx, p.y * kTopUvPerWorldPx}, {1.0f, 1.0f, 1.0f, 1.0f}});
                    }
                    // Re-derive ground-space depths: the lift only shifts y.
                    const std::size_t firstVert = topFieldVerts.size() - tris.size();
                    for (std::size_t v = firstVert; v < topFieldVerts.size(); v += 3) {
                        const glm::vec2& a = tris[v - firstVert];
                        const glm::vec2& b = tris[v - firstVert + 1];
                        const glm::vec2& c = tris[v - firstVert + 2];
                        topPrims.push_back({std::max({a.y, b.y, c.y}), tex, (int)v, 3});
                    }
                }
                continue;
            }

            for (const LandscapeTile* t : group) {
                if (topFieldVerts.size() + 12 > kRaisedVbufVertices) {
                    topsTruncated = true;
                    break;
                }
                const std::size_t firstVert = topFieldVerts.size();
                if (texturedTop) {
                    appendTexturedTop(topFieldVerts, iso, t->cell, *maskByTile[t], yOffset);
                    pushPrims(firstVert, 3);
                } else {
                    appendAtlasQuad(topFieldVerts, raised.gpu, iso, t->cell, t->tileIndex, yOffset);
                    pushPrims(firstVert, 6);
                }
            }
        }
        if (topsTruncated) {
            spdlog::error("LandscapeRenderer::renderRaised: top vertex overflow ({}), tiles skipped", kRaisedVbufVertices);
        }
    }

    // ------------------------------------------------------------------
    // Depth-sorted emission: walls and tops merge into one painter's-algorithm
    // sequence by ground y (walls first on ties), so an arm of a landmass
    // standing in front of another arm's walls (L-shapes, diagonal joins,
    // separate islands) occludes correctly. Batches switch pipeline/texture
    // only when it changes between consecutive primitives.
    // ------------------------------------------------------------------
    struct PrimRef {
        float depth;
        bool isWall;
        std::uint32_t index;
    };
    std::vector<PrimRef> order;
    order.reserve(wallDepths.size() + topPrims.size());
    for (std::uint32_t i = 0; i < wallDepths.size(); ++i) {
        order.push_back({wallDepths[i], true, i});
    }
    for (std::uint32_t i = 0; i < topPrims.size(); ++i) {
        order.push_back({topPrims[i].depth, false, i});
    }
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
            if (scratchWallVerts.size() + 6 > kWallVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || !batches.back().wall) {
                batches.push_back({true, nullptr, (int)scratchWallVerts.size(), 0});
            }
            const std::size_t q = static_cast<std::size_t>(ref.index) * 6;
            for (std::size_t k = 0; k < 6; ++k) {
                const RockWallVertex& v = worldWalls[q + k];
                const glm::vec2 p = camera.worldToScreen({v.x, v.y});
                scratchWallVerts.push_back({{p.x, p.y}, {v.r, v.g, v.b, v.a}});
            }
            batches.back().count += 6;
        } else {
            const TopPrim& tp = topPrims[ref.index];
            if (scratchRaisedVerts.size() + tp.count > kRaisedVbufVertices) {
                emitTruncated = true;
                continue;
            }
            if (batches.empty() || batches.back().wall || batches.back().tex != tp.tex) {
                batches.push_back({false, tp.tex, (int)scratchRaisedVerts.size(), 0});
            }
            for (int k = 0; k < tp.count; ++k) {
                const Vertex& v = topFieldVerts[static_cast<std::size_t>(tp.first) + k];
                const glm::vec2 p = camera.worldToScreen({v.pos[0], v.pos[1]});
                scratchRaisedVerts.push_back({{p.x, p.y}, {v.uv[0], v.uv[1]}, {v.color[0], v.color[1], v.color[2], v.color[3]}});
            }
            batches.back().count += tp.count;
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
