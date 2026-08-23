#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "GridRenderer.h"
#include "NodeField.h"
#include "PlaygroundScreenshot.h"
#include "PlaygroundSmokeTest.h"
#include "StoneGen.h"

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

// On Windows the GDI capture path owns this implementation
// (PlaygroundScreenshotWin32.cpp); elsewhere the GL readback below does.
#if !defined(_WIN32)
    #define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb_image_write.h>

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
GridRenderer g_grid;
std::optional<glm::ivec2> g_hoverNode;
std::optional<glm::ivec2> g_lastPaintNode;
bool g_ctrlDown = false;

// The paint canvas: a square cell map (24x24 cells -> 25x25 vertex nodes),
// same dimensions the SDFGeneratedLandscape / B-rep brushes use.
constexpr int kMapW = 24;
constexpr int kMapH = 24;
NodeField g_nodes;
StoneGenParams g_params;

std::string g_shotPath;
std::optional<float> g_cliZoom;
std::optional<glm::ivec2> g_cliCenter;
std::vector<glm::ivec2> g_cliNodes;
std::optional<int> g_cliSeed;
bool g_noUi = false;

// --- Units -----------------------------------------------------------------
// sokol_app reports the framebuffer in pixels and mouse positions in those
// same pixels, while ImGui — and with it the panel width — is laid out in
// logical points. This app keeps the whole scene in points too and converts
// exactly once, at the viewport: that way a HiDPI display only buys sharpness
// and does not move the cursor away from what it paints.
float dpiScale() {
    return std::max(sapp_dpi_scale(), 0.01f);
}

glm::vec2 windowSize() {
    return {sapp_widthf() / dpiScale(), sapp_heightf() / dpiScale()};
}

// Control panel: pinned to the left edge, full height. The scene canvas
// starts at the panel's right edge — camera and mouse work in canvas-local
// coordinates (canvas origin = panel width in logical points).
constexpr float kPanelWidth = 340.0f;

float panelWidth() {
    return g_state.imgui_ok ? kPanelWidth : 0.0f;
}

glm::vec2 canvasSize() {
    const glm::vec2 window = windowSize();
    return {std::max(window.x - panelWidth(), 0.0f), window.y};
}

void centerCamera(int viewW, int viewH) {
    const glm::ivec2 mid{kMapW / 2, kMapH / 2};
    const glm::vec2 world = g_iso.mapToField(mid);
    g_camera.zoom = 1.0f;
    g_camera.offset.x = static_cast<float>(viewW) * 0.5f - world.x * g_camera.zoom;
    g_camera.offset.y = static_cast<float>(viewH) * 0.5f - world.y * g_camera.zoom;
}

// Node under the cursor in canvas space, unclamped (may leave the map).
glm::ivec2 nodeAtMouse() {
    const glm::vec2 world =
        g_camera.screenToWorld({g_state.mouseX - panelWidth(), g_state.mouseY});
    return g_iso.fieldToNode(world);
}

void updateHover() {
    const glm::ivec2 node = nodeAtMouse();
    if (g_nodes.inBounds(node)) {
        g_hoverNode = node;
    } else {
        g_hoverNode.reset();
    }
}

void paintAtMouse() {
    if (!g_hoverNode) {
        return;
    }
    const bool on = !g_ctrlDown && !g_state.eraseMode;
    if (g_lastPaintNode && *g_lastPaintNode != *g_hoverNode) {
        // Bresenham between drag events: a fast stroke must not leave holes
        // in the node field (they would read as torn seeds in a later mesh).
        paintNodeLine(g_nodes, *g_lastPaintNode, *g_hoverNode, on);
    } else {
        g_nodes.setNode(*g_hoverNode, on);
    }
    g_lastPaintNode = g_hoverNode;
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("StoneGenerator: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("StoneGenerator: sg_setup FAILED");
        return;
    }

    if (!g_noUi) {
        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
    }

    g_grid.init();
    g_nodes.reset(kMapW + 1, kMapH + 1);

    if (g_cliSeed) {
        g_params.seed = *g_cliSeed;
    }
    for (const glm::ivec2& node : g_cliNodes) {
        g_nodes.setNode(node, true);
    }

    const glm::vec2 canvas = canvasSize();
    centerCamera(static_cast<int>(canvas.x), static_cast<int>(canvas.y));
    if (g_cliZoom || g_cliCenter || !g_cliNodes.empty()) {
        // Deterministic framing for screenshot comparisons.
        glm::vec2 worldCenter;
        if (g_cliCenter) {
            worldCenter = g_iso.mapToField(*g_cliCenter);
        } else if (!g_cliNodes.empty()) {
            glm::vec2 acc(0.0f);
            for (const glm::ivec2& node : g_cliNodes) {
                acc += g_iso.nodeToField(node);
            }
            worldCenter = acc / static_cast<float>(g_cliNodes.size());
        } else {
            worldCenter = g_iso.mapToField({kMapW / 2, kMapH / 2});
        }
        g_camera.zoom = g_cliZoom.value_or(1.0f);
        g_camera.offset.x = canvas.x * 0.5f - worldCenter.x * g_camera.zoom;
        g_camera.offset.y = canvas.y * 0.5f - worldCenter.y * g_camera.zoom;
    }
}

