#include <cstdint>

#include <spdlog/spdlog.h>

// Dear ImGui
#include <imgui.h>

// Sokol (implementation)
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

// Pick a backend for standalone runtime:
// - Windows: D3D11 (default for sokol_app, but we define explicitly)
// - macOS/iOS: Metal
// - Emscripten: GLES3/WebGL2
// - Linux/others: GLCore33
#if defined(_WIN32)
    #define SOKOL_D3D11
#elif defined(__APPLE__)
    #define SOKOL_METAL
#elif defined(__EMSCRIPTEN__)
    #define SOKOL_GLES3
#else
    #define SOKOL_GLCORE33
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

struct AppState {
    uint64_t last_time = 0;
    float dt = 1.0f / 60.0f;
    int frame_index = 0;
    bool gfx_ok = false;
    bool imgui_ok = false;
};

static AppState g_state;

static void init(void) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("NeverwhereGame: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.context = sapp_sgcontext();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();

    spdlog::info("NeverwhereGame: sg_setup() {}", g_state.gfx_ok ? "OK" : "FAILED");

    if (g_state.gfx_ok) {
        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
        spdlog::info("NeverwhereGame: simgui_setup() OK");
    }
}

static void frame(void) {
    const uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.last_time));
    g_state.last_time = now;
    g_state.frame_index++;

    if (!g_state.gfx_ok) {
        // Try again (e.g. if context wasn't ready for some reason)
        g_state.gfx_ok = sg_isvalid();
        if (!g_state.gfx_ok) {
            return;
        }
    }

    const int w = sapp_width();
    const int h = sapp_height();

    if (g_state.imgui_ok) {
        simgui_frame_desc_t fd = {};
        fd.width = w;
        fd.height = h;
        fd.delta_time = g_state.dt;
        fd.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&fd);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(380.0f, 160.0f), ImGuiCond_Once);
        ImGui::Begin("NeverwhereGame (debug)");
        ImGui::Text("Frame: %d", g_state.frame_index);
        ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
        ImGui::Text("Size: %dx%d  DPI: %.2f", w, h, sapp_dpi_scale());
        ImGui::Separator();
        ImGui::Text("Next: load resources + render world");
        ImGui::End();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = { 0.07f, 0.08f, 0.10f, 1.0f };

    sg_begin_default_pass(&action, w, h);
    // Runtime rendering will be added here (world renderer).
    if (g_state.imgui_ok) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    spdlog::info("NeverwhereGame: cleanup()");
    if (g_state.imgui_ok) {
        simgui_shutdown();
        g_state.imgui_ok = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

static void event(const sapp_event* ev) {
    // Input handling will be hooked here (and forwarded to ImGui later).
    if (g_state.imgui_ok) {
        simgui_handle_event(ev);
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "NeverwhereGame";
    desc.high_dpi = true;
#if defined(_WIN32)
    // Ensure logs are visible when started from a console.
    desc.win32_console_utf8 = true;
    desc.win32_console_attach = true;
#endif
    // Enable sokol_app internal logging (default is NO LOGGING).
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}

