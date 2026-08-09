#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "AtlasRenderer.h"
#include "MaskField.h"
#include "FlatAtlasGenerator.h"
#include "LandBrush.h"
#include "PlaygroundScreenshot.h"
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
AtlasRenderer g_renderer;
std::filesystem::path g_dataRoot;
std::optional<glm::ivec2> g_hoverNode;
bool g_ctrlDown = false;

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

// Brush palette: this fork keeps a single brush. Mask 3D extrudes the 2D
// mask silhouette (interpolated node fill, iso 0.5) into a thin slab with a
// sloped skirt (maskfield::MaskField), standing half-height below the grid.
struct PaintLayer {
    LandBrush brush;
    AtlasKind atlas;
    bool mask;
    const char* name;
};

constexpr int kLayerCount = 1;
PaintLayer g_layers[kLayerCount] = {
    {{}, AtlasKind::Flat, true, "Mask 3D"},
};
int g_activeLayer = 0;

// Mask layer: scalar-field params (heavy, debounced mesh rebuild) and the
// lift scale (cheap re-projection of the cached mesh, no field rebuild).
maskfield::MaskFieldParams g_maskParams;
float g_maskHeightScale = 96.0f;

// Shading palette of the cliff pass (uniforms only, instant) — the mask
// layer is shaded by this same palette.
struct ShadingParams {
    float lightAzimuth = 2.23f;   // radians, matches the previous fixed sun dir
    float lightElevation = 0.85f; // radians
    float darkColor[3] = {0.38f, 0.38f, 0.42f};
    float goldColor[3] = {0.75f, 0.62f, 0.5f};
    float grassA[3] = {0.4f, 0.62f, 0.35f};
    float grassB[3] = {0.6f, 0.65f, 0.4f};
    float veinThreshold = 0.8f;
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float backLight = 0.1f;
    float specStrength = 0.5f;
    float specPower = 24.0f;
    float gamma = 0.85f;
};
ShadingParams g_cliffShading;

// Material (ambientCG Ground061): uniform strengths (instant) and the CPU
// relief raster (debounced field rebuild). With no maps loaded everything
// falls back to the palette look (strengths zeroed in init()).
maskfield::ReliefMap g_reliefMap;
int g_matMapsLoaded = 0;
bool g_reliefLoaded = false;
std::string g_matDir = "resources/textures/ambientcg/Ground061";
float g_matTiling = 1.0f;
float g_matAlbedo = 1.0f;
float g_matNormal = 1.0f;
float g_matAo = 1.0f;
float g_matRough = 0.7f;

LandBrush& activeBrush() {
    return g_layers[g_activeLayer].brush;
}

bool looksLikeDataRoot(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "resources" / "textures", ec);
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

// --- Visual debug CLI (headless screenshot comparisons vs the editor) ---
// --no-ui                   hide the ImGui panel (clean captures)
// --demo                    paint the demo stroke (L-shape + detached node)
// --mask-nodes=x,y;x,y;...  paint these nodes on the mask layer
// --mask-spread=D           slope skirt around the mask plate: height ramps
//                           down to 0 over D cells outside the core contour
// --mask-sink=K             share of the height below the grid plane (the
//                           future water), 0..1 (default 0.5)
// --mat-dir=path            material set directory
//                           (default resources/textures/ambientcg/Ground061)
// --mat-tiling=T            material tiling repeats per world unit (default 1.0)
// --relief-depth=D          micro relief amplitude in world units
//                           (default 0.02 with a loaded map, 0 without)
// --zoom=Z                  camera zoom (default: centerCamera's 1.0)
// --center=cx,cy            camera center in cell coords
//                           (default: bbox center of --mask-nodes)
// --shot=path.png           capture the window client area to a PNG after
//                           the caches settle, then quit
bool g_noUi = false;
std::vector<glm::ivec2> g_cliMaskNodes;
std::optional<float> g_cliMaskSpread;
std::optional<float> g_cliMaskSink;
std::string g_cliMatDir;
std::optional<float> g_cliMatTiling;
std::optional<float> g_cliReliefDepth;
std::optional<float> g_cliZoom;
std::optional<glm::vec2> g_cliCenter;
std::string g_shotPath;

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

// Painted programmatically in --demo mode: an L-shaped node stroke plus a
// detached node — the slab follows the interpolated fill contour (diagonal
// walls on the half-painted cells, a small blob around the lone node).
void paintDemoPattern() {
    for (int y = 3; y <= 5; ++y) {
        g_layers[0].brush.setNode({18, y}, true);
        g_layers[0].brush.setNode({19, y}, true);
    }
    g_layers[0].brush.setNode({20, 5}, true);
    g_layers[0].brush.setNode({22, 4}, true);
}

