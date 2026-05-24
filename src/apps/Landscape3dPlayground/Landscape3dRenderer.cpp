#include "Landscape3dRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radians(float deg) {
    return deg * kPi / 180.0f;
}

glm::vec4 materialColor(TerrainMaterial material) {
    switch (material) {
    case TerrainMaterial::Sand:
        return {0.78f, 0.66f, 0.42f, 1.0f};
    case TerrainMaterial::Rock:
        return {0.42f, 0.43f, 0.46f, 1.0f};
    case TerrainMaterial::Grass:
    default:
        return {0.24f, 0.54f, 0.22f, 1.0f};
    }
}

glm::vec3 cellCorner(const TerrainScene& scene, int x, int z, float heightScale) {
    const float half = (float)scene.gridSize() * 0.5f;
    return {
        (float)x - half,
        scene.heightAt(x, z) * heightScale,
        (float)z - half
    };
}

glm::vec3 cellNormal(const glm::vec3& p00, const glm::vec3& p10, const glm::vec3& p01, const glm::vec3& p11) {
    const glm::vec3 n0 = glm::cross(p01 - p00, p10 - p00);
    const glm::vec3 n1 = glm::cross(p11 - p10, p01 - p10);
    const glm::vec3 n = n0 + n1;
    const float len = glm::length(n);
    return (len > 0.0001f) ? (n / len) : glm::vec3(0.0f, 1.0f, 0.0f);
}

const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal0;
layout(location=2) in vec4 color0;
out vec3 v_normal;
out vec4 v_color;
out float v_height;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_normal = normal0;
    v_color = color0;
    v_height = pos.y;
}
)";

const char* fs_src_glsl = R"(
#version 330
in vec3 v_normal;
in vec4 v_color;
in float v_height;
out vec4 frag_color;
uniform vec4 light_dir;
uniform vec4 options;
void main() {
    int debugMode = int(options.x + 0.5);
    vec3 n = normalize(v_normal);
    if (debugMode == 1) {
        frag_color = v_color;
        return;
    }
    if (debugMode == 2) {
        frag_color = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }
    if (debugMode == 3) {
        float h = clamp(v_height * 0.08 + 0.5, 0.0, 1.0);
        frag_color = vec4(h, h * 0.85, 1.0 - h, 1.0);
        return;
    }

    float diffuse = max(dot(n, normalize(light_dir.xyz)), 0.0);
    vec3 lit = v_color.rgb * (0.28 + diffuse * 0.72);
    frag_color = vec4(lit, v_color.a);
}
)";

const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float4x4 mvp; };
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float4 color0: TEXCOORD2;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color0: TEXCOORD1;
    float height0: TEXCOORD2;
};
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.normal0 = inp.normal0;
    o.color0 = inp.color0;
    o.height0 = inp.pos.y;
    return o;
}
)";

const char* fs_src_hlsl = R"(
cbuffer fs_params: register(b0) { float4 light_dir; float4 options; };
struct PSIn {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color0: TEXCOORD1;
    float height0: TEXCOORD2;
};
float4 main(PSIn inp): SV_Target0 {
    int debugMode = (int)(options.x + 0.5);
    float3 n = normalize(inp.normal0);
    if (debugMode == 1) {
        return inp.color0;
    }
    if (debugMode == 2) {
        return float4(n * 0.5 + 0.5, 1.0);
    }
    if (debugMode == 3) {
        float h = saturate(inp.height0 * 0.08 + 0.5);
        return float4(h, h * 0.85, 1.0 - h, 1.0);
    }

    float diffuse = max(dot(n, normalize(light_dir.xyz)), 0.0);
    float3 lit = inp.color0.rgb * (0.28 + diffuse * 0.72);
    return float4(lit, inp.color0.a);
}
)";

} // namespace

glm::vec3 Landscape3dCamera::position() const {
    const float yaw = radians(yawDeg);
    const float pitch = radians(pitchDeg);
    const float cp = std::cos(pitch);
    return target + glm::vec3(
        std::sin(yaw) * cp * distance,
        std::sin(pitch) * distance,
        std::cos(yaw) * cp * distance);
}

