#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "BrepRenderer.h"
#include "GridRenderer.h"
#include "NodeField.h"
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
GridRenderer g_grid;
BrepRenderer g_brep;
std::optional<glm::ivec2> g_hoverNode;
std::optional<glm::ivec2> g_lastPaintNode;
bool g_ctrlDown = false;

// The paint canvas: a square cell map (24x24 cells -> 25x25 vertex nodes),
// same dimensions the SDFGeneratedLandscape brushes use.
constexpr int kMapW = 24;
constexpr int kMapH = 24;
NodeField g_nodes;
int g_paintLevel = 1; // plateau level painted by LMB (1..kMaxNodeLevel)

BrepGenParams g_genParams;

// Material sets live under resources/textures/<vendor>/<name>/ (Poly Haven
// packs in polyhaven/, ambientCG in ambientcg/; name == leaf directory name
// == file prefix). The Poly Haven EXR maps were transcoded to PNG by
// tmp/convert_exr.py (stb cannot read EXR). Every directory with a loadable
// color map shows up in the picker (scanned two levels deep: vendor dirs).
// The walls are fully covered by the material: the composer contributes
// GEOMETRY ONLY, so the albedo strength defaults to 1 and the baked quad
// colors are just the fallback (strengths zeroed in reloadMaterial() if no
// maps load).
struct MatSetEntry {
    std::string name; // file prefix == leaf directory name
    std::string dir;  // path relative to the data root
};
std::filesystem::path g_dataRoot;
int g_matMapsMask = 0; // bit0 color, bit1 normal, bit2 AO, bit3 roughness
std::vector<MatSetEntry> g_matSets;
std::string g_matDir = "resources/textures/polyhaven/marble_cliff_01";
std::string g_matSet = "marble_cliff_01";
float g_matTiling = 0.35f;
float g_matAlbedo = 1.0f;
float g_matNormal = 1.0f;
float g_matAo = 0.0f;
float g_matRough = 1.0f;

// Grass top: the up-facing plateau surface trades the rock albedo for a tiled
// grass texture (view slot 4, shader weight smoothstep on the up-normal).
// Strength 0 disables it bit-for-bit.
std::string g_grassPath = "resources/textures/grass.png";
float g_grassStrength = 1.0f;
float g_grassTiling = 0.5f;

// Fixed sun (no lighting UI in v1): same defaults the SDF playgrounds use.
constexpr float kLightAzimuth = 2.23f;   // radians
constexpr float kLightElevation = 0.85f; // radians
constexpr float kAmbient = 0.35f;
constexpr float kDiffuse = 0.75f;
constexpr float kSpecStrength = 0.3f;
constexpr float kSpecPower = 24.0f;
constexpr float kGamma = 0.85f;

std::string g_shotPath;
std::optional<float> g_cliZoom;
std::optional<glm::ivec2> g_cliCenter;
std::vector<glm::ivec2> g_cliNodes;
std::vector<glm::ivec2> g_cliNodes2;
std::vector<glm::ivec2> g_cliNodes3;
std::optional<float> g_cliHeight;
std::optional<int> g_cliSeed;
std::optional<int> g_cliStyle; // 0 = Cyclopean, 1 = BlockCliff
std::string g_cliMatDir;
std::string g_cliMatSet;
std::optional<float> g_cliMatTiling;
std::string g_cliGrassPath;
std::optional<float> g_cliGrassTiling;
std::optional<float> g_cliRelief;
std::optional<float> g_cliReliefWavelength;
std::optional<float> g_cliWobble;
std::optional<float> g_cliWobbleWavelength;
std::optional<float> g_cliBatter;
std::optional<float> g_cliFootFlare;
std::optional<float> g_cliLedges;
std::optional<float> g_cliTalus;
std::optional<float> g_cliBevel;
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
    const int level = (!g_ctrlDown && !g_state.eraseMode) ? g_paintLevel : 0;
    if (g_lastPaintNode && *g_lastPaintNode != *g_hoverNode) {
        // Bresenham between drag events: a fast stroke must not leave holes
        // in the node field (they read as torn partial tiles in the mesh).
        paintNodeLine(g_nodes, *g_lastPaintNode, *g_hoverNode, level);
    } else {
        g_nodes.setNode(*g_hoverNode, level);
    }
    g_lastPaintNode = g_hoverNode;
}

