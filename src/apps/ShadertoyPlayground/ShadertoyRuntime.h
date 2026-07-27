// Shadertoy runtime: compiles demo passes (shim + Common + pass source) into
// sokol pipelines and runs them the shadertoy way — buffers A..D first
// (ping-pong, inputs read the previous frame), then the Image pass
// (fullscreen to the swapchain, or to a texture in cube mode).
#pragma once

#include "DemoDiscovery.h"

// Ensure a backend is selected everywhere we include sokol_gfx.h (this
// playground is single-source GLSL: GLCORE on every desktop platform).
#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif
#include <sokol_gfx.h>

namespace shadertoy {

// Fragment uniform block, std140 layout (must match the glsl_uniforms order
// in ShadertoyRuntime.cpp; verified by a static_assert on the size).
struct FsUniforms {
    float iResolution[3];        // 0
    float iTime;                 // 12
    float iTimeDelta;            // 16
    int iFrame;                  // 20
    float pad0[2];               // 24
    float iMouse[4];             // 32
    float iDate[4];              // 48
    float iChannelResolution[4][4]; // 64 (vec3 stride 16)
};

struct FrameParams {
    float width = 0.0f;          // framebuffer pixels
    float height = 0.0f;
    float timeSec = 0.0f;        // since demo load
    float timeDelta = 0.0f;
    int frameIndex = 0;          // since demo load
    float mouse[4] = {};         // shadertoy iMouse
    float date[4] = {};          // shadertoy iDate
};

class ShadertoyRuntime {
public:
    void init();
    void shutdown();

    // Compiles all passes and (re)allocates targets. On failure the previous
    // demo keeps running and lastError() describes the problem.
    bool loadDemo(const Demo& demo, int fbWidth, int fbHeight);
    void unloadDemo();
    // Re-reads sources from disk and recompiles (same as loadDemo on the
    // current demo).
    bool reloadDemo();
    bool isLoaded() const { return m_loaded; }
    const std::string& demoName() const { return m_demoName; }
    const std::string& lastError() const { return m_error; }

    bool cubeMode = false;

    // Frame sequence:
    //   renderBuffers(fp)                       — buffer passes (own sg_pass)
    //   if cubeMode: renderImageToTarget(fp)    — Image -> texture (own pass)
    //   <swapchain pass>
    //     if !cubeMode: drawImageFullscreen(fp) — Image into the current pass
    //     else:         CubePreview::draw(imageTextureView(), ...)
    //   endFrame()                              — ping-pong swap
    void renderBuffers(const FrameParams& fp);
    void renderImageToTarget(const FrameParams& fp);
    void drawImageFullscreen(const FrameParams& fp);
    void endFrame();

    sg_view imageTextureView() const { return m_imageTexView; }

private:
    struct BufferTarget {
        sg_image images[2]{};
        sg_view attachViews[2]{};
        sg_view texViews[2]{};
        int writeSlot = 0;
    };
    struct CompiledPass {
        PassKind kind = PassKind::Image;
        sg_shader shader{};
        sg_pipeline pip{};
        ChannelInput inputs[4];
        bool hasTextureChannels = false;
    };
    struct ExternalTexture {
        sg_image image{};
        sg_view view{};
        int width = 0;
        int height = 0;
    };

    bool compilePass(const Demo& demo, const DemoPass& pass, CompiledPass& out);
    void destroyPass(CompiledPass& pass);
    void ensureTargets(int w, int h);
    void destroyTargets();
    void clearTargets();
    void loadTextures(const Demo& demo);
    void destroyTextures();
    // Binds channels + uniforms of the pass and draws the fullscreen triangle
    // in the CURRENT pass. imageStage=true selects the Image-pass bindings
    // (current frame's buffers); buffer passes read the previous frame.
    void drawPass(const CompiledPass& pass, const FrameParams& fp, bool imageStage);
    void fillUniforms(const FrameParams& fp, FsUniforms& out) const;
    BufferTarget* bufferFor(PassKind kind);
    const BufferTarget* bufferFor(PassKind kind) const;

    bool m_loaded = false;
    std::string m_demoName;
    std::string m_error;
    Demo m_demo; // shallow copy for reload (sources re-read from disk)
    std::string m_commonSource;

    std::vector<CompiledPass> m_bufferPasses; // A..D order
    CompiledPass m_imagePass;
    bool m_hasImagePass = false;

    BufferTarget m_buffers[4];
    ExternalTexture m_textures[4];
    sg_image m_imageTarget{};
    sg_view m_imageAttachView{};
    sg_view m_imageTexView{};
    sg_image m_depthImage{};
    sg_view m_depthAttachView{};
    int m_targetW = 0;
    int m_targetH = 0;

    sg_image m_placeholderImg{};
    sg_view m_placeholderView{};
    sg_sampler m_samplerTex{};    // mipmap + repeat (texture channels)
    sg_sampler m_samplerBuf{};    // linear + clamp (buffer channels / placeholder)
    sg_pixel_format m_depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;
    sg_pixel_format m_colorFormat = SG_PIXELFORMAT_RGBA8;
};

} // namespace shadertoy
