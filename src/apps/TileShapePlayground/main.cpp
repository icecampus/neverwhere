#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <highground_core/highground.h>
#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "AtlasRenderer.h"
#include "FlatAtlasGenerator.h"
#include "LandBrush.h"
#include "PlaygroundSmokeTest.h"

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
    uint64_t last_time = 0;
    float dt = 1.0f / 60.0f;
    int frame_index = 0;
    bool gfx_ok = false;
    bool imgui_ok = false;

    bool dragging = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float camStartX = 0.0f;
    float camStartY = 0.0f;

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool painting = false;
    bool eraseMode = false;
};

AppState g_state;
topology_core::DiamondIsometry g_iso;
topology_core::Camera2D g_camera;
AtlasRenderer g_renderer;
std::filesystem::path g_dataRoot;
std::optional<glm::ivec2> g_hoverNode;
bool g_ctrlDown = false;

// Brush palette: three independent layers sharing one canvas. Each keeps its
// own node grid and presentation (2D grass / 2D yellow / raised 3D).
struct PaintLayer {
    LandBrush brush;
    AtlasKind atlas;
    bool raised;
    const char* name;
};

PaintLayer g_layers[3] = {
    {{}, AtlasKind::Grass, false, "Grass 2D"},
    {{}, AtlasKind::Flat, false, "Yellow 2D"},
    {{}, AtlasKind::Flat, true, "Raised 3D"},
};
int g_activeLayer = 0;
// Raised (3D) generation params for highground_core::generate — height, wall
// style and noise shaping all live here.
highground::Params g_raisedParams;

LandBrush& activeBrush() {
    return g_layers[g_activeLayer].brush;
}

bool looksLikeDataRoot(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "resources" / "assets" / "landscape" / "Grass" / "atlas.png", ec);
}

