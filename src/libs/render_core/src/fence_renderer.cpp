#include "render_core/fence_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spdlog/spdlog.h>

#include "render_core/depth_levels.h"

namespace render_core {

namespace {

// ---------------------------------------------------------------------------
// Shaders: pos (field-space, pre-camera; pos.z = ground fieldY + height lift)
// + baked vertex color. Same camera/depth math as the cliff/cyclopean passes.
// ---------------------------------------------------------------------------

static const char* fence_vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform vec2 z_range;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, (z_range.x - pos.z) * z_range.y, 1.0);
    v_color = color;
}
)";

static const char* fence_fs_src_glsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

static const char* fence_vs_src_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
    float2 z_range;
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
    o.pos = float4(clip, (z_range.x - inp.pos.z) * z_range.y, 1.0);
    o.color = inp.color;
    return o;
}
)";

static const char* fence_fs_src_hlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    return inp.color;
}
)";

static const char* fence_vs_src_msl = R"(
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
    o.pos = float4(clip, (params.z_range.x - in.pos.z) * params.z_range.y, 1.0);
    o.color = in.color;
    return o;
}
)";

static const char* fence_fs_src_msl = R"(
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

// FNV-1a over the content key; scalar-by-scalar so struct padding never leaks
// into the key.
void hashCombine(std::uint64_t& h, std::uint64_t v) {
    h ^= v;
    h *= 1099511628211ULL;
}

void hashFloat(std::uint64_t& h, float f) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    hashCombine(h, bits);
}

// Instance transform of one piece (geometric — the frame builder already
// annotated fenceId/corner, so no FenceModel is needed on this path):
// - post: on the cell center with a deterministic per-index quarter-turn;
// - section: pivot at the post one cell behind the anchor, yaw mapping +x
//   onto the section axis.
void appendPieceInstance(
    const FencePiece& p,
    std::size_t index,
    const fence_core::FenceMeshSet& meshes,
    fence_core::FenceInstanceShading shading,
    glm::vec4 flatColor,
    std::vector<fence_core::FenceWorldVertex>& out) {

    const fence_core::FenceMesh* mesh = nullptr;
    glm::vec3 offset{0.0f};
    float yawDeg = 0.0f;
    if (p.kind == 0) {
        mesh = p.corner ? &meshes.corner : &meshes.post;
        offset = {static_cast<float>(p.cell.x), 0.0f, static_cast<float>(p.cell.y)};
        yawDeg = static_cast<float>((index % 4) * 90);
    } else {
        mesh = p.length >= 2 ? &meshes.section3 : &meshes.section2;
        const glm::ivec2 postCell = p.cell - p.axis; // the post the section runs from
        offset = {static_cast<float>(postCell.x), 0.0f, static_cast<float>(postCell.y)};
        if (p.axis.x > 0) {
            yawDeg = 0.0f;
        } else if (p.axis.x < 0) {
            yawDeg = 180.0f;
        } else if (p.axis.y > 0) {
            yawDeg = -90.0f;
        } else {
            yawDeg = 90.0f;
        }
    }
    fence_core::appendFenceInstance(*mesh, offset, yawDeg, shading, flatColor, out);
}

} // namespace

void FenceRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        ensurePipeline();
    }
}

void FenceRenderer::shutdown() {
    destroyPipeline();
    for (auto& [uuid, cache] : caches) {
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(cache.vbuf);
            cache.vbuf = {};
        }
    }
    caches.clear();
    if (ghostVbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(ghostVbuf);
        ghostVbuf = {};
        ghostVbufSize = 0;
    }
    contentHash = 0;
}

void FenceRenderer::ensureFenceAsset(
    const std::string& assetUuid,
    const std::filesystem::path& meshDir,
    float metersToPoints) {

    auto it = assets.find(assetUuid);
    if (it != assets.end() && it->second.meshDir == meshDir &&
        it->second.metersToPoints == metersToPoints) {
        return;
    }
    AssetEntry entry;
    entry.meshDir = meshDir;
    entry.metersToPoints = metersToPoints > 0.0f ? metersToPoints : fence_core::kFenceMetersToPoints;
    assets[assetUuid] = std::move(entry); // (re)load happens lazily on use
}

void FenceRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // One render_core binary serves all shells — pick shader sources by the
    // active backend (same pattern as LandscapeRenderer/CliffRenderer).
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = fence_vs_src_hlsl;
        shd_desc.fragment_func.source = fence_fs_src_hlsl;
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = fence_vs_src_msl;
        shd_desc.fragment_func.source = fence_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = fence_vs_src_glsl;
        shd_desc.fragment_func.source = fence_fs_src_glsl;
    }
    for (int i = 0; i < 2; ++i) {
        shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[i].hlsl_sem_index = i;
    }
    fillVsUniformDesc(&shd_desc.uniform_blocks[0], sizeof(FenceVsParams));
    shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip_desc.depth.write_enabled = true;
    pip_desc.label = "render-core-fence-pip";
    pip = sg_make_pipeline(&pip_desc);

    // Ghost preview: same shader, but always on top and translucent (own
    // pipeline + own buffer — one sg_update_buffer per buffer per frame).
    pip_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip_desc.depth.write_enabled = false;
    pip_desc.colors[0].blend.enabled = true;
    pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Explicit classic-over alpha: sokol defaults to src=ONE/dst=ZERO for the
    // alpha channel, which would clobber the destination alpha.
    pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pip_desc.label = "render-core-fence-ghost-pip";
    ghostPip = sg_make_pipeline(&pip_desc);
}

