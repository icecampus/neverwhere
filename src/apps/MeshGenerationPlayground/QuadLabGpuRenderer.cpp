#include "QuadLabGpuRenderer.h"

#include "MeshPreview.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <spdlog/spdlog.h>

namespace meshgen_playground {
namespace {

struct VsParams {
    glm::mat4 mvp;
};

#if defined(SOKOL_D3D11)
const char* kMeshVsSrc = R"(
cbuffer vs_params: register(b0) {
    float4x4 mvp;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float4 color0: TEXCOORD2;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color: TEXCOORD1;
};
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.normal0 = inp.normal0;
    o.color = inp.color0;
    return o;
}
)";

const char* kMeshFsSrc = R"(
struct PSIn {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color: TEXCOORD1;
};
float4 main(PSIn inp): SV_Target0 {
    float3 n = normalize(inp.normal0);
    float3 light = normalize(float3(0.35, 0.82, 0.45));
    float ndotl = max(dot(n, light), 0.0);
    float shade = 0.55 + 0.45 * ndotl;
    return float4(inp.color.rgb * shade, inp.color.a);
}
)";

const char* kLineVsSrc = R"(
cbuffer vs_params: register(b0) {
    float4x4 mvp;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float4 color0: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.color = inp.color0;
    return o;
}
)";

const char* kLineFsSrc = R"(
struct PSIn {
    float4 pos: SV_Position;
    float4 color: TEXCOORD0;
};
float4 main(PSIn inp): SV_Target0 {
    return inp.color;
}
)";
#else
const char* kMeshVsSrc = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal0;
layout(location=2) in vec4 color0;
out vec3 v_normal;
out vec4 v_color;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_normal = normal0;
    v_color = color0;
}
)";

const char* kMeshFsSrc = R"(
#version 330
in vec3 v_normal;
in vec4 v_color;
out vec4 frag_color;
void main() {
    vec3 n = normalize(v_normal);
    vec3 light = normalize(vec3(0.35, 0.82, 0.45));
    float ndotl = max(dot(n, light), 0.0);
    float shade = 0.55 + 0.45 * ndotl;
    frag_color = vec4(v_color.rgb * shade, v_color.a);
}
)";

const char* kLineVsSrc = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color0;
out vec4 v_color;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_color = color0;
}
)";

const char* kLineFsSrc = R"(
#version 330
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";
#endif

struct MeshVertex {
    float pos[3];
    float normal[3];
    float color[4];
};

struct LineVertex {
    float pos[3];
    float color[4];
};

