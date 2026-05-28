#include "render_core/mesh_preview_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace render_core {
namespace {

static const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal0;
layout(location=2) in vec2 uv0;
layout(location=3) in vec4 color0;
layout(location=4) in float face_kind0;
layout(location=5) in float cliff_distance0;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_color;
out float v_face_kind;
out float v_cliff_distance;

uniform vec2 output_size;
uniform vec2 world_center;
uniform vec2 pan;
uniform float zoom;
uniform float anchor_y;
uniform vec2 iso_scale;
uniform float height_scale;

void main() {
    vec2 centered = vec2(pos.x - world_center.x, pos.z - world_center.y);
    vec2 screen = vec2(
        output_size.x * 0.5 + pan.x + (centered.x - centered.y) * iso_scale.x * zoom,
        anchor_y + pan.y + ((centered.x + centered.y) * iso_scale.y - pos.y * height_scale) * zoom
    );
    vec2 clip_pos = vec2(
        (screen.x / output_size.x) * 2.0 - 1.0,
        1.0 - (screen.y / output_size.y) * 2.0
    );
    gl_Position = vec4(clip_pos, 0.0, 1.0);
    v_world_pos = pos;
    v_normal = normal0;
    v_uv = uv0;
    v_color = color0;
    v_face_kind = face_kind0;
    v_cliff_distance = cliff_distance0;
}
)";

static const char* fs_src_glsl = R"(
#version 330
in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_color;
in float v_face_kind;
in float v_cliff_distance;
out vec4 frag_color;

uniform sampler2D grass_tex;
uniform sampler2D rock_tex;
uniform vec4 light_dir;
uniform vec4 options0;
uniform vec4 options1;
uniform vec4 options2;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 macroWarp(vec2 uv, vec3 worldPos) {
    float macroScale = max(0.001, options2.x);
    float macroStrength = options2.y;
    float n0 = hash21(worldPos.xz * macroScale + vec2(11.3, 7.1));
    float n1 = hash21(worldPos.xz * macroScale * 0.73 + vec2(3.7, 19.1));
    return uv + (vec2(n0, n1) - 0.5) * macroStrength;
}

void main() {
    float ambient = options0.x;
    float diffuseStrength = options0.y;
    float wallBrightness = options0.z;
    float textureScale = max(0.001, options0.w);
    float cliffRadius = max(0.001, options1.x);
    float cliffStrength = options1.y;
    float minTopBrightness = options1.z;
    float edgeDarkness = options1.w;
    float debugMode = options2.z;

    bool wall = v_face_kind > 0.5;
    vec2 uv = macroWarp(v_uv * textureScale, v_world_pos);
    vec4 albedo = wall ? texture(rock_tex, uv) : texture(grass_tex, uv);
    albedo *= v_color;

    vec3 n = normalize(v_normal);
    if (n.y < 0.0) {
        n = -n;
    }
    vec3 l = normalize(light_dir.xyz);
    float lambert = max(0.0, dot(n, l));
    float wrapped = clamp(dot(n, l) * 0.5 + 0.5, 0.0, 1.0);
    float cliffProximity = clamp(1.0 - v_cliff_distance / cliffRadius, 0.0, 1.0);

    float lighting = wall
        ? wallBrightness * (ambient + diffuseStrength * wrapped)
        : ambient + diffuseStrength * lambert;
    if (!wall) {
        float topDarkening = max(minTopBrightness, 1.0 - cliffStrength * cliffProximity);
        lighting *= topDarkening * (1.0 - edgeDarkness * cliffProximity);
    }

    vec4 lit = vec4(albedo.rgb * lighting, albedo.a);

    if (debugMode > 4.5) {
        frag_color = v_color;
    } else if (debugMode > 3.5) {
        frag_color = vec4(vec3(cliffProximity), 1.0);
    } else if (debugMode > 2.5) {
        frag_color = vec4(fract(uv), 0.35, 1.0);
    } else if (debugMode > 1.5) {
        frag_color = vec4(n * 0.5 + 0.5, 1.0);
    } else if (debugMode > 0.5) {
        frag_color = albedo;
    } else {
        frag_color = lit;
    }
}
)";

static const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) {
    float2 output_size;
    float2 world_center;
    float2 pan;
    float zoom;
    float anchor_y;
    float2 iso_scale;
    float height_scale;
    float _pad0;
};

struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float2 uv0: TEXCOORD2;
    float4 color0: TEXCOORD3;
    float faceKind0: TEXCOORD4;
    float cliffDistance0: TEXCOORD5;
};

