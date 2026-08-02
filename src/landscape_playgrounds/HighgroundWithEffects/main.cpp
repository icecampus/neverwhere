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

#include <highground_core/cliff_field.h>
#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "AtlasRenderer.h"
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

// Brush palette: independent layers sharing one canvas. Each keeps its
// own node grid and presentation (2D grass / 2D yellow / cliff or stone
// scalar-field).
struct PaintLayer {
    LandBrush brush;
    AtlasKind atlas;
    bool cliff;
    bool stone;
    const char* name;
};

PaintLayer g_layers[4] = {
    {{}, AtlasKind::Grass, false, false, "Grass 2D"},
    {{}, AtlasKind::Flat, false, false, "Yellow 2D"},
    {{}, AtlasKind::Flat, true, false, "Cliff 3D"},
    {{}, AtlasKind::Flat, false, true, "Stone 3D"},
};
int g_activeLayer = 0;
// Cliff layer: scalar-field params (heavy, debounced mesh rebuild) and the
// shading palette (uniforms only, instant). Mirrors CliffFieldPlayground.
cliff::FieldParams g_cliffParams;
// Stone layer: StoneCubePlayground voronoi stones over the same slab —
// its own params/height scale, same debounce mechanics.
stone_gen::StoneFieldParams g_stoneParams;
// Cliff layer lift: field px per 1.0 plateau height (cheap re-projection,
// no mesh rebuild).
float g_cliffHeightScale = 96.0f;
float g_stoneHeightScale = 96.0f;
// Stone rim shading: depth below the top plane over which the grass fades
// into the wall palette (uniforms only, applies instantly).
float g_stoneGrassFade = 0.12f;
// Stone rim shading: strength of the grass->stone gradient towards the wall
// across the rim band (baked per-vertex rim weight; uniform-only, instant).
float g_stoneRimShade = 1.0f;
// Stone top texture: mix over the procedural grass palette (0 = palette only)
// and tiling in tiles per world unit (uniform-only, instant).
float g_stoneTopTexMix = 1.0f;
float g_stoneTopTexTiles = 1.0f;
// Grass underlay: bottom canvas layer — the same tiling grass.png spread over
// the map grid, the highground stands on it. Toggle + grid-aligned tiling.
bool g_grassUnderlay = true;
float g_underlayTilesPerCell = 1.0f;
// Palette-only knobs of the cliff pass. The sun, the ambient/diffuse balance
// and the gamma moved to g_scene: the ground pass needs the very same values,
// and that shared light is what stitches the two together.
struct ShadingParams {
    float darkColor[3] = {0.38f, 0.38f, 0.42f};
    float goldColor[3] = {0.75f, 0.62f, 0.5f};
    float grassA[3] = {0.4f, 0.62f, 0.35f};
    float grassB[3] = {0.6f, 0.65f, 0.4f};
    float veinThreshold = 0.8f;
    float backLight = 0.1f;
    float specStrength = 0.5f;
    float specPower = 24.0f;
};
ShadingParams g_cliffShading;

// Sun, tone, contact AO and the shadow map — shared by both passes.
SceneStitchSettings g_scene;

// Seam materials: how the wall meets the ground and how the plateau keeps its
// distance from the ground it shares grass.png with. Uniform-only, instant.
struct SeamParams {
    float rimContactAo = 0.45f; // stone layers only (baked rim weight)
    float skirtHeight = 0.14f;  // scalar-field units above the ground plane
    float skirtFrequency = 5.0f;
    // Grass creeping up the wall: off by default. The noise mask cuts flat
    // green blotches into the rock that read as texture errors rather than
    // vegetation — the foot needs geometry (a prop brush), not a decal.
    float overgrowth = 0.0f;
    float topBrightness = 1.12f;
    float bounceStrength = 0.35f;
    float bounceTint[3] = {0.72f, 0.95f, 0.62f};
    float skyStrength = 0.2f;
    float skyTint[3] = {0.80f, 0.88f, 1.10f};
    // Plateau top material. It samples the very same grass.png as the ground,
    // so without its own tint, tiling and UV rotation the two read as one
    // continuous plane and the silhouette flips between a mound and a pit.
    float topTexture = 0.7f;
    float topTiling = 1.7f;
    float topRotation = 0.6f; // radians
    float topTint[3] = {0.86f, 0.94f, 0.80f};
    // How deep the mesh is pushed below the ground plane (re-projection only).
    float sink = 0.05f;
};
SeamParams g_seam;

