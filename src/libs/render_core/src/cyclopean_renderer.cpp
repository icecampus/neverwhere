#include "render_core/cyclopean_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include <landscape_mesh/landscape_mesh.h>

#include "atlas_tile_types.h"

namespace render_core {

namespace {

// World-unit -> field-px lift scale, matching the cliff pass default
// (CliffParams::heightScale = 96) so both 3D passes share the lift.
constexpr float kHeightScalePx = 96.0f;

// ---------------------------------------------------------------------------
// Shaders: pos (field-space, pre-camera; pos.z = raw ground fieldY) + baked
// vertex color. Same camera/depth math as the cliff pass.
// ---------------------------------------------------------------------------

static const char* cyclopean_vs_src_glsl = R"(
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

static const char* cyclopean_fs_src_glsl = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

static const char* cyclopean_vs_src_hlsl = R"(
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

static const char* cyclopean_fs_src_hlsl = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target {
    return inp.color;
}
)";

static const char* cyclopean_vs_src_msl = R"(
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

static const char* cyclopean_fs_src_msl = R"(
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

// FNV-1a over the content key (nodes + mesh params); scalar-by-scalar so
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

std::uint64_t hashParams(const CyclopeanParams& p) {
    std::uint64_t h = 1469598103934665603ULL;
    hashFloat(h, p.raisedHeight);
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.rockSeed)));
    hashFloat(h, p.rockAmplitude);
    hashCombine(h, p.rockEnabled ? 1ULL : 0ULL);
    hashFloat(h, p.cornerBevel);
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.wallSubdivH)));
    hashCombine(h, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.wallSubdivV)));
    return h;
}

// Collect the on-nodes of one asset's tiles (same vertex-node convention as
// the cliff pass).
std::vector<glm::ivec2> collectOnNodes(const std::vector<const LandscapeTile*>& group) {
    std::unordered_map<std::uint64_t, glm::ivec2> unique;
    unique.reserve(group.size() * 4);
    for (const LandscapeTile* t : group) {
        const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t->tileIndex));
        const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t->cell);
        for (int i = 0; i < 4; ++i) {
            if (mask[i]) {
                unique.emplace(nodeKey(corners[i]), corners[i]);
            }
        }
    }
    std::vector<glm::ivec2> nodes;
    nodes.reserve(unique.size());
    for (const auto& [key, node] : unique) {
        nodes.push_back(node);
    }
    return nodes;
}

// Content key: the mesh params + the sorted on-node set. The same content
// hashes identically every frame, so an untouched asset keeps its cache.
std::uint64_t contentKey(std::vector<glm::ivec2> nodes, const CyclopeanParams& params) {
    std::uint64_t h = hashParams(params);
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

} // namespace

void CyclopeanRenderer::init(sg_pixel_format depthFormat_) {
    depthFormat = depthFormat_;
    if (depthFormat != SG_PIXELFORMAT_NONE) {
        ensurePipeline();
    }
}

void CyclopeanRenderer::shutdown() {
    destroyPipeline();
    for (auto& [uuid, cache] : caches) {
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_destroy_buffer(cache.vbuf);
            cache.vbuf = {};
        }
    }
    caches.clear();
}

void CyclopeanRenderer::ensureCyclopeanAsset(const std::string& assetUuid, const CyclopeanParams& params) {
    assets[assetUuid] = params;
}

