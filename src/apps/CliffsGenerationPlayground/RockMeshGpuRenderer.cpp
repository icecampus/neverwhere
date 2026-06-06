#include "RockMeshGpuRenderer.h"
#include "RockFractureRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace render_playground {
namespace {

#if defined(SOKOL_D3D11)
const char* kVsSrc = R"(
cbuffer vs_params: register(b0) {
    float4x4 mvp;
};
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 worldNormal: TEXCOORD0;
    float3 worldPos: TEXCOORD1;
};
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.worldNormal = inp.normal0;
    o.worldPos = inp.pos;
    return o;
}
)";

const char* kFsSrc = R"(
cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 rock_tint;
    float ambient_strength;
    float diffuse_strength;
    float specular_strength;
    float shininess;
    float3 pad0;
};
struct PSIn {
    float4 pos: SV_Position;
    float3 worldNormal: TEXCOORD0;
    float3 worldPos: TEXCOORD1;
};
float4 main(PSIn inp): SV_Target0 {
    float3 n = normalize(inp.worldNormal);
    float3 l = normalize(light_dir.xyz);
    float ndotl = max(dot(n, l), 0.0);
    float upDot = saturate(n.z * 0.5 + 0.5);
    float3 ground = float3(0.18, 0.16, 0.14);
    float3 sky = float3(0.42, 0.46, 0.52);
    float3 ambient = lerp(ground, sky, upDot) * ambient_strength;
    float3 diffuse = rock_tint.rgb * diffuse_strength * ndotl;
    float3 v = normalize(-inp.worldPos);
    float3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), shininess) * specular_strength;
    return float4(ambient + diffuse + spec, 1.0);
}
)";
#else
const char* kVsSrc = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal0;
uniform mat4 mvp;
out vec3 v_worldNormal;
out vec3 v_worldPos;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_worldNormal = normal0;
    v_worldPos = pos;
}
)";

const char* kFsSrc = R"(
#version 330
in vec3 v_worldNormal;
in vec3 v_worldPos;
out vec4 frag_color;
uniform vec4 light_dir;
uniform vec4 rock_tint;
uniform float ambient_strength;
uniform float diffuse_strength;
uniform float specular_strength;
uniform float shininess;
void main() {
    vec3 n = normalize(v_worldNormal);
    vec3 l = normalize(light_dir.xyz);
    float ndotl = max(dot(n, l), 0.0);
    float upDot = clamp(n.z * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground = vec3(0.18, 0.16, 0.14);
    vec3 sky = vec3(0.42, 0.46, 0.52);
    vec3 ambient = mix(ground, sky, upDot) * ambient_strength;
    vec3 diffuse = rock_tint.rgb * diffuse_strength * ndotl;
    vec3 v = normalize(-v_worldPos);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), shininess) * specular_strength;
    frag_color = vec4(ambient + diffuse + vec3(spec), 1.0);
}
)";
#endif

int nextCapacity(int value) {
    int cap = 4096;
    while (cap < value) {
        cap *= 2;
    }
    return cap;
}

} // namespace

void RockMeshGpuRenderer::init() {
    if (m_initialized) {
        return;
    }

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.label = "rock-mesh-output-sampler";
    m_outputSampler = sg_make_sampler(&samplerDesc);

    ensurePipeline();
    m_initialized = m_pipeline.id != SG_INVALID_ID && m_outputSampler.id != SG_INVALID_ID;
    spdlog::info("RockMeshGpuRenderer::init {}", m_initialized ? "OK" : "FAILED");
}

void RockMeshGpuRenderer::shutdown() {
    destroyMeshBuffers();
    destroyTarget();
    destroyPipeline();
    if (m_outputSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_outputSampler);
        m_outputSampler = {SG_INVALID_ID};
    }
    m_initialized = false;
}

bool RockMeshGpuRenderer::validOutput() const {
    return m_colorTextureView.id != SG_INVALID_ID && m_outputSampler.id != SG_INVALID_ID;
}