int countSetBits(int mask) {
    int n = 0;
    while (mask) {
        n += mask & 1;
        mask >>= 1;
    }
    return n;
}

void reloadMaterial() {
    g_matMapsMask = g_brep.loadMaterialMaps((g_dataRoot / g_matDir).string(), g_matSet);
    if (g_matMapsMask == 0) {
        // Palette fallback: the shader with all-zero strengths is bit-for-bit
        // the plain baked quad color.
        g_matAlbedo = g_matNormal = g_matAo = g_matRough = 0.0f;
    }
}

void reloadGrass() {
    if (!g_brep.loadGrassMap((g_dataRoot / g_grassPath).string())) {
        // No texture -> no grass (strength 0 keeps the bindings placeholder).
        g_grassStrength = 0.0f;
    }
}

// Strength defaults for a freshly picked set: full coverage, AO on only when
// the set actually ships an AO map (Poly Haven cliff packs don't, Rock064
// does).
void applySetDefaults() {
    if (g_matMapsMask == 0) {
        return;
    }
    g_matAlbedo = g_matNormal = g_matRough = 1.0f;
    g_matAo = (g_matMapsMask & 4) ? 0.5f : 0.0f;
}

// Every directory with a loadable color map under resources/textures (two
// levels deep: <vendor>/<name>/) is a material set (name == leaf directory
// name == file prefix).
void scanMaterialSets() {
    g_matSets.clear();
    std::error_code ec;
    const std::filesystem::path texRoot = g_dataRoot / "resources" / "textures";
    std::vector<std::filesystem::path> parents{texRoot};
    for (const std::filesystem::directory_entry& vendor :
        std::filesystem::directory_iterator(texRoot, ec)) {
        if (vendor.is_directory()) {
            parents.push_back(vendor.path());
        }
    }
    for (const std::filesystem::path& parent : parents) {
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(parent, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') {
                continue;
            }
            if (probeMaterialMaps(entry.path().string(), name) & 1) {
                g_matSets.push_back({name, entry.path().lexically_relative(g_dataRoot).string()});
            }
        }
    }
    std::sort(
        g_matSets.begin(),
        g_matSets.end(),
        [](const MatSetEntry& a, const MatSetEntry& b) { return a.name < b.name; });
    spdlog::info("B-repGeneratedLandscape: {} material set(s) under resources/textures", g_matSets.size());
    for (const MatSetEntry& set : g_matSets) {
        spdlog::info("  material set: {} ({})", set.name, set.dir);
    }
}

