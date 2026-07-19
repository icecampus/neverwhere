#include <cstdint>
#include <filesystem>
#include <cstdlib>
#include <unordered_set>

#include <spdlog/spdlog.h>

// Dear ImGui
#include <imgui.h>

// Runtime
#include <game_runtime/lib.h>

// Runtime data & rendering
#include <game_data/assets.h>
#include <game_data/map.h>
#include <game_data/types.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include <render_core/world_renderer.h>
#include <render_core/world_frame_builder.h>

// Sokol (implementation)
#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

// We rely on render_core/sokol_config.h for backend selection in other TUs.
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

// Global state for Sokol callbacks
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

    // Overlay toggles (editor parity)
    bool showGrid = true;
    bool showCursor = true;
};

static AppState g_state;

// Game Runtime
static std::unique_ptr<game_runtime::Runtime> g_runtime;

// Rendering
static topology_core::DiamondIsometry g_iso;
static topology_core::Camera2D g_camera;
static render_core::WorldRenderer g_worldRenderer;
static render_core::WorldFrame g_frame;
static game_data::AssetIndex g_assetIndex;

static bool looksLikeDataRoot(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const bool hasResources = fs::exists(dir / "resources", ec);
    const bool hasAssets = fs::exists(dir / "resources" / "assets", ec);
    const bool hasChapters = fs::exists(dir / "resources" / "chapters", ec);
    return hasResources && hasAssets && hasChapters;
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

#if defined(_WIN32)
#include <Windows.h>
static std::filesystem::path getExecutableDir() {
    wchar_t buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    return std::filesystem::path(buf).parent_path();
}
#endif

// Pure data + GPU upload live in render_core/world_frame_builder (shared with
// the editor's play-test tab).
static void rebuildWorld() {
    if (!g_runtime || !g_runtime->currentSession()) return;
    const game_data::Map* map = g_runtime->currentSession()->world().map();
    if (!map) return;

    render_core::collectWorldFrame(*map, g_frame);
    render_core::ensureWorldAssets(g_assetIndex, g_frame, g_worldRenderer);

    spdlog::info("World collected: {} landscape tiles, {} sprites",
        g_frame.landscapeTiles.size(), g_frame.sprites.size());
}

static void init(void) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("EpicGameClient: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();

    spdlog::info("EpicGameClient: sg_setup() {}", g_state.gfx_ok ? "OK" : "FAILED");

    if (g_state.gfx_ok && g_runtime && g_runtime->currentSession()) {
        try {
            g_worldRenderer.init();
            rebuildWorld();
        } catch (const std::exception& e) {
            spdlog::error("Init renderer failed: {}", e.what());
        }

        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
        spdlog::info("EpicGameClient: simgui_setup() OK");
    }
}

static void frame(void) {
    const uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.last_time));
    g_state.last_time = now;
    g_state.frame_index++;

    // Обновляем Runtime
    if (g_runtime && g_runtime->currentSession()) {
        g_runtime->update(g_state.dt);
    }

    if (!g_state.gfx_ok) {
        g_state.gfx_ok = sg_isvalid();
        if (!g_state.gfx_ok) {
            return;
        }
    }

    const int w = sapp_width();
    const int h = sapp_height();

    const glm::vec2 screenPos(g_state.mouseX, g_state.mouseY);
    const glm::vec2 worldPos = g_camera.screenToWorld(screenPos);
    const glm::ivec2 hoveredCell = g_iso.fieldToMap(worldPos);

    g_frame.showGrid = g_state.showGrid;
    g_frame.cursorCell = g_state.showCursor ? std::optional<glm::ivec2>(hoveredCell) : std::nullopt;

    if (g_state.imgui_ok) {
        simgui_frame_desc_t fd = {};
        fd.width = w;
        fd.height = h;
        fd.delta_time = g_state.dt;
        fd.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&fd);

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(420.0f, 210.0f), ImGuiCond_Once);

        ImGui::Begin("EpicGameClient (debug)");
        ImGui::Text("Frame: %d", g_state.frame_index);
        ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
        ImGui::Text("Size: %dx%d  DPI: %.2f", w, h, sapp_dpi_scale());
        ImGui::Text("Camera offset: (%.1f, %.1f)", g_camera.offset.x, g_camera.offset.y);
        ImGui::Text("Camera zoom: %.3f", g_camera.zoom);
        ImGui::Text("Hovered cell: (%d, %d)", hoveredCell.x, hoveredCell.y);
        ImGui::Checkbox("Show grid", &g_state.showGrid);
        ImGui::SameLine();
        ImGui::Checkbox("Show cursor", &g_state.showCursor);
        ImGui::Separator();
        ImGui::Text("Landscape tiles: %d", (int)g_frame.landscapeTiles.size());
        ImGui::Text("Sprites: %d", (int)g_frame.sprites.size());
        if (g_runtime && g_runtime->currentSession()) {
            auto* session = g_runtime->currentSession();
            ImGui::Text("Session time: %.1f s", session->sessionTime());
            ImGui::Text("World day: %d %02d:%02d",
                session->world().getDay(),
                session->world().getHour(),
                session->world().getMinute());
        }
        ImGui::End();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = { 0.07f, 0.08f, 0.10f, 1.0f };

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    // World rendering (editor parity: landscape + sprites + overlays)
    g_worldRenderer.render(g_frame, g_iso, g_camera, w, h);
    if (g_state.imgui_ok) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    spdlog::info("EpicGameClient: cleanup()");

    // Cleanup Runtime
    g_runtime.reset();

    if (g_state.imgui_ok) {
        simgui_shutdown();
        g_state.imgui_ok = false;
    }
    g_worldRenderer.shutdown();
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