void CyclopeanRenderer::ensurePipeline() {
    if (pip.id != SG_INVALID_ID) return;

    sg_shader_desc shd_desc = {};
    // One render_core binary serves all shells — pick shader sources by the
    // active backend (same pattern as LandscapeRenderer/CliffRenderer).
    if (sg_query_backend() == SG_BACKEND_D3D11) {
        shd_desc.vertex_func.source = cyclopean_vs_src_hlsl;
        shd_desc.fragment_func.source = cyclopean_fs_src_hlsl;
    } else if (sg_query_backend() == SG_BACKEND_METAL_MACOS) {
        shd_desc.vertex_func.source = cyclopean_vs_src_msl;
        shd_desc.fragment_func.source = cyclopean_fs_src_msl;
    } else {
        shd_desc.vertex_func.source = cyclopean_vs_src_glsl;
        shd_desc.fragment_func.source = cyclopean_fs_src_glsl;
    }
    for (int i = 0; i < 2; ++i) {
        shd_desc.attrs[i].hlsl_sem_name = "TEXCOORD";
        shd_desc.attrs[i].hlsl_sem_index = i;
    }
    fillVsUniformDesc(&shd_desc.uniform_blocks[0], sizeof(CyclopeanVsParams));
    shd = sg_make_shader(&shd_desc);

    sg_pipeline_desc pip_desc = {};
    pip_desc.shader = shd;
    pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos
    pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip_desc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip_desc.depth.pixel_format = depthFormat;
    pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip_desc.depth.write_enabled = true;
    pip_desc.label = "render-core-cyclopean-pip";
    pip = sg_make_pipeline(&pip_desc);
}

void CyclopeanRenderer::destroyPipeline() {
    if (pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(pip);
        pip.id = SG_INVALID_ID;
    }
    if (shd.id != SG_INVALID_ID) {
        sg_destroy_shader(shd);
        shd.id = SG_INVALID_ID;
    }
}

void CyclopeanRenderer::render(
    const std::vector<LandscapeTile>& tiles,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    double nowSec) {

    if (pip.id == SG_INVALID_ID || tiles.empty()) return;

    // Group tiles by asset uuid (only registered cyclopean3d assets).
    std::unordered_map<std::string, std::vector<const LandscapeTile*>> groups;
    for (const LandscapeTile& t : tiles) {
        if (assets.find(t.assetUuid) == assets.end()) continue;
        groups[t.assetUuid].push_back(&t);
    }
    if (groups.empty()) return;

    // Depth range along the iso view ray (same anchor/formula as the raised
    // and cliff passes, so all passes share the depth buffer consistently).
    const float groundCenterY = camera.screenToWorld({viewWidth * 0.5f, viewHeight * 0.5f}).y;
    CyclopeanVsParams vs{};
    vs.view_size[0] = static_cast<float>(viewWidth);
    vs.view_size[1] = static_cast<float>(viewHeight);
    vs.camera_offset[0] = camera.offset.x;
    vs.camera_offset[1] = camera.offset.y;
    vs.z_range[0] = groundCenterY + 100000.0f;
    vs.z_range[1] = 1.0f / 200000.0f;
    vs.camera_zoom = camera.zoom;

    for (auto& [uuid, group] : groups) {
        const CyclopeanParams& params = assets.find(uuid)->second;
        AssetCache& cache = caches[uuid];

        // Content changed -> schedule a debounced rebuild; the stale mesh (if
        // any) keeps rendering in the meantime.
        const std::uint64_t key = contentKey(collectOnNodes(group), params);
        if (key != cache.contentHash) {
            cache.contentHash = key;
            cache.pending = true;
            cache.pendingSince = -1.0;
        }
        if (cache.pending) {
            if (cache.pendingSince < 0.0) {
                cache.pendingSince = nowSec;
            }
            if ((nowSec - cache.pendingSince) > 0.3) {
                rebuildMesh(cache, group, iso, params);
                cache.pending = false;
            }
        }

        if (cache.stream.empty() || cache.vbuf.id == SG_INVALID_ID) continue;

        sg_bindings bind = {};
        bind.vertex_buffers[0] = cache.vbuf;
        sg_apply_pipeline(pip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vs, sizeof(vs)});
        sg_draw(0, static_cast<int>(cache.stream.size()), 1);
    }
}

