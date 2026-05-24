#include <algorithm>
#include <cstdint>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Landscape3dRenderer.h"
#include "TerrainScene.h"

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

namespace {

struct AppState {
    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;

    bool orbiting = false;
    bool panning = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float startYaw = 45.0f;
    float startPitch = 35.0f;
    glm::vec3 startTarget{0.0f};
};

AppState g_state;
TerrainSceneSettings g_sceneSettings;
TerrainScene g_scene;
Landscape3dRenderParams g_renderParams;
Landscape3dCamera g_camera;
Landscape3dRenderer g_renderer;
bool g_needsMeshRebuild = true;

void regenerateScene() {
    g_scene.generate(g_sceneSettings);
    g_needsMeshRebuild = true;
}

void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 430.0f), ImGuiCond_Once);

    ImGui::Begin("Landscape3dPlayground");
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
    ImGui::Separator();

    int gridSize = g_sceneSettings.gridSize;
    if (ImGui::SliderInt("Grid Size", &gridSize, 8, 96)) {
        g_sceneSettings.gridSize = gridSize;
        regenerateScene();
    }

    if (ImGui::InputInt("Seed", &g_sceneSettings.seed)) {
        regenerateScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate")) {
        g_sceneSettings.seed += 1;
        regenerateScene();
    }

    if (ImGui::SliderFloat("Height Scale", &g_renderParams.heightScale, 0.0f, 12.0f)) {
        g_needsMeshRebuild = true;
    }

    ImGui::Separator();
    ImGui::Text("Lighting");
    ImGui::SliderFloat("Light Yaw", &g_renderParams.lightYawDeg, -180.0f, 180.0f);
    ImGui::SliderFloat("Light Pitch", &g_renderParams.lightPitchDeg, 5.0f, 85.0f);

    ImGui::Separator();
    ImGui::Text("Debug");
    const char* debugModes[] = {"Lit", "Material", "Normals", "Height"};
    ImGui::Combo("Debug Mode", &g_renderParams.debugMode, debugModes, 4);
    ImGui::Checkbox("Wireframe Overlay", &g_renderParams.showWireframe);

    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::Checkbox("Perspective", &g_camera.perspective);
    ImGui::SliderFloat("Yaw", &g_camera.yawDeg, -180.0f, 180.0f);
    ImGui::SliderFloat("Pitch", &g_camera.pitchDeg, 10.0f, 80.0f);
    ImGui::SliderFloat("Distance", &g_camera.distance, 8.0f, 120.0f);
    ImGui::SliderFloat("Ortho Scale", &g_camera.orthoScale, 4.0f, 80.0f);
    if (ImGui::Button("Reset Camera")) {
        g_camera = {};
    }

    ImGui::Separator();
    ImGui::Text("Triangles: %d", g_renderer.triangleCount());
    ImGui::Text("Lines: %d", g_renderer.lineCount());
    ImGui::Text("Controls: RMB orbit, MMB pan, wheel zoom");
    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Landscape3dPlayground: init()");

    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("Landscape3dPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");

    if (!g_state.gfxOk) return;

    simgui_desc_t imguiDesc = {};
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;

    g_renderer.init();
    regenerateScene();
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
    g_state.lastTime = now;
    g_state.frameIndex++;

    if (!g_state.gfxOk) return;

    const int w = sapp_width();
    const int h = sapp_height();

    if (g_needsMeshRebuild) {
        g_renderer.rebuildMesh(g_scene, g_renderParams.heightScale);
        g_needsMeshRebuild = false;
    }

    if (g_state.imguiOk) {
        simgui_frame_desc_t fd = {};
        fd.width = w;
        fd.height = h;
        fd.delta_time = g_state.dt;
        fd.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&fd);
        drawUi();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;
    action.stencil.load_action = SG_LOADACTION_CLEAR;

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    g_renderer.render(g_camera, g_renderParams, w, h);

    if (g_state.imguiOk) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("Landscape3dPlayground: cleanup()");
    g_renderer.shutdown();
    if (g_state.imguiOk) {
        simgui_shutdown();
        g_state.imguiOk = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* ev) {
    if (g_state.imguiOk) {
        simgui_handle_event(ev);
    }

    if (g_state.imguiOk) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }

    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT || ev->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
            g_state.orbiting = ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT;
            g_state.panning = ev->mouse_button == SAPP_MOUSEBUTTON_MIDDLE;
            g_state.dragStartX = ev->mouse_x;
            g_state.dragStartY = ev->mouse_y;
            g_state.startYaw = g_camera.yawDeg;
            g_state.startPitch = g_camera.pitchDeg;
            g_state.startTarget = g_camera.target;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.orbiting = false;
        }
        if (ev->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
            g_state.panning = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE: {
        const float dx = ev->mouse_x - g_state.dragStartX;
        const float dy = ev->mouse_y - g_state.dragStartY;
        if (g_state.orbiting) {
            g_camera.yawDeg = g_state.startYaw + dx * 0.25f;
            g_camera.pitchDeg = std::clamp(g_state.startPitch + dy * 0.18f, 10.0f, 80.0f);
        } else if (g_state.panning) {
            const float scale = g_camera.orthoScale / std::max(1.0f, (float)sapp_height());
            g_camera.target = g_state.startTarget + (-g_camera.right() * dx + g_camera.up() * dy) * scale * 2.0f;
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_SCROLL: {
        if (ev->scroll_y == 0.0f) break;
        const float zoom = (ev->scroll_y > 0.0f) ? 0.90f : 1.10f;
        g_camera.distance = std::clamp(g_camera.distance * zoom, 8.0f, 140.0f);
        g_camera.orthoScale = std::clamp(g_camera.orthoScale * zoom, 4.0f, 90.0f);
        break;
    }
    default:
        break;
    }
}

} // namespace

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
    desc.window_title = "Landscape3dPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}