struct VSOut {
    float4 pos: SV_Position;
    float3 worldPos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float2 uv0: TEXCOORD2;
    float4 color0: TEXCOORD3;
    float faceKind0: TEXCOORD4;
    float cliffDistance0: TEXCOORD5;
};

VSOut main(VSIn inp) {
    VSOut o;
    float2 centered = float2(inp.pos.x - world_center.x, inp.pos.z - world_center.y);
    float2 screen = float2(
        output_size.x * 0.5 + pan.x + (centered.x - centered.y) * iso_scale.x * zoom,
        anchor_y + pan.y + ((centered.x + centered.y) * iso_scale.y - inp.pos.y * height_scale) * zoom
    );
    float2 clip;
    clip.x = (screen.x / output_size.x) * 2.0 - 1.0;
    clip.y = 1.0 - (screen.y / output_size.y) * 2.0;
    o.pos = float4(clip, 0.0, 1.0);
    o.worldPos = inp.pos;
    o.normal0 = inp.normal0;
    o.uv0 = inp.uv0;
    o.color0 = inp.color0;
    o.faceKind0 = inp.faceKind0;
    o.cliffDistance0 = inp.cliffDistance0;
    return o;
}
)";

static const char* fs_src_hlsl = R"(
Texture2D grass_tex: register(t0);
Texture2D rock_tex: register(t1);
SamplerState material_smp: register(s0);

cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 options0;
    float4 options1;
    float4 options2;
};

struct PSIn {
    float4 pos: SV_Position;
    float3 worldPos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float2 uv0: TEXCOORD2;
    float4 color0: TEXCOORD3;
    float faceKind0: TEXCOORD4;
    float cliffDistance0: TEXCOORD5;
};

float hash21(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 macroWarp(float2 uv, float3 worldPos) {
    float macroScale = max(0.001, options2.x);
    float macroStrength = options2.y;
    float n0 = hash21(worldPos.xz * macroScale + float2(11.3, 7.1));
    float n1 = hash21(worldPos.xz * macroScale * 0.73 + float2(3.7, 19.1));
    return uv + (float2(n0, n1) - 0.5) * macroStrength;
}

float4 main(PSIn inp): SV_Target0 {
    float ambient = options0.x;
    float diffuseStrength = options0.y;
    float wallBrightness = options0.z;
    float textureScale = max(0.001, options0.w);
    float cliffRadius = max(0.001, options1.x);
    float cliffStrength = options1.y;
    float minTopBrightness = options1.z;
    float edgeDarkness = options1.w;
    float debugMode = options2.z;

    bool wall = inp.faceKind0 > 0.5;
    float2 uv = macroWarp(inp.uv0 * textureScale, inp.worldPos);
    float4 albedo = wall ? rock_tex.Sample(material_smp, uv) : grass_tex.Sample(material_smp, uv);
    albedo *= inp.color0;

    float3 n = normalize(inp.normal0);
    if (n.y < 0.0) {
        n = -n;
    }
    float3 l = normalize(light_dir.xyz);
    float lambert = max(0.0, dot(n, l));
    float wrapped = clamp(dot(n, l) * 0.5 + 0.5, 0.0, 1.0);
    float cliffProximity = clamp(1.0 - inp.cliffDistance0 / cliffRadius, 0.0, 1.0);

    float lighting = wall
        ? wallBrightness * (ambient + diffuseStrength * wrapped)
        : ambient + diffuseStrength * lambert;
    if (!wall) {
        float topDarkening = max(minTopBrightness, 1.0 - cliffStrength * cliffProximity);
        lighting *= topDarkening * (1.0 - edgeDarkness * cliffProximity);
    }

    float4 lit = float4(albedo.rgb * lighting, albedo.a);

    if (debugMode > 4.5) {
        return inp.color0;
    } else if (debugMode > 3.5) {
        return float4(cliffProximity, cliffProximity, cliffProximity, 1.0);
    } else if (debugMode > 2.5) {
        return float4(frac(uv), 0.35, 1.0);
    } else if (debugMode > 1.5) {
        return float4(n * 0.5 + 0.5, 1.0);
    } else if (debugMode > 0.5) {
        return albedo;
    }
    return lit;
}
)";

std::size_t nextCapacity(std::size_t value) {
    std::size_t capacity = 4096;
    while (capacity < value) {
        capacity *= 2;
    }
    return capacity;
}

