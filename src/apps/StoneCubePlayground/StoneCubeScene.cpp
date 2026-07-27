#include "pch.h"

#include "StoneCubeScene.h"

#include "StoneCubeSdfGlsl.h"

#include <sokol_glue.h>
#include <spdlog/spdlog.h>

namespace stonecube {

static_assert(sizeof(StoneCubeScene::Uniforms) == 112, "7 x vec4 std140 block");

namespace {

sg_view makeTextureView(sg_image image) {
    sg_view_desc desc = {};
    desc.texture.image = image;
    return sg_make_view(&desc);
}

sg_view makeColorAttachView(sg_image image) {
    sg_view_desc desc = {};
    desc.color_attachment.image = image;
    return sg_make_view(&desc);
}

} // namespace

void StoneCubeScene::init() {
    m_depthFormat = sglue_swapchain().depth_format;
    m_colorFormat = sglue_swapchain().color_format;

    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    m_sampler = sg_make_sampler(&smp);

    // Raymarch pass.
    {
        sg_shader_desc shd = {};
        shd.vertex_func.source = fullscreenVertexSource();
        shd.fragment_func.source = raymarchFragmentSource();
        shd.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.uniform_blocks[0].size = sizeof(Uniforms);
        shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
        const char* names[] = {"boxSize", "shape1", "shape2", "look",
            "camera", "target", "resolution"};
        for (int i = 0; i < 7; ++i) {
            shd.uniform_blocks[0].glsl_uniforms[i].glsl_name = names[i];
            shd.uniform_blocks[0].glsl_uniforms[i].type = SG_UNIFORMTYPE_FLOAT4;
            shd.uniform_blocks[0].glsl_uniforms[i].array_count = 1;
        }
        shd.label = "stonecube-scene-shader";
        m_sceneShader = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_sceneShader;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.colors[0].pixel_format = m_colorFormat;
        pip.depth.pixel_format = m_depthFormat;
        pip.depth.compare = SG_COMPAREFUNC_ALWAYS;
        pip.depth.write_enabled = false;
        pip.label = "stonecube-scene-pipeline";
        m_scenePip = sg_make_pipeline(&pip);
    }

    // Blit pass.
    {
        sg_shader_desc shd = {};
        shd.vertex_func.source = blitVertexSource();
        shd.fragment_func.source = blitFragmentSource();
        shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[0].view_slot = 0;
        shd.texture_sampler_pairs[0].sampler_slot = 0;
        shd.texture_sampler_pairs[0].glsl_name = "demo_tex";
        shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        shd.label = "stonecube-blit-shader";
        m_blitShader = sg_make_shader(&shd);

        sg_pipeline_desc pip = {};
        pip.shader = m_blitShader;
        pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        pip.colors[0].pixel_format = m_colorFormat;
        pip.depth.pixel_format = m_depthFormat;
        pip.depth.compare = SG_COMPAREFUNC_ALWAYS;
        pip.depth.write_enabled = false;
        pip.label = "stonecube-blit-pipeline";
        m_blitPip = sg_make_pipeline(&pip);
    }

    if (sg_query_shader_state(m_sceneShader) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(m_scenePip) != SG_RESOURCESTATE_VALID) {
        spdlog::error("StoneCubePlayground: scene pipeline creation failed");
    }
}

void StoneCubeScene::shutdown() {
    if (m_colorAttach.id != SG_INVALID_ID) sg_destroy_view(m_colorAttach);
    if (m_colorTex.id != SG_INVALID_ID) sg_destroy_view(m_colorTex);
    if (m_colorImg.id != SG_INVALID_ID) sg_destroy_image(m_colorImg);
    if (m_depthAttach.id != SG_INVALID_ID) sg_destroy_view(m_depthAttach);
    if (m_depthImg.id != SG_INVALID_ID) sg_destroy_image(m_depthImg);
    if (m_scenePip.id != SG_INVALID_ID) sg_destroy_pipeline(m_scenePip);
    if (m_sceneShader.id != SG_INVALID_ID) sg_destroy_shader(m_sceneShader);
    if (m_blitPip.id != SG_INVALID_ID) sg_destroy_pipeline(m_blitPip);
    if (m_blitShader.id != SG_INVALID_ID) sg_destroy_shader(m_blitShader);
    if (m_sampler.id != SG_INVALID_ID) sg_destroy_sampler(m_sampler);
}

void StoneCubeScene::ensureTarget(int w, int h) {
    if (w <= 0 || h <= 0 || (w == m_targetW && h == m_targetH && m_colorImg.id != SG_INVALID_ID)) {
        return;
    }
    if (m_colorAttach.id != SG_INVALID_ID) sg_destroy_view(m_colorAttach);
    if (m_colorTex.id != SG_INVALID_ID) sg_destroy_view(m_colorTex);
    if (m_colorImg.id != SG_INVALID_ID) sg_destroy_image(m_colorImg);
    if (m_depthAttach.id != SG_INVALID_ID) sg_destroy_view(m_depthAttach);
    if (m_depthImg.id != SG_INVALID_ID) sg_destroy_image(m_depthImg);
    m_targetW = w;
    m_targetH = h;

    sg_image_desc img = {};
    img.usage.color_attachment = true;
    img.width = w;
    img.height = h;
    img.pixel_format = m_colorFormat;
    img.sample_count = 1;
    img.label = "stonecube-color";
    m_colorImg = sg_make_image(&img);
    m_colorAttach = makeColorAttachView(m_colorImg);
    m_colorTex = makeTextureView(m_colorImg);

    sg_image_desc depth = {};
    depth.usage.depth_stencil_attachment = true;
    depth.width = w;
    depth.height = h;
    depth.pixel_format = m_depthFormat;
    depth.sample_count = 1;
    depth.label = "stonecube-depth";
    m_depthImg = sg_make_image(&depth);
    sg_view_desc view = {};
    view.depth_stencil_attachment.image = m_depthImg;
    m_depthAttach = sg_make_view(&view);
}

void StoneCubeScene::drawScene(const Params& params, const Camera& cam,
    float renderScale, int fbWidth, int fbHeight) {
    const int w = std::max(64, static_cast<int>(fbWidth * renderScale));
    const int h = std::max(64, static_cast<int>(fbHeight * renderScale));
    ensureTarget(w, h);

    Uniforms u = {};
    std::memcpy(u.boxSize, params.boxSize, sizeof(u.boxSize));
    std::memcpy(u.shape1, params.shape1, sizeof(u.shape1));
    std::memcpy(u.shape2, params.shape2, sizeof(u.shape2));
    std::memcpy(u.look, params.look, sizeof(u.look));
    u.camera[0] = cam.yaw;
    u.camera[1] = cam.pitch;
    u.camera[2] = cam.dist;
    u.camera[3] = 0.0f;
    std::memcpy(u.target, cam.target, sizeof(cam.target));
    u.target[3] = 0.0f;
    u.resolution[0] = static_cast<float>(w);
    u.resolution[1] = static_cast<float>(h);
    u.resolution[2] = 0.0f;
    u.resolution[3] = 0.0f;

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_pass pass = {};
    pass.action = action;
    pass.attachments.colors[0] = m_colorAttach;
    pass.attachments.depth_stencil = m_depthAttach;
    sg_begin_pass(&pass);
    sg_apply_pipeline(m_scenePip);
    const sg_range range = {&u, sizeof(u)};
    sg_apply_uniforms(0, &range);
    sg_draw(0, 3, 1);
    sg_end_pass();
}

void StoneCubeScene::drawBlit() const {
    if (m_colorTex.id == SG_INVALID_ID || m_blitPip.id == SG_INVALID_ID) {
        return;
    }
    sg_apply_pipeline(m_blitPip);
    sg_bindings bind = {};
    bind.views[0] = m_colorTex;
    bind.samplers[0] = m_sampler;
    sg_apply_bindings(&bind);
    sg_draw(0, 3, 1);
}

} // namespace stonecube
