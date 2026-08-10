#include "pch.h"

#include "MeshView.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <sokol_glue.h>

namespace shapemlhost {

namespace {

const char* vsSource() {
    return R"glsl(
#version 330
uniform mat4 mvp;
layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 nrm;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 color;
out vec3 v_nrm;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_nrm = nrm;
    v_uv = uv;
    v_color = color;
}
)glsl";
}

const char* fsSource() {
    return R"glsl(
#version 330
uniform sampler2D diffuse_tex;
uniform vec4 light_dir;
in vec3 v_nrm;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
    vec3 albedo = v_color.rgb * texture(diffuse_tex, v_uv).rgb;
    vec3 n = normalize(v_nrm);
    float sun = max(dot(n, normalize(light_dir.xyz)), 0.0);
    // Hemisphere ambient + one directional sun: enough to read the shapes.
    float hemi = 0.30 + 0.20 * n.y;
    vec3 col = albedo * (hemi + 0.85 * sun);
    frag_color = vec4(col, 1.0);
}
)glsl";
}

} // namespace

void MeshView::init() {
    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_REPEAT;
    smp.wrap_v = SG_WRAP_REPEAT;
    m_sampler = sg_make_sampler(&smp);

    sg_shader_desc shd = {};
    shd.vertex_func.source = vsSource();
    shd.fragment_func.source = fsSource();
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
    shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.texture_sampler_pairs[0].view_slot = 0;
    shd.texture_sampler_pairs[0].sampler_slot = 0;
    shd.texture_sampler_pairs[0].glsl_name = "diffuse_tex";
    shd.label = "shapeml-mesh-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; // pos
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3; // nrm
    pip.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2; // uv
    pip.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT4; // color
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.index_type = SG_INDEXTYPE_UINT32;
    pip.cull_mode = SG_CULLMODE_BACK;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.depth.pixel_format = sglue_swapchain().depth_format;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "shapeml-mesh-pipeline";
    m_pip = sg_make_pipeline(&pip);

    // 1x1 white fallback for texture-less draw ranges (the FS always samples).
    const std::uint8_t white[4] = {255, 255, 255, 255};
    sg_image_desc img = {};
    img.width = 1;
    img.height = 1;
    img.pixel_format = SG_PIXELFORMAT_RGBA8;
    img.data.mip_levels[0].ptr = white;
    img.data.mip_levels[0].size = sizeof(white);
    img.label = "shapeml-white-1x1";
    m_whiteImg = sg_make_image(&img);
    sg_view_desc vd = {};
    vd.texture.image = m_whiteImg;
    vd.label = "shapeml-white-view";
    m_whiteView = sg_make_view(&vd);

    if (sg_query_shader_state(m_shader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_pip) != SG_RESOURCESTATE_VALID) {
        spdlog::error("ShapemlPlayground: mesh pipeline creation failed");
    }
}

void MeshView::shutdown() {
    for (auto& [path, view] : m_texCache) {
        sg_destroy_view(view);
    }
    for (auto& [path, img] : m_texImages) {
        sg_destroy_image(img);
    }
    m_texCache.clear();
    m_texImages.clear();
    if (m_whiteView.id != SG_INVALID_ID) sg_destroy_view(m_whiteView);
    if (m_whiteImg.id != SG_INVALID_ID) sg_destroy_image(m_whiteImg);
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    if (m_pip.id != SG_INVALID_ID) sg_destroy_pipeline(m_pip);
    if (m_shader.id != SG_INVALID_ID) sg_destroy_shader(m_shader);
    if (m_sampler.id != SG_INVALID_ID) sg_destroy_sampler(m_sampler);
}

void MeshView::clearModel() {
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    if (m_ibuf.id != SG_INVALID_ID) sg_destroy_buffer(m_ibuf);
    m_vbuf = {};
    m_ibuf = {};
    m_draws.clear();
    m_indexCount = 0;
    m_vertCount = 0;
}

void MeshView::setModel(const DerivedModel& model) {
    clearModel();
    if (model.vertices.empty() || model.indices.empty()) {
        return;
    }

    sg_buffer_desc vb = {};
    vb.usage.vertex_buffer = true;
    vb.data.ptr = model.vertices.data();
    vb.data.size = model.vertices.size() * sizeof(Vertex);
    vb.label = "shapeml-vbuf";
    m_vbuf = sg_make_buffer(&vb);

    sg_buffer_desc ib = {};
    ib.usage.index_buffer = true;
    ib.data.ptr = model.indices.data();
    ib.data.size = model.indices.size() * sizeof(std::uint32_t);
    ib.label = "shapeml-ibuf";
    m_ibuf = sg_make_buffer(&ib);

    m_draws = model.draws;
    m_indexCount = static_cast<int>(model.indices.size());
    m_vertCount = static_cast<int>(model.vertices.size());

    // Preload textures now (resource creation outside the pass, not lazily
    // inside draw()).
    for (const DrawRange& range : m_draws) {
        if (!range.texturePath.empty()) {
            textureFor(range.texturePath);
        }
    }
}

sg_view MeshView::textureFor(const std::string& path) {
    const auto it = m_texCache.find(path);
    if (it != m_texCache.end()) {
        return it->second;
    }

    int w = 0;
    int h = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, nullptr, 4);
    if (!pixels) {
        spdlog::warn("ShapemlPlayground: texture not loaded, using white: {}", path);
        m_texCache.emplace(path, m_whiteView);
        return m_whiteView;
    }

    sg_image_desc img = {};
    img.width = w;
    img.height = h;
    img.pixel_format = SG_PIXELFORMAT_RGBA8;
    img.data.mip_levels[0].ptr = pixels;
    img.data.mip_levels[0].size = static_cast<size_t>(w) * h * 4;
    img.label = path.c_str();
    sg_image image = sg_make_image(&img);
    stbi_image_free(pixels);

    sg_view_desc vd = {};
    vd.texture.image = image;
    sg_view view = sg_make_view(&vd);
    m_texImages.emplace(path, image);
    m_texCache.emplace(path, view);
    return view;
}

void MeshView::draw(const Camera& cam, int fbWidth, int fbHeight) {
    if (m_indexCount == 0) {
        return;
    }
    const float aspect =
        fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const glm::mat4 proj =
        glm::perspective(glm::radians(42.0f), aspect, 0.05f, 2000.0f);
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
    const float sunYaw = -0.7f;
    const float sunPitch = 0.9f;
    fsu.lightDir[0] = std::cos(sunPitch) * std::sin(sunYaw);
    fsu.lightDir[1] = std::sin(sunPitch);
    fsu.lightDir[2] = std::cos(sunPitch) * std::cos(sunYaw);
    fsu.lightDir[3] = 0.0f;

    sg_apply_pipeline(m_pip);
    sg_apply_uniforms(0, {&vsu, sizeof(vsu)});
    sg_apply_uniforms(1, {&fsu, sizeof(fsu)});

    for (const DrawRange& range : m_draws) {
        const sg_view view = range.texturePath.empty()
            ? m_whiteView : textureFor(range.texturePath);
        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = m_vbuf;
        bindings.index_buffer = m_ibuf;
        bindings.views[0] = view;
        bindings.samplers[0] = m_sampler;
        sg_apply_bindings(&bindings);
        sg_draw(range.firstIndex, range.indexCount, 1);
    }
}

} // namespace shapemlhost
