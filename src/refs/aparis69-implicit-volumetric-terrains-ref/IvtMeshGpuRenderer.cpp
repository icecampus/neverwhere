#include "IvtMeshGpuRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

namespace ivt_view {
namespace {

#if defined(SOKOL_D3D11)
const char* kMeshVsSrc = R"(
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

const char* kMeshFsSrc = R"(
cbuffer fs_params: register(b0) {
    float4 light_dir;
    float4 terrain_tint;
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
    float upDot = saturate(n.y * 0.5 + 0.5);
    float3 ground = float3(0.18, 0.16, 0.14);
    float3 sky = float3(0.42, 0.46, 0.52);
    float3 ambient = lerp(ground, sky, upDot) * ambient_strength;
    float3 diffuse = terrain_tint.rgb * diffuse_strength * ndotl;
    float3 v = normalize(-inp.worldPos);
    float3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), shininess) * specular_strength;
    return float4(ambient + diffuse + spec, 1.0);
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
uniform mat4 mvp;
out vec3 v_worldNormal;
out vec3 v_worldPos;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_worldNormal = normal0;
    v_worldPos = pos;
}
)";

const char* kMeshFsSrc = R"(
#version 330
in vec3 v_worldNormal;
in vec3 v_worldPos;
out vec4 frag_color;
uniform vec4 light_dir;
uniform vec4 terrain_tint;
uniform float ambient_strength;
uniform float diffuse_strength;
uniform float specular_strength;
uniform float shininess;
void main() {
    vec3 n = normalize(v_worldNormal);
    vec3 l = normalize(light_dir.xyz);
    float ndotl = max(dot(n, l), 0.0);
    float upDot = clamp(n.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ground = vec3(0.18, 0.16, 0.14);
    vec3 sky = vec3(0.42, 0.46, 0.52);
    vec3 ambient = mix(ground, sky, upDot) * ambient_strength;
    vec3 diffuse = terrain_tint.rgb * diffuse_strength * ndotl;
    vec3 v = normalize(-v_worldPos);
    vec3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), shininess) * specular_strength;
    frag_color = vec4(ambient + diffuse + vec3(spec), 1.0);
}
)";

const char* kLineVsSrc = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec4 color0;
uniform mat4 mvp;
out vec4 v_color;
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

int nextCapacity(int value) {
    int cap = 4096;
    while (cap < value) {
        cap *= 2;
    }
    return cap;
}

struct GridLineVertex {
    float pos[3];
    float color[4];
};

void pushLine(std::vector<GridLineVertex>& verts, const Vec3& a, const Vec3& b, float r, float g, float bl, float aAlpha) {
    GridLineVertex va{};
    va.pos[0] = a.x;
    va.pos[1] = a.y;
    va.pos[2] = a.z;
    va.color[0] = r;
    va.color[1] = g;
    va.color[2] = bl;
    va.color[3] = aAlpha;
    GridLineVertex vb = va;
    vb.pos[0] = b.x;
    vb.pos[1] = b.y;
    vb.pos[2] = b.z;
    verts.push_back(va);
    verts.push_back(vb);
}

} // namespace

void IvtMeshGpuRenderer::init() {
    if (m_initialized) {
        return;
    }

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.label = "ivt-mesh-output-sampler";
    m_outputSampler = sg_make_sampler(&samplerDesc);

    ensurePipelines();
    m_initialized = m_meshPipeline.id != SG_INVALID_ID && m_linePipeline.id != SG_INVALID_ID
        && m_outputSampler.id != SG_INVALID_ID;
    spdlog::info("IvtMeshGpuRenderer::init {}", m_initialized ? "OK" : "FAILED");
}

void IvtMeshGpuRenderer::shutdown() {
    destroyMeshBuffers();
    destroyLineBuffer();
    destroyTarget();
    destroyPipelines();
    if (m_outputSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_outputSampler);
        m_outputSampler = {SG_INVALID_ID};
    }
    m_initialized = false;
}

bool IvtMeshGpuRenderer::validOutput() const {
    return m_colorTextureView.id != SG_INVALID_ID && m_outputSampler.id != SG_INVALID_ID;
}

