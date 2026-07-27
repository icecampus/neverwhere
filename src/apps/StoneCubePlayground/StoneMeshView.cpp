#include "pch.h"

#include "StoneMeshView.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sokol_glue.h>
#include <spdlog/spdlog.h>

namespace stonecube {

namespace {

const char* meshVsSource() {
    return R"GLSL(
#version 330

uniform mat4 mvp;

layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;

out vec3 v_n;
out vec2 v_uv;

void main() {
    v_n = normal;
    v_uv = uv;
    gl_Position = mvp * vec4(pos, 1.0);
}
)GLSL";
}

const char* meshFsSource() {
    return R"GLSL(
#version 330

uniform sampler2D albedo_tex; // rgb = albedo, a = AO
uniform sampler2D normal_tex; // object-space normal (0.5+0.5n)
uniform vec4 light_dir;

in vec3 v_n;
in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 alb = texture(albedo_tex, v_uv);
    vec3 n = normalize(texture(normal_tex, v_uv).rgb * 2.0 - 1.0);
    float dif = clamp(dot(n, normalize(light_dir.xyz)), 0.0, 1.0);
    vec3 col = alb.rgb * (0.25 + 1.1 * dif) * alb.a;
    fragColor = vec4(pow(col, vec3(0.4545)), 1.0);
}
)GLSL";
}

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// CPU box-filter mip chain (sokol does not generate mipmaps; the baked
// bump-normal detail is above texel frequency at minification and sparkles
// without mips).
sg_image makeTexture(int size, const std::vector<std::uint8_t>& rgba, const char* label) {
    int levels = 1;
    while ((size >> levels) > 1) {
        ++levels;
    }
    if (levels > SG_MAX_MIPMAPS) {
        levels = SG_MAX_MIPMAPS;
    }

    std::vector<std::vector<std::uint8_t>> mipData(static_cast<size_t>(levels));
    sg_image_desc desc = {};
    desc.width = size;
    desc.height = size;
    desc.num_mipmaps = levels;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = rgba.data();
    desc.data.mip_levels[0].size = rgba.size();

    int pw = size;
    const std::uint8_t* prev = rgba.data();
    for (int level = 1; level < levels; ++level) {
        const int lw = std::max(1, pw / 2);
        auto& data = mipData[static_cast<size_t>(level)];
        data.resize(static_cast<size_t>(lw) * lw * 4);
        for (int y = 0; y < lw; ++y) {
            for (int x = 0; x < lw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(2 * x + dx, pw - 1);
                            const int sy = std::min(2 * y + dy, pw - 1);
                            sum += prev[(static_cast<size_t>(sy) * pw + sx) * 4 + c];
                        }
                    }
                    data[(static_cast<size_t>(y) * lw + x) * 4 + c] =
                        static_cast<std::uint8_t>(sum / 4);
                }
            }
        }
        desc.data.mip_levels[level].ptr = data.data();
        desc.data.mip_levels[level].size = data.size();
        prev = data.data();
        pw = lw;
    }

    desc.label = label;
    return sg_make_image(&desc);
}

sg_view makeTextureView(sg_image image) {
    sg_view_desc desc = {};
    desc.texture.image = image;
    return sg_make_view(&desc);
}

} // namespace

void StoneMeshView::init() {
    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.mipmap_filter = SG_FILTER_LINEAR; // trilinear: calm the baked bump detail
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    m_sampler = sg_make_sampler(&smp);

    sg_shader_desc shd = {};
    shd.vertex_func.source = meshVsSource();
    shd.fragment_func.source = meshFsSource();
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsUniforms);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    shd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[1].size = sizeof(FsUniforms);
    shd.uniform_blocks[1].layout = SG_UNIFORMLAYOUT_STD140;
    shd.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shd.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    const char* texNames[] = {"albedo_tex", "normal_tex"};
    for (int i = 0; i < 2; ++i) {
        shd.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[i].view_slot = i;
        shd.texture_sampler_pairs[i].sampler_slot = 0;
        shd.texture_sampler_pairs[i].glsl_name = texNames[i];
    }
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.label = "stonecube-mesh-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.cull_mode = SG_CULLMODE_BACK;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.depth.pixel_format = sglue_swapchain().depth_format;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "stonecube-mesh-pipeline";
    m_pip = sg_make_pipeline(&pip);

    if (sg_query_shader_state(m_shader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_pip) != SG_RESOURCESTATE_VALID) {
        spdlog::error("StoneCubePlayground: mesh pipeline creation failed");
    }
}

