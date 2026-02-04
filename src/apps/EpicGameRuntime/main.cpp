#include <cstdint>
#include <filesystem>
#include <unordered_set>

#include <spdlog/spdlog.h>

// Dear ImGui
#include <imgui.h>

// Runtime data & rendering
#include <game_data/assets.h>
#include <game_data/map.h>
#include <game_data/types.h>

#include <topology_core/camera2d.h>
#include <topology_core/staggered_isometry.h>

#include <render_core/landscape_renderer.h>

// Sokol (implementation)
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

// We rely on render_core/sokol_config.h for backend selection in other TUs.
#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE33)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE33
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

struct AppConfig {
    std::filesystem::path mapPath = "resources/chapters/Base/maps/map.json";
    std::filesystem::path assetsRoot = "resources/assets";
};

static AppConfig g_cfg;

static topology_core::StaggeredIsometry g_iso;
static topology_core::Camera2D g_camera;
static render_core::LandscapeRenderer g_land;
static std::vector<render_core::LandscapeTile> g_tiles;

static void init(void) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("EpicGameRuntime: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.context = sapp_sgcontext();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();

    spdlog::info("EpicGameRuntime: sg_setup() {}", g_state.gfx_ok ? "OK" : "FAILED");

    if (g_state.gfx_ok) {
        // Load map + assets (No-Qt)
        try {
            const game_data::Map map = game_data::Map::load(g_cfg.mapPath);
            const game_data::AssetIndex assetIndex = game_data::AssetIndex::load(g_cfg.assetsRoot);

            g_tiles.clear();
            for (const auto& obj : map.layer(game_data::LayerType::BaseLandscape)) {
                if (obj.type != game_data::GameObjectType::Landscape) continue;
                if (!obj.landscapeData) continue;

                render_core::LandscapeTile t;
                t.cell = obj.position;
                t.assetUuid = obj.assetUuid;
                t.tileIndex = obj.landscapeData->tileIndex;
                g_tiles.push_back(std::move(t));
            }

            // Setup renderer + upload atlases we need
            g_land.init();
            std::unordered_set<std::string> uniqueAtlases;
            uniqueAtlases.reserve(8);
            for (const auto& t : g_tiles) {
                uniqueAtlases.insert(t.assetUuid);
            }

            for (const auto& uuid : uniqueAtlases) {
                const game_data::AssetIndexEntry* entry = assetIndex.find(uuid);
                if (!entry) {
                    spdlog::warn("Atlas not found for assetUuid={}", uuid);
                    continue;
                }
                g_land.ensureAtlas(uuid, entry->atlasPath, entry->cols, entry->rows);
                spdlog::info("Loaded atlas uuid={} path={}", uuid, entry->atlasPath.string());
            }

            spdlog::info("Loaded tiles: {}", g_tiles.size());
        } catch (const std::exception& e) {
            spdlog::error("Init data failed: {}", e.what());
        }

        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
        spdlog::info("EpicGameRuntime: simgui_setup() OK");
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
        ImGui::SetNextWindowSize(ImVec2(420.0f, 170.0f), ImGuiCond_Once);
        const glm::vec2 screenPos(g_state.mouseX, g_state.mouseY);
        const glm::vec2 worldPos = g_camera.screenToWorld(screenPos);
        const glm::ivec2 hoveredCell = g_iso.fieldToMap(worldPos);

        ImGui::Begin("EpicGameRuntime (debug)");
        ImGui::Text("Frame: %d", g_state.frame_index);
        ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
        ImGui::Text("Size: %dx%d  DPI: %.2f", w, h, sapp_dpi_scale());
        ImGui::Text("Camera offset: (%.1f, %.1f)", g_camera.offset.x, g_camera.offset.y);
        ImGui::Text("Camera zoom: %.3f", g_camera.zoom);
        ImGui::Text("Hovered cell: (%d, %d)", hoveredCell.x, hoveredCell.y);
        ImGui::Separator();
        ImGui::Text("Tiles (BaseLandscape): %d", (int)g_tiles.size());
        ImGui::End();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = { 0.07f, 0.08f, 0.10f, 1.0f };

    sg_begin_default_pass(&action, w, h);
    // World rendering (MVP: landscape)
    if (!g_tiles.empty()) {
        g_land.render(g_tiles, g_iso, g_camera, w, h);
    }
    if (g_state.imgui_ok) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    spdlog::info("EpicGameRuntime: cleanup()");
    if (g_state.imgui_ok) {
        simgui_shutdown();
        g_state.imgui_ok = false;
    }
    g_land.shutdown();
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

        // Keep world point under cursor stable (same formula as MapMouseArea.qml)
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
    // Very small CLI:
    //   --map <path>
    //   --assets-root <path>
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if ((arg == "--map") && (i + 1 < argc)) {
            g_cfg.mapPath = argv[++i];
        } else if ((arg == "--assets-root") && (i + 1 < argc)) {
            g_cfg.assetsRoot = argv[++i];
        }
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "EpicGameRuntime";
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