void drawImGui(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(h)), ImGuiCond_Always);

    ImGui::Begin(
        "Stone Generator",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Vertex-node seed brush");
    ImGui::Text("LMB paint  |  Ctrl+LMB erase  |  RMB pan  |  wheel zoom");
    if (g_hoverNode) {
        ImGui::Text("Hover node: (%d, %d)%s",
            g_hoverNode->x, g_hoverNode->y, g_nodes.isOn(*g_hoverNode) ? " on" : "");
    } else {
        ImGui::Text("Hover node: (out of bounds)");
    }
    ImGui::Text("Seed nodes: %d", g_nodes.onNodeCount());
    ImGui::Checkbox("Erase mode", &g_state.eraseMode);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Stone generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Scaffold: generator not wired yet.");
        ImGui::TextWrapped(
            "Painted nodes are the seed silhouette. Generation experiments "
            "go in StoneGen.h — do not fork B-rep or stone_gen from here.");
        ImGui::SliderInt("Seed", &g_params.seed, 0, 9999);
    }

    ImGui::Separator();
    if (ImGui::Button("Clear nodes")) {
        g_nodes.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) {
        centerCamera(w - static_cast<int>(panelWidth()), h);
    }
    ImGui::End();
}

// Portable --shot capture. Win32 grabs the window through GDI; everywhere else
// we read the GL default framebuffer back, which is why this lives in the TU
// that owns SOKOL_IMPL — glad must never be pulled into a translation unit
// that already has sokol's GL headers (documented Linux trap).
bool capturePlaygroundPng(const char* path) {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    const int width = sapp_width();
    const int height = sapp_height();
    if (!path || path[0] == '\0' || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // GL's origin is bottom-left, PNG rows run top-down.
    stbi_flip_vertically_on_write(1);
    const int ok = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    stbi_flip_vertically_on_write(0);
    if (!ok) {
        spdlog::error("capturePlaygroundPng: stbi_write_png failed for {}", path);
        return false;
    }
    spdlog::info("capturePlaygroundPng: saved {}x{} to {}", width, height, path);
    return true;
#else
    return captureWindowClientPng(path);
#endif
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.last_time)));
    g_state.last_time = now;
    g_state.frame_index++;

    if (!g_state.gfx_ok) {
        return;
    }

    const float dpi = dpiScale();
    const glm::vec2 window = windowSize();
    const int w = static_cast<int>(std::lround(window.x));
    const int h = static_cast<int>(std::lround(window.y));
    updateHover();

    if (g_state.imgui_ok) {
        // simgui_new_frame() wants the framebuffer size and does the division
        // by dpi_scale itself; everything downstream of it is in points.
        simgui_frame_desc_t fd = {};
        fd.width = sapp_width();
        fd.height = sapp_height();
        fd.delta_time = g_state.dt;
        fd.dpi_scale = dpi;
        simgui_new_frame(&fd);
        drawImGui(w, h);
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.12f, 0.14f, 0.16f, 1.0f};
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    // The scene renders only into the canvas right of the panel. This is the
    // one place that leaves logical points for framebuffer pixels; sokol_imgui
    // resets viewport and scissor to the full framebuffer for its own draws,
    // so no restore is needed.
    const int canvasW = std::max(w - static_cast<int>(panelWidth()), 0);
    const int vpX = static_cast<int>(std::lround(panelWidth() * dpi));
    const int vpW = static_cast<int>(std::lround(canvasW * dpi));
    const int vpH = static_cast<int>(std::lround(h * dpi));
    sg_apply_viewport(vpX, 0, vpW, vpH, true);
    sg_apply_scissor_rect(vpX, 0, vpW, vpH, true);

    g_grid.render(
        g_iso,
        g_camera,
        canvasW,
        h,
        kMapW,
        kMapH,
        g_hoverNode.value_or(glm::ivec2{-1, -1}),
        g_hoverNode.has_value(),
        g_nodes.nodes.data(),
        g_nodes.width,
        g_nodes.height);

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();

    // Headless capture: wait a 1 s wall-time floor so the first frame is
    // fully presented, then quit.
    if (!g_shotPath.empty() && stm_sec(stm_now()) >= 1.0) {
        if (capturePlaygroundPng(g_shotPath.c_str())) {
            spdlog::info("StoneGenerator: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("StoneGenerator: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    spdlog::info("StoneGenerator: cleanup()");
    g_grid.shutdown();
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

    // sokol_app hands out framebuffer pixels; the rest of the app is in
    // points, so this is where the cursor enters that frame. Nothing below may
    // read ev->mouse_x directly.
    g_state.mouseX = ev->mouse_x / dpiScale();
    g_state.mouseY = ev->mouse_y / dpiScale();

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
            g_lastPaintNode.reset();
            updateHover();
            paintAtMouse();
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = true;
            g_state.dragStartX = g_state.mouseX;
            g_state.dragStartY = g_state.mouseY;
            g_state.camStartX = g_camera.offset.x;
            g_state.camStartY = g_camera.offset.y;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g_state.painting = false;
            g_lastPaintNode.reset();
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (g_state.painting) {
            updateHover();
            paintAtMouse();
        }
        if (g_state.dragging) {
            g_camera.offset.x = g_state.camStartX + (g_state.mouseX - g_state.dragStartX);
            g_camera.offset.y = g_state.camStartY + (g_state.mouseY - g_state.dragStartY);
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL: {
        const float delta = ev->scroll_y;
        if (delta == 0.0f) {
            break;
        }
        const float zoomFactor = (delta > 0.0f) ? 1.1f : 0.9f;
        const float oldZoom = g_camera.zoom;
        const float newZoom = std::clamp(oldZoom * zoomFactor, 0.15f, 3.0f);
        const float mouseX = g_state.mouseX - panelWidth();
        const float mapX = (mouseX - g_camera.offset.x) / oldZoom;
        const float mapY = (g_state.mouseY - g_camera.offset.y) / oldZoom;
        g_camera.offset.x = mouseX - mapX * newZoom;
        g_camera.offset.y = g_state.mouseY - mapY * newZoom;
        g_camera.zoom = newZoom;
        break;
    }
    default:
        break;
    }
}

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

std::optional<glm::ivec2> parseVec2Arg(const std::string& text) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return std::nullopt;
    }
    return glm::ivec2{std::atoi(text.substr(0, comma).c_str()), std::atoi(text.substr(comma + 1).c_str())};
}

} // namespace

