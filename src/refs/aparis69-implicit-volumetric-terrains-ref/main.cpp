#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "BatchExport.h"
#include "IvtRenderer.h"
#include "IvtScene.h"

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
    std::uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;
};

AppState g_state;
ivt_view::IvtSettings g_settings;
ivt_view::IvtScene g_scene;
ivt_view::IvtRenderer g_renderer;

bool g_rebuildRequested = true;
bool g_resetViewForModel = false;

bool sceneCombo(int& sceneIndex) {
    bool changed = false;
    const auto kind = static_cast<ivt_view::IvtSceneKind>(sceneIndex);
    if (ImGui::BeginCombo("Scene", ivt_view::IvtScene::sceneName(kind))) {
        const int count = ivt_view::IvtScene::sceneCount();
        for (int i = 0; i < count; i++) {
            const bool selected = i == sceneIndex;
            const auto k = static_cast<ivt_view::IvtSceneKind>(i);
            if (ImGui::Selectable(ivt_view::IvtScene::sceneName(k), selected)) {
                sceneIndex = i;
                g_settings.scene = k;
                g_settings.mcResolution = ivt_view::IvtScene::defaultMcResolution(k);
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void drawUi() {
    const bool building = g_scene.isBuilding();
    const double buildElapsed = g_scene.buildElapsedSeconds();
    const char* buildStage = g_scene.buildStage();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("IVT Viewer", nullptr, rootFlags);

    constexpr float kPanelWidth = 320.0f;
    constexpr float kGutter = 12.0f;
    const float fullHeight = ImGui::GetContentRegionAvail().y;
    const float meshWidth = std::max(200.0f, ImGui::GetContentRegionAvail().x - kPanelWidth - kGutter);
    const float panelX = ImGui::GetCursorScreenPos().x + meshWidth + kGutter;

    const ImVec2 meshSize(meshWidth, fullHeight);
    ImGui::BeginChild("##meshPanel", meshSize, true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 meshOrigin = ImGui::GetCursorScreenPos();
    g_renderer.handleMeshViewInput(meshOrigin, meshSize);
    g_scene.withModel([&](const ivt_view::IvtModel& model) {
        g_renderer.renderMeshGpu(model, meshSize);
        g_renderer.drawMeshViewOverlay(model, meshOrigin, meshSize, building, buildElapsed, buildStage);
    });
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(panelX, viewport->WorkPos.y));
    ImGui::BeginChild("##controls", ImVec2(kPanelWidth, fullHeight), true);
    ImGui::TextUnformatted("Implicit Volumetric Terrains");
    ImGui::TextDisabled("Paris 2019 TOG reference port");
    ImGui::Separator();

    int sceneIndex = static_cast<int>(g_settings.scene);
    if (sceneCombo(sceneIndex)) {
        g_rebuildRequested = true;
    }

    if (ImGui::SliderInt("MC resolution", &g_settings.mcResolution, 24, 200)) {
        g_rebuildRequested = true;
    }
    if (ImGui::InputInt("Seed", &g_settings.seed)) {
        g_rebuildRequested = true;
    }

    ImGui::Separator();
    if (ImGui::Button("Regenerate", ImVec2(-1.0f, 0.0f))) {
        g_rebuildRequested = true;
    }
    if (ImGui::Button("Reset camera", ImVec2(-1.0f, 0.0f))) {
        g_renderer.resetView();
        g_resetViewForModel = true;
    }

    ImGui::Separator();
    if (building) {
        ImGui::TextColored(ImVec4(0.86f, 0.66f, 0.35f, 1.0f), "Building... %.1fs", buildElapsed);
        ImGui::TextDisabled("%s", buildStage);
    } else {
        g_scene.withModel([&](const ivt_view::IvtModel& model) {
            if (model.generationFailed) {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "Build failed");
                ImGui::TextWrapped("%s", model.failureMessage.c_str());
            } else {
                ImGui::Text("triangles: %d", model.triangleCount);
                ImGui::Text("vertices: %d", model.vertexCount);
                ImGui::Text("build time: %.2fs", model.buildSeconds);
            }
        });
    }

    ImGui::Separator();
    ImGui::TextDisabled("Batch export: --batch-export --scene island");
    ImGui::TextDisabled("Smoke test: --smoke-test");
    ImGui::EndChild();

    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("aparis69-implicit-volumetric-terrains-ref: init()");

    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");
    if (!g_state.gfxOk) {
        return;
    }

    simgui_desc_t imguiDesc = {};
    imguiDesc.max_vertices = 8u * 1024u * 1024u;
    imguiDesc.logger.func = slog_func;
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;

    g_settings.mcResolution = ivt_view::IvtScene::defaultMcResolution(g_settings.scene);
    g_renderer.initGpu();
}

void frame() {
    const std::uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
    g_state.lastTime = now;
    g_state.frameIndex++;

    if (g_rebuildRequested) {
        g_rebuildRequested = false;
        g_scene.requestAsyncRebuild(g_settings);
    }

    if (g_resetViewForModel) {
        g_resetViewForModel = false;
        g_scene.withModel([&](const ivt_view::IvtModel& model) {
            g_renderer.resetViewForModel(model);
        });
    }

    static std::uint64_t s_lastModelRevision = 0;
    const std::uint64_t modelRevision = g_scene.modelRevision();
    if (modelRevision != s_lastModelRevision && !g_scene.isBuilding()) {
        s_lastModelRevision = modelRevision;
        g_scene.withModel([&](const ivt_view::IvtModel& model) {
            if (model.triangleCount > 0 && !model.generationFailed) {
                g_renderer.resetViewForModel(model);
                spdlog::info("IVT viewer: auto-framed scene (tri={})", model.triangleCount);
            }
        });
    }

    if (!g_state.gfxOk) {
        return;
    }

    const int w = sapp_width();
    const int h = sapp_height();

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
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (g_state.imguiOk) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("aparis69-implicit-volumetric-terrains-ref: cleanup()");
    g_renderer.shutdownGpu();
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
}

bool wantsBatchMode(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--batch-export") == 0
            || std::strcmp(argv[i], "--smoke-test") == 0
            || std::strcmp(argv[i], "--help") == 0
            || std::strcmp(argv[i], "-h") == 0) {
            return true;
        }
        if (std::strcmp(argv[i], "--scene") == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    if (wantsBatchMode(argc, argv)) {
        return runIvtBatchExport(argc, argv);
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "aparis69-implicit-volumetric-terrains-ref";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
