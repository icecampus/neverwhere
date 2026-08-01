#include "pch.h"

#include "ShadertoyRuntime.h"

#include "GlslShim.h"

#include <sokol_glue.h>
#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace shadertoy {

static_assert(sizeof(FsUniforms) == 128, "FsUniforms must match the std140 layout");

namespace {

const char* kChannelNames[] = {"iChannel0", "iChannel1", "iChannel2", "iChannel3"};

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

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

// CPU box-filter mip chain for texture channels (sokol does not generate
// mipmaps; Shane's tex3D demos visibly rely on them for the distance blur).
sg_image loadTextureWithMips(const std::filesystem::path& path, int& outW, int& outH) {
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &n, 4);
    if (pixels == nullptr) {
        spdlog::warn("texture load failed: {}", path.string());
        return {SG_INVALID_ID};
    }
    outW = w;
    outH = h;

    int levels = 1;
    while ((w >> levels) > 1 || (h >> levels) > 1) {
        ++levels;
    }
    if (levels > SG_MAX_MIPMAPS) {
        levels = SG_MAX_MIPMAPS;
    }

    std::vector<std::vector<stbi_uc>> mipData(static_cast<size_t>(levels));
    sg_image_desc desc = {};
    desc.width = w;
    desc.height = h;
    desc.num_mipmaps = levels;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.data.mip_levels[0].ptr = pixels;
    desc.data.mip_levels[0].size = static_cast<size_t>(w) * h * 4;

    int pw = w, ph = h;
    const stbi_uc* prev = pixels;
    for (int level = 1; level < levels; ++level) {
        const int lw = std::max(1, pw / 2);
        const int lh = std::max(1, ph / 2);
        auto& data = mipData[static_cast<size_t>(level)];
        data.resize(static_cast<size_t>(lw) * lh * 4);
        for (int y = 0; y < lh; ++y) {
            for (int x = 0; x < lw; ++x) {
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(2 * x + dx, pw - 1);
                            const int sy = std::min(2 * y + dy, ph - 1);
                            sum += prev[(static_cast<size_t>(sy) * pw + sx) * 4 + c];
                        }
                    }
                    data[(static_cast<size_t>(y) * lw + x) * 4 + c] = static_cast<stbi_uc>(sum / 4);
                }
            }
        }
        desc.data.mip_levels[level].ptr = data.data();
        desc.data.mip_levels[level].size = data.size();
        prev = data.data();
        pw = lw;
        ph = lh;
    }

    sg_image image = sg_make_image(&desc);
    stbi_image_free(pixels);
    return image;
}

} // namespace

void ShadertoyRuntime::init() {
    sg_sampler_desc smp = {};
    smp.min_filter = SG_FILTER_LINEAR;
    smp.mag_filter = SG_FILTER_LINEAR;
    smp.mipmap_filter = SG_FILTER_LINEAR; // trilinear for texture channels
    smp.wrap_u = SG_WRAP_REPEAT;
    smp.wrap_v = SG_WRAP_REPEAT;
    smp.label = "shadertoy-sampler-texture";
    m_samplerTex = sg_make_sampler(&smp);

    smp.min_filter = SG_FILTER_LINEAR;
    smp.mipmap_filter = SG_FILTER_NEAREST; // buffers have a single level
    smp.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp.label = "shadertoy-sampler-buffer";
    m_samplerBuf = sg_make_sampler(&smp);

    const std::uint32_t white = 0xFFFFFFFFu;
    sg_image_desc img = {};
    img.width = 1;
    img.height = 1;
    img.pixel_format = SG_PIXELFORMAT_RGBA8;
    img.data.mip_levels[0].ptr = &white;
    img.data.mip_levels[0].size = sizeof(white);
    img.label = "shadertoy-placeholder";
    m_placeholderImg = sg_make_image(&img);
    m_placeholderView = makeTextureView(m_placeholderImg);

    m_depthFormat = sglue_swapchain().depth_format;
    m_colorFormat = sglue_swapchain().color_format;

    // Fullscreen blit (scaled image target -> swapchain).
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
    shd.label = "shadertoy-blit-shader";
    m_blitShader = sg_make_shader(&shd);

    sg_pipeline_desc pip = {};
    pip.shader = m_blitShader;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pip.colors[0].pixel_format = m_colorFormat;
    pip.depth.pixel_format = m_depthFormat;
    pip.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip.depth.write_enabled = false;
    pip.label = "shadertoy-blit-pipeline";
    m_blitPip = sg_make_pipeline(&pip);
}