// Data-only smoke scenario (AGENTS.md): key operations without window/GPU.
static int runSmokeTest(const std::filesystem::path& mapPath, const std::filesystem::path& assetsRoot) {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("EpicGameClient: --smoke run");

    int failures = 0;
    const auto check = [&failures](bool ok, const std::string& name) {
        if (ok) {
            spdlog::info("TEST PASS: {}", name);
        } else {
            spdlog::error("TEST FAIL: {}", name);
            failures++;
        }
    };

    // 1. Asset index loads and contains both slice and image assets.
    game_data::AssetIndex assetIndex;
    try {
        assetIndex = game_data::AssetIndex::load(assetsRoot);
    } catch (const std::exception& e) {
        spdlog::error("TEST FAIL: asset index load threw ({})", e.what());
        return 1;
    }
    check(!assetIndex.byUuid.empty(), "asset index is not empty");

    std::size_t sliceCount = 0;
    std::size_t imageCount = 0;
    for (const auto& [_, entry] : assetIndex.byUuid) {
        if (entry.isSlice()) sliceCount++;
        if (entry.isImage()) imageCount++;
    }
    check(sliceCount > 0, "asset index has slice (atlas) assets");
    check(imageCount > 0, "asset index has image (Tile2D) assets");

    // 2. Map loads and produces render lists.
    game_data::Map map;
    try {
        map = game_data::Map::load(mapPath);
    } catch (const std::exception& e) {
        spdlog::error("TEST FAIL: map load threw ({})", e.what());
        return 1;
    }

    render_core::WorldFrame frame;
    render_core::collectWorldFrame(map, frame);
    check(!frame.landscapeTiles.empty(), "map has landscape tiles");
    check(!frame.sprites.empty(), "map has Tile2D sprites");

    // 3. Every referenced asset resolves in the index.
    std::size_t missing = 0;
    for (const auto& t : frame.landscapeTiles) {
        if (!assetIndex.find(t.assetUuid)) missing++;
    }
    for (const auto& s : frame.sprites) {
        if (!assetIndex.find(s.assetUuid)) {
            missing++;
            spdlog::warn("Missing sprite asset assetUuid={}", s.assetUuid);
        }
    }
    check(missing == 0, "all map assetUuids resolve in asset index");

    // 4. Isometry round-trip on a sample of cells.
    topology_core::DiamondIsometry iso;
    bool roundTripOk = true;
    for (const glm::ivec2 cell : {glm::ivec2(0, 0), glm::ivec2(1, 1), glm::ivec2(3, 12), glm::ivec2(-2, 5), glm::ivec2(10, -7), glm::ivec2(-4, -9)}) {
        if (iso.fieldToMap(iso.mapToField(cell)) != cell) {
            spdlog::error("Round-trip mismatch at cell ({}, {})", cell.x, cell.y);
            roundTripOk = false;
        }
    }
    check(roundTripOk, "diamond isometry fieldToMap(mapToField(cell)) == cell");

    // 5. Visible cell bounds for a viewport are sane.
    const topology_core::CellRegion region = iso.visibleCellBounds(glm::vec2(1280.0f, 720.0f), glm::vec2(0.0f, 0.0f));
    check(region.min.x <= region.max.x && region.min.y <= region.max.y, "visibleCellBounds is non-empty");

    spdlog::info(failures == 0 ? "TEST PASS: smoke scenario finished OK" : "TEST FAIL: {} check(s) failed", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    // Parse CLI arguments
    std::filesystem::path mapPath = "resources/chapters/Base/maps/map.json";
    std::filesystem::path assetsRoot = "resources/assets";
    std::filesystem::path dataRoot;
    bool smoke = false;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if ((arg == "--map") && (i + 1 < argc)) {
            mapPath = argv[++i];
        } else if ((arg == "--assets-root") && (i + 1 < argc)) {
            assetsRoot = argv[++i];
        } else if ((arg == "--data-root") && (i + 1 < argc)) {
            dataRoot = argv[++i];
        } else if (arg == "--smoke") {
            smoke = true;
        }
    }

    // Resolve data root
    if (dataRoot.empty()) {
        if (const char* env = std::getenv("NW_DATA_ROOT"); env && *env) {
            dataRoot = env;
        }
    }
    if (dataRoot.empty()) {
        dataRoot = findDataRootUpwards(std::filesystem::current_path());
    }
#if defined(_WIN32)
    if (dataRoot.empty()) {
        const auto exeDir = getExecutableDir();
        if (!exeDir.empty()) {
            dataRoot = findDataRootUpwards(exeDir);
        }
    }
#endif

    if (!dataRoot.empty()) {
        if (mapPath.is_relative()) {
            mapPath = dataRoot / mapPath;
        }
        if (assetsRoot.is_relative()) {
            assetsRoot = dataRoot / assetsRoot;
        }
    }

    spdlog::set_level(spdlog::level::info);
    spdlog::info("EpicGameClient: dataRoot={}", dataRoot.empty() ? "<none>" : dataRoot.string());

    if (smoke) {
        return runSmokeTest(mapPath, assetsRoot);
    }

    // Load assets index
    try {
        g_assetIndex = game_data::AssetIndex::load(assetsRoot);
        spdlog::info("Loaded {} assets", g_assetIndex.byUuid.size());
    } catch (const std::exception& e) {
        spdlog::error("Failed to load asset index: {}", e.what());
    }

    // Create and initialize Runtime
    game_runtime::RuntimeConfig config;
    config.windowTitle = "EpicGameClient";
    config.defaultMap = mapPath;
    config.assetsRoot = assetsRoot;
    config.dataRoot = dataRoot;
    config.enableEditorExtensions = false; // Standalone player mode

    g_runtime = std::make_unique<game_runtime::Runtime>(config);

    if (!g_runtime->initialize()) {
        spdlog::error("Failed to initialize Runtime");
        return 1;
    }

    // Create game session from fixture
    auto fixture = game_runtime::Fixture::create()
        .withName("Default Session")
        .withMap(mapPath.string())
        .newGame()
        .build();

    auto* session = g_runtime->createSession(fixture);

    if (!session) {
        spdlog::error("Failed to create game session");
        return 1;
    }

    spdlog::info("Game session created successfully");

    // Run Sokol app
    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "EpicGameClient";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