void pickMaterialSet(const MatSetEntry& set) {
    g_matSet = set.name;
    g_matDir = set.dir;
    reloadMaterial();
    applySetDefaults();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("B-repGeneratedLandscape: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("B-repGeneratedLandscape: sg_setup FAILED");
        return;
    }

    if (!g_noUi) {
        simgui_desc_t imgui_desc = {};
        simgui_setup(&imgui_desc);
        g_state.imgui_ok = true;
    }

    g_grid.init();
    g_brep.init();
    g_nodes.reset(kMapW + 1, kMapH + 1);

    g_dataRoot = findDataRootUpwards(std::filesystem::current_path());
    if (g_dataRoot.empty()) {
        spdlog::warn("B-repGeneratedLandscape: data root not found, using CWD");
        g_dataRoot = std::filesystem::current_path();
    }
    spdlog::info("dataRoot={}", g_dataRoot.string());
    scanMaterialSets();
    if (g_cliMatDir.empty()) {
        // The default and --mat-set names resolve vendor-agnostically
        // through the scanned list.
        for (const MatSetEntry& set : g_matSets) {
            if (set.name == g_matSet) {
                g_matDir = set.dir;
                break;
            }
        }
    }
    reloadMaterial();
    applySetDefaults();
    reloadGrass();

    if (g_cliHeight) {
        g_genParams.raisedHeight = *g_cliHeight;
    }
    if (g_cliSeed) {
        g_genParams.rockSeed = *g_cliSeed;
    }
    if (g_cliStyle) {
        g_genParams.wallStyle = *g_cliStyle == 0
            ? brepmesh::WallStyleId::Cyclopean
            : brepmesh::WallStyleId::BlockCliff;
    }
    for (const glm::ivec2& node : g_cliNodes) {
        g_nodes.setNode(node, 1);
    }
    for (const glm::ivec2& node : g_cliNodes2) {
        g_nodes.setNode(node, 2);
    }
    for (const glm::ivec2& node : g_cliNodes3) {
        g_nodes.setNode(node, 3);
    }

    const glm::vec2 canvas = canvasSize();
    centerCamera(static_cast<int>(canvas.x), static_cast<int>(canvas.y));
    const bool hasCliNodes = !g_cliNodes.empty() || !g_cliNodes2.empty() || !g_cliNodes3.empty();
    if (g_cliZoom || g_cliCenter || hasCliNodes) {
        // Deterministic framing for screenshot comparisons.
        glm::vec2 worldCenter;
        if (g_cliCenter) {
            worldCenter = g_iso.mapToField(*g_cliCenter);
        } else if (hasCliNodes) {
            glm::vec2 acc(0.0f);
            int count = 0;
            for (const glm::ivec2& node : g_cliNodes) {
                acc += g_iso.nodeToField(node);
                ++count;
            }
            for (const glm::ivec2& node : g_cliNodes2) {
                acc += g_iso.nodeToField(node);
                ++count;
            }
            for (const glm::ivec2& node : g_cliNodes3) {
                acc += g_iso.nodeToField(node);
                ++count;
            }
            worldCenter = acc / static_cast<float>(std::max(count, 1));
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
        "B-rep Landscape",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Vertex-node land brush (B-rep walls)");
    ImGui::Text("LMB paint  |  Ctrl+LMB erase  |  RMB pan  |  wheel zoom");
    if (g_hoverNode) {
        ImGui::Text("Hover node: (%d, %d) L%d",
            g_hoverNode->x, g_hoverNode->y, (int)g_nodes.levelAt(*g_hoverNode));
    } else {
        ImGui::Text("Hover node: (out of bounds)");
    }
    ImGui::Text("On nodes: %d (max level %d)", g_nodes.onNodeCount(), (int)g_nodes.maxLevel());
    ImGui::Checkbox("Erase mode", &g_state.eraseMode);
    ImGui::Text("Paint level:");
    ImGui::SameLine();
    for (int lv = 1; lv <= (int)kMaxNodeLevel; ++lv) {
        if (lv > 1) {
            ImGui::SameLine();
        }
        ImGui::RadioButton(std::to_string(lv).c_str(), &g_paintLevel, lv);
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("B-rep generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("(applied after a 0.3 s edit pause)");
        ImGui::SliderFloat("Raised height", &g_genParams.raisedHeight, 0.5f, 8.0f, "%.2f");
        ImGui::SliderInt("Rock seed", &g_genParams.rockSeed, 0, 9999);
        ImGui::SliderFloat("Rock amplitude", &g_genParams.rockAmplitude, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Corner bevel", &g_genParams.cornerBevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderInt("Wall subdiv H", &g_genParams.wallSubdivH, 4, 16);
        ImGui::SliderInt("Wall subdiv V", &g_genParams.wallSubdivV, 4, 16);
        ImGui::Checkbox("Rock displacement", &g_genParams.rockEnabled);
        const char* styles[] = {"Cyclopean", "BlockCliff"};
        int styleIdx = g_genParams.wallStyle == brepmesh::WallStyleId::Cyclopean ? 0 : 1;
        if (ImGui::Combo("Wall style", &styleIdx, styles, 2)) {
            g_genParams.wallStyle = styleIdx == 0
                ? brepmesh::WallStyleId::Cyclopean
                : brepmesh::WallStyleId::BlockCliff;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Wall profile (rebuild)");
        ImGui::SliderFloat("Batter", &g_genParams.wallBatter, 0.0f, 0.5f, "%.2f");
        ImGui::SliderFloat("Foot flare", &g_genParams.wallFootFlare, 0.0f, 0.4f, "%.2f");
        ImGui::SliderFloat("Ledges", &g_genParams.wallLedgeAmp, 0.0f, 0.2f, "%.2f");
        ImGui::SliderInt("Ledge count", &g_genParams.wallLedgeCount, 1, 6);
        ImGui::SliderFloat("Talus width", &g_genParams.talusWidth, 0.0f, 0.8f, "%.2f");
        ImGui::SliderFloat("Talus height", &g_genParams.talusHeightFrac, 0.05f, 0.5f, "%.2f");
        ImGui::SliderFloat("Height scale", &g_genParams.heightScale, 32.0f, 192.0f, "%.0f px");
        ImGui::Separator();
        ImGui::TextDisabled("Deformation (live, no rebuild)");
        ImGui::SliderFloat("Top relief", &g_genParams.deform.reliefAmp, 0.0f, 0.6f, "%.2f");
        ImGui::SliderFloat("Relief wavelength", &g_genParams.deform.reliefWavelength, 1.0f, 8.0f, "%.1f");
        ImGui::SliderFloat("Rim wobble", &g_genParams.deform.wobbleAmp, 0.0f, 0.4f, "%.2f");
        ImGui::SliderFloat("Wobble wavelength", &g_genParams.deform.wobbleWavelength, 1.0f, 8.0f, "%.1f");
    }

    if (ImGui::CollapsingHeader(("Material (" + g_matSet + ")").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!g_matSets.empty()) {
            if (ImGui::BeginCombo("Set", g_matSet.c_str())) {
                for (const MatSetEntry& set : g_matSets) {
                    const bool selected = set.name == g_matSet;
                    if (ImGui::Selectable(set.name.c_str(), selected)) {
                        pickMaterialSet(set);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Text("Maps: %d/4%s", countSetBits(g_matMapsMask), g_matMapsMask != 0 ? "" : " (quad-color look)");
        ImGui::SliderFloat("Mat tiling", &g_matTiling, 0.05f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Albedo", &g_matAlbedo, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Normal map", &g_matNormal, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("AO", &g_matAo, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Roughness", &g_matRough, 0.0f, 1.0f, "%.2f");
        if (ImGui::Button("Reload material")) {
            reloadMaterial();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Grass top (%s)", g_grassStrength > 0.0f ? g_grassPath.c_str() : "missing");
        ImGui::SliderFloat("Grass", &g_grassStrength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Grass tiling", &g_grassTiling, 0.05f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    }

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        const BrepStats& st = g_brep.stats();
        ImGui::Text("Quads: %d (top %d, walls %d)", st.quadCount, st.topQuadCount, st.wallQuadCount);
        ImGui::Text("Vertices: %d", st.vertexCount);
        ImGui::Text("Rebuild: %.1f ms", st.rebuildMs);
        ImGui::Text("Seams: %s (%d/%d edges)",
            st.seamsPassed ? "ok" : "FAILED", st.seamMismatches, st.seamCheckedEdges);
        if (st.pending) {
            ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Rebuilding...");
        }
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
        // The brush paints vertex nodes; the hover cell shown under the
        // cursor is the cell whose Up corner is that node.
        g_hoverNode.has_value()
            ? glm::ivec2{std::min(g_hoverNode->x, kMapW - 1), std::min(g_hoverNode->y, kMapH - 1)}
            : glm::ivec2{-1, -1},
        g_hoverNode.has_value());

    // Debounced B-rep rebuild (change detection inside setContent).
    g_brep.setContent(g_nodes.nodes.data(), g_nodes.width, g_nodes.height, g_genParams);
    g_brep.update(g_iso, stm_sec(now));

    // Fragment uniforms: fixed sun + the constant iso view direction
    // (viewer -> scene; the lift scale sets its world-space slope), lighting
    // and material strengths (instant, no mesh rebuild).
    BrepFsParams fs{};
    const float lightCe = std::cos(kLightElevation);
    fs.lightDir[0] = lightCe * std::sin(kLightAzimuth);
    fs.lightDir[1] = std::sin(kLightElevation);
    fs.lightDir[2] = lightCe * std::cos(kLightAzimuth);
    const float halfH = g_iso.dims.cellSize().y * 0.5f;
    const float viewY = 2.0f * halfH / std::max(g_genParams.heightScale, 1.0f);
    const float viewLen = std::sqrt(2.0f + viewY * viewY);
    fs.viewDir[0] = -1.0f / viewLen;
    fs.viewDir[1] = -viewY / viewLen;
    fs.viewDir[2] = -1.0f / viewLen;
    fs.params0[0] = kAmbient;
    fs.params0[1] = kDiffuse;
    fs.params0[2] = kSpecStrength;
    fs.params0[3] = kSpecPower;
    fs.params1[0] = kGamma;
    fs.params1[1] = g_brep.materialVTile();
    fs.params2[0] = g_matTiling;
    fs.params2[1] = g_matAlbedo;
    fs.params2[2] = g_matNormal;
    fs.params2[3] = g_matAo;
    fs.params3[0] = g_matRough;
    fs.params4[0] = g_grassStrength;
    fs.params4[1] = g_grassTiling;

    g_brep.render(g_camera, canvasW, h, fs);

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();

    // Headless capture: grab the framebuffer once the B-rep cache has settled
    // (no pending debounced rebuild) plus a 1 s wall-time floor, then quit.
    if (!g_shotPath.empty() && stm_sec(stm_now()) >= 1.0 && !g_brep.stats().pending) {
        if (capturePlaygroundPng(g_shotPath.c_str())) {
            spdlog::info("B-repGeneratedLandscape: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("B-repGeneratedLandscape: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    spdlog::info("B-repGeneratedLandscape: cleanup()");
    g_brep.shutdown();
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
// --shot=path.png         capture the framebuffer after the cache settles, quit
// --zoom=Z                camera zoom (default: centerCamera's 1.0)
// --center=cx,cy          camera center in cell coords
//                         (default: bbox center of --nodes)
// --nodes="x,y;x,y;..."   paint these nodes (level 1) on startup
// --nodes2= / --nodes3=   same for plateau levels 2 and 3 (terraces)
// --height=H              plateau height in world units (default 3.0)
// --seed=N                rock displacement seed (default 1337)
// --style=cyclopean|block wall style (default cyclopean)
// --mat-dir=path          material set directory
//                         (default resources/textures/polyhaven/marble_cliff_01)
// --mat-set=name          material file prefix (default marble_cliff_01);
//                         every resources/textures/<vendor>/<name>/ dir with
//                         a color map is also switchable live from the
//                         Material panel combo
// --mat-tiling=T          material tiling repeats per world unit (default 0.35)
// --grass=path            grass-top albedo texture
//                         (default resources/textures/grass.png)
// --grass-tiling=T        grass tiling repeats per world unit (default 0.5)
// --relief=A              top relief amplitude, world units (default 0.25, 0=off)
// --relief-wavelength=L   top relief wavelength, cells (default 4.5)
// --wobble=A              rim wander amplitude, cells (default 0.2, 0=off)
// --wobble-wavelength=L   rim wander wavelength, cells (default 4.0)
// --batter=A              wall lean-back at the foot, cells (default 0.22)
// --foot-flare=A          extra foot swell, cells (default 0.12)
// --ledges=A              strata ledges amplitude, cells (default 0.06)
// --talus=W               talus apron width, cells (default 0.35, 0=off)
// --bevel=B               corner bevel 0..0.45 (default 0.3)
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
        if (arg.rfind("--nodes2=", 0) == 0) {
            g_cliNodes2 = parseNodesArg(arg.substr(9));
        }
        if (arg.rfind("--nodes3=", 0) == 0) {
            g_cliNodes3 = parseNodesArg(arg.substr(9));
        }
        if (arg.rfind("--height=", 0) == 0) {
            g_cliHeight = static_cast<float>(std::atof(arg.substr(9).c_str()));
        }
        if (arg.rfind("--seed=", 0) == 0) {
            g_cliSeed = std::atoi(arg.substr(7).c_str());
        }
        if (arg.rfind("--style=", 0) == 0) {
            const std::string style = arg.substr(8);
            if (style == "cyclopean") {
                g_cliStyle = 0;
            } else if (style == "block") {
                g_cliStyle = 1;
            }
        }
        if (arg.rfind("--mat-dir=", 0) == 0) {
            g_cliMatDir = arg.substr(10);
        }
        if (arg.rfind("--mat-set=", 0) == 0) {
            g_cliMatSet = arg.substr(10);
        }
        if (arg.rfind("--mat-tiling=", 0) == 0) {
            g_cliMatTiling = static_cast<float>(std::atof(arg.substr(13).c_str()));
        }
        if (arg.rfind("--grass=", 0) == 0) {
            g_cliGrassPath = arg.substr(8);
        }
        if (arg.rfind("--grass-tiling=", 0) == 0) {
            g_cliGrassTiling = static_cast<float>(std::atof(arg.substr(15).c_str()));
        }
        if (arg.rfind("--relief=", 0) == 0) {
            g_cliRelief = static_cast<float>(std::atof(arg.substr(9).c_str()));
        }
        if (arg.rfind("--relief-wavelength=", 0) == 0) {
            g_cliReliefWavelength = static_cast<float>(std::atof(arg.substr(20).c_str()));
        }
        if (arg.rfind("--wobble=", 0) == 0) {
            g_cliWobble = static_cast<float>(std::atof(arg.substr(9).c_str()));
        }
        if (arg.rfind("--wobble-wavelength=", 0) == 0) {
            g_cliWobbleWavelength = static_cast<float>(std::atof(arg.substr(20).c_str()));
        }
        if (arg.rfind("--batter=", 0) == 0) {
            g_cliBatter = static_cast<float>(std::atof(arg.substr(9).c_str()));
        }
        if (arg.rfind("--foot-flare=", 0) == 0) {
            g_cliFootFlare = static_cast<float>(std::atof(arg.substr(13).c_str()));
        }
        if (arg.rfind("--ledges=", 0) == 0) {
            g_cliLedges = static_cast<float>(std::atof(arg.substr(9).c_str()));
        }
        if (arg.rfind("--talus=", 0) == 0) {
            g_cliTalus = static_cast<float>(std::atof(arg.substr(8).c_str()));
        }
        if (arg.rfind("--bevel=", 0) == 0) {
            g_cliBevel = static_cast<float>(std::atof(arg.substr(8).c_str()));
        }
    }
    if (!g_cliMatDir.empty()) {
        g_matDir = g_cliMatDir;
    }
    if (!g_cliMatSet.empty()) {
        // The name resolves through the scanned list in init().
        g_matSet = g_cliMatSet;
    }
    if (g_cliMatTiling) {
        g_matTiling = *g_cliMatTiling;
    }
    if (!g_cliGrassPath.empty()) {
        g_grassPath = g_cliGrassPath;
    }
    if (g_cliGrassTiling) {
        g_grassTiling = *g_cliGrassTiling;
    }
    if (g_cliRelief) {
        g_genParams.deform.reliefAmp = *g_cliRelief;
    }
    if (g_cliReliefWavelength) {
        g_genParams.deform.reliefWavelength = *g_cliReliefWavelength;
    }
    if (g_cliWobble) {
        g_genParams.deform.wobbleAmp = *g_cliWobble;
    }
    if (g_cliWobbleWavelength) {
        g_genParams.deform.wobbleWavelength = *g_cliWobbleWavelength;
    }
    if (g_cliBatter) {
        g_genParams.wallBatter = *g_cliBatter;
    }
    if (g_cliFootFlare) {
        g_genParams.wallFootFlare = *g_cliFootFlare;
    }
    if (g_cliLedges) {
        g_genParams.wallLedgeAmp = *g_cliLedges;
    }
    if (g_cliTalus) {
        g_genParams.talusWidth = *g_cliTalus;
    }
    if (g_cliBevel) {
        g_genParams.cornerBevel = *g_cliBevel;
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        const bool ok = runBrepSmokeTest();
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
    desc.window_title = "B-repGeneratedLandscape - B-rep Landscape";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