std::filesystem::path findDataRootUpwards(std::filesystem::path startDir) {
    std::error_code ec;
    startDir = std::filesystem::weakly_canonical(startDir, ec);
    if (startDir.empty()) {
        startDir = std::filesystem::current_path(ec);
    }

    std::filesystem::path dir = startDir;
    for (int i = 0; i < 16; ++i) {
        if (looksLikeDataRoot(dir)) {
            return dir;
        }
        if (!dir.has_parent_path()) {
            break;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

void centerCamera(int viewW, int viewH) {
    const glm::ivec2 mid{g_layers[0].brush.width() / 2, g_layers[0].brush.height() / 2};
    const glm::vec2 world = g_iso.mapToField(mid);
    g_camera.zoom = 1.0f;
    g_camera.offset.x = static_cast<float>(viewW) * 0.5f - world.x * g_camera.zoom;
    g_camera.offset.y = static_cast<float>(viewH) * 0.5f - world.y * g_camera.zoom;
}

void applyBrushAtMouse() {
    if (!g_hoverNode) {
        return;
    }
    const bool on = !g_ctrlDown && !g_state.eraseMode;
    activeBrush().setNode(*g_hoverNode, on);
}

bool g_demoPattern = false;
bool g_demoNode = false;

// --- Visual debug CLI (headless screenshot comparisons vs the editor) ---
// --no-ui                      hide the ImGui panel (clean captures)
// --nodes=x,y;x,y;...          paint these nodes on the raised layer
// --raised-atlas=grass|flat    atlas for the raised layer (default flat)
// --zoom=Z                     camera zoom (default: centerCamera's 1.0)
// --center=cx,cy               camera center in cell coords
//                              (default: bbox center of --nodes)
bool g_noUi = false;
std::vector<glm::ivec2> g_cliNodes;
bool g_cliRaisedGrassAtlas = false;
std::optional<float> g_cliZoom;
std::optional<glm::vec2> g_cliCenter;

std::vector<glm::ivec2> parseNodesArg(const std::string& value) {
    std::vector<glm::ivec2> out;
    std::string token;
    for (size_t i = 0; i <= value.size(); ++i) {
        const char c = i < value.size() ? value[i] : ';';
        if (c == ';') {
            const size_t comma = token.find(',');
            if (comma != std::string::npos) {
                out.emplace_back(
                    std::atoi(token.substr(0, comma).c_str()),
                    std::atoi(token.substr(comma + 1).c_str()));
            }
            token.clear();
        } else {
            token += c;
        }
    }
    return out;
}

std::optional<glm::vec2> parseVec2Arg(const std::string& value) {
    const size_t comma = value.find(',');
    if (comma == std::string::npos) {
        return std::nullopt;
    }
    return glm::vec2(
        std::atof(value.substr(0, comma).c_str()),
        std::atof(value.substr(comma + 1).c_str()));
}

// Painted programmatically in --demo mode: one raised blob, one lone raised
// node, plus grass/yellow flat strokes — used for visual verification.
void paintDemoPattern() {
    for (int y = 9; y <= 12; ++y) {
        for (int x = 9; x <= 12; ++x) {
            g_layers[2].brush.setNode({x, y}, true);
        }
    }
    g_layers[2].brush.setNode({16, 10}, true);
    for (int y = 15; y <= 18; ++y) {
        for (int x = 6; x <= 9; ++x) {
            g_layers[0].brush.setNode({x, y}, true);
        }
    }
    for (int x = 13; x <= 16; ++x) {
        g_layers[1].brush.setNode({x, 16}, true);
    }
}

void updateHover() {
    const glm::vec2 world = g_camera.screenToWorld({g_state.mouseX, g_state.mouseY});
    const glm::ivec2 node = g_iso.fieldToNode(world);
    if (g_layers[0].brush.isNodeEditable(node)) {
        g_hoverNode = node;
    } else {
        g_hoverNode.reset();
    }
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("TileShapePlayground: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("TileShapePlayground: sg_setup FAILED");
        return;
    }

    if (!g_noUi) {
        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
    }

    g_renderer.init();
    for (PaintLayer& layer : g_layers) {
        layer.brush.reset(24, 24);
    }
    if (g_demoPattern) {
        paintDemoPattern();
    }
    if (g_demoNode) {
        g_layers[0].brush.setNode({10, 10}, true);
    }
    for (const glm::ivec2& node : g_cliNodes) {
        g_layers[2].brush.setNode(node, true);
    }
    if (g_cliRaisedGrassAtlas) {
        g_layers[2].atlas = AtlasKind::Grass;
    }

    g_dataRoot = findDataRootUpwards(std::filesystem::current_path());
    if (g_dataRoot.empty()) {
        spdlog::warn("TileShapePlayground: data root not found, using CWD");
        g_dataRoot = std::filesystem::current_path();
    }
    spdlog::info("dataRoot={}", g_dataRoot.string());

    const auto atlasPath = g_dataRoot / "resources" / "assets" / "landscape" / "Grass" / "atlas.png";
    if (!g_renderer.loadAtlasFromFile(AtlasKind::Grass, atlasPath.string(), 4, 6)) {
        spdlog::error("TileShapePlayground: Grass atlas missing at {}", atlasPath.string());
    }

    const FlatAtlasImage flat = generateFlatAtlas();
    if (!g_renderer.loadAtlasFromRgba(
            AtlasKind::Flat,
            flat.rgba.data(),
            flat.width,
            flat.height,
            flat.cols,
            flat.rows)) {
        spdlog::error("TileShapePlayground: failed to upload generated flat atlas");
    }

    // Tiled ground textures for the raised top (world-UV repeat): real grass
    // for the Grass kind, a generated solid tile for the Flat kind.
    const auto grassTopPath =
        g_dataRoot / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png";
    if (!g_renderer.loadTopTextureFromFile(AtlasKind::Grass, grassTopPath.string())) {
        spdlog::warn("TileShapePlayground: raised-top grass texture missing at {}", grassTopPath.string());
    }
    {
        // Solid ochre 2x2 (matches the FlatAtlasGenerator fill color).
        const std::uint8_t solid[] = {
            210, 170, 90, 255, 210, 170, 90, 255,
            210, 170, 90, 255, 210, 170, 90, 255,
        };
        if (!g_renderer.loadTopTextureFromRgba(AtlasKind::Flat, solid, 2, 2)) {
            spdlog::error("TileShapePlayground: failed to upload solid raised-top texture");
        }
    }

    centerCamera(sapp_width(), sapp_height());
    if (g_demoNode) {
        const glm::vec2 nodePos = g_iso.nodeToField({10, 10});
        g_camera.zoom = 2.0f;
        g_camera.offset.x = static_cast<float>(sapp_width()) * 0.5f - nodePos.x * g_camera.zoom;
        g_camera.offset.y = static_cast<float>(sapp_height()) * 0.5f - nodePos.y * g_camera.zoom;
    }
    if (g_cliZoom || g_cliCenter || !g_cliNodes.empty()) {
        // Deterministic framing for screenshot comparisons.
        glm::vec2 worldCenter;
        if (g_cliCenter) {
            worldCenter = g_iso.mapToField(glm::ivec2(*g_cliCenter));
        } else {
            glm::vec2 acc(0.0f);
            for (const glm::ivec2& node : g_cliNodes) {
                acc += g_iso.nodeToField(node);
            }
            worldCenter = acc / static_cast<float>(g_cliNodes.size());
        }
        g_camera.zoom = g_cliZoom.value_or(1.0f);
        g_camera.offset.x = static_cast<float>(sapp_width()) * 0.5f - worldCenter.x * g_camera.zoom;
        g_camera.offset.y = static_cast<float>(sapp_height()) * 0.5f - worldCenter.y * g_camera.zoom;
    }
}

void drawImGui(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 260.0f), ImGuiCond_Once);

    const glm::vec2 world = g_camera.screenToWorld({g_state.mouseX, g_state.mouseY});
    const glm::ivec2 cell = g_iso.fieldToMap(world);

    ImGui::Begin("TileShapePlayground");
    ImGui::Text("Vertex land brush");
    ImGui::Text("LMB paint  |  Ctrl+LMB erase  |  RMB pan  |  wheel zoom");
    ImGui::Separator();

    ImGui::Text("Brush palette:");
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::RadioButton(g_layers[i].name, &g_activeLayer, i);
    }
    if (g_layers[g_activeLayer].raised) {
        ImGui::SliderFloat("Raised height", &g_raisedParams.height, 4.0f, 128.0f, "%.0f px");
        ImGui::Checkbox("Rock walls", &g_raisedParams.rockWalls);
        if (g_raisedParams.rockWalls) {
            ImGui::SliderFloat("Rock amplitude", &g_raisedParams.amplitude, 0.0f, 0.6f, "%.2f");
            ImGui::SliderFloat("Corner bevel", &g_raisedParams.bevel, 0.0f, 0.45f, "%.2f");
        }
    }
    ImGui::Separator();
    ImGui::Text("Frame: %d  dt: %.2f ms", g_state.frame_index, 1000.0f * g_state.dt);
    ImGui::Text("View: %dx%d", w, h);
    ImGui::Text("Camera: (%.0f, %.0f) zoom %.2f", g_camera.offset.x, g_camera.offset.y, g_camera.zoom);
    ImGui::Text("Hover cell: (%d, %d)", cell.x, cell.y);
    if (g_hoverNode) {
        ImGui::Text("Hover node: (%d, %d) %s",
            g_hoverNode->x,
            g_hoverNode->y,
            activeBrush().nodeIsOn(*g_hoverNode) ? "ON" : "off");
        const auto type = activeBrush().cellTypeAt(cell);
        ImGui::Text("Cell type: %s (atlas %d)",
            landscape_core::tileTypeName(type).data(),
            LandBrush::atlasIndexByType(type));
    } else {
        ImGui::Text("Hover node: (out of bounds)");
    }
    ImGui::Text("On nodes: %s %d, %s %d, %s %d",
        g_layers[0].name,
        g_layers[0].brush.onNodeCount(),
        g_layers[1].name,
        g_layers[1].brush.onNodeCount(),
        g_layers[2].name,
        g_layers[2].brush.onNodeCount());
    ImGui::Checkbox("Erase mode", &g_state.eraseMode);
    if (ImGui::Button("Clear layer")) {
        activeBrush().clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) {
        centerCamera(w, h);
    }
    ImGui::End();
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.last_time)));
    g_state.last_time = now;
    g_state.frame_index++;

    if (!g_state.gfx_ok) {
        return;
    }

    const int w = sapp_width();
    const int h = sapp_height();
    updateHover();

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
    action.colors[0].clear_value = {0.12f, 0.14f, 0.16f, 1.0f};

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    PaintLayerView views[3];
    for (int i = 0; i < 3; ++i) {
        views[i] = {&g_layers[i].brush, g_layers[i].atlas, g_layers[i].raised};
    }
    g_renderer.render(
        views,
        3,
        g_iso,
        g_camera,
        w,
        h,
        g_hoverNode.value_or(glm::ivec2{-1, -1}),
        g_hoverNode.has_value(),
        g_layers[g_activeLayer].raised,
        &g_raisedParams);

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("TileShapePlayground: cleanup()");
    g_renderer.shutdown();
    if (g_state.imgui_ok) {
        simgui_shutdown();
        g_state.imgui_ok = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* ev) {
    if (g_state.imgui_ok) {
        simgui_handle_event(ev);
    }

    g_state.mouseX = ev->mouse_x;
    g_state.mouseY = ev->mouse_y;

    if (ev->type == SAPP_EVENTTYPE_KEY_DOWN || ev->type == SAPP_EVENTTYPE_KEY_UP) {
        if (ev->key_code == SAPP_KEYCODE_LEFT_CONTROL || ev->key_code == SAPP_KEYCODE_RIGHT_CONTROL) {
            g_ctrlDown = (ev->type == SAPP_EVENTTYPE_KEY_DOWN);
        }
    }

    if (g_state.imgui_ok) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse &&
            (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN || ev->type == SAPP_EVENTTYPE_MOUSE_MOVE ||
                ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL)) {
            return;
        }
    }

    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g_state.painting = true;
            updateHover();
            applyBrushAtMouse();
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = true;
            g_state.dragStartX = ev->mouse_x;
            g_state.dragStartY = ev->mouse_y;
            g_state.camStartX = g_camera.offset.x;
            g_state.camStartY = g_camera.offset.y;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g_state.painting = false;
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        updateHover();
        if (g_state.painting) {
            applyBrushAtMouse();
        }
        if (g_state.dragging) {
            g_camera.offset.x = g_state.camStartX + (ev->mouse_x - g_state.dragStartX);
            g_camera.offset.y = g_state.camStartY + (ev->mouse_y - g_state.dragStartY);
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL: {
        const float delta = ev->scroll_y;
        if (delta == 0.0f) {
            break;
        }
        const float zoomFactor = (delta > 0.0f) ? 1.1f : 0.9f;
        const float oldZoom = g_camera.zoom;
        float newZoom = std::clamp(oldZoom * zoomFactor, 0.15f, 3.0f);
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

} // namespace

int main(int argc, char* argv[]) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--smoke") {
            smoke = true;
        }
        if (arg == "--demo") {
            g_demoPattern = true;
        }
        if (arg == "--demo-node") {
            g_demoNode = true;
        }
        if (arg == "--no-ui") {
            g_noUi = true;
        }
        if (arg.rfind("--nodes=", 0) == 0) {
            g_cliNodes = parseNodesArg(arg.substr(8));
        }
        if (arg == "--raised-atlas=grass") {
            g_cliRaisedGrassAtlas = true;
        }
        if (arg.rfind("--zoom=", 0) == 0) {
            g_cliZoom = static_cast<float>(std::atof(arg.substr(7).c_str()));
        }
        if (arg.rfind("--center=", 0) == 0) {
            g_cliCenter = parseVec2Arg(arg.substr(9));
        }
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        const bool ok = runTileShapeSmokeTest();
        return ok ? 0 : 1;
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "TileShapePlayground - Vertex Land Tiles";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
