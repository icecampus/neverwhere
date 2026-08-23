#include "pch.h"

#include "StoneMeshRenderer.h"

#include <algorithm>
#include <vector>

#include <spdlog/spdlog.h>

namespace {

// Projected, lit vertex — the exact layout of GridColorVertex (GridRenderer.h)
// so this pass shares the grid's color shader/vertex format.
struct StoneFieldVertex {
    float x, y, z;
    float r, g, b, a;
};

// World -> field projection with the shared baked-depth convention:
// z = (kZFar - (groundY + y*kHeightScale*0.5)) * kZScale. The height term
// (inside the consistent 0<z<1 window) lets raised fragments win their own
// ground row but lose to nearer rows. Same anchors as GridRenderer's
// bakedDepth; kHeightScale matches the cliff/fence 96 pt-per-unit family.
glm::vec3 stoneWorldToField(const topology_core::DiamondIsometry& iso, glm::vec3 world) {
    constexpr float kZFar = 100000.0f;
    constexpr float kZScale = 1.0f / 200000.0f;
    constexpr float kHeightScale = 96.0f;
    const float halfW = iso.dims.cellSize().x * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    const float fieldX = (world.x - world.z) * halfW + halfW;
    const float groundY = (world.x + world.z) * halfH + halfH;
    const float screenY = groundY - world.y * kHeightScale;
    const float z = (kZFar - (groundY + world.y * kHeightScale * 0.5f)) * kZScale;
    return {fieldX, screenY, z};
}

// ---------------------------------------------------------------------------
// Color pass shaders (flat RGBA, lighting pre-baked into the vertex color):
// the same GLSL/HLSL/MSL triple GridRenderer carries.
// ---------------------------------------------------------------------------

static const char* kColorVsGlsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color;
out vec4 v_color;
uniform vec2 view_size;
uniform vec2 camera_offset;
uniform float camera_zoom;
void main() {
    vec2 screen = (pos.xy * camera_zoom) + camera_offset;
    vec2 clip = vec2((screen.x / view_size.x) * 2.0 - 1.0, 1.0 - (screen.y / view_size.y) * 2.0);
    gl_Position = vec4(clip, pos.z, 1.0);
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

static const char* kColorVsHlsl = R"(
cbuffer vs_params: register(b0) {
    float2 view_size;
    float2 camera_offset;
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
    o.pos = float4(clip, inp.pos.z, 1.0);
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

static const char* kColorVsMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct VsParams {
    float2 view_size;
    float2 camera_offset;
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
    o.pos = float4(clip, in.pos.z, 1.0);
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

void fillVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(StoneMeshRenderer::VsParams);
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

} // namespace

void StoneMeshRenderer::init() {
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
    shd.label = "stonegen-mesh-shd";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.colors[0].blend.enabled = true;
    pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Explicit classic-over alpha: sokol defaults to src=ONE/dst=ZERO for the
    // alpha channel, which would clobber the destination alpha.
    pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // The one depth-WRITING pass of the playground: the rock self-occludes
    // and arbitrates against the grid plane via the shared baked-depth
    // convention (the grid only tests, never writes).
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "stonegen-mesh-pip";
    m_pip = sg_make_pipeline(&pip);

    sg_buffer_desc buf = {};
    buf.size = 8 * 65536 * sizeof(StoneFieldVertex);
    buf.usage.dynamic_update = true;
    buf.label = "stonegen-mesh-vbuf";
    m_vbuf = sg_make_buffer(&buf);

    m_ready = true;
}

void StoneMeshRenderer::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vbuf);
        m_vbuf = {};
    }
    if (m_pip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_pip);
        m_pip = {};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {};
    }
    m_ready = false;
}

void StoneMeshRenderer::render(
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    const StoneMesh& mesh,
    glm::vec3 worldPos,
    std::uint64_t contentKey) {

    if (!m_ready) {
        return;
    }

    if (contentKey != m_cacheKey) {
        const int vertCount = static_cast<int>(mesh.pos.size() / 3);
        std::vector<StoneFieldVertex> verts;
        verts.reserve(static_cast<std::size_t>(vertCount));
        for (int i = 0; i < vertCount; ++i) {
            const glm::vec3 world =
                glm::vec3{mesh.pos[i * 3], mesh.pos[i * 3 + 1], mesh.pos[i * 3 + 2]} + worldPos;
            const glm::vec3 f = stoneWorldToField(iso, world);
            verts.push_back({
                f.x, f.y, f.z,
                mesh.col[i * 4], mesh.col[i * 4 + 1], mesh.col[i * 4 + 2], mesh.col[i * 4 + 3]});
        }
        const std::size_t capacity = 8 * 65536;
        m_vertCount = static_cast<int>(std::min(verts.size(), capacity));
        if (verts.size() > capacity) {
            spdlog::warn("StoneMeshRenderer: {} verts exceed buffer capacity, truncated",
                verts.size());
        }
        if (m_vertCount > 0) {
            // One sg_update_buffer per buffer per frame, and the cache makes
            // sure even that one happens only on content changes.
            sg_update_buffer(
                m_vbuf, sg_range{verts.data(), m_vertCount * sizeof(StoneFieldVertex)});
        }
        m_cacheKey = contentKey;
    }

    if (m_vertCount == 0) {
        return;
    }

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;

    sg_bindings bind{};
    bind.vertex_buffers[0] = m_vbuf;

    sg_apply_pipeline(m_pip);
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
    sg_draw(0, m_vertCount, 1);
}
