#include "pch.h"

#include "FenceRenderer.h"

#include <cmath>
#include <vector>

namespace {

struct FenceColorVertex {
    float x, y, z;
    float r, g, b, a;
};

struct VsParams {
    float view_size[2];
    float camera_offset[2];
    float camera_zoom;
};

// ---------------------------------------------------------------------------
// Color pass shaders (flat RGBA): one source set per backend, the same
// GLSL/HLSL/MSL triple the GridRenderer carries.
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

// Same constant z-anchor as the grid overlay (SDFGeneratedLandscape gotcha:
// never camera-derived, or overlay and future mesh streams would disagree).
constexpr float kZFar = 100000.0f;
constexpr float kZScale = 1.0f / 200000.0f;

float bakedDepth(float fieldY) {
    return (kZFar - fieldY) * kZScale;
}

void pushVert(std::vector<FenceColorVertex>& out, glm::vec2 p, glm::vec4 color) {
    out.push_back({p.x, p.y, bakedDepth(p.y), color.r, color.g, color.b, color.a});
}

// Filled diamond centered on a point (post marker, ghost cell tint).
void appendDiamond(
    std::vector<FenceColorVertex>& out,
    glm::vec2 center,
    float radiusX,
    float radiusY,
    glm::vec4 color) {

    const glm::vec2 left{center.x - radiusX, center.y};
    const glm::vec2 up{center.x, center.y - radiusY};
    const glm::vec2 right{center.x + radiusX, center.y};
    const glm::vec2 down{center.x, center.y + radiusY};
    pushVert(out, left, color);
    pushVert(out, up, color);
    pushVert(out, right, color);
    pushVert(out, left, color);
    pushVert(out, right, color);
    pushVert(out, down, color);
}

// Thick band between two field points (a section between its posts).
void appendBand(
    std::vector<FenceColorVertex>& out,
    glm::vec2 a,
    glm::vec2 b,
    float thickness,
    glm::vec4 color) {

    const glm::vec2 d = b - a;
    const float len = std::max(std::sqrt(d.x * d.x + d.y * d.y), 1e-4f);
    const glm::vec2 perp{-d.y / len * thickness, d.x / len * thickness};
    const glm::vec2 a0 = a - perp;
    const glm::vec2 a1 = a + perp;
    const glm::vec2 b0 = b - perp;
    const glm::vec2 b1 = b + perp;
    pushVert(out, a0, color);
    pushVert(out, b0, color);
    pushVert(out, b1, color);
    pushVert(out, a0, color);
    pushVert(out, b1, color);
    pushVert(out, a1, color);
}

void appendGhostPiece(
    std::vector<FenceColorVertex>& out,
    const topology_core::DiamondIsometry& iso,
    const FenceModel::StrokePiece& piece,
    glm::vec4 color) {

    const glm::vec2 half = iso.dims.cellSize() * 0.5f;
    if (piece.kind == FencePieceKind::Post) {
        appendDiamond(out, iso.mapToField(piece.cell), half.x * 0.55f, half.y * 0.55f, color);
        return;
    }
    const glm::vec2 a = iso.mapToField(piece.cell);
    const glm::vec2 b = iso.mapToField(piece.cell + piece.axis * (piece.length - 1));
    appendBand(out, a, b, half.y * 0.45f, color);
}

} // namespace

void FenceRenderer::init() {
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
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsParams);
    shd.uniform_blocks[0].hlsl_register_b_n = 0;
    shd.uniform_blocks[0].msl_buffer_n = 0;
    shd.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "view_size";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shd.uniform_blocks[0].glsl_uniforms[1].glsl_name = "camera_offset";
    shd.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shd.uniform_blocks[0].glsl_uniforms[2].glsl_name = "camera_zoom";
    shd.uniform_blocks[0].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    shd.label = "fencepath-fence-shd";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.colors[0].blend.enabled = true;
    pip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Explicit classic-over alpha (sokol would default to ONE/ZERO and clobber
    // the destination alpha).
    pip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    // Ground-plane overlay convention: depth-tested, never written.
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = false;
    pip.label = "fencepath-fence-tri-pip";
    m_triPip = sg_make_pipeline(&pip);

    sg_buffer_desc buf = {};
    buf.size = 65536 * sizeof(FenceColorVertex);
    buf.usage.dynamic_update = true;
    buf.label = "fencepath-fence-vbuf";
    m_vbuf = sg_make_buffer(&buf);

    m_ready = true;
}

void FenceRenderer::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vbuf);
        m_vbuf = {};
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

void FenceRenderer::render(
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewW,
    int viewH,
    const FenceModel& model,
    int selectedFence,
    const std::vector<FenceModel::StrokePiece>* ghost,
    bool ghostValid) {

    if (!m_ready) {
        return;
    }

    const glm::vec2 half = iso.dims.cellSize() * 0.5f;
    const glm::vec4 sectionColor{0.30f, 0.32f, 0.36f, 1.0f};
    const glm::vec4 postColor{0.55f, 0.55f, 0.58f, 1.0f};
    const glm::vec4 sectionSelected{0.85f, 0.65f, 0.28f, 1.0f};
    const glm::vec4 postSelected{0.95f, 0.78f, 0.32f, 1.0f};

    std::vector<FenceColorVertex> verts;
    verts.reserve(model.pieces().size() * 6 + (ghost ? ghost->size() * 6 : 0) + 64);

    // Painter order inside one stream: section bands, post diamonds on top,
    // the ghost preview last.
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Section) {
            continue;
        }
        const FencePiece* postA = model.pieceById(piece.postA);
        const FencePiece* postB = model.pieceById(piece.postB);
        if (!postA || !postB) {
            continue;
        }
        const glm::vec4 color = piece.fenceId == selectedFence ? sectionSelected : sectionColor;
        appendBand(verts, iso.mapToField(postA->cell), iso.mapToField(postB->cell), half.y * 0.4f, color);
    }
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Post) {
            continue;
        }
        const glm::vec4 color = piece.fenceId == selectedFence ? postSelected : postColor;
        appendDiamond(verts, iso.mapToField(piece.cell), half.x * 0.55f, half.y * 0.55f, color);
    }

    if (ghost && !ghost->empty()) {
        const glm::vec4 ghostColor = ghostValid
            ? glm::vec4{0.35f, 0.90f, 0.45f, 0.40f}
            : glm::vec4{0.95f, 0.30f, 0.30f, 0.45f};
        for (const FenceModel::StrokePiece& piece : *ghost) {
            appendGhostPiece(verts, iso, piece, ghostColor);
        }
    }

    if (verts.empty()) {
        return;
    }
    sg_update_buffer(m_vbuf, sg_range{verts.data(), verts.size() * sizeof(FenceColorVertex)});

    VsParams vsParams{};
    vsParams.view_size[0] = static_cast<float>(viewW);
    vsParams.view_size[1] = static_cast<float>(viewH);
    vsParams.camera_offset[0] = camera.offset.x;
    vsParams.camera_offset[1] = camera.offset.y;
    vsParams.camera_zoom = camera.zoom;

    sg_bindings bind{};
    bind.vertex_buffers[0] = m_vbuf;

    sg_apply_pipeline(m_triPip);
    sg_apply_bindings(&bind);
    sg_apply_uniforms(0, sg_range{&vsParams, sizeof(vsParams)});
    sg_draw(0, static_cast<int>(verts.size()), 1);
}