void ShadertoyRuntime::shutdown() {
    unloadDemo();
    if (m_blitPip.id != SG_INVALID_ID) sg_destroy_pipeline(m_blitPip);
    if (m_blitShader.id != SG_INVALID_ID) sg_destroy_shader(m_blitShader);
    m_blitPip = {};
    m_blitShader = {};
    if (m_placeholderView.id != SG_INVALID_ID) sg_destroy_view(m_placeholderView);
    if (m_placeholderImg.id != SG_INVALID_ID) sg_destroy_image(m_placeholderImg);
    if (m_samplerTex.id != SG_INVALID_ID) sg_destroy_sampler(m_samplerTex);
    if (m_samplerBuf.id != SG_INVALID_ID) sg_destroy_sampler(m_samplerBuf);
    m_placeholderView = {};
    m_placeholderImg = {};
    m_samplerTex = {};
    m_samplerBuf = {};
}

bool ShadertoyRuntime::loadDemo(const Demo& demo, int fbWidth, int fbHeight) {
    // Compile into fresh resources first: on any failure the current demo
    // keeps running untouched.
    std::vector<CompiledPass> newBufferPasses;
    CompiledPass newImagePass;
    bool hasImagePass = false;
    std::string error;

    for (const DemoPass& pass : demo.passes) {
        CompiledPass compiled;
        if (!compilePass(demo, pass, compiled)) {
            error = m_error;
            for (CompiledPass& p : newBufferPasses) destroyPass(p);
            if (hasImagePass) destroyPass(newImagePass);
            m_error = error;
            return false;
        }
        if (pass.kind == PassKind::Image) {
            newImagePass = compiled;
            hasImagePass = true;
        } else {
            newBufferPasses.push_back(compiled);
        }
    }
    if (!hasImagePass) {
        m_error = "demo has no Image pass";
        for (CompiledPass& p : newBufferPasses) destroyPass(p);
        return false;
    }

    unloadDemo();
    m_demo = demo;
    m_demoName = demo.name;
    m_commonSource = demo.commonSource;
    m_bufferPasses = std::move(newBufferPasses);
    m_imagePass = newImagePass;
    m_hasImagePass = true;
    m_loaded = true;
    m_error.clear();

    loadTextures(demo);
    ensureTargets(fbWidth, fbHeight);
    spdlog::info("ShadertoyPlayground: loaded demo '{}' ({} buffer passes)",
        m_demoName, m_bufferPasses.size());
    return true;
}

void ShadertoyRuntime::unloadDemo() {
    for (CompiledPass& p : m_bufferPasses) destroyPass(p);
    m_bufferPasses.clear();
    if (m_hasImagePass) {
        destroyPass(m_imagePass);
        m_hasImagePass = false;
    }
    destroyTextures();
    destroyTargets();
    m_loaded = false;
    m_demoName.clear();
}

bool ShadertoyRuntime::reloadDemo() {
    if (!m_loaded) {
        return false;
    }
    return loadDemo(m_demo, m_targetW, m_targetH);
}