void RockMeshGpuRenderer::ensurePipeline() {
    if (m_pipeline.id != SG_INVALID_ID) {
        return;
    }

    sg_shader_desc shdDesc = {};
    shdDesc.vertex_func.source = kVsSrc;
    shdDesc.fragment_func.source = kFsSrc;
#if defined(SOKOL_D3D11)
    shdDesc.attrs[0].hlsl_sem_name = "TEXCOORD";
    shdDesc.attrs[0].hlsl_sem_index = 0;
    shdDesc.attrs[1].hlsl_sem_name = "TEXCOORD";
    shdDesc.attrs[1].hlsl_sem_index = 1;
#endif

    shdDesc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shdDesc.uniform_blocks[0].size = sizeof(VsParams);
    shdDesc.uniform_blocks[0].hlsl_register_b_n = 0;
    shdDesc.uniform_blocks[0].msl_buffer_n = 0;
    shdDesc.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shdDesc.uniform_blocks[0].spirv_set0_binding_n = 0;
    shdDesc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shdDesc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shdDesc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shdDesc.uniform_blocks[1].size = sizeof(FsParams);
    shdDesc.uniform_blocks[1].hlsl_register_b_n = 0;
    shdDesc.uniform_blocks[1].msl_buffer_n = 1;
    shdDesc.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shdDesc.uniform_blocks[1].spirv_set0_binding_n = 1;
    shdDesc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shdDesc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shdDesc.uniform_blocks[1].glsl_uniforms[1].glsl_name = "rock_tint";
    shdDesc.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    shdDesc.uniform_blocks[1].glsl_uniforms[2].glsl_name = "ambient_strength";
    shdDesc.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    shdDesc.uniform_blocks[1].glsl_uniforms[3].glsl_name = "diffuse_strength";
    shdDesc.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    shdDesc.uniform_blocks[1].glsl_uniforms[4].glsl_name = "specular_strength";
    shdDesc.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    shdDesc.uniform_blocks[1].glsl_uniforms[5].glsl_name = "shininess";
    shdDesc.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT;

    shdDesc.label = "rock-mesh-shader";
    m_shader = sg_make_shader(&shdDesc);
    if (m_shader.id == SG_INVALID_ID) {
        return;
    }

    sg_pipeline_desc pipDesc = {};
    pipDesc.shader = m_shader;
    pipDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pipDesc.index_type = SG_INDEXTYPE_UINT32;
    pipDesc.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pipDesc.cull_mode = SG_CULLMODE_BACK;
    pipDesc.face_winding = SG_FACEWINDING_CCW;
    pipDesc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    pipDesc.colors[0].blend.enabled = false;
    pipDesc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pipDesc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipDesc.depth.write_enabled = true;
    pipDesc.label = "rock-mesh-pipeline";
    m_pipeline = sg_make_pipeline(&pipDesc);
}

void RockMeshGpuRenderer::destroyPipeline() {
    if (m_pipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_pipeline);
        m_pipeline = {SG_INVALID_ID};
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader = {SG_INVALID_ID};
    }
}

void RockMeshGpuRenderer::ensureTarget(int width, int height) {
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
    colorDesc.label = "rock-mesh-color";
    m_colorImage = sg_make_image(&colorDesc);

    sg_image_desc depthDesc = {};
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depthDesc.sample_count = 1;
    depthDesc.label = "rock-mesh-depth";
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
    }
}

void RockMeshGpuRenderer::destroyTarget() {
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

void RockMeshGpuRenderer::ensureMeshBuffers(int vertexCount, int indexCount) {
    if (vertexCount <= 0 || indexCount <= 0) {
        return;
    }

    if (m_vertexBuffer.id != SG_INVALID_ID && vertexCount <= m_vertexCapacity && indexCount <= m_indexCapacity) {
        return;
    }

    destroyMeshBuffers();
    m_vertexCapacity = nextCapacity(vertexCount);
    m_indexCapacity = nextCapacity(indexCount);

    sg_buffer_desc vbDesc = {};
    vbDesc.size = (std::size_t)m_vertexCapacity * sizeof(GpuVertex);
    vbDesc.usage.dynamic_update = true;
    vbDesc.label = "rock-mesh-vb";
    m_vertexBuffer = sg_make_buffer(&vbDesc);

    sg_buffer_desc ibDesc = {};
    ibDesc.size = (std::size_t)m_indexCapacity * sizeof(std::uint32_t);
    ibDesc.usage.dynamic_update = true;
    ibDesc.label = "rock-mesh-ib";
    ibDesc.usage.index_buffer = true;
    m_indexBuffer = sg_make_buffer(&ibDesc);

    if (m_vertexBuffer.id == SG_INVALID_ID || m_indexBuffer.id == SG_INVALID_ID) {
        destroyMeshBuffers();
    }
}

void RockMeshGpuRenderer::destroyMeshBuffers() {
    if (m_indexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_indexBuffer);
        m_indexBuffer = {SG_INVALID_ID};
    }
    if (m_vertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vertexBuffer);
        m_vertexBuffer = {SG_INVALID_ID};
    }
    m_vertexCapacity = 0;
    m_indexCapacity = 0;
    m_cachedVertexCount = -1;
    m_cachedIndexCount = -1;
}

