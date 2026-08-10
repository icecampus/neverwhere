#include "pch.h"

#include <cmath>
#include <cstdlib>
#include <optional>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "FenceModel.h"
#include "FenceRenderer.h"
#include "FenceToolSmokeTest.h"
#include "GridRenderer.h"
#include "PlaygroundScreenshot.h"

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
};

AppState g_state;
topology_core::DiamondIsometry g_iso;
topology_core::Camera2D g_camera;
GridRenderer g_grid;
FenceRenderer g_fenceRenderer;
FenceModel g_fence;
std::optional<glm::ivec2> g_hoverNode;
std::optional<glm::ivec2> g_hoverCell;

// The layout canvas: a square cell map, same dimensions the
// SDFGeneratedLandscape brushes use.
constexpr int kMapW = 24;
constexpr int kMapH = 24;

// Fence tool state: the selected fence (double-click on a post), the active
// stroke drag (axis-locked once it leaves the start cell) and the active move
// drag (parallel shift of the selected fence).
int g_selectedFence = -1;
bool g_eraseMode = false;
bool g_strokeDrag = false;
glm::ivec2 g_strokeStart{0, 0};
glm::ivec2 g_strokeDir{0, 0};
int g_strokeCells = 0;
bool g_moveDrag = false;
glm::ivec2 g_moveStart{0, 0};
glm::ivec2 g_moveDelta{0, 0};
// Manual double-click detection: sokol_app has no double-click event.
glm::ivec2 g_lastClickCell{-1, -1};
double g_lastClickSec = -1.0;

std::string g_shotPath;
std::optional<float> g_cliZoom;
std::optional<glm::ivec2> g_cliCenter;
bool g_noUi = false;
bool g_demoFences = false;

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

// Cell under the cursor in canvas space, unclamped (may leave the map).
glm::ivec2 cellAtMouse() {
    const glm::vec2 world =
        g_camera.screenToWorld({g_state.mouseX - panelWidth(), g_state.mouseY});
    return g_iso.fieldToMap(world);
}

glm::ivec2 clampToMap(glm::ivec2 cell) {
    return {std::clamp(cell.x, 0, kMapW - 1), std::clamp(cell.y, 0, kMapH - 1)};
}

void updateHover() {
    const glm::vec2 world =
        g_camera.screenToWorld({g_state.mouseX - panelWidth(), g_state.mouseY});
    const glm::ivec2 node = g_iso.fieldToNode(world);
    // Vertex nodes of a WxH cell map span (W+1)x(H+1); the border ones are
    // valid too (their footprint simply clips to the in-map cells).
    if (node.x >= 0 && node.y >= 0 && node.x <= kMapW && node.y <= kMapH) {
        g_hoverNode = node;
    } else {
        g_hoverNode.reset();
    }
    const glm::ivec2 cell = g_iso.fieldToMap(world);
    if (cell.x >= 0 && cell.y >= 0 && cell.x < kMapW && cell.y < kMapH) {
        g_hoverCell = cell;
    } else {
        g_hoverCell.reset();
    }
}

// Dominant-axis lock of a stroke drag: the fence is always axis-parallel, so
// the direction is the larger cell delta component (once it is non-zero).
void updateStrokeDrag() {
    const glm::ivec2 delta = clampToMap(cellAtMouse()) - g_strokeStart;
    if (delta.x != 0 || delta.y != 0) {
        if (std::abs(delta.x) >= std::abs(delta.y)) {
            g_strokeDir = {delta.x > 0 ? 1 : -1, 0};
            g_strokeCells = std::abs(delta.x);
        } else {
            g_strokeDir = {0, delta.y > 0 ? 1 : -1};
            g_strokeCells = std::abs(delta.y);
        }
        // planStroke counts the cells to cover: a new fence covers the start
        // cell too, an extension starts past the anchor post.
        const FencePiece* startPiece = g_fence.pieceAt(g_strokeStart);
        if (!startPiece) {
            g_strokeCells += 1;
        }
    }
}