bool ShadertoyRuntime::compilePass(const Demo& demo, const DemoPass& pass, CompiledPass& out) {
    const std::string source = readTextFile(pass.sourcePath);
    if (source.empty()) {
        m_error = "empty or unreadable pass source: " + pass.sourcePath.string();
        return false;
    }
    const std::string fsSource = std::string(fragmentPreamble()) + demo.commonSource +
        "\n" + source + fragmentEpilog();

    out.kind = pass.kind;
    for (int i = 0; i < 4; ++i) {
        out.inputs[i] = pass.inputs[i];
    }

    sg_shader_desc shd = {};
    shd.vertex_func.source = fullscreenVertexSource();
    shd.fragment_func.source = fsSource.c_str();

    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[0].size = sizeof(FsUniforms);
    shd.uniform_blocks[0].layout = SG_UNIFORMLAYOUT_STD140;
    struct UEntry { const char* name; sg_uniform_type type; uint16_t count; };
    const UEntry entries[] = {
        {"iResolution", SG_UNIFORMTYPE_FLOAT3, 1},
        {"iTime", SG_UNIFORMTYPE_FLOAT, 1},
        {"iTimeDelta", SG_UNIFORMTYPE_FLOAT, 1},
        {"iFrame", SG_UNIFORMTYPE_INT, 1},
        {"iMouse", SG_UNIFORMTYPE_FLOAT4, 1},
        {"iDate", SG_UNIFORMTYPE_FLOAT4, 1},
        {"iChannelResolution[0]", SG_UNIFORMTYPE_FLOAT4, 4},
    };
    for (int i = 0; i < 7; ++i) {
        shd.uniform_blocks[0].glsl_uniforms[i].glsl_name = entries[i].name;
        shd.uniform_blocks[0].glsl_uniforms[i].type = entries[i].type;
        shd.uniform_blocks[0].glsl_uniforms[i].array_count = entries[i].count;
    }

    bool hasTextureChannels = false;
    for (int i = 0; i < 4; ++i) {
        hasTextureChannels = hasTextureChannels || pass.inputs[i].kind == ChannelKind::Texture;
    }
    out.hasTextureChannels = hasTextureChannels;

    for (int i = 0; i < 4; ++i) {
        shd.views[i].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        shd.views[i].texture.image_type = SG_IMAGETYPE_2D;
        shd.views[i].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        shd.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.texture_sampler_pairs[i].view_slot = i;
        // Every declared sampler must be referenced: slot 0 (clamp/linear)
        // is used by all non-texture channels, slot 1 (repeat/mipmap) is
        // declared only when the pass actually has texture channels.
        shd.texture_sampler_pairs[i].sampler_slot =
            (pass.inputs[i].kind == ChannelKind::Texture) ? 1 : 0;
        shd.texture_sampler_pairs[i].glsl_name = kChannelNames[i];
    }
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    if (hasTextureChannels) {
        shd.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
        shd.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    }
    shd.label = pass.sourcePath.filename().string().c_str();

    out.shader = sg_make_shader(&shd);
    if (sg_query_shader_state(out.shader) != SG_RESOURCESTATE_VALID) {
        m_error = "shader compile failed: " + demo.name + " / " +
            pass.sourcePath.filename().string();
        return false;
    }

    sg_pipeline_desc pip = {};
    pip.shader = out.shader;
    pip.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    // Color format must match the pass attachments: buffer passes render
    // into RGBA16F targets, the Image pass renders into the swapchain (or
    // the image target in cube mode, which shares the swapchain format).
    pip.colors[0].pixel_format = (pass.kind == PassKind::Image)
        ? m_colorFormat : SG_PIXELFORMAT_RGBA16F;
    // Every pass (offscreen buffers and the swapchain alike) gets a depth
    // attachment, so one depth format works for all pipelines.
    pip.depth.pixel_format = m_depthFormat;
    pip.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pip.depth.write_enabled = false;
    pip.label = pass.sourcePath.filename().string().c_str();
    out.pip = sg_make_pipeline(&pip);
    if (sg_query_pipeline_state(out.pip) != SG_RESOURCESTATE_VALID) {
        m_error = "pipeline creation failed: " + demo.name + " / " +
            pass.sourcePath.filename().string();
        sg_destroy_shader(out.shader);
        out.shader = {SG_INVALID_ID};
        return false;
    }
    return true;
}