void RockMeshGpuRenderer::uploadMesh(const RockFractureModel& model) {
    const int vertexCount = model.vertexCount;
    const int indexCount = (int)model.meshIndices.size();
    if (vertexCount <= 0 || indexCount < 3) {
        return;
    }

    ensureMeshBuffers(vertexCount, indexCount);
    if (m_vertexBuffer.id == SG_INVALID_ID) {
        return;
    }

    if (m_cachedVertexCount == vertexCount && m_cachedIndexCount == indexCount) {
        return;
    }

    std::vector<GpuVertex> vertices((std::size_t)vertexCount);
    for (int i = 0; i < vertexCount; i++) {
        const Vec3& p = model.meshVertices[(std::size_t)i];
        vertices[(std::size_t)i].pos[0] = p.x;
        vertices[(std::size_t)i].pos[1] = p.y;
        vertices[(std::size_t)i].pos[2] = p.z;
        if ((std::size_t)i < model.meshNormals.size()) {
            const Vec3& n = model.meshNormals[(std::size_t)i];
            vertices[(std::size_t)i].normal[0] = n.x;
            vertices[(std::size_t)i].normal[1] = n.y;
            vertices[(std::size_t)i].normal[2] = n.z;
        } else {
            vertices[(std::size_t)i].normal[0] = 0.0f;
            vertices[(std::size_t)i].normal[1] = 0.0f;
            vertices[(std::size_t)i].normal[2] = 1.0f;
        }
    }

    sg_range vbRange = {vertices.data(), vertices.size() * sizeof(GpuVertex)};
    sg_update_buffer(m_vertexBuffer, &vbRange);

    sg_range ibRange = {model.meshIndices.data(), model.meshIndices.size() * sizeof(std::uint32_t)};
    sg_update_buffer(m_indexBuffer, &ibRange);

    m_cachedVertexCount = vertexCount;
    m_cachedIndexCount = indexCount;
}

void RockMeshGpuRenderer::invalidateMeshCache() {
    m_cachedVertexCount = -1;
    m_cachedIndexCount = -1;
}

bool RockMeshGpuRenderer::render(
    const RockFractureModel& model,
    const RockFractureShading& shading,
    const RockMeshGpuCamera& camera,
    int width,
    int height) {

    if (!m_initialized || m_pipeline.id == SG_INVALID_ID) {
        return false;
    }
    if (model.meshIndices.size() < 3 || model.meshVertices.empty()) {
        return false;
    }

    ensureTarget(width, height);
    if (!validOutput() || m_depthAttachmentView.id == SG_INVALID_ID) {
        return false;
    }

    uploadMesh(model);
    if (m_vertexBuffer.id == SG_INVALID_ID || m_indexBuffer.id == SG_INVALID_ID) {
        return false;
    }

    const glm::vec3 target(camera.target[0], camera.target[1], camera.target[2]);
    const glm::vec3 eye(camera.cameraPos[0], camera.cameraPos[1], camera.cameraPos[2]);
    const glm::vec3 up(0.0f, 0.0f, 1.0f);
    const glm::mat4 view = glm::lookAt(eye, target, up);
    const float halfH = std::max(0.5f, camera.orthoHalfHeight);
    const float halfW = halfH * std::max(0.25f, camera.aspect);
    const float span = glm::length(target - eye);
    const glm::mat4 proj = glm::ortho(-halfW, halfW, -halfH, halfH, -span * 4.0f, span * 4.0f);
    const glm::mat4 mvp = proj * view;

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.071f, 0.082f, 0.106f, 1.0f};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;

    sg_pass pass = {};
    pass.action = action;
    pass.attachments.colors[0] = m_colorAttachmentView;
    pass.attachments.depth_stencil = m_depthAttachmentView;
    sg_begin_pass(&pass);

    sg_apply_pipeline(m_pipeline);

    m_bindings.vertex_buffers[0] = m_vertexBuffer;
    m_bindings.index_buffer = m_indexBuffer;
    sg_apply_bindings(&m_bindings);

    VsParams vs = {};
    std::memcpy(vs.mvp, glm::value_ptr(mvp), sizeof(vs.mvp));
    sg_range vsRange = {&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vsRange);

    FsParams fs = {};
    fs.lightDir[0] = shading.lightDir.x;
    fs.lightDir[1] = shading.lightDir.y;
    fs.lightDir[2] = shading.lightDir.z;
    fs.lightDir[3] = 0.0f;
    fs.rockTint[0] = shading.rockTint.x;
    fs.rockTint[1] = shading.rockTint.y;
    fs.rockTint[2] = shading.rockTint.z;
    fs.rockTint[3] = 1.0f;
    fs.ambientStrength = shading.ambientStrength;
    fs.diffuseStrength = shading.diffuseStrength;
    fs.specularStrength = shading.specularStrength;
    fs.shininess = shading.shininess;
    sg_range fsRange = {&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fsRange);

    const int indexCount = (int)model.meshIndices.size();
    sg_draw(0, indexCount, 1);

    sg_end_pass();
    return true;
}

} // namespace render_playground