void FenceRenderer::destroyPipeline() {
    if (ghostPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(ghostPip);
        ghostPip.id = SG_INVALID_ID;
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

void FenceRenderer::render(
    const std::vector<FencePiece>& pieces,
    const std::vector<FencePiece>& ghost,
    bool ghostValid,
    int selectedFenceId,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight) {

    if (pip.id == SG_INVALID_ID) return;
    if (pieces.empty() && ghost.empty()) return;

    // Content key over the committed pieces + selection + per-asset lift: an
    // untouched frame keeps its cached streams (the camera is VS state).
    std::uint64_t key = 1469598103934665603ULL;
    for (const FencePiece& p : pieces) {
        hashCombine(key, static_cast<std::uint32_t>(p.cell.x));
        hashCombine(key, static_cast<std::uint32_t>(p.cell.y));
        hashCombine(key, static_cast<std::uint32_t>(p.kind));
        hashCombine(key, static_cast<std::uint32_t>(p.axis.x));
        hashCombine(key, static_cast<std::uint32_t>(p.axis.y));
        hashCombine(key, static_cast<std::uint32_t>(p.length));
        hashCombine(key, static_cast<std::uint32_t>(p.fenceId));
        hashCombine(key, p.corner ? 1ULL : 0ULL);
        for (const char c : p.assetUuid) {
            hashCombine(key, static_cast<std::uint8_t>(c));
        }
    }
    hashCombine(key, static_cast<std::uint32_t>(selectedFenceId));
    for (const auto& [uuid, entry] : assets) {
        hashFloat(key, entry.metersToPoints);
    }
    if (key != contentHash) {
        contentHash = key;
        rebuildStreams(pieces, selectedFenceId, iso);
    }

    // Depth range along the iso view ray (same anchor/formula as the other 3D
    // passes, so all passes share the depth buffer consistently).
    const float groundCenterY = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y;
    FenceVsParams vs{};
    vs.view_size[0] = static_cast<float>(viewWidth);
    vs.view_size[1] = static_cast<float>(viewHeight);
    vs.camera_offset[0] = camera.offset.x;
    vs.camera_offset[1] = camera.offset.y;
    vs.z_range[0] = groundCenterY + kZFarOffset;
    vs.z_range[1] = kZScale;
    vs.camera_zoom = camera.zoom;

    for (auto& [uuid, cache] : caches) {
        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID) continue;
        sg_bindings bind = {};
        bind.vertex_buffers[0] = cache.vbuf;
        sg_apply_pipeline(pip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
        sg_draw(0, static_cast<int>(cache.stream.size()), 1);
    }

    // Ghost preview on top (green = applicable, red = rejected).
    if (!ghost.empty() && ghostPip.id != SG_INVALID_ID) {
        const glm::vec4 tint = ghostValid
            ? glm::vec4{0.30f, 0.85f, 0.35f, 0.45f}
            : glm::vec4{0.90f, 0.25f, 0.20f, 0.45f};
        const float halfW = iso.dims.cellSize().x * 0.5f;
        const float halfH = iso.dims.cellSize().y * 0.5f;

        std::vector<FenceVertex> stream;
        std::vector<fence_core::FenceWorldVertex> world;
        std::size_t index = 0;
        for (const FencePiece& p : ghost) {
            AssetEntry* entry = nullptr;
            auto it = assets.find(p.assetUuid);
            if (it != assets.end()) {
                entry = &it->second;
                if (!entry->meshesTried) {
                    entry->meshesTried = true;
                    std::string error;
                    if (!fence_core::loadFenceMeshSet(entry->meshDir.string(), &entry->meshes, &error)) {
                        spdlog::warn("FenceRenderer: mesh load failed for {}: {}", p.assetUuid, error);
                    }
                }
            }
            if (entry && entry->meshes.ok) {
                world.clear();
                appendPieceInstance(p, index, entry->meshes, fence_core::FenceInstanceShading::Flat, tint, world);
                stream.reserve(stream.size() + world.size());
                for (const fence_core::FenceWorldVertex& v : world) {
                    const float fieldX = (v.pos[0] - v.pos[2]) * halfW + halfW;
                    const float fieldY = (v.pos[0] + v.pos[2]) * halfH;
                    const float liftPx = v.pos[1] * entry->metersToPoints;
                    stream.push_back(FenceVertex{
                        {fieldX, fieldY - liftPx, liftedGroundY(fieldY, liftPx)},
                        {v.color[0], v.color[1], v.color[2], v.color[3]}});
                }
            }
            ++index;
        }

        const std::size_t bytes = stream.size() * sizeof(FenceVertex);
        if (bytes > 0) {
            if (ghostVbufSize < bytes) {
                if (ghostVbuf.id != SG_INVALID_ID) {
                    sg_destroy_buffer(ghostVbuf);
                }
                sg_buffer_desc buf_desc = {};
                buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
                buf_desc.usage.dynamic_update = true;
                buf_desc.label = "render-core-fence-ghost-vbuf";
                ghostVbuf = sg_make_buffer(&buf_desc);
                ghostVbufSize = buf_desc.size;
            }
            if (ghostVbuf.id != SG_INVALID_ID) {
                sg_update_buffer(ghostVbuf, sg_range{stream.data(), bytes});
                sg_bindings bind = {};
                bind.vertex_buffers[0] = ghostVbuf;
                sg_apply_pipeline(ghostPip);
                sg_apply_bindings(&bind);
                sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
                sg_draw(0, static_cast<int>(stream.size()), 1);
            }
        }
    }
}

void FenceRenderer::rebuildStreams(
    const std::vector<FencePiece>& pieces,
    int selectedFenceId,
    const topology_core::DiamondIsometry& iso) {

    // Per-asset streams (one draw per asset), all rebuilt on a global key
    // change — fences are light, so no region split and no debounce.
    for (auto& [uuid, cache] : caches) {
        cache.stream.clear();
    }

    const float halfW = iso.dims.cellSize().x * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;

    std::vector<fence_core::FenceWorldVertex> world;
    std::size_t index = 0;
    for (const FencePiece& p : pieces) {
        auto it = assets.find(p.assetUuid);
        if (it != assets.end()) {
            AssetEntry& entry = it->second;
            if (!entry.meshesTried) {
                entry.meshesTried = true;
                std::string error;
                if (!fence_core::loadFenceMeshSet(entry.meshDir.string(), &entry.meshes, &error)) {
                    spdlog::warn("FenceRenderer: mesh load failed for {}: {}", p.assetUuid, error);
                }
            }
            if (entry.meshes.ok) {
                const bool selected = p.fenceId == selectedFenceId && selectedFenceId >= 0;
                world.clear();
                appendPieceInstance(
                    p, index, entry.meshes,
                    selected ? fence_core::FenceInstanceShading::Selected
                             : fence_core::FenceInstanceShading::Lit,
                    glm::vec4{0.0f}, world);
                AssetCache& cache = caches[p.assetUuid];
                cache.stream.reserve(cache.stream.size() + world.size());
                for (const fence_core::FenceWorldVertex& v : world) {
                    // Projection matches the cliff/cyclopean passes
                    // (DiamondIsometry::nodeToField, no +halfH on y); pos.z
                    // carries the ground fieldY plus the height lift.
                    const float fieldX = (v.pos[0] - v.pos[2]) * halfW + halfW;
                    const float fieldY = (v.pos[0] + v.pos[2]) * halfH;
                    const float liftPx = v.pos[1] * entry.metersToPoints;
                    cache.stream.push_back(FenceVertex{
                        {fieldX, fieldY - liftPx, liftedGroundY(fieldY, liftPx)},
                        {v.color[0], v.color[1], v.color[2], v.color[3]}});
                }
            }
        }
        ++index;
    }

    // Upload the fresh streams (at most one sg_update_buffer per buffer per
    // frame — this rebuild runs at most once per render call).
    for (auto& [uuid, cache] : caches) {
        const std::size_t bytes = cache.stream.size() * sizeof(FenceVertex);
        if (bytes == 0) continue;
        if (cache.vbufSize < bytes) {
            if (cache.vbuf.id != SG_INVALID_ID) {
                sg_destroy_buffer(cache.vbuf);
            }
            sg_buffer_desc buf_desc = {};
            buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
            buf_desc.usage.dynamic_update = true;
            buf_desc.label = "render-core-fence-vbuf";
            cache.vbuf = sg_make_buffer(&buf_desc);
            cache.vbufSize = buf_desc.size;
        }
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_update_buffer(cache.vbuf, sg_range{cache.stream.data(), bytes});
        }
    }
}

} // namespace render_core
