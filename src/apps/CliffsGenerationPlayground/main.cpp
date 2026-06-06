#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "RockFractureRenderer.h"
#include "RockFractureScene.h"
#include "RenderTypes.h"

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
render_playground::RockFractureSettings g_settings;
render_playground::RockFractureScene g_scene;
render_playground::RockFractureRenderer g_renderer;

bool g_rebuildRequested = true;
bool g_showControls = true;
bool g_showDebugView = true;
bool g_showMeshView = true;
float g_debugViewHeightRatio = 0.32f;

constexpr float kRightPanelGutter = 12.0f;
constexpr float kDebugMeshSplitterHeight = 6.0f;
constexpr float kMinDebugViewHeight = 80.0f;
constexpr float kMinMeshViewHeight = 120.0f;

float clampDebugViewHeight(float debugHeight, float fullHeight) {
    const float maxDebugHeight = std::max(
        kMinDebugViewHeight,
        fullHeight - kMinMeshViewHeight - kDebugMeshSplitterHeight - kRightPanelGutter);
    return std::clamp(debugHeight, kMinDebugViewHeight, maxDebugHeight);
}

bool drawVerticalSplitter(const char* id, float x, float y, float width, float fullHeight, float& debugHeight) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::InvisibleButton(id, ImVec2(width, kDebugMeshSplitterHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (active) {
        debugHeight = clampDebugViewHeight(debugHeight + ImGui::GetIO().MouseDelta.y, fullHeight);
        g_debugViewHeightRatio = debugHeight / std::max(1.0f, fullHeight);
    }

    const ImVec2 splitMin{x, y};
    const ImVec2 splitMax{x + width, y + kDebugMeshSplitterHeight};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 lineColor = active ? IM_COL32(120, 170, 230, 255)
        : hovered ? IM_COL32(95, 110, 130, 255)
                  : IM_COL32(58, 66, 78, 255);
    const float midY = y + kDebugMeshSplitterHeight * 0.5f;
    drawList->AddLine({x + 8.0f, midY}, {x + width - 8.0f, midY}, lineColor, active ? 2.0f : 1.2f);
    return active;
}

bool kKindCombo(int& kindIndex) {
    bool changed = false;
    if (ImGui::BeginCombo("Fracture kind", render_playground::RockFractureScene::kindName(static_cast<render_playground::RockFractureKind>(kindIndex)))) {
        const int count = render_playground::RockFractureScene::kindCount();
        for (int i = 0; i < count; i++) {
            const bool selected = i == kindIndex;
            const render_playground::RockFractureKind k = static_cast<render_playground::RockFractureKind>(i);
            if (ImGui::Selectable(render_playground::RockFractureScene::kindName(k), selected)) {
                kindIndex = i;
                g_settings.kind = k;
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

void drawSceneControls() {
    if (!ImGui::CollapsingHeader("Rock Fracture", ImGuiTreeNodeFlags_DefaultOpen)) return;

    int kindIndex = static_cast<int>(g_settings.kind);
    if (kKindCombo(kindIndex)) g_rebuildRequested = true;

    ImGui::Separator();
    if (ImGui::SliderFloat("Tile size",       &g_settings.tileSize,         6.0f, 60.0f)) g_rebuildRequested = true;
    if (ImGui::SliderFloat("Poisson radius",  &g_settings.poissonRadius,   0.1f, 2.0f))  g_rebuildRequested = true;
    if (ImGui::SliderInt  ("Poisson tries",   &g_settings.poissonTries,     100, 50000))  g_rebuildRequested = true;
    if (ImGui::SliderFloat("Fracture inflate",&g_settings.fractureInflate, 1.0f, 8.0f))  g_rebuildRequested = true;
    if (ImGui::InputInt   ("Seed",            &g_settings.seed))                              g_rebuildRequested = true;

    ImGui::Separator();
    if (ImGui::SliderInt("MC resolution", &g_settings.mcResolution, 40, 200)) g_rebuildRequested = true;
    static const double smoothMin = 0.05;
    static const double smoothMax = 1.5;
    static const double bvhMin = 0.1;
    static const double bvhMax = 2.0;
    if (ImGui::SliderScalar("Smoothing radius", ImGuiDataType_Double, &g_settings.blockSmoothingRadius, &smoothMin, &smoothMax)) g_rebuildRequested = true;
    if (ImGui::SliderScalar("BVH transition",   ImGuiDataType_Double, &g_settings.bvhTransitionRadius,   &bvhMin,     &bvhMax))     g_rebuildRequested = true;

    ImGui::Separator();
    if (ImGui::Checkbox("Use OpenMP",           &g_settings.useOpenMP))   g_rebuildRequested = true;
    if (ImGui::Checkbox("Use texture warping",  &g_settings.useTextureWarp)) g_rebuildRequested = true;
    if (ImGui::Checkbox("Show mesh wireframe",  &g_renderer.shading().showWireframe)) {}
    if (ImGui::Checkbox("Show world grid",      &g_renderer.shading().showWorldGrid)) {}
    if (ImGui::Checkbox("Show samples (top)",   &g_renderer.shading().showSamples2d))   {}
    if (ImGui::Checkbox("Show fractures (top)", &g_renderer.shading().showFractures2d)) {}

    if (ImGui::Button("Regenerate")) {
        g_rebuildRequested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        g_renderer.resetView();
    }
}

void drawShadingControls() {
    if (!ImGui::CollapsingHeader("Shading", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& s = g_renderer.shading();
    if (ImGui::SliderFloat3("Light dir",     &s.lightDir.x,       -1.0f, 1.0f)) {}
    if (ImGui::SliderFloat3("Sky color",     &s.skyColor.x,       0.0f,  1.0f)) {}
    if (ImGui::SliderFloat3("Ground color",  &s.groundColor.x,    0.0f,  1.0f)) {}
    if (ImGui::SliderFloat3("Rock tint",     &s.rockTint.x,       0.0f,  1.0f)) {}
    if (ImGui::SliderFloat ("Ambient",       &s.ambientStrength,   0.0f,  1.5f)) {}
    if (ImGui::SliderFloat ("Diffuse",       &s.diffuseStrength,   0.0f,  1.5f)) {}
    if (ImGui::SliderFloat ("Specular",      &s.specularStrength,  0.0f,  1.5f)) {}
    if (ImGui::SliderFloat ("Shininess",     &s.shininess,         1.0f, 128.0f)) {}
    if (ImGui::SliderFloat ("Rim",           &s.rimStrength,       0.0f,  1.5f)) {}
    if (ImGui::SliderFloat ("Rim power",     &s.rimPower,          0.5f,  8.0f)) {}
    if (ImGui::SliderFloat ("Ground shadow", &s.groundShadowStrength, 0.0f, 1.0f)) {}
    if (ImGui::SliderFloat ("Fog",           &s.fogStrength,       0.0f,  1.0f)) {}
}

void drawStats(const render_playground::RockFractureModel& m) {
    if (!ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Text("Frame: %d (%.2f ms)", g_state.frameIndex, 1000.0f * g_state.dt);
    if (g_scene.isBuilding()) {
        const double elapsed = g_scene.buildElapsedSeconds();
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.4f, 1.0f),
            "Building... %.1fs (%s)", elapsed, g_scene.buildStage());
    } else {
        ImGui::Text("Build: %.2f s", m.buildSeconds);
    }
    ImGui::Text("Samples: %d",         m.sampleCount);
    ImGui::Text("Fractures: %d",       m.fractureCount);
    ImGui::Text("Clusters: %d",        m.clusterCount);
    ImGui::Text("Verts / tris: %d / %d", m.vertexCount, m.triangleCount);
    ImGui::Text("Field range: [%.3f, %.3f]", m.fieldMin, m.fieldMax);
    ImGui::Text("Warping: %s",
        m.usedTextureWarp
            ? (m.usedFallbackTexture ? "Perlin fallback" : "rock1.png")
            : "disabled");
    ImGui::Text("OpenMP: %s", m.usedOpenMP ? "yes" : "no");
    if (m.generationFailed) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Generation failed:");
        ImGui::TextWrapped("%s", m.failureMessage.c_str());
    }
}

void drawUi() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Cliffs Generation Playground", nullptr, rootFlags);

    const float gutter = kRightPanelGutter;
    // Lock both the left panel and the right column to the window's content height so the
    // BeginChild calls never auto-grow past the window bottom (which would push the cursor
    // off-screen and clamp the right-side viewport to negative height -> invisible mesh).
    const float fullHeight = ImGui::GetContentRegionAvail().y;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, ImGui::GetContentRegionAvail().x * 0.32f));
    const float rightX = ImGui::GetCursorScreenPos().x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - leftPanelWidth - gutter);

    ImGui::BeginChild("##controlPanel", ImVec2(leftPanelWidth, fullHeight), true);
    drawSceneControls();
    drawShadingControls();
    g_scene.withModel([&](const render_playground::RockFractureModel& model) {
        drawStats(model);
    });
    ImGui::EndChild();

    ImGui::SetCursorScreenPos(ImVec2(rightX, viewport->WorkPos.y));

    const bool building = g_scene.isBuilding();
    const double buildElapsed = g_scene.buildElapsedSeconds();
    const char* buildStage = g_scene.buildStage();

    g_scene.withModel([&](const render_playground::RockFractureModel& model) {
        if (g_showDebugView && g_showMeshView) {
            float debugHeight = clampDebugViewHeight(g_debugViewHeightRatio * fullHeight, fullHeight);
            const ImVec2 debugSize(rightWidth, debugHeight);
            ImGui::BeginChild("##debugView", debugSize, true);
            g_renderer.drawDebugView(model, debugSize, building, buildElapsed, buildStage);
            ImGui::EndChild();

            const float splitterY = viewport->WorkPos.y + debugHeight;
            drawVerticalSplitter("##debugMeshSplitter", rightX, splitterY, rightWidth, fullHeight, debugHeight);

            ImGui::SetCursorScreenPos(ImVec2(rightX, splitterY + kDebugMeshSplitterHeight));
            const float meshHeight = std::max(1.0f, fullHeight - debugHeight - kDebugMeshSplitterHeight);
            const ImVec2 meshSize(rightWidth, meshHeight);
            ImGui::BeginChild("##meshView", meshSize, true);
            g_renderer.drawMeshView(model, meshSize, building, buildElapsed, buildStage);
            ImGui::EndChild();
        } else if (g_showMeshView) {
            const ImVec2 meshSize(rightWidth, fullHeight);
            ImGui::BeginChild("##meshView", meshSize, true);
            g_renderer.drawMeshView(model, meshSize, building, buildElapsed, buildStage);
            ImGui::EndChild();
        } else if (g_showDebugView) {
            const ImVec2 debugSize(rightWidth, fullHeight);
            ImGui::BeginChild("##debugView", debugSize, true);
            g_renderer.drawDebugView(model, debugSize, building, buildElapsed, buildStage);
            ImGui::EndChild();
        }
    });

    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("CliffsGenerationPlayground: init()");

    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("CliffsGenerationPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");

    if (!g_state.gfxOk) return;

    simgui_desc_t imguiDesc = {};
    imguiDesc.max_vertices = 8u * 1024u * 1024u; // 8M verts to fit Rock Fracture MC up to res=200
    imguiDesc.logger.func = slog_func;
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;
    spdlog::info("CliffsGenerationPlayground: simgui_setup() done, max_vertices=8M");
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

    // Periodic status heartbeat (every 60 frames ≈ 1s) for visibility.
    if (g_state.frameIndex % 60 == 0) {
        g_scene.withModel([&](const render_playground::RockFractureModel& m) {
            spdlog::info(
                "frame: idx={} building={} stage={} elapsed={:.1f}s | model: tri={} v={} buildSec={:.2f}",
                g_state.frameIndex, g_scene.isBuilding() ? "yes" : "no",
                g_scene.buildStage(), g_scene.buildElapsedSeconds(),
                m.triangleCount, m.vertexCount, m.buildSeconds);
        });
    }

    if (!g_state.gfxOk) return;

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
    spdlog::info("CliffsGenerationPlayground: cleanup()");
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
    desc.window_title = "Cliffs Generation Playground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