void CyclopeanRenderer::rebuildMesh(
    AssetCache& cache,
    const std::vector<const LandscapeTile*>& group,
    const topology_core::DiamondIsometry& iso,
    const CyclopeanParams& params) {

    // Full path for the whole asset: tiles -> on-nodes -> bbox node grid ->
    // cell solid-mask -> landscape_mesh compose (Cyclopean wall style) ->
    // field-space vertex stream with the baked quad colors.
    cache.stream.clear();

    const std::vector<glm::ivec2> onNodes = collectOnNodes(group);
    if (!onNodes.empty()) {
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
        // One-cell margin around the silhouette (the boundary walls/bevels
        // must not cross the node-grid border), same as the cliff pass.
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

        landscape_mesh::SolidMeshBuildRequest request;
        request.mask = landscape_mesh::solidMaskFromNodes(nodes.data(), nodesX, nodesY);
        request.baseHeight = 0.0f;
        request.topHeight = params.raisedHeight;
        request.level = 1;
        request.maxLevel = 1;
        request.includeWalls = true;
        request.fadeWallDisplacementAtBottom = false;

        // Single-level plateau: the level height equals the plateau top (same
        // convention as the SDFGeneratedLandscape wall-mesh pass).
        landscape_mesh::MeshBuildSettings settings;
        settings.cellSize = 1.0f;
        settings.levelHeight = params.raisedHeight;
        settings.cornerBevel = params.cornerBevel;
        settings.rockEnabled = params.rockEnabled;
        settings.rockSeed = params.rockSeed;
        settings.rockAmplitude = params.rockAmplitude;
        settings.wallHorizontalSubdivisions = params.wallSubdivH;
        settings.wallVerticalSubdivisions = params.wallSubdivV;
        settings.wallStyle = landscape_mesh::WallStyleId::Cyclopean;

        const landscape_mesh::CompositionResult result =
            landscape_mesh::composeSolidMaskMesh(request, settings);
        if (!result.seams.passed) {
            spdlog::warn("CyclopeanRenderer: wall mesh seam validation failed ({} of {} edges)",
                result.seams.mismatches, result.seams.checkedEdges);
        }
        spdlog::info("CyclopeanRenderer: rebuilt mesh at ({}, {}) — {} nodes, {} quads",
            minX, minY, onNodes.size(), result.quads.size());

        // Projection matches the cliff pass (DiamondIsometry::nodeToField,
        // no +halfH on y); z carries the raw ground fieldY (normalized in the
        // VS via z_range). Quads triangulate as (a-b-c, a-c-d).
        const glm::vec2 cellSz = iso.dims.cellSize();
        const float halfW = cellSz.x * 0.5f;
        const float halfH = cellSz.y * 0.5f;
        const float invCellSize = 1.0f / settings.cellSize;
        cache.stream.reserve(result.quads.size() * 6);
        const auto pushVertex = [&](const landscape_mesh::Vec3& v, const landscape_mesh::ColorRgba& c) {
            const float mapX = static_cast<float>(minX) + v.x * invCellSize;
            const float mapZ = static_cast<float>(minY) + v.z * invCellSize;
            const float fieldX = (mapX - mapZ) * halfW + halfW;
            const float fieldY = (mapX + mapZ) * halfH;
            cache.stream.push_back(CyclopeanVertex{
                {fieldX, fieldY - v.y * kHeightScalePx, fieldY},
                {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f}});
        };
        for (const landscape_mesh::MeshQuad& quad : result.quads) {
            pushVertex(quad.a, quad.color);
            pushVertex(quad.b, quad.color);
            pushVertex(quad.c, quad.color);
            pushVertex(quad.a, quad.color);
            pushVertex(quad.c, quad.color);
            pushVertex(quad.d, quad.color);
        }
    }

    // Upload the fresh stream.
    const std::size_t bytes = cache.stream.size() * sizeof(CyclopeanVertex);
    if (bytes > 0) {
        if (cache.vbufSize < bytes) {
            if (cache.vbuf.id != SG_INVALID_ID) {
                sg_destroy_buffer(cache.vbuf);
            }
            sg_buffer_desc buf_desc = {};
            buf_desc.size = ((bytes / (std::size_t{1} << 20)) + 1) * (std::size_t{1} << 20);
            buf_desc.usage.dynamic_update = true;
            buf_desc.label = "render-core-cyclopean-vbuf";
            cache.vbuf = sg_make_buffer(&buf_desc);
            cache.vbufSize = buf_desc.size;
        }
        if (cache.vbuf.id != SG_INVALID_ID) {
            sg_update_buffer(cache.vbuf, sg_range{cache.stream.data(), bytes});
        }
    }
}

} // namespace render_core