glm::vec3 Landscape3dCamera::right() const {
    return glm::normalize(glm::cross(glm::normalize(target - position()), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Landscape3dCamera::up() const {
    const glm::vec3 forward = glm::normalize(target - position());
    return glm::normalize(glm::cross(right(), forward));
}

glm::mat4 Landscape3dCamera::viewMatrix() const {
    return glm::lookAtRH(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Landscape3dCamera::projectionMatrix(float aspect) const {
    if (perspective) {
        return glm::perspectiveRH_ZO(radians(38.0f), aspect, 0.1f, 300.0f);
    }

    const float y = std::max(1.0f, orthoScale);
    const float x = y * std::max(0.1f, aspect);
    return glm::orthoRH_ZO(-x, x, -y, y, -200.0f, 300.0f);
}

void Landscape3dRenderer::init() {
    ensurePipelines();
}

void Landscape3dRenderer::shutdown() {
    destroyMeshBuffers();
    destroyPipelines();
}

void Landscape3dRenderer::rebuildMesh(const TerrainScene& scene, float heightScale) {
    destroyMeshBuffers();

    if (scene.gridSize() <= 0) return;

    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<TerrainVertex> lineVertices;

    const int grid = scene.gridSize();
    vertices.reserve((std::size_t)grid * (std::size_t)grid * 4);
    indices.reserve((std::size_t)grid * (std::size_t)grid * 6);
    lineVertices.reserve((std::size_t)grid * (std::size_t)grid * 8);

    for (int z = 0; z < grid; z++) {
        for (int x = 0; x < grid; x++) {
            const glm::vec3 p00 = cellCorner(scene, x, z, heightScale);
            const glm::vec3 p10 = cellCorner(scene, x + 1, z, heightScale);
            const glm::vec3 p01 = cellCorner(scene, x, z + 1, heightScale);
            const glm::vec3 p11 = cellCorner(scene, x + 1, z + 1, heightScale);
            const glm::vec3 n = cellNormal(p00, p10, p01, p11);
            const glm::vec4 c = materialColor(scene.materialAt(x, z));

            const std::uint32_t base = (std::uint32_t)vertices.size();
            vertices.push_back({p00, n, c});
            vertices.push_back({p01, n, c});
            vertices.push_back({p10, n, c});
            vertices.push_back({p11, n, c});

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 2);
            indices.push_back(base + 1);
            indices.push_back(base + 3);

            const glm::vec4 lineColor{0.035f, 0.04f, 0.045f, 1.0f};
            const glm::vec3 upNormal{0.0f, 1.0f, 0.0f};
            lineVertices.push_back({p00, upNormal, lineColor});
            lineVertices.push_back({p10, upNormal, lineColor});
            lineVertices.push_back({p10, upNormal, lineColor});
            lineVertices.push_back({p11, upNormal, lineColor});
            lineVertices.push_back({p11, upNormal, lineColor});
            lineVertices.push_back({p01, upNormal, lineColor});
            lineVertices.push_back({p01, upNormal, lineColor});
            lineVertices.push_back({p00, upNormal, lineColor});
        }
    }

    sg_buffer_desc vbuf = {};
    vbuf.usage.vertex_buffer = true;
    vbuf.data.ptr = vertices.data();
    vbuf.data.size = vertices.size() * sizeof(TerrainVertex);
    vbuf.label = "landscape3d-vertices";
    m_vertexBuffer = sg_make_buffer(&vbuf);

    sg_buffer_desc ibuf = {};
    ibuf.usage.index_buffer = true;
    ibuf.data.ptr = indices.data();
    ibuf.data.size = indices.size() * sizeof(std::uint32_t);
    ibuf.label = "landscape3d-indices";
    m_indexBuffer = sg_make_buffer(&ibuf);

    sg_buffer_desc lbuf = {};
    lbuf.usage.vertex_buffer = true;
    lbuf.data.ptr = lineVertices.data();
    lbuf.data.size = lineVertices.size() * sizeof(TerrainVertex);
    lbuf.label = "landscape3d-line-vertices";
    m_lineVertexBuffer = sg_make_buffer(&lbuf);

    m_terrainBindings = {};
    m_terrainBindings.vertex_buffers[0] = m_vertexBuffer;
    m_terrainBindings.index_buffer = m_indexBuffer;
    m_lineBindings = {};
    m_lineBindings.vertex_buffers[0] = m_lineVertexBuffer;
    m_indexCount = (int)indices.size();
    m_lineVertexCount = (int)lineVertices.size();
}

void Landscape3dRenderer::render(const Landscape3dCamera& camera, const Landscape3dRenderParams& params, int width, int height) {
    if (m_terrainPipeline.id == SG_INVALID_ID || m_vertexBuffer.id == SG_INVALID_ID || m_indexCount <= 0) return;

    const float aspect = (height > 0) ? ((float)width / (float)height) : 1.0f;
    const glm::mat4 mvp = camera.projectionMatrix(aspect) * camera.viewMatrix();

    VsParams vs = {};
    std::memcpy(vs.mvp, glm::value_ptr(mvp), sizeof(vs.mvp));

    const float yaw = radians(params.lightYawDeg);
    const float pitch = radians(params.lightPitchDeg);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));

    FsParams fs = {};
    fs.lightDir[0] = lightDir.x;
    fs.lightDir[1] = lightDir.y;
    fs.lightDir[2] = lightDir.z;
    fs.lightDir[3] = 0.0f;
    fs.options[0] = (float)params.debugMode;

    sg_range vsRange = {&vs, sizeof(vs)};
    sg_range fsRange = {&fs, sizeof(fs)};

    sg_apply_pipeline(m_terrainPipeline);
    sg_apply_bindings(&m_terrainBindings);
    sg_apply_uniforms(0, &vsRange);
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);

    if (params.showWireframe && m_linePipeline.id != SG_INVALID_ID && m_lineVertexCount > 0) {
        FsParams lineFs = fs;
        lineFs.options[0] = 1.0f;
        sg_range lineFsRange = {&lineFs, sizeof(lineFs)};

        sg_apply_pipeline(m_linePipeline);
        sg_apply_bindings(&m_lineBindings);
        sg_apply_uniforms(0, &vsRange);
        sg_apply_uniforms(1, &lineFsRange);
        sg_draw(0, m_lineVertexCount, 1);
    }
}

void Landscape3dRenderer::ensurePipelines() {
    if (m_terrainPipeline.id != SG_INVALID_ID) return;

    sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
    shd.vertex_func.source = vs_src_hlsl;
    shd.fragment_func.source = fs_src_hlsl;
    shd.attrs[0].hlsl_sem_name = "TEXCOORD";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "TEXCOORD";
    shd.attrs[1].hlsl_sem_index = 1;
    shd.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd.attrs[2].hlsl_sem_index = 2;
#else
    shd.vertex_func.source = vs_src_glsl;
    shd.fragment_func.source = fs_src_glsl;
#endif

    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsParams);
    shd.uniform_blocks[0].hlsl_register_b_n = 0;
    shd.uniform_blocks[0].msl_buffer_n = 0;
    shd.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[1].size = sizeof(FsParams);
    shd.uniform_blocks[1].hlsl_register_b_n = 0;
    shd.uniform_blocks[1].msl_buffer_n = 1;
    shd.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shd.uniform_blocks[1].spirv_set0_binding_n = 1;
    shd.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shd.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd.uniform_blocks[1].glsl_uniforms[1].glsl_name = "options";
    shd.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;

    shd.label = "landscape3d-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc terrain = {};
    terrain.shader = m_shader;
    terrain.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    terrain.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    terrain.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
    terrain.index_type = SG_INDEXTYPE_UINT32;
    terrain.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    terrain.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    terrain.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    terrain.depth.write_enabled = true;
    terrain.label = "landscape3d-terrain-pipeline";
    m_terrainPipeline = sg_make_pipeline(&terrain);

    sg_pipeline_desc lines = terrain;
    lines.index_type = SG_INDEXTYPE_NONE;
    lines.primitive_type = SG_PRIMITIVETYPE_LINES;
    lines.depth.write_enabled = false;
    lines.label = "landscape3d-line-pipeline";
    m_linePipeline = sg_make_pipeline(&lines);
}

void Landscape3dRenderer::destroyPipelines() {
    if (m_linePipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_linePipeline);
        m_linePipeline.id = SG_INVALID_ID;
    }
    if (m_terrainPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_terrainPipeline);
        m_terrainPipeline.id = SG_INVALID_ID;
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader.id = SG_INVALID_ID;
    }
}

void Landscape3dRenderer::destroyMeshBuffers() {
    if (m_lineVertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_lineVertexBuffer);
        m_lineVertexBuffer.id = SG_INVALID_ID;
    }
    if (m_indexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_indexBuffer);
        m_indexBuffer.id = SG_INVALID_ID;
    }
    if (m_vertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vertexBuffer);
        m_vertexBuffer.id = SG_INVALID_ID;
    }

    m_terrainBindings = {};
    m_lineBindings = {};
    m_indexCount = 0;
    m_lineVertexCount = 0;
}