void ShadertoyRuntime::destroyPass(CompiledPass& pass) {
    if (pass.pip.id != SG_INVALID_ID) sg_destroy_pipeline(pass.pip);
    if (pass.shader.id != SG_INVALID_ID) sg_destroy_shader(pass.shader);
    pass.pip = {SG_INVALID_ID};
    pass.shader = {SG_INVALID_ID};
}

void ShadertoyRuntime::ensureTargets(int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (w == m_targetW && h == m_targetH &&
        m_imageTarget.id != SG_INVALID_ID) {
        return;
    }
    destroyTargets();
    m_targetW = w;
    m_targetH = h;

    for (BufferTarget& buf : m_buffers) {
        for (int slot = 0; slot < 2; ++slot) {
            sg_image_desc img = {};
            img.usage.color_attachment = true;
            img.width = w;
            img.height = h;
            img.pixel_format = SG_PIXELFORMAT_RGBA16F;
            img.sample_count = 1;
            img.label = "shadertoy-buffer";
            buf.images[slot] = sg_make_image(&img);
            buf.attachViews[slot] = makeColorAttachView(buf.images[slot]);
            buf.texViews[slot] = makeTextureView(buf.images[slot]);
        }
        buf.writeSlot = 0;
    }

    {
        sg_image_desc img = {};
        img.usage.color_attachment = true;
        img.width = w;
        img.height = h;
        img.pixel_format = m_colorFormat; // sampled by the cube preview
        img.sample_count = 1;
        img.label = "shadertoy-image-target";
        m_imageTarget = sg_make_image(&img);
        m_imageAttachView = makeColorAttachView(m_imageTarget);
        m_imageTexView = makeTextureView(m_imageTarget);
    }

    {
        sg_image_desc img = {};
        img.usage.depth_stencil_attachment = true;
        img.width = w;
        img.height = h;
        img.pixel_format = m_depthFormat;
        img.sample_count = 1;
        img.label = "shadertoy-depth";
        m_depthImage = sg_make_image(&img);
        sg_view_desc view = {};
        view.depth_stencil_attachment.image = m_depthImage;
        m_depthAttachView = sg_make_view(&view);
    }

    clearTargets();
}

void ShadertoyRuntime::destroyTargets() {
    for (BufferTarget& buf : m_buffers) {
        for (int slot = 0; slot < 2; ++slot) {
            if (buf.attachViews[slot].id != SG_INVALID_ID) sg_destroy_view(buf.attachViews[slot]);
            if (buf.texViews[slot].id != SG_INVALID_ID) sg_destroy_view(buf.texViews[slot]);
            if (buf.images[slot].id != SG_INVALID_ID) sg_destroy_image(buf.images[slot]);
            buf.attachViews[slot] = buf.texViews[slot] = {SG_INVALID_ID};
            buf.images[slot] = {SG_INVALID_ID};
        }
        buf.writeSlot = 0;
    }
    if (m_imageAttachView.id != SG_INVALID_ID) sg_destroy_view(m_imageAttachView);
    if (m_imageTexView.id != SG_INVALID_ID) sg_destroy_view(m_imageTexView);
    if (m_imageTarget.id != SG_INVALID_ID) sg_destroy_image(m_imageTarget);
    if (m_depthAttachView.id != SG_INVALID_ID) sg_destroy_view(m_depthAttachView);
    if (m_depthImage.id != SG_INVALID_ID) sg_destroy_image(m_depthImage);
    m_imageAttachView = m_imageTexView = m_depthAttachView = {SG_INVALID_ID};
    m_imageTarget = m_depthImage = {SG_INVALID_ID};
    m_targetW = m_targetH = 0;
}