void pushVertex(
    std::vector<MeshPreviewRenderer::Vertex>& vertices,
    const glm::vec3& position,
    const glm::vec3& normal,
    const glm::vec2& uv,
    const glm::vec4& color,
    float faceKind,
    float cliffDistance) {

    vertices.push_back({
        {position.x, position.y, position.z},
        {normal.x, normal.y, normal.z},
        {uv.x, uv.y},
        {color.r, color.g, color.b, color.a},
        faceKind,
        cliffDistance,
    });
}

} // namespace

void MeshPreviewRenderer::init() {
    if (m_initialized) {
        return;
    }

    ensurePipeline();
    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.label = "mesh-preview-output-sampler";
    m_outputSampler = sg_make_sampler(&samplerDesc);
    m_initialized = true;
}

void MeshPreviewRenderer::shutdown() {
    destroyTarget();
    destroyVertexBuffer();
    destroyPipeline();
    if (m_outputSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_outputSampler);
        m_outputSampler = {SG_INVALID_ID};
    }
    m_vertices.clear();
    m_initialized = false;
}

bool MeshPreviewRenderer::render(
    const std::vector<MeshPreviewQuad>& quads,
    const MeshPreviewRenderParams& params,
    sg_view grassView,
    sg_view rockView,
    sg_sampler materialSampler,
    int width,
    int height) {

    if (!m_initialized) {
        init();
    }
    if (m_pipeline.id == SG_INVALID_ID || grassView.id == SG_INVALID_ID || rockView.id == SG_INVALID_ID ||
        materialSampler.id == SG_INVALID_ID || width <= 0 || height <= 0) {
        return false;
    }

    ensureTarget(width, height);
    if (!validOutput() || m_outputAttachmentView.id == SG_INVALID_ID) {
        return false;
    }

    m_vertices.clear();
    m_vertices.reserve(quads.size() * 6);

    std::vector<const MeshPreviewQuad*> drawOrder;
    drawOrder.reserve(quads.size());
    for (const MeshPreviewQuad& quad : quads) {
        drawOrder.push_back(&quad);
    }
    std::sort(drawOrder.begin(), drawOrder.end(), [](const MeshPreviewQuad* lhs, const MeshPreviewQuad* rhs) {
        return lhs->depth < rhs->depth;
    });

    for (const MeshPreviewQuad* quad : drawOrder) {
        pushVertex(m_vertices, quad->a, quad->normal, quad->uvA, quad->color, quad->faceKind, quad->cliffDistance);
        pushVertex(m_vertices, quad->b, quad->normal, quad->uvB, quad->color, quad->faceKind, quad->cliffDistance);
        pushVertex(m_vertices, quad->c, quad->normal, quad->uvC, quad->color, quad->faceKind, quad->cliffDistance);
        pushVertex(m_vertices, quad->a, quad->normal, quad->uvA, quad->color, quad->faceKind, quad->cliffDistance);
        pushVertex(m_vertices, quad->c, quad->normal, quad->uvC, quad->color, quad->faceKind, quad->cliffDistance);
        pushVertex(m_vertices, quad->d, quad->normal, quad->uvD, quad->color, quad->faceKind, quad->cliffDistance);
    }

    ensureVertexBuffer(m_vertices.size());
    if (m_vertexBuffer.id == SG_INVALID_ID) {
        return false;
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.075f, 0.085f, 0.105f, 1.0f};

    sg_pass pass = {};
    pass.action = action;
    pass.attachments.colors[0] = m_outputAttachmentView;
    sg_begin_pass(&pass);

    if (!m_vertices.empty()) {
        sg_range vertexRange = {m_vertices.data(), m_vertices.size() * sizeof(Vertex)};
        sg_update_buffer(m_vertexBuffer, &vertexRange);

        m_bindings.vertex_buffers[0] = m_vertexBuffer;
        m_bindings.views[0] = grassView;
        m_bindings.views[1] = rockView;
        m_bindings.samplers[0] = materialSampler;

        sg_apply_pipeline(m_pipeline);

        VsParams vs = {};
        vs.outputSize[0] = (float)width;
        vs.outputSize[1] = (float)height;
        vs.worldCenter[0] = params.worldCenter.x;
        vs.worldCenter[1] = params.worldCenter.y;
        vs.pan[0] = params.pan.x;
        vs.pan[1] = params.pan.y;
        vs.zoom = params.zoom;
        vs.anchorY = params.anchorY;
        vs.isoScale[0] = params.isoScaleX;
        vs.isoScale[1] = params.isoScaleY;
        vs.heightScale = params.heightScale;
        sg_range vsRange = {&vs, sizeof(vs)};
        sg_apply_uniforms(0, &vsRange);

        FsParams fs = {};
        fs.lightDir[0] = params.lightDirection.x;
        fs.lightDir[1] = params.lightDirection.y;
        fs.lightDir[2] = params.lightDirection.z;
        fs.lightDir[3] = 0.0f;
        fs.options0[0] = params.ambient;
        fs.options0[1] = params.diffuse;
        fs.options0[2] = params.wallBrightness;
        fs.options0[3] = params.textureScale;
        fs.options1[0] = params.cliffDarkeningRadius;
        fs.options1[1] = params.cliffDarkeningStrength;
        fs.options1[2] = params.minTopBrightness;
        fs.options1[3] = params.edgeDarkness;
        fs.options2[0] = params.macroScale;
        fs.options2[1] = params.macroStrength;
        fs.options2[2] = (float)params.debugMode;
        fs.options2[3] = 0.0f;
        sg_range fsRange = {&fs, sizeof(fs)};
        sg_apply_uniforms(1, &fsRange);

        sg_apply_bindings(&m_bindings);
        sg_draw(0, (int)m_vertices.size(), 1);
    }

    sg_end_pass();
    return true;
}