void StoneMeshView::shutdown() {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    if (m_pip.id != SG_INVALID_ID) sg_destroy_pipeline(m_pip);
    if (m_shader.id != SG_INVALID_ID) sg_destroy_shader(m_shader);
    if (m_sampler.id != SG_INVALID_ID) sg_destroy_sampler(m_sampler);
    if (m_albedoView.id != SG_INVALID_ID) sg_destroy_view(m_albedoView);
    if (m_albedoImg.id != SG_INVALID_ID) sg_destroy_image(m_albedoImg);
    if (m_normalView.id != SG_INVALID_ID) sg_destroy_view(m_normalView);
    if (m_normalImg.id != SG_INVALID_ID) sg_destroy_image(m_normalImg);
}

void StoneMeshView::setMesh(const stone_gen::StoneMesh& mesh) {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    m_vbuf = {};
    m_ibuf = {};
    m_indexCount = 0;

    std::vector<Vertex> vertices(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const stone_gen::StoneMeshVertex& v = mesh.vertices[i];
        vertices[i] = {v.pos.x, v.pos.y, v.pos.z,
            v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y};
    }

    sg_buffer_desc vb = {};
    vb.usage.vertex_buffer = true;
    vb.data.ptr = vertices.data();
    vb.data.size = vertices.size() * sizeof(Vertex);
    vb.label = "stonecube-mesh-vbuf";
    m_vbuf = sg_make_buffer(&vb);

    sg_buffer_desc ib = {};
    ib.usage.index_buffer = true;
    ib.data.ptr = mesh.indices.data();
    ib.data.size = mesh.indices.size() * sizeof(std::uint32_t);
    ib.label = "stonecube-mesh-ibuf";
    m_ibuf = sg_make_buffer(&ib);

    m_indexCount = static_cast<int>(mesh.indices.size());
}

void StoneMeshView::setTextures(int size, const std::vector<std::uint8_t>& albedo,
    const std::vector<std::uint8_t>& normal) {
    if (m_albedoView.id != SG_INVALID_ID) sg_destroy_view(m_albedoView);
    if (m_albedoImg.id != SG_INVALID_ID) sg_destroy_image(m_albedoImg);
    if (m_normalView.id != SG_INVALID_ID) sg_destroy_view(m_normalView);
    if (m_normalImg.id != SG_INVALID_ID) sg_destroy_image(m_normalImg);
    m_albedoImg = makeTexture(size, albedo, "stonecube-albedo");
    m_albedoView = makeTextureView(m_albedoImg);
    m_normalImg = makeTexture(size, normal, "stonecube-normal");
    m_normalView = makeTextureView(m_normalImg);
}

void StoneMeshView::draw(const Camera& cam, const float lightDir[3], int fbWidth,
    int fbHeight) const {
    if (m_indexCount == 0 || m_albedoView.id == SG_INVALID_ID) {
        return;
    }
    const float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(42.0f), aspect, 0.1f, 100.0f);
    const float cp = std::cos(cam.pitch);
    const float sp = std::sin(cam.pitch);
    const glm::vec3 eye(cam.target[0] + cam.dist * cp * std::sin(cam.yaw),
        cam.target[1] + cam.dist * sp,
        cam.target[2] + cam.dist * cp * std::cos(cam.yaw));
    const glm::mat4 view = glm::lookAt(eye,
        glm::vec3(cam.target[0], cam.target[1], cam.target[2]),
        glm::vec3(0.0f, 1.0f, 0.0f));

    VsUniforms vsu = {};
    const glm::mat4 mvp = proj * view;
    std::memcpy(vsu.mvp, &mvp, sizeof(vsu.mvp));
    FsUniforms fsu = {};
    fsu.lightDir[0] = lightDir[0];
    fsu.lightDir[1] = lightDir[1];
    fsu.lightDir[2] = lightDir[2];
    fsu.lightDir[3] = 0.0f;

    sg_apply_pipeline(m_pip);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_vbuf;
    bind.index_buffer = m_ibuf;
    bind.views[0] = m_albedoView;
    bind.views[1] = m_normalView;
    bind.samplers[0] = m_sampler;
    sg_apply_bindings(&bind);
    const sg_range vsRange = {&vsu, sizeof(vsu)};
    sg_apply_uniforms(0, &vsRange);
    const sg_range fsRange = {&fsu, sizeof(fsu)};
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);
}

} // namespace stonecube