// Boulder ring at the foot of the highground (mesh cache, debounced).
ScreeParams g_scree;

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
// --cliff-nodes=x,y;x,y;...    paint these nodes on the cliff layer
// --zoom=Z                     camera zoom (default: centerCamera's 1.0)
// --center=cx,cy               camera center in cell coords
//                              (default: bbox center of --cliff-nodes)
// --shot=path.png              capture the window client area to a PNG after
//                              the caches settle, then quit
bool g_noUi = false;
std::vector<glm::ivec2> g_cliCliffNodes;
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

// Painted programmatically in --demo mode: a cliff blob plus a stone blob
// plus grass/yellow flat strokes — used for visual verification.
void paintDemoPattern() {
    for (int y = 14; y <= 17; ++y) {
        for (int x = 15; x <= 18; ++x) {
            g_layers[2].brush.setNode({x, y}, true);
        }
    }
    g_layers[2].brush.setNode({14, 15}, true);
    for (int y = 6; y <= 9; ++y) {
        for (int x = 6; x <= 9; ++x) {
            g_layers[3].brush.setNode({x, y}, true);
        }
    }
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
    spdlog::info("HighgroundWithEffectsPlayground: init()");

    stm_setup();
    g_state.last_time = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfx_ok = sg_isvalid();
    if (!g_state.gfx_ok) {
        spdlog::error("HighgroundWithEffectsPlayground: sg_setup FAILED");
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
    for (const glm::ivec2& node : g_cliCliffNodes) {
        g_layers[2].brush.setNode(node, true);
    }

    g_dataRoot = findDataRootUpwards(std::filesystem::current_path());
    if (g_dataRoot.empty()) {
        spdlog::warn("HighgroundWithEffectsPlayground: data root not found, using CWD");
        g_dataRoot = std::filesystem::current_path();
    }
    spdlog::info("dataRoot={}", g_dataRoot.string());

    const auto atlasPath = g_dataRoot / "resources" / "assets" / "landscape" / "Grass" / "atlas.png";
    if (!g_renderer.loadAtlasFromFile(AtlasKind::Grass, atlasPath.string(), 4, 6)) {
        spdlog::error("HighgroundWithEffectsPlayground: Grass atlas missing at {}", atlasPath.string());
    }

    const FlatAtlasImage flat = generateFlatAtlas();
    if (!g_renderer.loadAtlasFromRgba(
            AtlasKind::Flat,
            flat.rgba.data(),
            flat.width,
            flat.height,
            flat.cols,
            flat.rows)) {
        spdlog::error("HighgroundWithEffectsPlayground: failed to upload generated flat atlas");
    }

    // Tiling texture for the stone/cliff flat tops (mix lives in
    // g_stoneTopTexMix; on failure keep the procedural palette only).
    const auto topTexPath = g_dataRoot / "resources" / "textures" / "grass.png";
    if (!g_renderer.loadTopTextureFromFile(topTexPath.string())) {
        g_stoneTopTexMix = 0.0f;
        g_grassUnderlay = false;
    }

    const glm::vec2 canvas = canvasSize();
    centerCamera(static_cast<int>(canvas.x), static_cast<int>(canvas.y));
    if (g_demoNode) {
        const glm::vec2 nodePos = g_iso.nodeToField({10, 10});
        g_camera.zoom = 2.0f;
        g_camera.offset.x = canvas.x * 0.5f - nodePos.x * g_camera.zoom;
        g_camera.offset.y = canvas.y * 0.5f - nodePos.y * g_camera.zoom;
    }
    if (g_cliZoom || g_cliCenter || !g_cliCliffNodes.empty()) {
        // Deterministic framing for screenshot comparisons.
        glm::vec2 worldCenter;
        if (g_cliCenter) {
            worldCenter = g_iso.mapToField(glm::ivec2(*g_cliCenter));
        } else {
            glm::vec2 acc(0.0f);
            for (const glm::ivec2& node : g_cliCliffNodes) {
                acc += g_iso.nodeToField(node);
            }
            worldCenter = acc / static_cast<float>(g_cliCliffNodes.size());
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
        "HighgroundWithEffectsPlayground",
        nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Vertex land brush");
    ImGui::Text("LMB paint  |  Ctrl+LMB erase  |  RMB pan  |  wheel zoom");
    ImGui::Separator();

    ImGui::Text("Brush palette:");
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        ImGui::RadioButton(g_layers[i].name, &g_activeLayer, i);
    }
    ImGui::Checkbox("Grass underlay", &g_grassUnderlay);
    if (g_grassUnderlay) {
        ImGui::SliderFloat("Underlay tiling", &g_underlayTilesPerCell, 0.25f, 4.0f);
    }
    if (g_layers[g_activeLayer].cliff) {
        // Scalar-field surface nets: edits are debounced (0.3 s) into a full
        // field rebuild — continuous slider drags just keep postponing it.
        cliff::FieldParams& p = g_cliffParams;
        ShadingParams& sh = g_cliffShading;
        // Lift scale: cheap re-projection of the cached mesh, no field rebuild.
        ImGui::SliderFloat("Cliff height", &g_cliffHeightScale, 4.0f, 128.0f, "%.0f px");
        if (ImGui::CollapsingHeader("Field", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Ground-slab underlay toggle: off by default (standalone
            // highground); a field param, so this goes through the debounce.
            ImGui::Checkbox("Ground slab", &p.groundEnabled);
            ImGui::SliderFloat("Cell size", &p.cellSize, 0.03f, 0.07f, "%.3f");
            ImGui::TextDisabled("(applied after a 0.3 s edit pause)");
            ImGui::SliderFloat("Plateau height", &p.plateauHeight, 0.4f, 2.0f);
            ImGui::SliderInt("Blur passes", &p.blurPasses, 0, 6);
            ImGui::SliderFloat("Edge radius", &p.edgeRadius, 0.0f, 0.12f);
            ImGui::SliderFloat("Fbm amplitude", &p.fbmAmplitude, 0.0f, 0.08f);
            ImGui::SliderFloat("Fbm frequency", &p.fbmFrequency, 2.0f, 10.0f);
            ImGui::SliderInt("Fbm octaves", &p.fbmOctaves, 1, 3);
        }
        if (ImGui::CollapsingHeader("Grooves", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Period", &p.groovePeriod, 0.2f, 0.8f);
            ImGui::SliderFloat("Depth", &p.grooveDepthMax, 0.02f, 0.2f);
            ImGui::SliderFloat("Mask width", &p.grooveMaskWidth, 0.05f, 0.6f);
            ImGui::SliderFloat("Fade K", &p.grooveFadeK, 0.2f, 2.0f);
            ImGui::SliderFloat("Rim fade", &p.grooveRimFade, 0.0f, 0.4f);
            ImGui::SliderFloat("Smooth radius", &p.grooveSmooth, 0.005f, 0.06f);
            ImGui::SliderFloat("Phase", &p.groovePhase, 0.0f, 0.8f);
        }
        if (ImGui::CollapsingHeader("Shading", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Uniform-only: instant, no mesh rebuild.
            ImGui::SliderFloat("Vein threshold", &sh.veinThreshold, 0.6f, 0.95f);
            ImGui::ColorEdit3("Dark color", sh.darkColor);
            ImGui::ColorEdit3("Gold color", sh.goldColor);
            ImGui::ColorEdit3("Grass A", sh.grassA);
            ImGui::ColorEdit3("Grass B", sh.grassB);
            ImGui::SliderFloat("Spec strength", &sh.specStrength, 0.0f, 1.5f);
            ImGui::SliderFloat("Spec power", &sh.specPower, 4.0f, 64.0f);
            ImGui::TextDisabled("(sun / ambient / gamma: Scene section)");
        }
        const CliffStats& st = g_renderer.cliffStatsFor(&g_layers[g_activeLayer].brush);
        ImGui::Text("Cliff mesh: %d verts, %d tris", st.vertexCount, st.triangleCount);
        ImGui::Text("Watertight: %s", st.watertight ? "yes" : "NO");
        ImGui::Text("Rebuild: %.0f ms (%d voxels)", st.rebuildMs, st.voxelCount);
        if (st.pending) {
            ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Rebuilding...");
        }
    }
    if (g_layers[g_activeLayer].stone) {
        // StoneCubePlayground voronoi stones over the same slab; edits are
        // debounced (0.3 s) into a full field rebuild, same as the cliff.
        stone_gen::StoneFieldParams& p = g_stoneParams;
        ImGui::SliderFloat("Stone height", &g_stoneHeightScale, 4.0f, 128.0f, "%.0f px");
        if (ImGui::CollapsingHeader("Stones", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("(applied after a 0.3 s edit pause)");
            ImGui::SliderFloat("Stone size", &p.voroScale, 1.0f, 6.0f);
            ImGui::SliderFloat("Jitter", &p.cellJitter, 0.0f, 1.0f);
            ImGui::SliderFloat("Groove depth", &p.grooveDepth, 0.0f, 0.2f);
            ImGui::SliderFloat("Groove K", &p.grooveK, 0.5f, 6.0f);
            ImGui::SliderFloat("Mask width", &p.grooveMaskWidth, 0.05f, 0.6f);
            ImGui::SliderFloat("Seed", &p.seed, 0.0f, 16.0f);
            ImGui::SliderInt("Blur passes", &p.blurPasses, 0, 4);
            ImGui::Checkbox("Flat top", &p.flatTop);
            if (p.flatTop) {
                ImGui::SliderFloat("Top fade lo", &p.flatTopLo, 0.0f, 1.0f);
                ImGui::SliderFloat("Top fade hi", &p.flatTopHi, 0.0f, 1.0f);
                ImGui::SliderFloat("Rim width", &p.rimWidth, 0.05f, 1.0f);
                ImGui::SliderFloat("Rim bulge", &p.rimBulge, 0.0f, 2.0f);
                ImGui::SliderFloat("Rim notch", &p.rimNotch, 0.0f, 0.15f);
                ImGui::SliderFloat("Grass fade", &g_stoneGrassFade, 0.02f, 0.3f);
                ImGui::SliderFloat("Rim shade", &g_stoneRimShade, 0.0f, 1.0f);
                ImGui::SliderFloat("Top texture", &g_stoneTopTexMix, 0.0f, 1.0f);
                ImGui::SliderFloat("Tex tiling", &g_stoneTopTexTiles, 0.1f, 4.0f);
            }
        }
        if (ImGui::CollapsingHeader("Field", ImGuiTreeNodeFlags_DefaultOpen)) {
            cliff::FieldParams& b = p.base;
            ImGui::Checkbox("Ground slab", &b.groundEnabled);
            ImGui::SliderFloat("Cell size", &b.cellSize, 0.03f, 0.07f, "%.3f");
            ImGui::SliderFloat("Plateau height", &b.plateauHeight, 0.4f, 2.0f);
            ImGui::SliderFloat("Edge radius", &b.edgeRadius, 0.0f, 0.12f);
            ImGui::SliderFloat("Fbm amplitude", &p.fbmAmplitude, 0.0f, 0.08f);
            ImGui::SliderFloat("Fbm frequency", &p.fbmFrequency, 2.0f, 10.0f);
        }
        const CliffStats& st = g_renderer.cliffStatsFor(&g_layers[g_activeLayer].brush);
        ImGui::Text("Stone mesh: %d verts, %d tris", st.vertexCount, st.triangleCount);
        ImGui::Text("Watertight: %s", st.watertight ? "yes" : "NO");
        ImGui::Text("Rebuild: %.0f ms (%d voxels)", st.rebuildMs, st.voxelCount);
        if (st.pending) {
            ImGui::TextColored({1.0f, 0.8f, 0.2f, 1.0f}, "Rebuilding...");
        }
    }
    ImGui::Separator();
    // Everything that ties the highground to the ground it stands on. All of
    // it is uniform- or re-projection-only except the scree ring, which goes
    // through the mesh debounce.
    if (ImGui::CollapsingHeader("Scene / Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        SceneStitchSettings& sc = g_scene;
        ImGui::SliderAngle("Sun azimuth", &sc.lightAzimuth, -180.0f, 180.0f);
        ImGui::SliderAngle("Sun elevation", &sc.lightElevation, 5.0f, 89.0f);
        ImGui::SliderFloat("Ambient", &sc.ambient, 0.05f, 0.8f);
        ImGui::SliderFloat("Diffuse", &sc.diffuse, 0.0f, 1.2f);
        ImGui::SliderFloat("Gamma", &sc.gamma, 0.5f, 1.5f);
        ImGui::Checkbox("Light the ground", &sc.groundLit);

        ImGui::Separator();
        ImGui::Checkbox("Contact AO", &sc.aoEnabled);
        if (sc.aoEnabled) {
            ImGui::SliderFloat("AO strength", &sc.aoStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("AO radius", &sc.aoRadius, 0.05f, 2.0f, "%.2f cells");
            ImGui::SliderFloat("AO up the wall", &sc.aoWallFade, 0.0f, 1.0f);
        }

        ImGui::Separator();
        ImGui::Checkbox("Shadows", &sc.shadowsEnabled);
        if (sc.shadowsEnabled) {
            ImGui::SliderFloat("Shadow strength", &sc.shadowStrength, 0.0f, 1.0f);
            ImGui::SliderFloat("Shadow bias", &sc.shadowBias, 0.0f, 0.02f, "%.4f");
            if (!g_renderer.shadowMapReady()) {
                ImGui::TextColored({1.0f, 0.5f, 0.3f, 1.0f}, "shadow target unavailable");
            }
        }
    }
    if (ImGui::CollapsingHeader("Seam / Scree")) {
        SeamParams& sm = g_seam;
        ImGui::SliderFloat("Sink", &sm.sink, 0.0f, 0.3f);
        ImGui::SliderFloat("Skirt height", &sm.skirtHeight, 0.0f, 0.5f);
        ImGui::SliderFloat("Skirt frequency", &sm.skirtFrequency, 1.0f, 16.0f);
        ImGui::SliderFloat("Overgrowth", &sm.overgrowth, 0.0f, 1.0f);
        ImGui::SliderFloat("Rim contact AO", &sm.rimContactAo, 0.0f, 1.0f);
        ImGui::SliderFloat("Top brightness", &sm.topBrightness, 0.7f, 1.5f);
        ImGui::SliderFloat("Top texture", &sm.topTexture, 0.0f, 1.0f);
        ImGui::SliderFloat("Top tiling", &sm.topTiling, 0.2f, 4.0f);
        ImGui::SliderAngle("Top UV rotation", &sm.topRotation, 0.0f, 90.0f);
        ImGui::ColorEdit3("Top tint", sm.topTint);
        ImGui::SliderFloat("Grass bounce", &sm.bounceStrength, 0.0f, 1.0f);
        ImGui::ColorEdit3("Bounce tint", sm.bounceTint);
        ImGui::SliderFloat("Sky tint", &sm.skyStrength, 0.0f, 1.0f);
        ImGui::ColorEdit3("Sky color", sm.skyTint);

        ImGui::Separator();
        ImGui::Checkbox("Scree ring", &g_scree.enabled);
        if (g_scree.enabled) {
            ImGui::TextDisabled("(applied after a 0.3 s edit pause)");
            ImGui::SliderInt("Scree density", &g_scree.perCell, 1, 12);
            ImGui::SliderFloat("Scree band", &g_scree.band, 0.2f, 3.0f, "%.2f cells");
            ImGui::SliderFloat("Scree min size", &g_scree.sizeMin, 0.02f, 0.3f);
            ImGui::SliderFloat("Scree max size", &g_scree.sizeMax, 0.05f, 0.5f);
            ImGui::SliderFloat("Scree buried", &g_scree.buried, 0.0f, 0.9f);
            ImGui::SliderFloat("Scree seed", &g_scree.seed, 0.0f, 16.0f);
            const CliffStats& sst = g_renderer.cliffStatsFor(&g_layers[g_activeLayer].brush);
            ImGui::Text("Scree boulders: %d", sst.screeCount);
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
    ImGui::Text("On nodes: %s %d, %s %d, %s %d, %s %d",
        g_layers[0].name,
        g_layers[0].brush.onNodeCount(),
        g_layers[1].name,
        g_layers[1].brush.onNodeCount(),
        g_layers[2].name,
        g_layers[2].brush.onNodeCount(),
        g_layers[3].name,
        g_layers[3].brush.onNodeCount());
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

    // The scene renders only into the canvas right of the panel. This is the
    // one place that leaves logical points for framebuffer pixels; sokol_imgui
    // resets viewport and scissor to the full framebuffer for its own draws,
    // so no restore is needed.
    const int canvasW = std::max(w - static_cast<int>(panelWidth()), 0);
    const int vpX = static_cast<int>(std::lround(panelWidth() * dpi));
    const int vpW = static_cast<int>(std::lround(canvasW * dpi));
    const int vpH = static_cast<int>(std::lround(h * dpi));

    PaintLayerView views[4];
    for (int i = 0; i < 4; ++i) {
        views[i].brush = &g_layers[i].brush;
        views[i].atlas = g_layers[i].atlas;
        views[i].cliff = g_layers[i].cliff;
        views[i].cliffParams = &g_cliffParams;
        views[i].stone = g_layers[i].stone;
        views[i].stoneParams = &g_stoneParams;
        views[i].cliffHeightScale = g_layers[i].stone ? g_stoneHeightScale : g_cliffHeightScale;
        views[i].sink = g_seam.sink;
        views[i].scree = &g_scree;
    }

    // Cliff shading uniforms: palette from the UI state. The sun and the tone
    // come from the shared scene block (SceneStitchParams), so both passes see
    // literally the same values; the view direction is constant for the iso
    // camera (viewer -> scene, mirrors CliffFieldPlayground's
    // rd = normalize(p - camPos)).
    const ShadingParams& s = g_cliffShading;
    CliffFsParams cliffFs{};
    const float halfH = g_iso.dims.cellSize().y * 0.5f;
    const float viewY = 2.0f * halfH / std::max(g_cliffHeightScale, 1.0f);
    const float viewLen = std::sqrt(2.0f + viewY * viewY);
    cliffFs.viewDir[0] = -1.0f / viewLen;
    cliffFs.viewDir[1] = -viewY / viewLen;
    cliffFs.viewDir[2] = -1.0f / viewLen;
    std::memcpy(cliffFs.darkColor, s.darkColor, sizeof(s.darkColor));
    std::memcpy(cliffFs.goldColor, s.goldColor, sizeof(s.goldColor));
    std::memcpy(cliffFs.grassA, s.grassA, sizeof(s.grassA));
    std::memcpy(cliffFs.grassB, s.grassB, sizeof(s.grassB));
    cliffFs.params0[0] = s.veinThreshold;
    cliffFs.params0[1] = g_scene.ambient;
    cliffFs.params0[2] = g_scene.diffuse;
    cliffFs.params0[3] = s.specStrength;
    cliffFs.params1[0] = s.specPower;
    cliffFs.params1[1] = g_scene.gamma;
    cliffFs.params1[2] = s.backLight;
    // Seam materials. params3[1] (height -> world) and params5[3] (ground
    // plane) are per-layer and get filled by the renderer.
    cliffFs.params2[2] = g_seam.topTexture;
    cliffFs.params2[3] = g_seam.topTiling;
    cliffFs.params3[0] = g_seam.rimContactAo;
    cliffFs.params3[2] = g_seam.skirtHeight;
    cliffFs.params3[3] = g_seam.skirtFrequency;
    cliffFs.params4[0] = g_seam.overgrowth;
    cliffFs.params4[1] = g_seam.topBrightness;
    cliffFs.params4[2] = g_seam.bounceStrength;
    cliffFs.params4[3] = g_seam.skyStrength;
    std::memcpy(cliffFs.params5, g_seam.bounceTint, sizeof(g_seam.bounceTint));
    std::memcpy(cliffFs.params6, g_seam.skyTint, sizeof(g_seam.skyTint));
    cliffFs.params6[3] = g_seam.topRotation;
    std::memcpy(cliffFs.params7, g_seam.topTint, sizeof(g_seam.topTint));
    cliffFs.params7[3] = g_scene.aoEnabled ? g_scene.aoWallFade : 0.0f;

    // Stone palette (per-layer shading override): the same omphalos shader,
    // re-tinted to gray granite — dark groove floors, plain stone faces, no
    // gold veins, weak spec; grassy tops stay.
    CliffFsParams stoneFs = cliffFs;
    stoneFs.darkColor[0] = 0.16f;
    stoneFs.darkColor[1] = 0.16f;
    stoneFs.darkColor[2] = 0.18f;
    stoneFs.goldColor[0] = 0.55f;
    stoneFs.goldColor[1] = 0.53f;
    stoneFs.goldColor[2] = 0.48f;
    stoneFs.params0[0] = 2.0f;  // vein threshold above the fbm range: veins off
    stoneFs.params0[3] = 0.15f; // spec strength
    // Rim stitch shading: the flat top sits exactly at plateauHeight +
    // edgeRadius; above it (+eps in the shader) boulders keep the wall
    // palette, below it the grass yields to stone over the fade depth.
    stoneFs.params1[3] = g_stoneParams.base.plateauHeight + g_stoneParams.base.edgeRadius;
    stoneFs.params2[0] = g_stoneGrassFade;
    stoneFs.params2[1] = g_stoneRimShade;
    stoneFs.params2[2] = g_stoneTopTexMix;
    stoneFs.params2[3] = g_stoneTopTexTiles;
    for (int i = 0; i < 4; ++i) {
        if (g_layers[i].stone) {
            views[i].shadingOverride = &stoneFs;
        }
    }

    const UnderlayParams underlay{g_grassUnderlay, g_underlayTilesPerCell};

    SceneFrame sceneFrame;
    sceneFrame.layers = views;
    sceneFrame.layerCount = 4;
    sceneFrame.iso = &g_iso;
    sceneFrame.camera = &g_camera;
    sceneFrame.viewW = canvasW;
    sceneFrame.viewH = h;
    sceneFrame.hoverNode = g_hoverNode.value_or(glm::ivec2{-1, -1});
    sceneFrame.hasHover = g_hoverNode.has_value();
    sceneFrame.cliffShading = &cliffFs;
    sceneFrame.underlay = &underlay;
    sceneFrame.stitch = &g_scene;
    sceneFrame.nowSec = stm_sec(stm_now());

    // Offscreen work (mesh caches, AO field, shadow map) opens its own passes,
    // so it has to happen before the swapchain pass.
    g_renderer.prepare(sceneFrame);

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

    sg_apply_viewport(vpX, 0, vpW, vpH, true);
    sg_apply_scissor_rect(vpX, 0, vpW, vpH, true);

    g_renderer.render(sceneFrame);

    if (g_state.imgui_ok) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();

    // Headless capture: the mesh caches are debounced on wall time and the
    // rebuild itself takes a while, so wait for every painted 3D layer to
    // actually have geometry rather than for a frame count — under llvmpipe
    // frames fly by and a plain counter grabs the flat preview tiles. The
    // frame cap is the escape hatch for scenes that never produce a mesh.
    bool meshesReady = true;
    for (int i = 0; i < 4 && meshesReady; ++i) {
        if ((!g_layers[i].cliff && !g_layers[i].stone)
            || g_layers[i].brush.onNodeCount() == 0) {
            continue;
        }
        const CliffStats& stats = g_renderer.cliffStatsFor(&g_layers[i].brush);
        meshesReady = !stats.pending && stats.triangleCount > 0;
    }
    if (!g_shotPath.empty() && g_state.frame_index >= 120
        && (meshesReady || g_state.frame_index >= 900)) {
        if (capturePlaygroundPng(g_shotPath.c_str())) {
            spdlog::info("HighgroundWithEffectsPlayground: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("HighgroundWithEffectsPlayground: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    spdlog::info("HighgroundWithEffectsPlayground: cleanup()");
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
    // Playground default: no ground-slab underlay under the cliff mesh — the
    // highground stands alone; the underlay will be authored separately.
    g_cliffParams.groundEnabled = false;
    g_stoneParams.base.groundEnabled = false;
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
        if (arg.rfind("--cliff-nodes=", 0) == 0) {
            g_cliCliffNodes = parseNodesArg(arg.substr(14));
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
    desc.window_title = "HighgroundWithEffectsPlayground - Vertex Land Tiles";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
