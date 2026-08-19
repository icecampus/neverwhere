#include "pch.h"

#include "GridRenderer.h"

#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Color pass shaders (flat RGBA overlay): one source set per backend, the
// same GLSL/HLSL/MSL triple the other playgrounds carry. The vertex shader
// applies the 2D camera and maps points to clip space; z comes pre-baked.
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

// MSL shaders (Metal backend, macOS). Entry point "_main" is sokol's default
// for Metal; vertex attributes map by index ([[attribute(N)]]).
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

// The z-buffer anchor is a constant, not camera-derived (SDFGeneratedLandscape
// gotcha: overlay vertices are re-projected every frame, while the B-rep mesh
// stream bakes z once per rebuild — a camera-dependent anchor would let the
// grid hop over/under the mesh on pan).
constexpr float kZFar = 100000.0f;
constexpr float kZScale = 1.0f / 200000.0f;

float bakedDepth(float fieldY) {
    return (kZFar - fieldY) * kZScale;
}

void fillVsUniformDesc(sg_shader_uniform_block* block) {
    block->stage = SG_SHADERSTAGE_VERTEX;
    block->size = sizeof(GridRenderer::VsParams);
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

void appendDiamondOutline(
    std::vector<GridColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2 a = corners[i];
        const glm::vec2 b = corners[(i + 1) % 4];
        out.push_back({a.x, a.y, bakedDepth(a.y), color.r, color.g, color.b, color.a});
        out.push_back({b.x, b.y, bakedDepth(b.y), color.r, color.g, color.b, color.a});
    }
}

// Solid diamond of a cell (2 triangles) for the hover footprint tint.
void appendDiamondFill(
    std::vector<GridColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    glm::ivec2 cell,
    glm::vec4 color) {

    const auto corners = iso.cellDiamondCorners(cell); // Left, Up, Right, Down
    const int tris[6] = {0, 1, 2, 0, 2, 3};
    for (const int idx : tris) {
        const glm::vec2 p = corners[idx];
        out.push_back({p.x, p.y, bakedDepth(p.y), color.r, color.g, color.b, color.a});
    }
}

} // namespace

void GridRenderer::init() {
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
    shd.label = "brep-color-shd";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.primitive_type = SG_PRIMITIVETYPE_LINES;
    pip.colors[0].blend.enabled = true;
    pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Explicit classic-over alpha: sokol defaults to src=ONE/dst=ZERO for the
    // alpha channel, which would clobber the destination alpha.
    pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // The overlay shares the z-buffer convention of the 3D pass (baked
    // ground-plane depth): depth-tested, but not written, so overlay pieces
    // never occlude each other.
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = false;
    pip.label = "brep-color-line-pip";
    m_linePip = sg_make_pipeline(&pip);

    // Same color stream as triangles: translucent hover footprint fill.
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.label = "brep-color-tri-pip";
    m_triPip = sg_make_pipeline(&pip);

    sg_buffer_desc buf = {};
    buf.size = 8 * 65536 * sizeof(GridColorVertex);
    buf.usage.dynamic_update = true;
    buf.label = "brep-color-vbuf";
    m_vbuf = sg_make_buffer(&buf);

    m_ready = true;
}

void GridRenderer::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vbuf);
        m_vbuf = {};
    }
    if (m_linePip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_linePip);
        m_linePip = {};
    }
    if (m_triPip.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_triPip);
        m_triPip = {};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {};
    }
    m_ready = false;
}

void GridRenderer::render(
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    int mapW,
    int mapH,
    glm::ivec2 hoverCell,
    bool hasHover) {

    if (!m_ready) {
        return;
    }

    // One stream, two draws: hover fill triangles first (triangle pipeline),
    // then all the line vertices (line pipeline). A single buffer upload
    // feeds both (sokol allows one sg_update_buffer per buffer per frame).
    std::vector<GridColorVertex> verts;
    verts.reserve(static_cast<std::size_t>(mapW * mapH * 8 + 64));

    int fillVertCount = 0;
    const bool hoverInMap =
        hasHover && hoverCell.x >= 0 && hoverCell.y >= 0 && hoverCell.x < mapW && hoverCell.y < mapH;
    if (hoverInMap) {
        appendDiamondFill(verts, iso, hoverCell, {1.0f, 0.75f, 0.25f, 0.15f});
        fillVertCount = static_cast<int>(verts.size());
    }

    const glm::vec4 gridColor{0.45f, 0.48f, 0.52f, 0.55f};
    for (int y = 0; y < mapH; ++y) {
        for (int x = 0; x < mapW; ++x) {
            appendDiamondOutline(verts, iso, {x, y}, gridColor);
        }
    }

    if (hoverInMap) {
        // The cursor outline goes over the grid.
        appendDiamondOutline(verts, iso, hoverCell, {1.0f, 0.75f, 0.25f, 0.85f});
    }

    if (verts.empty()) {
        return;
    }
    sg_update_buffer(m_vbuf, sg_range{verts.data(), verts.size() * sizeof(GridColorVertex)});

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;

    sg_bindings bind{};
    bind.vertex_buffers[0] = m_vbuf;

    if (fillVertCount > 0) {
        sg_apply_pipeline(m_triPip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(0, fillVertCount, 1);
    }

    const int lineCount = static_cast<int>(verts.size()) - fillVertCount;
    if (lineCount > 0) {
        sg_apply_pipeline(m_linePip);
        sg_apply_bindings(&bind);
        sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
        sg_draw(fillVertCount, lineCount, 1);
    }
}