bool MeshPreviewRenderer::validOutput() const {
    return m_outputTextureView.id != SG_INVALID_ID && m_outputSampler.id != SG_INVALID_ID;
}

void MeshPreviewRenderer::ensurePipeline() {
    if (m_pipeline.id != SG_INVALID_ID) {
        return;
    }

    sg_shader_desc shdDesc = {};
#if defined(SOKOL_D3D11)
    shdDesc.vertex_func.source = vs_src_hlsl;
    shdDesc.fragment_func.source = fs_src_hlsl;
    for (int i = 0; i < 6; i++) {
        shdDesc.attrs[i].hlsl_sem_name = "TEXCOORD";
        shdDesc.attrs[i].hlsl_sem_index = (std::uint8_t)i;
    }
#else
    shdDesc.vertex_func.source = vs_src_glsl;
    shdDesc.fragment_func.source = fs_src_glsl;
#endif

    shdDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shdDesc.uniform_blocks[0].size = sizeof(VsParams);
    shdDesc.uniform_blocks[0].hlsl_register_b_n = 0;
    shdDesc.uniform_blocks[0].msl_buffer_n = 0;
    shdDesc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shdDesc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shdDesc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "output_size";
    shdDesc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    shdDesc.uniform_blocks[0].glsl_uniforms[1].glsl_name = "world_center";
    shdDesc.uniform_blocks[0].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT2;
    shdDesc.uniform_blocks[0].glsl_uniforms[2].glsl_name = "pan";
    shdDesc.uniform_blocks[0].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT2;
    shdDesc.uniform_blocks[0].glsl_uniforms[3].glsl_name = "zoom";
    shdDesc.uniform_blocks[0].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    shdDesc.uniform_blocks[0].glsl_uniforms[4].glsl_name = "anchor_y";
    shdDesc.uniform_blocks[0].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    shdDesc.uniform_blocks[0].glsl_uniforms[5].glsl_name = "iso_scale";
    shdDesc.uniform_blocks[0].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT2;
    shdDesc.uniform_blocks[0].glsl_uniforms[6].glsl_name = "height_scale";
    shdDesc.uniform_blocks[0].glsl_uniforms[6].type = SG_UNIFORMTYPE_FLOAT;

    shdDesc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shdDesc.uniform_blocks[1].size = sizeof(FsParams);
    shdDesc.uniform_blocks[1].hlsl_register_b_n = 0;
    shdDesc.uniform_blocks[1].msl_buffer_n = 1;
    shdDesc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shdDesc.uniform_blocks[1].spirv_set0_binding_n = 1;
    shdDesc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shdDesc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shdDesc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "options0";
    shdDesc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    shdDesc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "options1";
    shdDesc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT4;
    shdDesc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "options2";
    shdDesc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT4;

    const char* glslNames[] = {"grass_tex", "rock_tex"};
    for (int i = 0; i < 2; i++) {
        shdDesc.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shdDesc.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shdDesc.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shdDesc.views[i].texture.hlsl_register_t_n = (std::uint8_t)i;
        shdDesc.views[i].texture.msl_texture_n = (std::uint8_t)i;
        shdDesc.views[i].texture.wgsl_group1_binding_n = (std::uint8_t)i;
        shdDesc.views[i].texture.spirv_set1_binding_n = (std::uint8_t)i;
        shdDesc.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shdDesc.texture_sampler_pairs[i].view_slot = i;
        shdDesc.texture_sampler_pairs[i].sampler_slot = 0;
        shdDesc.texture_sampler_pairs[i].glsl_name = glslNames[i];
    }
    shdDesc.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shdDesc.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shdDesc.samplers[0].hlsl_register_s_n = 0;
    shdDesc.samplers[0].msl_sampler_n = 0;
    shdDesc.samplers[0].wgsl_group1_binding_n = 2;
    shdDesc.samplers[0].spirv_set1_binding_n = 2;

    m_shader = sg_make_shader(&shdDesc);
    if (m_shader.id == SG_INVALID_ID) {
        return;
    }

    sg_pipeline_desc pipDesc = {};
    pipDesc.shader = m_shader;
    pipDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pipDesc.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4;
    pipDesc.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
    pipDesc.layout.attrs[5].format = SG_VERTEXFORMAT_FLOAT;
    pipDesc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    pipDesc.colors[0].blend.enabled = true;
    pipDesc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pipDesc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipDesc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pipDesc.depth.pixel_format = SG_PIXELFORMAT_NONE;
    pipDesc.label = "mesh-preview-renderer-pipeline";
    m_pipeline = sg_make_pipeline(&pipDesc);
}