void IvtMeshGpuRenderer::ensurePipelines() {
    if (m_meshPipeline.id != SG_INVALID_ID) {
        return;
    }

    sg_shader_desc meshShd = {};
    meshShd.vertex_func.source = kMeshVsSrc;
    meshShd.fragment_func.source = kMeshFsSrc;
#if defined(SOKOL_D3D11)
    meshShd.attrs[0].hlsl_sem_name = "TEXCOORD";
    meshShd.attrs[0].hlsl_sem_index = 0;
    meshShd.attrs[1].hlsl_sem_name = "TEXCOORD";
    meshShd.attrs[1].hlsl_sem_index = 1;
#endif
    meshShd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    meshShd.uniform_blocks[0].size = sizeof(VsParams);
    meshShd.uniform_blocks[0].hlsl_register_b_n = 0;
    meshShd.uniform_blocks[0].msl_buffer_n = 0;
    meshShd.uniform_blocks[0].wgsl_group0_binding_n = 0;
    meshShd.uniform_blocks[0].spirv_set0_binding_n = 0;
    meshShd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    meshShd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    meshShd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    meshShd.uniform_blocks[1].size = sizeof(FsParams);
    meshShd.uniform_blocks[1].hlsl_register_b_n = 0;
    meshShd.uniform_blocks[1].msl_buffer_n = 1;
    meshShd.uniform_blocks[1].wgsl_group0_binding_n = 1;
    meshShd.uniform_blocks[1].spirv_set0_binding_n = 1;
    meshShd.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    meshShd.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    meshShd.uniform_blocks[1].glsl_uniforms[1].glsl_name = "terrain_tint";
    meshShd.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;
    meshShd.uniform_blocks[1].glsl_uniforms[2].glsl_name = "ambient_strength";
    meshShd.uniform_blocks[1].glsl_uniforms[2].type = SG_UNIFORMTYPE_FLOAT;
    meshShd.uniform_blocks[1].glsl_uniforms[3].glsl_name = "diffuse_strength";
    meshShd.uniform_blocks[1].glsl_uniforms[3].type = SG_UNIFORMTYPE_FLOAT;
    meshShd.uniform_blocks[1].glsl_uniforms[4].glsl_name = "specular_strength";
    meshShd.uniform_blocks[1].glsl_uniforms[4].type = SG_UNIFORMTYPE_FLOAT;
    meshShd.uniform_blocks[1].glsl_uniforms[5].glsl_name = "shininess";
    meshShd.uniform_blocks[1].glsl_uniforms[5].type = SG_UNIFORMTYPE_FLOAT;
    meshShd.label = "ivt-mesh-shader";
    m_meshShader = sg_make_shader(&meshShd);

    sg_shader_desc lineShd = {};
    lineShd.vertex_func.source = kLineVsSrc;
    lineShd.fragment_func.source = kLineFsSrc;
#if defined(SOKOL_D3D11)
    lineShd.attrs[0].hlsl_sem_name = "TEXCOORD";
    lineShd.attrs[0].hlsl_sem_index = 0;
    lineShd.attrs[1].hlsl_sem_name = "TEXCOORD";
    lineShd.attrs[1].hlsl_sem_index = 1;
#endif
    lineShd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    lineShd.uniform_blocks[0].size = sizeof(VsParams);
    lineShd.uniform_blocks[0].hlsl_register_b_n = 0;
    lineShd.uniform_blocks[0].msl_buffer_n = 0;
    lineShd.uniform_blocks[0].wgsl_group0_binding_n = 0;
    lineShd.uniform_blocks[0].spirv_set0_binding_n = 0;
    lineShd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    lineShd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    lineShd.label = "ivt-line-shader";
    m_lineShader = sg_make_shader(&lineShd);

    if (m_meshShader.id == SG_INVALID_ID || m_lineShader.id == SG_INVALID_ID) {
        return;
    }

    sg_pipeline_desc meshPip = {};
    meshPip.shader = m_meshShader;
    meshPip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    meshPip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    meshPip.index_type = SG_INDEXTYPE_UINT32;
    meshPip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    meshPip.cull_mode = SG_CULLMODE_BACK;
    meshPip.face_winding = SG_FACEWINDING_CCW;
    meshPip.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    meshPip.colors[0].blend.enabled = false;
    meshPip.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    meshPip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    meshPip.depth.write_enabled = true;
    meshPip.label = "ivt-mesh-pipeline";
    m_meshPipeline = sg_make_pipeline(&meshPip);

    sg_pipeline_desc linePip = {};
    linePip.shader = m_lineShader;
    linePip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    linePip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    linePip.primitive_type = SG_PRIMITIVETYPE_LINES;
    linePip.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    linePip.colors[0].blend.enabled = true;
    linePip.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    linePip.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    linePip.colors[0].blend.op_rgb = SG_BLENDOP_ADD;
    linePip.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    linePip.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    linePip.colors[0].blend.op_alpha = SG_BLENDOP_ADD;
    linePip.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    linePip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    linePip.depth.write_enabled = true;
    linePip.label = "ivt-line-pipeline";
    m_linePipeline = sg_make_pipeline(&linePip);
}