// --- Visual debug CLI ----------------------------------------------------------
// --smoke                 run the CPU smoke test and exit (no window)
// --no-ui                 hide the ImGui panel (clean captures)
// --shot=path.png         capture the framebuffer after 1 s, quit
// --zoom=Z                camera zoom (default: centerCamera's 1.0)
// --center=cx,cy          camera center in cell coords
//                         (default: bbox center of --nodes)
// --nodes="x,y;x,y;..."   paint these seed nodes on startup
// --seed=N                generator seed (placeholder, default 1)
int main(int argc, char* argv[]) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--smoke") {
            smoke = true;
        }
        if (arg == "--no-ui") {
            g_noUi = true;
        }
        if (arg.rfind("--shot=", 0) == 0) {
            g_shotPath = arg.substr(7);
        }
        if (arg.rfind("--zoom=", 0) == 0) {
            g_cliZoom = static_cast<float>(std::atof(arg.substr(7).c_str()));
        }
        if (arg.rfind("--center=", 0) == 0) {
            g_cliCenter = parseVec2Arg(arg.substr(9));
        }
        if (arg.rfind("--nodes=", 0) == 0) {
            g_cliNodes = parseNodesArg(arg.substr(8));
        }
        if (arg.rfind("--seed=", 0) == 0) {
            g_cliSeed = std::atoi(arg.substr(7).c_str());
        }
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        const bool ok = runStoneGeneratorSmokeTest();
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
    desc.window_title = "StoneGenerator - Stone Generator";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
