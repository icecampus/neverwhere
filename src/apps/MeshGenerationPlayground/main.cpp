#include "LandscapeBowlScenario.h"
#include "MeshPreview.h"
#include "ObjectGenerationScenario.h"
#include "PlaygroundLog.h"
#include "PlaygroundSmokeTest.h"
#include "PlaygroundState.h"
#include "PlaygroundUi.h"
#include "RectangleCliffScenario.h"
#include "RockFractureScenario.h"

#include <cstdint>
#include <mutex>

#include <imgui.h>
#include <spdlog/spdlog.h>

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

namespace meshgen_playground {

namespace {

void simguiLog(
    const char* tag,
    std::uint32_t logLevel,
    std::uint32_t logItemId,
    const char* message,
    std::uint32_t lineNumber,
    const char* filename,
    void* userData) {

    (void)userData;

    const char* safeTag = tag ? tag : "simgui";
    const char* safeMessage = message ? message : "<no message>";
    const char* safeFilename = filename ? filename : "<unknown>";

    switch (logLevel) {
    case 0:
    case 1:
        spdlog::error("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    case 2:
        spdlog::warn("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    default:
        spdlog::info("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    }
}

void init() {
    spdlog::info("init: start");
    spdlog::info("init: calling stm_setup()");
    stm_setup();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.lastTime = stm_now();
    }
    spdlog::info("init: stm_setup() done, lastTime recorded");

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;

    spdlog::info("init: calling sg_setup() with environment from sglue_environment()");
    sg_setup(&desc);

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.gfxOk = sg_isvalid();
    }

    spdlog::info("init: sg_setup() result sg_isvalid()={}", g_state.gfxOk);

    if (!g_state.gfxOk) {
        spdlog::error("init: sg_setup FAILED, sg_isvalid()=false, cannot continue graphics init");
        return;
    }

    spdlog::info("init: calling simgui_setup()");
    simgui_desc_t imguiDesc = {};
    imguiDesc.max_vertices = 8u * 1024u * 1024u; // 8M verts to fit Rock Fracture MC up to res=200
    imguiDesc.logger.func = simguiLog;
    simgui_setup(&imguiDesc);

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.imguiOk = true;
    }
    spdlog::info("init: simgui_setup() done, imguiOk=true");

    initProductionPreviewTextures();

    spdlog::info("init: calling rebuildRectangleCliffModel()");
    rebuildRectangleCliffModel();
    spdlog::info("init: calling rebuildLandscapeBowlModel()");
    rebuildLandscapeBowlModel();
    spdlog::info("init: calling rebuildObjectGenerationModel()");
    rebuildObjectGenerationModel();
    spdlog::info("init: calling rebuildRockFractureModel()");
    rebuildRockFractureModel();
    warmupProductionPreviewRenderer();
    runTestScenario();
    spdlog::info("init: complete");
}

void frame() {
    const std::uint64_t now = stm_now();
    float dt;
    int frameIndex;
    bool gfxOk;
    bool imguiOk;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
        g_state.lastTime = now;
        g_state.frameIndex++;
        frameIndex = g_state.frameIndex;
        g_state.dt = dt;
        gfxOk = g_state.gfxOk;
        imguiOk = g_state.imguiOk;
    }

    if (frameIndex <= 5) {
        spdlog::info("frame: index={}, dt={:.3f}ms, gfxOk={}, imguiOk={}, window={}x{}, dpi={}",
            frameIndex, dt * 1000.0f, gfxOk, imguiOk,
            sapp_width(), sapp_height(), sapp_dpi_scale());
    }

    if (frameIndex % 600 == 0) {
        spdlog::info("frame: heartbeat index={}, dt={:.3f}ms, gfxOk={}, imguiOk={}",
            frameIndex, dt * 1000.0f, gfxOk, imguiOk);
    }

    if (!gfxOk) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_state.gfxFailureLogged) {
            spdlog::error("frame: skipping frame #{}, gfxOk=false, no graphics context available", frameIndex);
            g_state.gfxFailureLogged = true;
        }
        return;
    }

    const int width = sapp_width();
    const int height = sapp_height();

    if (imguiOk) {
        simgui_frame_desc_t frameDesc = {};
        frameDesc.width = width;
        frameDesc.height = height;
        frameDesc.delta_time = dt;
        frameDesc.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&frameDesc);
        drawUi();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (imguiOk) {
        simgui_render();
        if (frameIndex <= 5) {
            const ImDrawData* drawData = ImGui::GetDrawData();
            spdlog::info("frame: post-simgui_render drawData cmdLists={}, vertices={}, indices={}, display={}x{}, framebufferScale={}x{}",
                drawData ? drawData->CmdListsCount : -1,
                drawData ? drawData->TotalVtxCount : -1,
                drawData ? drawData->TotalIdxCount : -1,
                drawData ? drawData->DisplaySize.x : -1.0f,
                drawData ? drawData->DisplaySize.y : -1.0f,
                drawData ? drawData->FramebufferScale.x : -1.0f,
                drawData ? drawData->FramebufferScale.y : -1.0f);
        }
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("cleanup: start");

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_state.imguiOk) {
            shutdownProductionPreviewTextures();
            spdlog::info("cleanup: calling simgui_shutdown()");
            simgui_shutdown();
            g_state.imguiOk = false;
        }

        if (sg_isvalid()) {
            spdlog::info("cleanup: calling sg_shutdown()");
            sg_shutdown();
        }
    }

    spdlog::info("cleanup: done, total frames rendered={}", g_state.frameIndex);
    spdlog::default_logger()->flush();
}

void event(const sapp_event* ev) {
    bool imguiOk;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        imguiOk = g_state.imguiOk;
    }

    if (imguiOk) {
        simgui_handle_event(ev);
    }

    if (ev->type == SAPP_EVENTTYPE_RESIZED) {
        spdlog::info("event: window resized to {}x{}", ev->window_width, ev->window_height);
    }
}

} // namespace

} // namespace meshgen_playground

int main(int argc, char* argv[]) {
    (void)argc;

    meshgen_playground::setupLogger(argv[0]);

    spdlog::info("main: MeshGenerationPlayground starting");
    spdlog::info("main: build config: C++20, SOKOL_D3D11={}, SOKOL_METAL={}, SOKOL_GLCORE={}, SOKOL_GLES3={}",
#if defined(SOKOL_D3D11)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_METAL)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_GLCORE)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_GLES3)
        "YES"
#else
        "NO"
#endif
    );

    sapp_desc desc = {};
    desc.init_cb = meshgen_playground::init;
    desc.frame_cb = meshgen_playground::frame;
    desc.cleanup_cb = meshgen_playground::cleanup;
    desc.event_cb = meshgen_playground::event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "MeshGenerationPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    spdlog::info("main: sapp_desc configured: window={}x{}, sample_count={}, high_dpi={}, window_title=\"{}\"",
        desc.width, desc.height, desc.sample_count, desc.high_dpi, desc.window_title);
    spdlog::info("main: calling sapp_run()");

    sapp_run(&desc);

    spdlog::info("main: sapp_run() returned, exiting");
    spdlog::default_logger()->flush();
    return 0;
}