void buildDemoFences() {
    // A long fence with a perpendicular branch (T-junction) and a second,
    // independent fence below.
    g_fence.applyStroke({2, 3}, {1, 0}, 10);
    g_fence.applyStroke({8, 3}, {0, 1}, 6);
    g_fence.applyStroke({14, 10}, {1, 0}, 7);
    g_fence.applyStroke({17, 10}, {0, 1}, 4);
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("FencePathPlayground: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("FencePathPlayground: sg_setup FAILED");
        return;
    }

    if (!g_noUi) {
        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
    }

    g_grid.init();
    g_fenceRenderer.init();
    g_fence.reset(kMapW, kMapH);
    if (g_demoFences) {
        buildDemoFences();
    }

    const glm::vec2 canvas = canvasSize();
    centerCamera(static_cast<int>(canvas.x), static_cast<int>(canvas.y));
    if (g_cliZoom || g_cliCenter) {
        // Deterministic framing for screenshot comparisons.
        const glm::vec2 worldCenter =
            g_cliCenter ? g_iso.mapToField(*g_cliCenter) : g_iso.mapToField(glm::ivec2{kMapW / 2, kMapH / 2});
        g_camera.zoom = g_cliZoom.value_or(1.0f);
        g_camera.offset.x = canvas.x * 0.5f - worldCenter.x * g_camera.zoom;
        g_camera.offset.y = canvas.y * 0.5f - worldCenter.y * g_camera.zoom;
    }
}

