#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <spdlog/spdlog.h>

// Dear ImGui
#include <imgui.h>

#include <topology_core/camera2d.h>
#include <topology_core/staggered_isometry.h>

#include "MaterialTypes.h"
#include "SplattingRenderer.h"

// Sokol (implementation)
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

// Ensure backend selection (same logic as render_core/sokol_config.h)
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

struct AppState {
    uint64_t last_time = 0;
    float dt = 1.0f / 60.0f;
    int frame_index = 0;
    bool gfx_ok = false;
    bool imgui_ok = false;

    // Camera interaction
    bool dragging = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float camStartX = 0.0f;
    float camStartY = 0.0f;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
};

static AppState g_state;

static topology_core::StaggeredIsometry g_iso;
static topology_core::Camera2D g_camera;
static SplattingRenderer g_renderer;
static MaterialMap g_materialMap;

// All splatting parameters in one struct
static SplattingParams g_params;

static std::filesystem::path g_dataRoot;

static bool looksLikeDataRoot(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(dir / "resources", ec);
}

static std::filesystem::path findDataRootUpwards(std::filesystem::path startDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    startDir = fs::weakly_canonical(startDir, ec);
    if (startDir.empty()) startDir = fs::current_path(ec);
    if (startDir.empty()) return {};

    fs::path dir = startDir;
    for (int i = 0; i < 16; i++) {
        if (looksLikeDataRoot(dir)) return dir;
        if (!dir.has_parent_path()) break;
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

// Build a demo map with two showcase areas:
// Area A (left): Sand cells surrounded by grass (grass around sand)
// Area B (right): Sand cells surrounded by rock (sand in rock ring)
static void buildDemoMap() {
    const int mapW = 20;
    const int mapH = 16;
    
    // Fill entire map with grass as base
    g_materialMap.init(mapW, mapH, (MaterialId)MaterialType::Grass);
    
    // === AREA A (left side): Grass surrounding sand ===
    // Sand patch in the center-left area (rows 4-11, cols 2-7)
    // With grass border around it
    for (int y = 5; y <= 10; y++) {
        for (int x = 3; x <= 6; x++) {
            g_materialMap.set(x, y, (MaterialId)MaterialType::Sand);
        }
    }
    
    // === AREA B (right side): Rock ring surrounding sand ===
    // First fill area with rock (rows 4-11, cols 12-17)
    for (int y = 4; y <= 11; y++) {
        for (int x = 12; x <= 17; x++) {
            g_materialMap.set(x, y, (MaterialId)MaterialType::Rock);
        }
    }
    // Then carve out sand center (rows 6-9, cols 14-15)
    for (int y = 6; y <= 9; y++) {
        for (int x = 14; x <= 15; x++) {
            g_materialMap.set(x, y, (MaterialId)MaterialType::Sand);
        }
    }
    
    spdlog::info("Built demo map {}x{} with two showcase areas", mapW, mapH);
    spdlog::info("  Area A (left): Grass surrounding Sand");
    spdlog::info("  Area B (right): Rock ring surrounding Sand");
}

static void init(void) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("TextureBlendLandscape: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    spdlog::info("TextureBlendLandscape: sg_setup() {}", g_state.gfx_ok ? "OK" : "FAILED");

    if (!g_state.gfx_ok) return;

    simgui_desc_t imgui_desc = {};
    simgui_setup(&imgui_desc);
    g_state.imgui_ok = true;

    g_renderer.init();

    // Locate data root (repo root) so we can resolve resources paths.
    g_dataRoot = findDataRootUpwards(std::filesystem::current_path());
    if (g_dataRoot.empty()) {
        spdlog::warn("Failed to auto-detect data root. Using CWD.");
        g_dataRoot = std::filesystem::current_path();
    }
    spdlog::info("dataRoot={}", g_dataRoot.string());

    // Load test textures
    const auto texDir = g_dataRoot / "src" / "landscape_playgrounds" / "TextureBlendLandscape" / "resources" / "materials";
    g_renderer.loadMaterial(0, (texDir / "grass.png").string());
    g_renderer.loadMaterial(1, (texDir / "sand.png").string());
    g_renderer.loadMaterial(2, (texDir / "rock.png").string());
    g_renderer.loadMaterial(3, (texDir / "rock.png").string());
    g_renderer.loadNoiseTexture((texDir / "noise.png").string());

    // Build the demo map with showcase areas
    buildDemoMap();
    
    // Center camera roughly on the map
    const glm::vec2 cellSize = g_iso.dims.cellSize();
    g_camera.offset.x = 200.0f;
    g_camera.offset.y = 100.0f;
}

static void drawImGui(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 420.0f), ImGuiCond_Once);

    const glm::vec2 screenPos(g_state.mouseX, g_state.mouseY);
    const glm::vec2 worldPos = g_camera.screenToWorld(screenPos);
    const glm::ivec2 hoveredCell = g_iso.fieldToMap(worldPos);

    ImGui::Begin("TextureBlendLandscape");
    ImGui::Text("Frame: %d", g_state.frame_index);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
    ImGui::Text("Size: %dx%d  DPI: %.2f", w, h, sapp_dpi_scale());
    ImGui::Separator();
    
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Demo Areas:");
    ImGui::Text("  Left:  Grass around Sand");
    ImGui::Text("  Right: Rock ring around Sand");
    ImGui::Separator();

    // Debug Mode selection
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Debug Visualization:");
    const char* debugModes[] = { "Normal", "Material ID", "UV Coords", "Blend Weights", "Center Only" };
    ImGui::Combo("Debug Mode", &g_params.debugMode, debugModes, 5);
    if (g_params.debugMode > 0) {
        ImGui::TextWrapped("Hint: Mode %d - %s", g_params.debugMode, 
            g_params.debugMode == 1 ? "Shows material IDs as colors" :
            g_params.debugMode == 2 ? "Shows UV coords (RG gradient)" :
            g_params.debugMode == 3 ? "Shows blend weights (RGB)" :
            "Shows center texture only (no blending)");
    }
    ImGui::Separator();

    // UV Mode selection
    ImGui::Text("UV Mode:");
    int uvModeInt = static_cast<int>(g_params.uvMode);
    ImGui::RadioButton("World UV (continuous)", &uvModeInt, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Random Tile UV", &uvModeInt, 1);
    g_params.uvMode = static_cast<UvMode>(uvModeInt);

    if (g_params.uvMode == UvMode::WorldUV) {
        ImGui::SliderFloat("World UV Scale", &g_params.worldUvScale, 16.0f, 512.0f);
    } else {
        ImGui::SliderFloat("Random UV Strength", &g_params.randomUvStrength, 0.0f, 1.0f);
    }
    ImGui::Separator();

    ImGui::Text("Blending");
    ImGui::SliderFloat("Blend Sharpness", &g_params.blendSharpness, 0.0f, 1.0f);
    ImGui::SliderFloat("Noise Scale", &g_params.noiseScale, 0.1f, 16.0f);
    ImGui::SliderFloat("Tile Scale", &g_params.tileScale, 0.1f, 8.0f);
    ImGui::Separator();

    ImGui::Text("Visual improvements");
    ImGui::SliderFloat("Macro Scale", &g_params.macroScale, 0.01f, 2.0f);
    ImGui::SliderFloat("Macro Strength", &g_params.macroStrength, 0.0f, 0.5f);
    ImGui::SliderFloat("Height Influence", &g_params.heightInfluence, 0.0f, 1.0f);
    ImGui::SliderFloat("Edge Darkness", &g_params.edgeDarkness, 0.0f, 0.8f);
    ImGui::SliderFloat("Edge Width", &g_params.edgeWidth, 0.05f, 1.0f);

    ImGui::Separator();
    ImGui::Text("Camera offset: (%.1f, %.1f)", g_camera.offset.x, g_camera.offset.y);
    ImGui::Text("Camera zoom: %.3f", g_camera.zoom);
    ImGui::Text("Hovered cell: (%d, %d)", hoveredCell.x, hoveredCell.y);
    ImGui::Text("Map: %dx%d", g_materialMap.width, g_materialMap.height);

    if (ImGui::Button("Reset Camera")) {
        g_camera.offset = { 200.0f, 100.0f };
        g_camera.zoom = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rebuild Demo Map")) {
        buildDemoMap();
    }

    ImGui::End();
}

static void frame(void) {
    const uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.last_time));
    g_state.last_time = now;
    g_state.frame_index++;

    if (!g_state.gfx_ok) return;

    const int w = sapp_width();
    const int h = sapp_height();

    if (g_state.imgui_ok) {
        simgui_frame_desc_t fd = {};
        fd.width = w;
        fd.height = h;
        fd.delta_time = g_state.dt;
        fd.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&fd);
        drawImGui(w, h);
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = { 0.07f, 0.08f, 0.10f, 1.0f };

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    g_renderer.render(
        g_materialMap,
        g_iso,
        g_camera,
        w,
        h,
        g_params
    );

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    spdlog::info("TextureBlendLandscape: cleanup()");
    g_renderer.shutdown();
    if (g_state.imgui_ok) {
        simgui_shutdown();
        g_state.imgui_ok = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

static void event(const sapp_event* ev) {
    // Always forward to ImGui first
    if (g_state.imgui_ok) {
        simgui_handle_event(ev);
    }

    g_state.mouseX = ev->mouse_x;
    g_state.mouseY = ev->mouse_y;

    // If UI wants the mouse, don't move the camera.
    if (g_state.imgui_ok) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }

    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = true;
            g_state.dragStartX = ev->mouse_x;
            g_state.dragStartY = ev->mouse_y;
            g_state.camStartX = g_camera.offset.x;
            g_state.camStartY = g_camera.offset.y;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (g_state.dragging) {
            const float dx = ev->mouse_x - g_state.dragStartX;
            const float dy = ev->mouse_y - g_state.dragStartY;
            g_camera.offset.x = g_state.camStartX + dx;
            g_camera.offset.y = g_state.camStartY + dy;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL: {
        const float delta = ev->scroll_y;
        if (delta == 0.0f) break;

        const float zoomFactor = (delta > 0.0f) ? 1.1f : 0.9f;
        const float oldZoom = g_camera.zoom;
        float newZoom = oldZoom * zoomFactor;
        newZoom = std::max(0.1f, std::min(3.0f, newZoom));

        // Keep world point under cursor stable
        const float mapX = (ev->mouse_x - g_camera.offset.x) / oldZoom;
        const float mapY = (ev->mouse_y - g_camera.offset.y) / oldZoom;
        g_camera.offset.x = ev->mouse_x - mapX * newZoom;
        g_camera.offset.y = ev->mouse_y - mapY * newZoom;
        g_camera.zoom = newZoom;
        break;
    }
    default:
        break;
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
    desc.window_title = "TextureBlendLandscape - Isometric Diamond Cells";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