glm::vec4 imguiColorToGlm(ImU32 color) {
    return glm::vec4(
        ((color >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
        ((color >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
        ((color >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
        ((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

void pushTriangleVertex(
    std::vector<MeshVertex>& vertices,
    const Vec3& position,
    const Vec3& normal,
    const glm::vec4& color) {

    MeshVertex vertex{};
    vertex.pos[0] = position.x;
    vertex.pos[1] = position.y;
    vertex.pos[2] = position.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    vertex.color[0] = color.r;
    vertex.color[1] = color.g;
    vertex.color[2] = color.b;
    vertex.color[3] = color.a;
    vertices.push_back(vertex);
}

void pushLineVertex(
    std::vector<LineVertex>& vertices,
    const Vec3& position,
    const glm::vec4& color) {

    LineVertex vertex{};
    vertex.pos[0] = position.x;
    vertex.pos[1] = position.y;
    vertex.pos[2] = position.z;
    vertex.color[0] = color.r;
    vertex.color[1] = color.g;
    vertex.color[2] = color.b;
    vertex.color[3] = color.a;
    vertices.push_back(vertex);
}

glm::mat4 makeQuadLabMvp(
    const QuadLabPreviewCamera& camera,
    const MeshQuadsPreviewOptions& options,
    float aspect) {

    const glm::vec3 pivot(
        options.projectionCenterX,
        options.projectionCenterY,
        options.projectionCenterZ);

    const float yaw = glm::radians(camera.orbitYawDegrees);
    const float pitch = glm::radians(camera.orbitPitchDegrees);
    const float distance = 4.5f / std::max(0.35f, camera.zoom);
    const float cosPitch = std::cos(pitch);
    const glm::vec3 eyeOffset{
        distance * cosPitch * std::sin(yaw),
        distance * std::sin(pitch),
        distance * cosPitch * std::cos(yaw),
    };

    glm::vec3 eye = pivot + eyeOffset;
    glm::vec3 target = pivot;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = glm::normalize(target - eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    const glm::vec3 up = glm::cross(right, forward);
    const float panScale = 0.0045f / camera.zoom;
    eye -= right * camera.pan.x * panScale;
    eye -= up * camera.pan.y * panScale;
    target -= right * camera.pan.x * panScale;
    target -= up * camera.pan.y * panScale;

    const glm::mat4 view = glm::lookAt(eye, target, worldUp);
    const glm::mat4 projection = glm::perspective(glm::radians(42.0f), std::max(0.1f, aspect), 0.05f, 64.0f);
    return projection * view;
}

} // namespace

void QuadLabGpuRenderer::init() {
    if (m_initialized) {
        return;
    }
    ensurePipeline();
    m_initialized = m_meshPipeline.id != SG_INVALID_ID && m_linePipeline.id != SG_INVALID_ID;
}

void QuadLabGpuRenderer::shutdown() {
    destroyTarget();
    destroyPipeline();
    if (m_triangleBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_triangleBuffer);
        m_triangleBuffer = {SG_INVALID_ID};
    }
    if (m_lineBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_lineBuffer);
        m_lineBuffer = {SG_INVALID_ID};
    }
    m_initialized = false;
}

bool QuadLabGpuRenderer::validOutput() const {
    return m_colorTextureView.id != SG_INVALID_ID && m_outputSampler.id != SG_INVALID_ID;
}

bool QuadLabGpuRenderer::render(
    const std::vector<MeshQuad>& quads,
    const QuadLabPreviewCamera& camera,
    const MeshQuadsPreviewOptions& options,
    int width,
    int height) {

    if (!m_initialized) {
        init();
    }
    if (!m_initialized || width <= 0 || height <= 0) {
        return false;
    }

    ensureTarget(width, height);
    if (!validOutput() || m_colorAttachmentView.id == SG_INVALID_ID || m_depthAttachmentView.id == SG_INVALID_ID) {
        return false;
    }

    std::vector<MeshVertex> triangleVertices;
    std::vector<LineVertex> lineVertices;
    triangleVertices.reserve(quads.size() * 6);
    lineVertices.reserve(quads.size() * 8);

    const glm::vec4 wireColor(0.08f, 0.08f, 0.08f, 0.85f);
    for (const MeshQuad& quad : quads) {
        const glm::vec4 faceColor = imguiColorToGlm(quad.color);
        pushTriangleVertex(triangleVertices, quad.a, quad.normal, faceColor);
        pushTriangleVertex(triangleVertices, quad.b, quad.normal, faceColor);
        pushTriangleVertex(triangleVertices, quad.c, quad.normal, faceColor);
        pushTriangleVertex(triangleVertices, quad.a, quad.normal, faceColor);
        pushTriangleVertex(triangleVertices, quad.c, quad.normal, faceColor);
        pushTriangleVertex(triangleVertices, quad.d, quad.normal, faceColor);

        if (options.showWireframe) {
            pushLineVertex(lineVertices, quad.a, wireColor);
            pushLineVertex(lineVertices, quad.b, wireColor);
            pushLineVertex(lineVertices, quad.b, wireColor);
            pushLineVertex(lineVertices, quad.c, wireColor);
            pushLineVertex(lineVertices, quad.c, wireColor);
            pushLineVertex(lineVertices, quad.d, wireColor);
            pushLineVertex(lineVertices, quad.d, wireColor);
            pushLineVertex(lineVertices, quad.a, wireColor);
        }
    }

    ensureVertexBuffers(triangleVertices.size(), lineVertices.size());
    if (m_triangleBuffer.id == SG_INVALID_ID) {
        return false;
    }

    if (!triangleVertices.empty()) {
        const sg_range triangleRange = {triangleVertices.data(), triangleVertices.size() * sizeof(MeshVertex)};
        sg_update_buffer(m_triangleBuffer, &triangleRange);
    }
    if (!lineVertices.empty() && m_lineBuffer.id != SG_INVALID_ID) {
        const sg_range lineRange = {lineVertices.data(), lineVertices.size() * sizeof(LineVertex)};
        sg_update_buffer(m_lineBuffer, &lineRange);
    }

    const float aspect = (float)width / (float)std::max(1, height);
    VsParams vsParams{};
    vsParams.mvp = makeQuadLabMvp(camera, options, aspect);

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.075f, 0.085f, 0.105f, 1.0f};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;

    sg_pass pass = {};
    pass.action = action;
    pass.attachments.colors[0] = m_colorAttachmentView;
    pass.attachments.depth_stencil = m_depthAttachmentView;
    sg_begin_pass(&pass);

    if (!triangleVertices.empty()) {
        m_bindings.vertex_buffers[0] = m_triangleBuffer;
        sg_apply_pipeline(m_meshPipeline);
        sg_apply_bindings(&m_bindings);
        const sg_range vsRange = {&vsParams, sizeof(vsParams)};
        sg_apply_uniforms(0, &vsRange);
        sg_draw(0, (int)triangleVertices.size(), 1);
    }

    if (options.showWireframe && !lineVertices.empty() && m_lineBuffer.id != SG_INVALID_ID) {
        m_bindings.vertex_buffers[0] = m_lineBuffer;
        sg_apply_pipeline(m_linePipeline);
        sg_apply_bindings(&m_bindings);
        const sg_range vsRange = {&vsParams, sizeof(vsParams)};
        sg_apply_uniforms(0, &vsRange);
        sg_draw(0, (int)lineVertices.size(), 1);
    }

    sg_end_pass();
    return true;
}

void QuadLabGpuRenderer::ensurePipeline() {
    if (m_meshPipeline.id != SG_INVALID_ID) {
        return;
    }

    sg_shader_desc meshShaderDesc = {};
    meshShaderDesc.vertex_func.source = kMeshVsSrc;
    meshShaderDesc.fragment_func.source = kMeshFsSrc;
#if defined(SOKOL_D3D11)
    meshShaderDesc.attrs[0].hlsl_sem_name = "TEXCOORD";
    meshShaderDesc.attrs[0].hlsl_sem_index = 0;
    meshShaderDesc.attrs[1].hlsl_sem_name = "TEXCOORD";
    meshShaderDesc.attrs[1].hlsl_sem_index = 1;
    meshShaderDesc.attrs[2].hlsl_sem_name = "TEXCOORD";
    meshShaderDesc.attrs[2].hlsl_sem_index = 2;
#endif
    meshShaderDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    meshShaderDesc.uniform_blocks[0].size = sizeof(VsParams);
    meshShaderDesc.uniform_blocks[0].hlsl_register_b_n = 0;
    meshShaderDesc.uniform_blocks[0].msl_buffer_n = 0;
    meshShaderDesc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    meshShaderDesc.uniform_blocks[0].spirv_set0_binding_n = 0;
    meshShaderDesc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    meshShaderDesc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    meshShaderDesc.label = "quad-lab-mesh-shader";
    m_meshShader = sg_make_shader(&meshShaderDesc);

    sg_shader_desc lineShaderDesc = {};
    lineShaderDesc.vertex_func.source = kLineVsSrc;
    lineShaderDesc.fragment_func.source = kLineFsSrc;
#if defined(SOKOL_D3D11)
    lineShaderDesc.attrs[0].hlsl_sem_name = "TEXCOORD";
    lineShaderDesc.attrs[0].hlsl_sem_index = 0;
    lineShaderDesc.attrs[1].hlsl_sem_name = "TEXCOORD";
    lineShaderDesc.attrs[1].hlsl_sem_index = 1;
#endif
    lineShaderDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    lineShaderDesc.uniform_blocks[0].size = sizeof(VsParams);
    lineShaderDesc.uniform_blocks[0].hlsl_register_b_n = 0;
    lineShaderDesc.uniform_blocks[0].msl_buffer_n = 0;
    lineShaderDesc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    lineShaderDesc.uniform_blocks[0].spirv_set0_binding_n = 0;
    lineShaderDesc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    lineShaderDesc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    lineShaderDesc.label = "quad-lab-line-shader";
    m_lineShader = sg_make_shader(&lineShaderDesc);

    if (m_meshShader.id == SG_INVALID_ID || m_lineShader.id == SG_INVALID_ID) {
        spdlog::error("QuadLabGpuRenderer: shader creation failed");
        return;
    }

    sg_pipeline_desc meshPipelineDesc = {};
    meshPipelineDesc.shader = m_meshShader;
    meshPipelineDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    meshPipelineDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    meshPipelineDesc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
    meshPipelineDesc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    meshPipelineDesc.cull_mode = SG_CULLMODE_BACK;
    meshPipelineDesc.face_winding = SG_FACEWINDING_CCW;
    meshPipelineDesc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    meshPipelineDesc.colors[0].blend.enabled = false;
    meshPipelineDesc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    meshPipelineDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    meshPipelineDesc.depth.write_enabled = true;
    meshPipelineDesc.label = "quad-lab-mesh-pipeline";
    m_meshPipeline = sg_make_pipeline(&meshPipelineDesc);

    sg_pipeline_desc linePipelineDesc = {};
    linePipelineDesc.shader = m_lineShader;
    linePipelineDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    linePipelineDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    linePipelineDesc.primitive_type = SG_PRIMITIVETYPE_LINES;
    linePipelineDesc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    linePipelineDesc.colors[0].blend.enabled = true;
    linePipelineDesc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    linePipelineDesc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    linePipelineDesc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    linePipelineDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    linePipelineDesc.depth.write_enabled = false;
    linePipelineDesc.label = "quad-lab-line-pipeline";
    m_linePipeline = sg_make_pipeline(&linePipelineDesc);
}

void QuadLabGpuRenderer::destroyPipeline() {
    if (m_linePipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_linePipeline);
        m_linePipeline = {SG_INVALID_ID};
    }
    if (m_meshPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_meshPipeline);
        m_meshPipeline = {SG_INVALID_ID};
    }
    if (m_lineShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_lineShader);
        m_lineShader = {SG_INVALID_ID};
    }
    if (m_meshShader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_meshShader);
        m_meshShader = {SG_INVALID_ID};
    }
}

void QuadLabGpuRenderer::ensureTarget(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (m_width == width && m_height == height && validOutput() && m_depthAttachmentView.id != SG_INVALID_ID) {
        return;
    }

    destroyTarget();
    m_width = width;
    m_height = height;

    sg_image_desc colorDesc = {};
    colorDesc.usage.color_attachment = true;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    colorDesc.sample_count = 1;
    colorDesc.label = "quad-lab-color";
    m_colorImage = sg_make_image(&colorDesc);

    sg_image_desc depthDesc = {};
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depthDesc.sample_count = 1;
    depthDesc.label = "quad-lab-depth";
    m_depthImage = sg_make_image(&depthDesc);

    if (m_colorImage.id == SG_INVALID_ID || m_depthImage.id == SG_INVALID_ID) {
        destroyTarget();
        return;
    }

    sg_view_desc colorAttachDesc = {};
    colorAttachDesc.color_attachment.image = m_colorImage;
    m_colorAttachmentView = sg_make_view(&colorAttachDesc);

    sg_view_desc depthAttachDesc = {};
    depthAttachDesc.depth_stencil_attachment.image = m_depthImage;
    m_depthAttachmentView = sg_make_view(&depthAttachDesc);

    sg_view_desc colorTexDesc = {};
    colorTexDesc.texture.image = m_colorImage;
    m_colorTextureView = sg_make_view(&colorTexDesc);

    if (m_colorAttachmentView.id == SG_INVALID_ID || m_depthAttachmentView.id == SG_INVALID_ID
        || m_colorTextureView.id == SG_INVALID_ID) {
        destroyTarget();
        return;
    }

    if (m_outputSampler.id == SG_INVALID_ID) {
        sg_sampler_desc samplerDesc = {};
        samplerDesc.min_filter = SG_FILTER_LINEAR;
        samplerDesc.mag_filter = SG_FILTER_LINEAR;
        samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        samplerDesc.label = "quad-lab-output-sampler";
        m_outputSampler = sg_make_sampler(&samplerDesc);
    }
}

void QuadLabGpuRenderer::destroyTarget() {
    if (m_outputSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_outputSampler);
        m_outputSampler = {SG_INVALID_ID};
    }
    if (m_colorTextureView.id != SG_INVALID_ID) {
        sg_destroy_view(m_colorTextureView);
        m_colorTextureView = {SG_INVALID_ID};
    }
    if (m_depthAttachmentView.id != SG_INVALID_ID) {
        sg_destroy_view(m_depthAttachmentView);
        m_depthAttachmentView = {SG_INVALID_ID};
    }
    if (m_colorAttachmentView.id != SG_INVALID_ID) {
        sg_destroy_view(m_colorAttachmentView);
        m_colorAttachmentView = {SG_INVALID_ID};
    }
    if (m_depthImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_depthImage);
        m_depthImage = {SG_INVALID_ID};
    }
    if (m_colorImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_colorImage);
        m_colorImage = {SG_INVALID_ID};
    }
    m_width = 0;
    m_height = 0;
}