void IvtMeshGpuRenderer::destroyPipelines() {
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

void IvtMeshGpuRenderer::ensureTarget(int width, int height) {
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
    colorDesc.label = "ivt-mesh-color";
    m_colorImage = sg_make_image(&colorDesc);

    sg_image_desc depthDesc = {};
    depthDesc.usage.depth_stencil_attachment = true;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depthDesc.sample_count = 1;
    depthDesc.label = "ivt-mesh-depth";
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

void IvtMeshGpuRenderer::destroyTarget() {
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

void IvtMeshGpuRenderer::ensureMeshBuffers(int vertexCount, int indexCount) {
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
    vbDesc.label = "ivt-mesh-vb";
    m_vertexBuffer = sg_make_buffer(&vbDesc);

    sg_buffer_desc ibDesc = {};
    ibDesc.size = (std::size_t)m_indexCapacity * sizeof(std::uint32_t);
    ibDesc.usage.dynamic_update = true;
    ibDesc.usage.index_buffer = true;
    ibDesc.label = "ivt-mesh-ib";
    m_indexBuffer = sg_make_buffer(&ibDesc);

    if (m_vertexBuffer.id == SG_INVALID_ID || m_indexBuffer.id == SG_INVALID_ID) {
        destroyMeshBuffers();
    }
}

void IvtMeshGpuRenderer::destroyMeshBuffers() {
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

void IvtMeshGpuRenderer::ensureLineBuffer(int vertexCount) {
    if (vertexCount <= 0) {
        return;
    }
    if (m_lineVertexBuffer.id != SG_INVALID_ID && vertexCount <= m_lineVertexCapacity) {
        return;
    }
    destroyLineBuffer();
    m_lineVertexCapacity = nextCapacity(vertexCount);
    sg_buffer_desc vbDesc = {};
    vbDesc.size = (std::size_t)m_lineVertexCapacity * sizeof(LineVertex);
    vbDesc.usage.dynamic_update = true;
    vbDesc.label = "ivt-grid-vb";
    m_lineVertexBuffer = sg_make_buffer(&vbDesc);
    if (m_lineVertexBuffer.id == SG_INVALID_ID) {
        destroyLineBuffer();
    }
}

void IvtMeshGpuRenderer::destroyLineBuffer() {
    if (m_lineVertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_lineVertexBuffer);
        m_lineVertexBuffer = {SG_INVALID_ID};
    }
    m_lineVertexCapacity = 0;
}

void IvtMeshGpuRenderer::uploadMesh(const IvtModel& model) {
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
            vertices[(std::size_t)i].normal[1] = 1.0f;
            vertices[(std::size_t)i].normal[2] = 0.0f;
        }
    }

    sg_range vbRange = {vertices.data(), vertices.size() * sizeof(GpuVertex)};
    sg_update_buffer(m_vertexBuffer, &vbRange);

    std::vector<std::uint32_t> flippedIndices = model.meshIndices;
    for (std::size_t tri = 0; tri + 2 < flippedIndices.size(); tri += 3) {
        std::swap(flippedIndices[tri + 1], flippedIndices[tri + 2]);
    }
    sg_range ibRange = {flippedIndices.data(), flippedIndices.size() * sizeof(std::uint32_t)};
    sg_update_buffer(m_indexBuffer, &ibRange);

    m_cachedVertexCount = vertexCount;
    m_cachedIndexCount = indexCount;
}

void IvtMeshGpuRenderer::buildGridVertices(const WorldGridParams& grid, const Vec3& center, std::vector<LineVertex>& out) const {
    out.clear();
    if (grid.cellSize <= 0.0f || grid.extent <= 0.0f || grid.majorEvery < 1) {
        return;
    }

    std::vector<GridLineVertex> scratch;
    const float y = grid.groundY;
    const int minCell = (int)std::floor(-grid.extent / grid.cellSize);
    const int maxCell = (int)std::ceil(grid.extent / grid.cellSize);

    for (int i = minCell; i <= maxCell; i++) {
        if (i == 0) {
            continue;
        }
        const float coord = i * grid.cellSize;
        const bool major = (i % grid.majorEvery) == 0;
        const float alpha = major ? 0.82f : 0.58f;
        const float r = major ? 0.34f : 0.16f;
        const float g = major ? 0.38f : 0.18f;
        const float b = major ? 0.44f : 0.22f;
        pushLine(scratch,
            {center.x + coord, y, center.z - grid.extent},
            {center.x + coord, y, center.z + grid.extent},
            r, g, b, alpha);
        pushLine(scratch,
            {center.x - grid.extent, y, center.z + coord},
            {center.x + grid.extent, y, center.z + coord},
            r, g, b, alpha);
    }

    pushLine(scratch, {center.x - grid.extent, y, center.z}, {center.x + grid.extent, y, center.z}, 0.86f, 0.36f, 0.36f, 0.92f);
    pushLine(scratch, {center.x, y, center.z - grid.extent}, {center.x, y, center.z + grid.extent}, 0.38f, 0.55f, 0.90f, 0.92f);
    pushLine(scratch, {center.x, y, center.z}, {center.x, y + grid.extent * 0.45f, center.z}, 0.46f, 0.82f, 0.46f, 0.92f);

    out.resize(scratch.size());
    static_assert(sizeof(LineVertex) == sizeof(GridLineVertex));
    std::memcpy(out.data(), scratch.data(), scratch.size() * sizeof(LineVertex));
}

void IvtMeshGpuRenderer::drawGroundShadow(const float mvp[16], const IvtModel& model) {
    if (model.meshVertices.empty()) {
        return;
    }

    const float y = model.boundsMin.y - 0.02f;
    const float pad = 0.35f;
    GpuVertex verts[4] = {};
    const Vec3 corners[4] = {
        {model.boundsMin.x - pad, y, model.boundsMin.z - pad},
        {model.boundsMax.x + pad, y, model.boundsMin.z - pad},
        {model.boundsMax.x + pad, y, model.boundsMax.z + pad},
        {model.boundsMin.x - pad, y, model.boundsMax.z + pad},
    };
    for (int i = 0; i < 4; i++) {
        verts[i].pos[0] = corners[i].x;
        verts[i].pos[1] = corners[i].y;
        verts[i].pos[2] = corners[i].z;
        verts[i].normal[0] = 0.0f;
        verts[i].normal[1] = 1.0f;
        verts[i].normal[2] = 0.0f;
    }

    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    sg_buffer_desc vbDesc = {};
    vbDesc.data = {verts, sizeof(verts)};
    vbDesc.label = "ivt-shadow-vb";
    sg_buffer shadowVb = sg_make_buffer(&vbDesc);

    sg_buffer_desc ibDesc = {};
    ibDesc.data = {indices, sizeof(indices)};
    ibDesc.usage.index_buffer = true;
    ibDesc.label = "ivt-shadow-ib";
    sg_buffer shadowIb = sg_make_buffer(&ibDesc);

    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = shadowVb;
    bindings.index_buffer = shadowIb;

    sg_apply_pipeline(m_meshPipeline);
    sg_apply_bindings(&bindings);

    VsParams vs = {};
    std::memcpy(vs.mvp, mvp, sizeof(vs.mvp));
    sg_range vsRange = {&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vsRange);

    FsParams fs = {};
    fs.terrainTint[0] = 0.055f;
    fs.terrainTint[1] = 0.063f;
    fs.terrainTint[2] = 0.078f;
    fs.terrainTint[3] = 1.0f;
    fs.ambientStrength = 1.0f;
    fs.diffuseStrength = 0.0f;
    fs.specularStrength = 0.0f;
    sg_range fsRange = {&fs, sizeof(fs)};
    sg_apply_uniforms(1, &fsRange);

    sg_draw(0, 6, 1);

    sg_destroy_buffer(shadowIb);
    sg_destroy_buffer(shadowVb);
}

void IvtMeshGpuRenderer::drawWorldGrid(const float mvp[16], const WorldGridParams& grid, const Vec3& center) {
    std::vector<LineVertex> vertices;
    buildGridVertices(grid, center, vertices);
    if (vertices.empty()) {
        return;
    }

    ensureLineBuffer((int)vertices.size());
    if (m_lineVertexBuffer.id == SG_INVALID_ID) {
        return;
    }

    sg_range vbRange = {vertices.data(), vertices.size() * sizeof(LineVertex)};
    sg_update_buffer(m_lineVertexBuffer, &vbRange);

    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = m_lineVertexBuffer;

    sg_apply_pipeline(m_linePipeline);
    sg_apply_bindings(&bindings);

    VsParams vs = {};
    std::memcpy(vs.mvp, mvp, sizeof(vs.mvp));
    sg_range vsRange = {&vs, sizeof(vs)};
    sg_apply_uniforms(0, &vsRange);

    sg_draw(0, (int)vertices.size(), 1);
}

void IvtMeshGpuRenderer::invalidateMeshCache() {
    m_cachedVertexCount = -1;
    m_cachedIndexCount = -1;
}

bool IvtMeshGpuRenderer::render(
    const IvtModel& model,
    const IvtShading& shading,
    const IvtMeshGpuCamera& camera,
    const WorldGridParams& grid,
    bool showGrid,
    int width,
    int height) {

    if (!m_initialized || m_meshPipeline.id == SG_INVALID_ID) {
        return false;
    }

    ensureTarget(width, height);
    if (!validOutput() || m_depthAttachmentView.id == SG_INVALID_ID) {
        return false;
    }

    const glm::vec3 target(camera.target[0], camera.target[1], camera.target[2]);
    const glm::vec3 eye(camera.cameraPos[0], camera.cameraPos[1], camera.cameraPos[2]);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::mat4 view = glm::lookAtRH(eye, target, up);
    const float halfH = std::max(0.5f, camera.orthoHalfHeight);
    const float halfW = halfH * std::max(0.25f, camera.aspect);
    const glm::mat4 proj = glm::orthoRH_ZO(-halfW, halfW, -halfH, halfH, -200.0f, 300.0f);
    const glm::mat4 mvp = proj * view;
    float mvpData[16];
    std::memcpy(mvpData, glm::value_ptr(mvp), sizeof(mvpData));

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

    const Vec3 gridCenter{camera.target[0], camera.target[1], camera.target[2]};
    drawGroundShadow(mvpData, model);
    if (showGrid) {
        drawWorldGrid(mvpData, grid, gridCenter);
    }

    if (model.meshIndices.size() >= 3 && !model.meshVertices.empty()) {
        uploadMesh(model);
        if (m_vertexBuffer.id != SG_INVALID_ID && m_indexBuffer.id != SG_INVALID_ID) {
            sg_apply_pipeline(m_meshPipeline);
            m_meshBindings.vertex_buffers[0] = m_vertexBuffer;
            m_meshBindings.index_buffer = m_indexBuffer;
            sg_apply_bindings(&m_meshBindings);

            VsParams vs = {};
            std::memcpy(vs.mvp, mvpData, sizeof(vs.mvp));
            sg_range vsRange = {&vs, sizeof(vs)};
            sg_apply_uniforms(0, &vsRange);

            FsParams fs = {};
            fs.lightDir[0] = shading.lightDir.x;
            fs.lightDir[1] = shading.lightDir.y;
            fs.lightDir[2] = shading.lightDir.z;
            fs.lightDir[3] = 0.0f;
            fs.terrainTint[0] = shading.terrainTint.x;
            fs.terrainTint[1] = shading.terrainTint.y;
            fs.terrainTint[2] = shading.terrainTint.z;
            fs.terrainTint[3] = 1.0f;
            fs.ambientStrength = shading.ambientStrength;
            fs.diffuseStrength = shading.diffuseStrength;
            fs.specularStrength = shading.specularStrength;
            fs.shininess = shading.shininess;
            sg_range fsRange = {&fs, sizeof(fs)};
            sg_apply_uniforms(1, &fsRange);

            const int indexCount = (int)model.meshIndices.size();
            sg_draw(0, indexCount, 1);
        }
    }

    sg_end_pass();
    return true;
}

} // namespace ivt_view