// Shadertoy buffers start zeroed (feedback demos rely on it).
void ShadertoyRuntime::clearTargets() {
    for (BufferTarget& buf : m_buffers) {
        for (int slot = 0; slot < 2; ++slot) {
            sg_pass_action action = {};
            action.colors[0].load_action = SG_LOADACTION_CLEAR;
            action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
            sg_pass pass = {};
            pass.action = action;
            pass.attachments.colors[0] = buf.attachViews[slot];
            pass.attachments.depth_stencil = m_depthAttachView;
            sg_begin_pass(&pass);
            sg_end_pass();
        }
    }
    sg_commit();
}

void ShadertoyRuntime::loadTextures(const Demo& demo) {
    destroyTextures();
    for (const DemoPass& pass : demo.passes) {
        for (int i = 0; i < 4; ++i) {
            if (pass.inputs[i].kind != ChannelKind::Texture) {
                continue;
            }
            if (m_textures[i].image.id != SG_INVALID_ID) {
                continue; // already loaded (channel shared between passes)
            }
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path texDir = demo.dir / "textures";
            for (const auto& entry : fs::directory_iterator(texDir, ec)) {
                const std::string stem = entry.path().stem().string();
                if (stem == std::string("iChannel") + char('0' + i)) {
                    ExternalTexture tex;
                    tex.image = loadTextureWithMips(entry.path(), tex.width, tex.height);
                    if (tex.image.id != SG_INVALID_ID) {
                        tex.view = makeTextureView(tex.image);
                        m_textures[i] = tex;
                        spdlog::info("  iChannel{} <- {} ({}x{})", i,
                            entry.path().filename().string(), tex.width, tex.height);
                    }
                    break;
                }
            }
        }
    }
}

void ShadertoyRuntime::destroyTextures() {
    for (ExternalTexture& tex : m_textures) {
        if (tex.view.id != SG_INVALID_ID) sg_destroy_view(tex.view);
        if (tex.image.id != SG_INVALID_ID) sg_destroy_image(tex.image);
        tex = {};
    }
}

ShadertoyRuntime::BufferTarget* ShadertoyRuntime::bufferFor(PassKind kind) {
    const int index = static_cast<int>(kind) - static_cast<int>(PassKind::BufferA);
    return (index >= 0 && index < 4) ? &m_buffers[index] : nullptr;
}

const ShadertoyRuntime::BufferTarget* ShadertoyRuntime::bufferFor(PassKind kind) const {
    const int index = static_cast<int>(kind) - static_cast<int>(PassKind::BufferA);
    return (index >= 0 && index < 4) ? &m_buffers[index] : nullptr;
}

void ShadertoyRuntime::fillUniforms(const FrameParams& fp, FsUniforms& out) const {
    out = {};
    out.iResolution[0] = fp.width;
    out.iResolution[1] = fp.height;
    out.iResolution[2] = 1.0f;
    out.iTime = fp.timeSec;
    out.iTimeDelta = fp.timeDelta;
    out.iFrame = fp.frameIndex;
    std::memcpy(out.iMouse, fp.mouse, sizeof(out.iMouse));
    std::memcpy(out.iDate, fp.date, sizeof(out.iDate));
}