std::size_t nextBufferCapacity(std::size_t requiredCount) {
    std::size_t capacity = 64;
    while (capacity < requiredCount) {
        capacity *= 2;
    }
    return capacity;
}

void QuadLabGpuRenderer::ensureVertexBuffers(std::size_t triangleVertexCount, std::size_t lineVertexCount) {
    const std::size_t requiredTriangles = std::max<std::size_t>(triangleVertexCount, 1);
    const std::size_t requiredLines = std::max<std::size_t>(lineVertexCount, 1);

    if (m_triangleBuffer.id == SG_INVALID_ID || requiredTriangles > m_triangleCapacity) {
        if (m_triangleBuffer.id != SG_INVALID_ID) {
            sg_destroy_buffer(m_triangleBuffer);
        }
        m_triangleCapacity = nextBufferCapacity(requiredTriangles);
        sg_buffer_desc triangleDesc = {};
        triangleDesc.usage.vertex_buffer = true;
        triangleDesc.usage.dynamic_update = true;
        triangleDesc.size = m_triangleCapacity * sizeof(MeshVertex);
        triangleDesc.label = "quad-lab-triangle-buffer";
        m_triangleBuffer = sg_make_buffer(&triangleDesc);
    }

    if (m_lineBuffer.id == SG_INVALID_ID || requiredLines > m_lineCapacity) {
        if (m_lineBuffer.id != SG_INVALID_ID) {
            sg_destroy_buffer(m_lineBuffer);
        }
        m_lineCapacity = nextBufferCapacity(requiredLines);
        sg_buffer_desc lineDesc = {};
        lineDesc.usage.vertex_buffer = true;
        lineDesc.usage.dynamic_update = true;
        lineDesc.size = m_lineCapacity * sizeof(LineVertex);
        lineDesc.label = "quad-lab-line-buffer";
        m_lineBuffer = sg_make_buffer(&lineDesc);
    }
}

} // namespace meshgen_playground