void MeshPreviewRenderer::destroyPipeline() {
    if (m_pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_pipeline);
        m_pipeline = {SG_INVALID_ID};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {SG_INVALID_ID};
    }
}

void MeshPreviewRenderer::ensureTarget(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (m_width == width && m_height == height && validOutput() && m_outputAttachmentView.id != SG_INVALID_ID) {
        return;
    }

    destroyTarget();
    m_width = width;
    m_height = height;

    sg_image_desc imageDesc = {};
    imageDesc.usage.color_attachment = true;
    imageDesc.width = width;
    imageDesc.height = height;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.label = "mesh-preview-output-image";
    m_outputImage = sg_make_image(&imageDesc);
    if (m_outputImage.id == SG_INVALID_ID) {
        destroyTarget();
        return;
    }

    sg_view_desc attachmentViewDesc = {};
    attachmentViewDesc.color_attachment.image = m_outputImage;
    m_outputAttachmentView = sg_make_view(&attachmentViewDesc);

    sg_view_desc textureViewDesc = {};
    textureViewDesc.texture.image = m_outputImage;
    m_outputTextureView = sg_make_view(&textureViewDesc);

    if (m_outputAttachmentView.id == SG_INVALID_ID || m_outputTextureView.id == SG_INVALID_ID) {
        destroyTarget();
    }
}

void MeshPreviewRenderer::destroyTarget() {
    if (m_outputTextureView.id != SG_INVALID_ID) {
        sg_destroy_view(m_outputTextureView);
        m_outputTextureView = {SG_INVALID_ID};
    }
    if (m_outputAttachmentView.id != SG_INVALID_ID) {
        sg_destroy_view(m_outputAttachmentView);
        m_outputAttachmentView = {SG_INVALID_ID};
    }
    if (m_outputImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_outputImage);
        m_outputImage = {SG_INVALID_ID};
    }
    m_width = 0;
    m_height = 0;
}

void MeshPreviewRenderer::ensureVertexBuffer(std::size_t vertexCount) {
    if (vertexCount == 0) {
        return;
    }
    if (m_vertexBuffer.id != SG_INVALID_ID && vertexCount <= m_vertexCapacity) {
        return;
    }

    destroyVertexBuffer();
    m_vertexCapacity = nextCapacity(vertexCount);

    sg_buffer_desc bufferDesc = {};
    bufferDesc.size = m_vertexCapacity * sizeof(Vertex);
    bufferDesc.usage.dynamic_update = true;
    bufferDesc.label = "mesh-preview-vertices";
    m_vertexBuffer = sg_make_buffer(&bufferDesc);
    if (m_vertexBuffer.id == SG_INVALID_ID) {
        m_vertexCapacity = 0;
    }
}

void MeshPreviewRenderer::destroyVertexBuffer() {
    if (m_vertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vertexBuffer);
        m_vertexBuffer = {SG_INVALID_ID};
    }
    m_vertexCapacity = 0;
}

} // namespace render_core
