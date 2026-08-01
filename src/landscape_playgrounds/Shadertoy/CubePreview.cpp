#include "pch.h"

#include "CubePreview.h"

#include "GlslShim.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <sokol_glue.h>

namespace shadertoy {

namespace {

// 36 vertices (6 faces x 2 tris), pos3 + uv2 interleaved.
const float kCubeVertices[] = {
    // -Z face
    -1, -1, -1, 0, 0,   1, -1, -1, 1, 0,   1,  1, -1, 1, 1,
     1,  1, -1, 1, 1,  -1,  1, -1, 0, 1,  -1, -1, -1, 0, 0,
    // +Z face
    -1, -1,  1, 0, 0,   1, -1,  1, 1, 0,   1,  1,  1, 1, 1,
     1,  1,  1, 1, 1,  -1,  1,  1, 0, 1,  -1, -1,  1, 0, 0,
    // -X face
    -1, -1, -1, 0, 0,  -1, -1,  1, 1, 0,  -1,  1,  1, 1, 1,
    -1,  1,  1, 1, 1,  -1,  1, -1, 0, 1,  -1, -1, -1, 0, 0,
    // +X face
     1, -1, -1, 0, 0,   1, -1,  1, 1, 0,   1,  1,  1, 1, 1,
     1,  1,  1, 1, 1,   1,  1, -1, 0, 1,   1, -1, -1, 0, 0,
    // -Y face
    -1, -1, -1, 0, 0,   1, -1, -1, 1, 0,   1, -1,  1, 1, 1,
     1, -1,  1, 1, 1,  -1, -1,  1, 0, 1,  -1, -1, -1, 0, 0,
    // +Y face
    -1,  1, -1, 0, 0,   1,  1, -1, 1, 0,   1,  1,  1, 1, 1,
     1,  1,  1, 1, 1,  -1,  1,  1, 0, 1,  -1,  1, -1, 0, 0,
};

} // namespace

void CubePreview::init() {
    sg_buffer_desc buf = {};
    buf.usage.vertex_buffer = true;
    buf.data.ptr = kCubeVertices;
    buf.data.size = sizeof(kCubeVertices);
    buf.label = "shadertoy-cube-vbuf";
    m_vbuf = sg_make_buffer(&buf);

    sg_shader_desc shd = {};
    shd.vertex_func.source = cubeVertexSource();
    shd.fragment_func.source = cubeFragmentSource();
    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(glm::mat4);
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;
    shd.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.texture_sampler_pairs[0].view_slot = 0;
    shd.texture_sampler_pairs[0].sampler_slot = 0;
    shd.texture_sampler_pairs[0].glsl_name = "demo_tex";
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.label = "shadertoy-cube-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_shader;
    pip.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pip.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.cull_mode = SG_CULLMODE_BACK;
    pip.face_winding = SG_FACEWINDING_CCW;
    pip.depth.pixel_format = sglue_swapchain().depth_format;
    pip.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pip.depth.write_enabled = true;
    pip.label = "shadertoy-cube-pipeline";
    m_pip = sg_make_pipeline(&pip);

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    m_sampler = sg_make_sampler(&smp);

    m_ok = sg_query_shader_state(m_shader) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(m_pip) == SG_RESOURCESTATE_VALID;
}

void CubePreview::shutdown() {
    if (m_sampler.id != SG_INVALID_ID) sg_destroy_sampler(m_sampler);
    if (m_pip.id != SG_INVALID_ID) sg_destroy_pipeline(m_pip);
    if (m_shader.id != SG_INVALID_ID) sg_destroy_shader(m_shader);
    if (m_vbuf.id != SG_INVALID_ID) sg_destroy_buffer(m_vbuf);
    m_sampler = {};
    m_pip = {};
    m_shader = {};
    m_vbuf = {};
    m_ok = false;
}

void CubePreview::draw(sg_view demoTexture, int fbWidth, int fbHeight, float timeSec) {
    if (!m_ok || demoTexture.id == SG_INVALID_ID) {
        return;
    }
    const float aspect = fbHeight > 0 ? static_cast<float>(fbWidth) / fbHeight : 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -4.2f));
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), timeSec * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, timeSec * 0.31f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 mvp = proj * view * model;

    sg_apply_pipeline(m_pip);
    sg_bindings bind = {};
    bind.vertex_buffers[0] = m_vbuf;
    bind.views[0] = demoTexture;
    bind.samplers[0] = m_sampler;
    sg_apply_bindings(&bind);
    const sg_range range = {&mvp, sizeof(mvp)};
    sg_apply_uniforms(0, &range);
    sg_draw(0, 36, 1);
}

} // namespace shadertoy