void ShadertoyRuntime::drawPass(const CompiledPass& pass, const FrameParams& fp, bool imageStage) {
    FsUniforms uniforms;
    fillUniforms(fp, uniforms);

    sg_bindings bind = {};
    bind.samplers[0] = m_samplerBuf;
    if (pass.hasTextureChannels) {
        bind.samplers[1] = m_samplerTex;
    }
    for (int i = 0; i < 4; ++i) {
        const ChannelInput& in = pass.inputs[i];
        sg_view view = m_placeholderView;
        float w = 1.0f;
        float h = 1.0f;
        if (in.kind == ChannelKind::Texture && m_textures[i].view.id != SG_INVALID_ID) {
            view = m_textures[i].view;
            w = static_cast<float>(m_textures[i].width);
            h = static_cast<float>(m_textures[i].height);
        } else if (in.kind == ChannelKind::Buffer) {
            const BufferTarget* buf = bufferFor(in.buffer);
            if (buf != nullptr && buf->texViews[0].id != SG_INVALID_ID) {
                // Buffer passes read the previous frame (shadertoy semantics);
                // the Image pass reads the just-rendered current frame.
                const int slot = imageStage ? buf->writeSlot : (buf->writeSlot ^ 1);
                view = buf->texViews[slot];
                w = static_cast<float>(m_targetW);
                h = static_cast<float>(m_targetH);
            }
        }
        bind.views[i] = view;
        uniforms.iChannelResolution[i][0] = w;
        uniforms.iChannelResolution[i][1] = h;
        uniforms.iChannelResolution[i][2] = 1.0f;
    }

    sg_apply_pipeline(pass.pip);
    sg_apply_bindings(&bind);
    const sg_range range = {&uniforms, sizeof(uniforms)};
    sg_apply_uniforms(0, &range);
    sg_draw(0, 3, 1);
}

int ShadertoyRuntime::scaledWidth(float fbWidth) const {
    return std::max(64, static_cast<int>(fbWidth * renderScale));
}

int ShadertoyRuntime::scaledHeight(float fbHeight) const {
    return std::max(64, static_cast<int>(fbHeight * renderScale));
}

FrameParams ShadertoyRuntime::scaledParams(const FrameParams& fp) const {
    FrameParams out = fp;
    out.width = static_cast<float>(scaledWidth(fp.width));
    out.height = static_cast<float>(scaledHeight(fp.height));
    for (int i = 0; i < 4; ++i) {
        out.mouse[i] = fp.mouse[i] * renderScale;
    }
    return out;
}

void ShadertoyRuntime::renderBuffers(const FrameParams& fp) {
    if (!m_loaded) {
        return;
    }
    const FrameParams sfp = scaledParams(fp);
    ensureTargets(static_cast<int>(sfp.width), static_cast<int>(sfp.height));
    for (const CompiledPass& pass : m_bufferPasses) {
        BufferTarget* buf = bufferFor(pass.kind);
        if (buf == nullptr) {
            continue;
        }
        sg_pass_action action = {};
        action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        sg_pass passDesc = {};
        passDesc.action = action;
        passDesc.attachments.colors[0] = buf->attachViews[buf->writeSlot];
        passDesc.attachments.depth_stencil = m_depthAttachView;
        sg_begin_pass(&passDesc);
        drawPass(pass, sfp, false);
        sg_end_pass();
    }
}

void ShadertoyRuntime::renderImageToTarget(const FrameParams& fp) {
    if (!m_loaded || !m_hasImagePass) {
        return;
    }
    const FrameParams sfp = scaledParams(fp);
    ensureTargets(static_cast<int>(sfp.width), static_cast<int>(sfp.height));
    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_pass pass = {};
    pass.action = action;
    pass.attachments.colors[0] = m_imageAttachView;
    pass.attachments.depth_stencil = m_depthAttachView;
    sg_begin_pass(&pass);
    drawPass(m_imagePass, sfp, true);
    sg_end_pass();
}

void ShadertoyRuntime::drawImageBlit() {
    if (!m_loaded || m_imageTexView.id == SG_INVALID_ID || m_blitPip.id == SG_INVALID_ID) {
        return;
    }
    sg_apply_pipeline(m_blitPip);
    sg_bindings bind = {};
    bind.views[0] = m_imageTexView;
    bind.samplers[0] = m_samplerBuf;
    sg_apply_bindings(&bind);
    sg_draw(0, 3, 1);
}

void ShadertoyRuntime::endFrame() {
    for (BufferTarget& buf : m_buffers) {
        buf.writeSlot ^= 1;
    }
}

} // namespace shadertoy