void drawImGui(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(h)), ImGuiCond_Always);

    ImGui::Begin("Fence & Path", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::Text("Canvas: %dx%d cells", kMapW, kMapH);
    if (g_hoverCell) {
        ImGui::Text("Hover cell: %d, %d", g_hoverCell->x, g_hoverCell->y);
    } else {
        ImGui::Text("Hover cell: -");
    }
    ImGui::Text("Zoom: %.2f", g_camera.zoom);
    ImGui::Separator();
    ImGui::Text("Fences: %d, pieces: %d", g_fence.fenceCount(), static_cast<int>(g_fence.pieces().size()));
    ImGui::Text("Selected fence: %s", g_selectedFence >= 0 ? std::to_string(g_selectedFence).c_str() : "-");
    ImGui::Checkbox("Erase posts (LMB click)", &g_eraseMode);
    if (g_selectedFence >= 0) {
        if (ImGui::Button("Delete selected fence")) {
            g_fence.eraseFence(g_selectedFence);
            g_selectedFence = -1;
        }
    }
    ImGui::Separator();
    ImGui::TextWrapped("LMB drag on empty cell: new fence line (axis-locked).");
    ImGui::TextWrapped("LMB drag from a post: extend the fence.");
    ImGui::TextWrapped("Double-click a post: select the whole fence; drag it to move, Del to delete.");
    ImGui::TextWrapped("Del over a post (no selection): delete the post — the fence may split.");
    ImGui::Separator();
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
        g_hoverNode.has_value());

    // Ghost preview: the active stroke plan (green) or the rejected raw line
    // (red), or the move preview of the selected fence.
    std::vector<FenceModel::StrokePiece> ghost;
    bool ghostValid = false;
    if (g_strokeDrag && g_strokeDir != glm::ivec2{0, 0} && g_strokeCells > 0) {
        const FenceModel::StrokePlan plan = g_fence.planStroke(g_strokeStart, g_strokeDir, g_strokeCells);
        if (plan.valid) {
            ghost = plan.pieces;
            ghostValid = true;
        } else {
            const bool lead = g_fence.pieceAt(g_strokeStart) == nullptr;
            const glm::ivec2 runStart = lead ? g_strokeStart : g_strokeStart + g_strokeDir;
            for (int i = 0; i < g_strokeCells; ++i) {
                const glm::ivec2 cell = runStart + g_strokeDir * i;
                if (cell.x < 0 || cell.y < 0 || cell.x >= kMapW || cell.y >= kMapH) {
                    break;
                }
                ghost.push_back({FencePieceKind::Post, cell, glm::ivec2{0, 0}, 1});
            }
        }
    } else if (g_moveDrag && g_moveDelta != glm::ivec2{0, 0} && g_selectedFence >= 0) {
        ghostValid = g_fence.canTranslate(g_selectedFence, g_moveDelta);
        for (const FencePiece& piece : g_fence.pieces()) {
            if (piece.fenceId == g_selectedFence) {
                ghost.push_back({piece.kind, piece.cell + g_moveDelta, piece.axis, piece.length});
            }
        }
    }

    g_fenceRenderer.render(
        g_iso,
        g_camera,
        canvasW,
        h,
        g_fence,
        g_selectedFence,
        ghost.empty() ? nullptr : &ghost,
        ghostValid);

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();

    // Headless capture: no mesh caches here, so a short wall-time settle is
    // enough before grabbing the window.
    if (!g_shotPath.empty() && stm_sec(stm_now()) >= 1.0) {
        if (capturePlaygroundPng(g_shotPath.c_str())) {
            spdlog::info("FencePathPlayground: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("FencePathPlayground: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    spdlog::info("FencePathPlayground: cleanup()");
    g_fenceRenderer.shutdown();
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
            if (!g_hoverCell) {
                break;
            }
            const glm::ivec2 cell = *g_hoverCell;
            const double nowSec = stm_sec(stm_now());
            const bool doubleClick = g_lastClickCell == cell && (nowSec - g_lastClickSec) < 0.35;
            g_lastClickCell = cell;
            g_lastClickSec = nowSec;
            if (doubleClick) {
                // Select the whole fence by its post; the second click never
                // starts a stroke/move.
                g_strokeDrag = false;
                g_moveDrag = false;
                const FencePiece* piece = g_fence.pieceAt(cell);
                if (piece && piece->kind == FencePieceKind::Post) {
                    g_selectedFence = piece->fenceId;
                }
                break;
            }
            if (g_eraseMode) {
                g_fence.erasePostAt(cell);
                break;
            }
            if (g_selectedFence >= 0 && g_fence.pieceAt(cell) &&
                g_fence.pieceAt(cell)->fenceId == g_selectedFence) {
                // Drag on the selected fence = parallel shift of the whole fence.
                g_moveDrag = true;
                g_moveStart = cell;
                g_moveDelta = glm::ivec2{0, 0};
                break;
            }
            g_strokeDrag = true;
            g_strokeStart = cell;
            g_strokeDir = glm::ivec2{0, 0};
            g_strokeCells = 0;
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
            if (g_strokeDrag) {
                if (g_strokeDir != glm::ivec2{0, 0} && g_strokeCells > 0) {
                    g_fence.applyStroke(g_strokeStart, g_strokeDir, g_strokeCells);
                } else if (!g_fence.pieceAt(g_strokeStart)) {
                    // Plain click on empty ground: drop the selection.
                    g_selectedFence = -1;
                }
                g_strokeDrag = false;
            }
            if (g_moveDrag) {
                if (g_moveDelta != glm::ivec2{0, 0} && g_selectedFence >= 0) {
                    g_fence.translateFence(g_selectedFence, g_moveDelta);
                }
                g_moveDrag = false;
            }
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.dragging = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (g_state.dragging) {
            g_camera.offset.x = g_state.camStartX + (g_state.mouseX - g_state.dragStartX);
            g_camera.offset.y = g_state.camStartY + (g_state.mouseY - g_state.dragStartY);
        }
        if (g_strokeDrag) {
            updateStrokeDrag();
        }
        if (g_moveDrag) {
            g_moveDelta = clampToMap(cellAtMouse()) - g_moveStart;
        }
        break;
    case SAPP_EVENTTYPE_KEY_DOWN:
        if (ev->key_code == SAPP_KEYCODE_ESCAPE) {
            g_selectedFence = -1;
        }
        if (ev->key_code == SAPP_KEYCODE_DELETE || ev->key_code == SAPP_KEYCODE_BACKSPACE) {
            if (g_selectedFence >= 0) {
                g_fence.eraseFence(g_selectedFence);
                g_selectedFence = -1;
            } else if (g_hoverCell) {
                g_fence.erasePostAt(*g_hoverCell);
            }
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

// CPU smoke test (--smoke): projection and node/cell contract checks of the
// shared topology_core pieces this playground is built on. No window needed.
bool runProjectionSmokeTest() {
    const topology_core::DiamondIsometry iso;
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        if (ok) {
            spdlog::info("TEST PASS: {}", name);
        } else {
            spdlog::error("TEST FAIL: {}", name);
            ++failures;
        }
    };

    // mapToField/fieldToMap round trip over the whole canvas.
    bool cellsOk = true;
    for (int y = 0; y < kMapH && cellsOk; ++y) {
        for (int x = 0; x < kMapW && cellsOk; ++x) {
            cellsOk = iso.fieldToMap(iso.mapToField({x, y})) == glm::ivec2(x, y);
        }
    }
    check(cellsOk, "cell projection round trip");

    // nodeToField/fieldToNode round trip, border nodes included.
    bool nodesOk = true;
    for (int y = 0; y <= kMapH && nodesOk; ++y) {
        for (int x = 0; x <= kMapW && nodesOk; ++x) {
            nodesOk = iso.fieldToNode(iso.nodeToField({x, y})) == glm::ivec2(x, y);
        }
    }
    check(nodesOk, "node projection round trip");

    // Vertex-node contract: a node is the Up corner of the cell with the
    // same coordinates.
    const auto corners = iso.cellDiamondCorners({7, 9}); // Left, Up, Right, Down
    check(corners[1] == iso.nodeToField({7, 9}), "node == cell Up corner");

    // Diamond corner ordering: Left/Right share the mid row, Up/Down the mid
    // column, and the vertical span is symmetric.
    const glm::vec2 c = iso.mapToField({7, 9});
    const bool shapeOk = corners[0].x < c.x && corners[2].x > c.x &&
        corners[1].y < c.y && corners[3].y > c.y &&
        std::abs(corners[0].y - c.y) < 1e-4f && std::abs(corners[2].y - c.y) < 1e-4f &&
        std::abs(corners[1].x - c.x) < 1e-4f && std::abs(corners[3].x - c.x) < 1e-4f;
    check(shapeOk, "diamond corner geometry");

    // The 4 cells sharing a node each list that node among their corners.
    bool neighboursOk = true;
    for (const glm::ivec2 cell : topology_core::DiamondIsometry::nodeNeighbourCells({5, 6})) {
        const auto nodes = topology_core::DiamondIsometry::cellCornerNodes(cell);
        bool found = false;
        for (const glm::ivec2 n : nodes) {
            found = found || (n == glm::ivec2(5, 6));
        }
        neighboursOk = neighboursOk && found;
    }
    check(neighboursOk, "node neighbour cells contain the node");

    // Camera world<->screen inverse.
    topology_core::Camera2D cam;
    cam.offset = {123.0f, -45.0f};
    cam.zoom = 1.75f;
    const glm::vec2 p{640.0f, 320.0f};
    const glm::vec2 roundTrip = cam.worldToScreen(cam.screenToWorld(p));
    check(glm::length(roundTrip - p) < 1e-3f, "camera screen/world inverse");

    if (failures == 0) {
        spdlog::info("TEST PASS: FencePathPlayground smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: FencePathPlayground smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}

std::optional<glm::ivec2> parseVec2Arg(const std::string& text) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return std::nullopt;
    }
    return glm::ivec2{std::atoi(text.substr(0, comma).c_str()), std::atoi(text.substr(comma + 1).c_str())};
}

} // namespace

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
        if (arg == "--demo-fence") {
            g_demoFences = true;
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
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        const bool projectionOk = runProjectionSmokeTest();
        const bool fenceOk = runFenceToolSmokeTest();
        return (projectionOk && fenceOk) ? 0 : 1;
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "FencePathPlayground - Fence & Path Layout";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