void updateHover() {
    const glm::vec2 world =
        g_camera.screenToWorld({g_state.mouseX - panelWidth(), g_state.mouseY});
    const glm::ivec2 node = g_iso.fieldToNode(world);
    if (g_layers[0].brush.isNodeEditable(node)) {
        g_hoverNode = node;
    } else {
        g_hoverNode.reset();
    }
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("SDFWithMaterialLandscape: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("SDFWithMaterialLandscape: sg_setup FAILED");
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

    g_dataRoot = findDataRootUpwards(std::filesystem::current_path());
    if (g_dataRoot.empty()) {
        spdlog::warn("SDFWithMaterialLandscape: data root not found, using CWD");
        g_dataRoot = std::filesystem::current_path();
    }
    spdlog::info("dataRoot={}", g_dataRoot.string());

    const FlatAtlasImage flat = generateFlatAtlas();
    if (!g_renderer.loadAtlasFromRgba(
            AtlasKind::Flat,
            flat.rgba.data(),
            flat.width,
            flat.height,
            flat.cols,
            flat.rows)) {
        spdlog::error("SDFWithMaterialLandscape: failed to upload generated flat atlas");
    }

    // Material (ambientCG Ground061): GPU maps for the shader + the CPU
    // relief raster for the field. Missing maps keep the palette look
    // (strengths at 0, relief off).
    g_matMapsLoaded = g_renderer.loadMaterialMaps((g_dataRoot / g_matDir).string());
    if (g_matMapsLoaded == 0) {
        g_matAlbedo = g_matNormal = g_matAo = g_matRough = 0.0f;
    }
    g_reliefLoaded = g_renderer.loadReliefMap(
        (g_dataRoot / g_matDir / "Ground061_Displacement.jpg").string(),
        g_reliefMap);
    g_maskParams.reliefMap = &g_reliefMap;
    g_maskParams.reliefDepth = g_reliefLoaded ? g_cliReliefDepth.value_or(0.02f) : 0.0f;
    if (g_reliefLoaded) {
        g_maskParams.reliefVersion = 1;
    }

    if (g_demoPattern) {
        paintDemoPattern();
    }
    for (const glm::ivec2& node : g_cliMaskNodes) {
        g_layers[0].brush.setNode(node, true);
    }

    const glm::vec2 canvas = canvasSize();
    centerCamera(static_cast<int>(canvas.x), static_cast<int>(canvas.y));
    if (g_cliZoom || g_cliCenter || !g_cliMaskNodes.empty()) {
        // Deterministic framing for screenshot comparisons.
        glm::vec2 worldCenter;
        if (g_cliCenter) {
            worldCenter = g_iso.mapToField(glm::ivec2(*g_cliCenter));
        } else if (!g_cliMaskNodes.empty()) {
            glm::vec2 acc(0.0f);
            for (const glm::ivec2& node : g_cliMaskNodes) {
                acc += g_iso.nodeToField(node);
            }
            worldCenter = acc / static_cast<float>(g_cliMaskNodes.size());
        } else {
            // Zoom without an explicit center: the map middle (centerCamera).
            const glm::ivec2 mid{g_layers[0].brush.width() / 2, g_layers[0].brush.height() / 2};
            worldCenter = g_iso.mapToField(mid);
        }
        g_camera.zoom = g_cliZoom.value_or(1.0f);
        g_camera.offset.x = canvas.x * 0.5f - worldCenter.x * g_camera.zoom;
        g_camera.offset.y = canvas.y * 0.5f - worldCenter.y * g_camera.zoom;
    }
}

void drawImGui(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(h)), ImGuiCond_Always);

    const glm::vec2 world =
        g_camera.screenToWorld({g_state.mouseX - panelWidth(), g_state.mouseY});
    const glm::ivec2 cell = g_iso.fieldToMap(world);

    ImGui::Begin(
        "SDFWithMaterialLandscape",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Vertex land brush");
    ImGui::Text("LMB paint  |  Ctrl+LMB erase  |  RMB pan  |  wheel zoom");
    ImGui::Separator();

    ImGui::Text("Brush palette:");
    for (int i = 0; i < kLayerCount; ++i) {
        if (ImGui::Selectable(g_layers[i].name, g_activeLayer == i)) {
            g_activeLayer = i;
        }
    }
    if (g_layers[g_activeLayer].mask) {
        // The 2D mask silhouette (interpolated node fill, iso 0.5) extruded
        // into a thin slab (maskfield::MaskField); edits are debounced
        // (0.3 s) into a full field rebuild.
        ImGui::SliderFloat("Mask height", &g_maskHeightScale, 4.0f, 128.0f, "%.0f px");
        if (ImGui::CollapsingHeader("Mask field", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("(applied after a 0.3 s edit pause)");
            ImGui::SliderFloat("Height (world)", &g_maskParams.height, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("Spread", &g_maskParams.spreadDistance, 0.0f, 3.0f, "%.2f cells");
            ImGui::SliderFloat("Sink below grid", &g_maskParams.sinkFraction, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Cell size", &g_maskParams.cellSize, 0.03f, 0.12f, "%.3f");
            ImGui::SliderInt("Blur passes", &g_maskParams.blurPasses, 0, 3);
        }
        if (ImGui::CollapsingHeader("Material (Ground061)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Maps: %d/4%s", g_matMapsLoaded, g_matMapsLoaded > 0 ? "" : " (palette look)");
            ImGui::SliderFloat("Mat tiling", &g_matTiling, 0.01f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Albedo", &g_matAlbedo, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Normal map", &g_matNormal, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("AO", &g_matAo, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Roughness", &g_matRough, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("(relief rebuilds the mesh after the pause)");
            ImGui::SliderFloat("Relief depth", &g_maskParams.reliefDepth, 0.0f, 0.05f, "%.3f");
            ImGui::SliderFloat("Relief tiling", &g_maskParams.reliefTiling, 0.01f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::Button("Reload material")) {
                g_matMapsLoaded = g_renderer.loadMaterialMaps((g_dataRoot / g_matDir).string());
                g_reliefLoaded = g_renderer.loadReliefMap(
                    (g_dataRoot / g_matDir / "Ground061_Displacement.jpg").string(),
                    g_reliefMap);
                // Force the params memcmp to notice the new raster.
                ++g_maskParams.reliefVersion;
            }
        }
        const CliffStats& st = g_renderer.cliffStatsFor(&g_layers[g_activeLayer].brush);
        ImGui::Text("Mask mesh: %d verts, %d tris", st.vertexCount, st.triangleCount);
        ImGui::Text("Watertight: %s", st.watertight ? "yes" : "NO");
        ImGui::Text("Rebuild: %.0f ms (%d voxels)", st.rebuildMs, st.voxelCount);
        if (st.pending) {
            ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Rebuilding...");
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
    ImGui::Text("On nodes:");
    for (int i = 0; i < kLayerCount; ++i) {
        ImGui::SameLine();
        ImGui::Text(
            "%s %d%s",
            g_layers[i].name,
            g_layers[i].brush.onNodeCount(),
            i + 1 < kLayerCount ? "," : "");
    }
    ImGui::Checkbox("Erase mode", &g_state.eraseMode);
    if (ImGui::Button("Clear layer")) {
        activeBrush().clear();
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
    // The swapchain has a depth-stencil attachment: clear it every frame for
    // the z-buffered cliff pass.
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

    PaintLayerView views[kLayerCount];
    views[0].brush = &g_layers[0].brush;
    views[0].atlas = g_layers[0].atlas;
    views[0].cliffHeightScale = g_maskHeightScale;
    views[0].mask = g_layers[0].mask;
    views[0].maskParams = &g_maskParams;
    views[0].shadingOverride = nullptr;

    // Cliff shading uniforms: palette/light from the UI state; the view
    // direction is constant for the iso camera (viewer -> scene, mirrors
    // CliffFieldPlayground's rd = normalize(p - camPos)). The mask layer is
    // shaded by this same default palette; the top texture stays off.
    const ShadingParams& s = g_cliffShading;
    CliffFsParams cliffFs{};
    const float lightCe = std::cos(s.lightElevation);
    cliffFs.lightDir[0] = lightCe * std::sin(s.lightAzimuth);
    cliffFs.lightDir[1] = std::sin(s.lightElevation);
    cliffFs.lightDir[2] = lightCe * std::cos(s.lightAzimuth);
    const float halfH = g_iso.dims.cellSize().y * 0.5f;
    const float viewY = 2.0f * halfH / std::max(g_maskHeightScale, 1.0f);
    const float viewLen = std::sqrt(2.0f + viewY * viewY);
    cliffFs.viewDir[0] = -1.0f / viewLen;
    cliffFs.viewDir[1] = -viewY / viewLen;
    cliffFs.viewDir[2] = -1.0f / viewLen;
    std::memcpy(cliffFs.darkColor, s.darkColor, sizeof(s.darkColor));
    std::memcpy(cliffFs.goldColor, s.goldColor, sizeof(s.goldColor));
    std::memcpy(cliffFs.grassA, s.grassA, sizeof(s.grassA));
    std::memcpy(cliffFs.grassB, s.grassB, sizeof(s.grassB));
    cliffFs.params0[0] = s.veinThreshold;
    cliffFs.params0[1] = s.ambient;
    cliffFs.params0[2] = s.diffuse;
    cliffFs.params0[3] = s.specStrength;
    cliffFs.params1[0] = s.specPower;
    cliffFs.params1[1] = s.gamma;
    cliffFs.params1[2] = s.backLight;
    // params1[3] unused. Material uniforms (instant, no mesh rebuild):
    // tiling / albedo / normal / AO in params2, roughness in params3.x.
    cliffFs.params2[0] = g_matTiling;
    cliffFs.params2[1] = g_matAlbedo;
    cliffFs.params2[2] = g_matNormal;
    cliffFs.params2[3] = g_matAo;
    cliffFs.params3[0] = g_matRough;

    g_renderer.render(
        views,
        kLayerCount,
        g_iso,
        g_camera,
        canvasW,
        h,
        g_hoverNode.value_or(glm::ivec2{-1, -1}),
        g_hoverNode.has_value(),
        &cliffFs,
        stm_sec(stm_now()));

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();

    // Headless capture: grab the window client area once the field caches
    // have settled (no layer is pending a debounced rebuild), then quit.
    // The pending check is the precise intent — on fast machines 120 frames
    // pass well under the 0.3 s debounce and the meshes only make it into
    // the capture because the rebuild finished; on software renderers
    // (llvmpipe under xvfb) each frame costs seconds, so a blind frame count
    // stretches the wait to minutes while the debounce has long fired.
    bool cachesSettled = true;
    for (const PaintLayer& layer : g_layers) {
        if (layer.mask && g_renderer.cliffStatsFor(&layer.brush).pending) {
            cachesSettled = false;
            break;
        }
    }
    if (!g_shotPath.empty() && stm_sec(stm_now()) >= 1.0 &&
        (cachesSettled || g_state.frame_index >= 120)) {
        if (capturePlaygroundPng(g_shotPath.c_str())) {
            spdlog::info("SDFWithMaterialLandscape: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("SDFWithMaterialLandscape: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    spdlog::info("SDFWithMaterialLandscape: cleanup()");
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
            updateHover();
            applyBrushAtMouse();
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
        float newZoom = std::clamp(oldZoom * zoomFactor, 0.15f, 3.0f);
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
        if (arg == "--no-ui") {
            g_noUi = true;
        }
        if (arg.rfind("--mask-nodes=", 0) == 0) {
            g_cliMaskNodes = parseNodesArg(arg.substr(13));
        }
        if (arg.rfind("--mask-spread=", 0) == 0) {
            g_cliMaskSpread = static_cast<float>(std::atof(arg.substr(14).c_str()));
        }
        if (arg.rfind("--mask-sink=", 0) == 0) {
            g_cliMaskSink = static_cast<float>(std::atof(arg.substr(12).c_str()));
        }
        if (arg.rfind("--mat-dir=", 0) == 0) {
            g_cliMatDir = arg.substr(10);
        }
        if (arg.rfind("--mat-tiling=", 0) == 0) {
            g_cliMatTiling = static_cast<float>(std::atof(arg.substr(13).c_str()));
        }
        if (arg.rfind("--relief-depth=", 0) == 0) {
            g_cliReliefDepth = static_cast<float>(std::atof(arg.substr(15).c_str()));
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
    if (g_cliMaskSpread) {
        g_maskParams.spreadDistance = *g_cliMaskSpread;
    }
    if (g_cliMaskSink) {
        g_maskParams.sinkFraction = std::clamp(*g_cliMaskSink, 0.0f, 1.0f);
    }
    if (!g_cliMatDir.empty()) {
        g_matDir = g_cliMatDir;
    }
    if (g_cliMatTiling) {
        g_matTiling = *g_cliMatTiling;
        // Keep the geometry dunes aligned with the shader ripples (the UI
        // sliders can still be desynced deliberately).
        g_maskParams.reliefTiling = *g_cliMatTiling;
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
    desc.window_title = "SDFWithMaterialLandscape - Mask 3D + Material";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
