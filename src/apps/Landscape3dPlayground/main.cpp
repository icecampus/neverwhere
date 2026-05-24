#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <vector>

#include <imgui.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <spdlog/spdlog.h>
#include <topology_core/staggered_isometry.h>

#include "Landscape3dRenderer.h"
#include "TerrainScene.h"

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
    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;

    bool panning = false;
    bool brushEnabled = true;
    bool brushPainting = false;
    bool brushErasing = false;
    bool brushHoverValid = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    glm::vec3 startTarget{0.0f};
    glm::vec3 panAnchorWorld{0.0f};
    bool panHasAnchor = false;
    glm::vec3 brushWorld{0.0f};
    glm::vec2 brushField{0.0f};
    glm::ivec2 hoveredCell{-1, -1};
    glm::ivec2 hoveredNode{-1, -1};
    std::vector<glm::ivec2> brushTouchedNodes;
};

AppState g_state;
TerrainSceneSettings g_sceneSettings;
TerrainScene g_scene;
Landscape3dRenderParams g_renderParams;
Landscape3dCamera g_camera;
Landscape3dRenderer g_renderer;
bool g_needsMeshRebuild = true;
bool g_grassTextureLoaded = false;

constexpr float kFixedIsoYawDeg = Landscape3dCamera::fixedYawDeg;
constexpr float kFixedIsoPitchDeg = Landscape3dCamera::fixedPitchDeg;
constexpr float kFixedIsoDistance = Landscape3dCamera::fixedDistance;
constexpr float kDefaultOrthoScale = Landscape3dCamera::defaultOrthoScale;

bool looksLikeDataRoot(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(dir / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png", ec);
}

std::filesystem::path findDataRootUpwards(std::filesystem::path startDir) {
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

void applyFixedIsoCameraAngles() {
    g_camera.yawDeg = kFixedIsoYawDeg;
    g_camera.pitchDeg = kFixedIsoPitchDeg;
    g_camera.distance = kFixedIsoDistance;
    g_camera.perspective = false;
}

void resetIsoCameraView() {
    applyFixedIsoCameraAngles();
    g_camera.target = {0.0f, 0.0f, 0.0f};
    g_camera.orthoScale = kDefaultOrthoScale;
}

topology_core::StaggeredIsometry makeBrushIsometry() {
    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, g_renderParams.cubeSize);
    iso.dims.aspectRatio = Landscape3dCamera::editorGroundAspectRatio;
    return iso;
}

bool pickGroundPlaneAt(float screenX, float screenY, glm::vec3& world) {
    const int width = sapp_width();
    const int height = sapp_height();
    if (width <= 0 || height <= 0) return false;

    const float aspect = (float)width / (float)height;
    const glm::mat4 invMvp = glm::inverse(g_camera.projectionMatrix(aspect) * g_camera.viewMatrix());
    const float ndcX = (screenX / (float)width) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (screenY / (float)height) * 2.0f;

    glm::vec4 nearPoint = invMvp * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farPoint = invMvp * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (nearPoint.w == 0.0f || farPoint.w == 0.0f) return false;

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    const glm::vec3 rayStart = glm::vec3(nearPoint);
    const glm::vec3 rayEnd = glm::vec3(farPoint);
    const glm::vec3 ray = rayEnd - rayStart;
    if (std::abs(ray.y) < 0.0001f) return false;

    const float t = -rayStart.y / ray.y;
    if (t < 0.0f) return false;

    world = rayStart + ray * t;
    return true;
}

bool pickGroundAt(float screenX, float screenY, glm::vec3& world, glm::vec2& field, glm::ivec2& cell, glm::ivec2& node) {
    if (g_scene.gridSize() <= 0 || !pickGroundPlaneAt(screenX, screenY, world)) return false;

    topology_core::StaggeredIsometry iso = makeBrushIsometry();
    const glm::vec2 origin = iso.mapToField({g_scene.gridSize() / 2, g_scene.gridSize() / 2});
    field = glm::vec2(world.x, world.z) + origin;
    cell = iso.fieldToMap(field);
    node = iso.fieldToMap(field + glm::vec2(0.0f, iso.dims.cellSize().y * 0.5f));

    return node.x >= -1 && node.y >= 0 && node.x <= g_scene.gridSize() && node.y <= g_scene.gridSize() + 1;
}

void updateBrushHover(float screenX, float screenY) {
    g_state.mouseX = screenX;
    g_state.mouseY = screenY;
    g_state.brushHoverValid = pickGroundAt(
        screenX,
        screenY,
        g_state.brushWorld,
        g_state.brushField,
        g_state.hoveredCell,
        g_state.hoveredNode);
}

bool brushTouchedNode(const glm::ivec2& node) {
    return std::find(g_state.brushTouchedNodes.begin(), g_state.brushTouchedNodes.end(), node) !=
        g_state.brushTouchedNodes.end();
}

void markBrushTouchedNode(const glm::ivec2& node) {
    if (!brushTouchedNode(node)) {
        g_state.brushTouchedNodes.push_back(node);
    }
}

void applyBrushToHoveredNode(bool enabled) {
    if (!g_state.brushEnabled || !g_state.brushHoverValid) return;

    const glm::ivec2 node = g_state.hoveredNode;
    if (!enabled) {
        if (g_scene.setLandNodeLevel(node.x, node.y, 0)) {
            g_needsMeshRebuild = true;
        }
        return;
    }

    if (brushTouchedNode(node)) {
        return;
    }

    const std::uint8_t currentLevel = g_scene.landNodeLevelAt(node.x, node.y);
    const std::uint8_t nextLevel = std::min<std::uint8_t>((std::uint8_t)(currentLevel + 1), 2);
    markBrushTouchedNode(node);
    if (g_scene.setLandNodeLevel(node.x, node.y, nextLevel)) {
        g_needsMeshRebuild = true;
    }
}

void regenerateScene() {
    g_scene.generate(g_sceneSettings);
    g_needsMeshRebuild = true;
}

void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(430.0f, 620.0f), ImGuiCond_Once);

    ImGui::Begin("Landscape3dPlayground");
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
    ImGui::Separator();

    int gridSize = g_sceneSettings.gridSize;
    if (ImGui::SliderInt("Grid Size", &gridSize, 8, 96)) {
        g_sceneSettings.gridSize = gridSize;
        regenerateScene();
    }

    int minHeight = g_sceneSettings.minHeight;
    if (ImGui::SliderInt("Min Cubes", &minHeight, 0, 12)) {
        g_sceneSettings.minHeight = std::min(minHeight, g_sceneSettings.maxHeight - 1);
        regenerateScene();
    }

    int maxHeight = g_sceneSettings.maxHeight;
    if (ImGui::SliderInt("Max Cubes", &maxHeight, 2, 32)) {
        g_sceneSettings.maxHeight = std::max(maxHeight, g_sceneSettings.minHeight + 1);
        regenerateScene();
    }

    if (ImGui::InputInt("Seed", &g_sceneSettings.seed)) {
        regenerateScene();
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate")) {
        g_sceneSettings.seed += 1;
        regenerateScene();
    }

    float nodeThreshold = g_sceneSettings.nodeThreshold;
    if (ImGui::SliderFloat("Node Threshold", &nodeThreshold, 0.15f, 0.85f)) {
        g_sceneSettings.nodeThreshold = nodeThreshold;
        regenerateScene();
    }

    ImGui::Separator();
    ImGui::Text("Terrain Geometry");
    const char* terrainModes[] = {"Cubes Debug", "Valley Geometry"};
    int terrainMode = (int)g_renderParams.terrainMode;
    if (ImGui::Combo("Terrain Mode", &terrainMode, terrainModes, 2)) {
        g_renderParams.terrainMode = (Landscape3dTerrainMode)terrainMode;
        g_needsMeshRebuild = true;
    }

    if (ImGui::SliderFloat("Cube Size", &g_renderParams.cubeSize, 0.35f, 2.5f)) {
        g_needsMeshRebuild = true;
    }

    if (ImGui::SliderFloat("Height Step (cubes)", &g_renderParams.heightStepInCubes, 0.10f, 1.0f)) {
        g_needsMeshRebuild = true;
    }
    ImGui::Text("Level 2 max = %.2f cube height", g_renderParams.heightStepInCubes * 2.0f);

    int previewTileIndex = g_renderParams.previewTileIndex;
    if (ImGui::SliderInt("Preview Tile Index", &previewTileIndex, -1, 23)) {
        g_renderParams.previewTileIndex = previewTileIndex;
        g_needsMeshRebuild = true;
    }
    if (g_renderParams.previewTileIndex >= 0) {
        ImGui::Text("Preview Type: %s", tileTypeName(tileTypeFromAtlasIndex(g_renderParams.previewTileIndex)));
    } else {
        ImGui::Text("Preview Type: Generated nodes");
    }

    ImGui::Separator();
    ImGui::Text("Brush");
    ImGui::Checkbox("Brush Enabled", &g_state.brushEnabled);
    if (ImGui::Button("Clear Nodes")) {
        g_scene.clearLandNodes();
        g_needsMeshRebuild = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Regenerate Nodes")) {
        regenerateScene();
    }
    if (g_state.brushHoverValid) {
        ImGui::Text("Hovered Cell: %d, %d", g_state.hoveredCell.x, g_state.hoveredCell.y);
        ImGui::Text("Hovered Node: %d, %d [level %u]",
            g_state.hoveredNode.x,
            g_state.hoveredNode.y,
            (unsigned)g_scene.landNodeLevelAt(g_state.hoveredNode.x, g_state.hoveredNode.y));
        const auto affectedCells = TerrainScene::nodeNeighboursCells(g_state.hoveredNode);
        ImGui::Text("Affects: (%d,%d) (%d,%d) (%d,%d) (%d,%d)",
            affectedCells[0].x, affectedCells[0].y,
            affectedCells[1].x, affectedCells[1].y,
            affectedCells[2].x, affectedCells[2].y,
            affectedCells[3].x, affectedCells[3].y);
    } else {
        ImGui::Text("Hovered Node: none");
    }
    ImGui::Text("Controls: RMB drag pan, wheel zoom, LMB paint/raise");
    ImGui::Text("Erase: Ctrl+LMB or Ctrl+RMB");
    if (g_renderParams.previewTileIndex >= 0) {
        ImGui::Text("Preview mode ignores painted nodes");
    }

    ImGui::Separator();
    ImGui::Text("Lighting");
    ImGui::SliderFloat("Light Yaw", &g_renderParams.lightYawDeg, -180.0f, 180.0f);
    ImGui::SliderFloat("Light Pitch", &g_renderParams.lightPitchDeg, 5.0f, 85.0f);

    ImGui::Separator();
    ImGui::Text("Debug");
    const char* debugModes[] = {"Lit", "Top Texture", "Earth Sides", "Height", "Normals"};
    ImGui::Combo("Debug Mode", &g_renderParams.debugMode, debugModes, 5);
    ImGui::Checkbox("Use Grass Texture", &g_renderParams.useGrassTexture);
    ImGui::Checkbox("Wireframe Overlay", &g_renderParams.showWireframe);

    ImGui::Separator();
    applyFixedIsoCameraAngles();
    ImGui::Text("Iso Camera");
    ImGui::Text("Yaw/Pitch: %.1f / %.1f fixed", g_camera.yawDeg, g_camera.pitchDeg);
    bool targetChanged = false;
    targetChanged |= ImGui::DragFloat("Target X", &g_camera.target.x, 0.1f);
    targetChanged |= ImGui::DragFloat("Target Z", &g_camera.target.z, 0.1f);
    if (targetChanged) {
        g_camera.target.y = 0.0f;
    }
    ImGui::SliderFloat("Ortho Scale", &g_camera.orthoScale, 4.0f, 80.0f);
    if (ImGui::Button("Reset View")) {
        resetIsoCameraView();
    }

    ImGui::Separator();
    ImGui::Text("Triangles: %d", g_renderer.triangleCount());
    ImGui::Text("Lines: %d", g_renderer.lineCount());
    const Landscape3dTileStats& stats = g_renderer.tileStats();
    ImGui::Text("Tiles U/F/C/Lk/Ln/O: %d / %d / %d / %d / %d / %d",
        stats.unknown,
        stats.full,
        stats.corners,
        stats.lacks,
        stats.lines,
        stats.opposites);
    ImGui::Text("Grass texture: %s", g_grassTextureLoaded ? "loaded" : "fallback");
    ImGui::Text("Controls: RMB drag pan, MMB pan, wheel zoom");
    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Landscape3dPlayground: init()");

    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("Landscape3dPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");

    if (!g_state.gfxOk) return;

    simgui_desc_t imguiDesc = {};
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;

    g_renderer.init();
    resetIsoCameraView();
    const std::filesystem::path dataRoot = findDataRootUpwards(std::filesystem::current_path());
    const std::filesystem::path grassPath = dataRoot / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png";
    g_grassTextureLoaded = g_renderer.loadGrassTexture(grassPath);
    regenerateScene();
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
    g_state.lastTime = now;
    g_state.frameIndex++;

    if (!g_state.gfxOk) return;

    const int w = sapp_width();
    const int h = sapp_height();

    if (g_needsMeshRebuild) {
        g_renderer.rebuildMesh(g_scene, g_renderParams);
        g_needsMeshRebuild = false;
    }

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
    action.depth.load_action = SG_LOADACTION_CLEAR;
    action.depth.clear_value = 1.0f;
    action.stencil.load_action = SG_LOADACTION_CLEAR;

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    g_renderer.render(g_camera, g_renderParams, w, h);

    if (g_state.imguiOk) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("Landscape3dPlayground: cleanup()");
    g_renderer.shutdown();
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

    if (g_state.imguiOk) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            if (ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
                g_state.brushPainting = false;
                g_state.brushErasing = false;
                g_state.panning = false;
                g_state.panHasAnchor = false;
                g_state.brushTouchedNodes.clear();
            }
            return;
        }
    }

    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN: {
        updateBrushHover(ev->mouse_x, ev->mouse_y);
        const bool ctrl = (ev->modifiers & SAPP_MODIFIER_CTRL) != 0;
        const bool wantsBrush = g_state.brushEnabled &&
            g_renderParams.terrainMode == Landscape3dTerrainMode::ValleyGeometry &&
            (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT ||
             (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT && ctrl));

        if (wantsBrush) {
            g_state.brushPainting = true;
            g_state.brushErasing = ctrl || ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT;
            g_state.brushTouchedNodes.clear();
            applyBrushToHoveredNode(!g_state.brushErasing);
            break;
        }

        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT || ev->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
            g_state.panning = true;
            g_state.dragStartX = ev->mouse_x;
            g_state.dragStartY = ev->mouse_y;
            g_state.startTarget = g_camera.target;
            g_state.panHasAnchor = pickGroundPlaneAt(ev->mouse_x, ev->mouse_y, g_state.panAnchorWorld);
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT || ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.brushPainting = false;
            g_state.brushErasing = false;
            g_state.brushTouchedNodes.clear();
        }
        if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.panning = false;
            g_state.panHasAnchor = false;
        }
        if (ev->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
            g_state.panning = false;
            g_state.panHasAnchor = false;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE: {
        updateBrushHover(ev->mouse_x, ev->mouse_y);
        if (g_state.brushPainting) {
            applyBrushToHoveredNode(!g_state.brushErasing);
            break;
        }

        const float dx = ev->mouse_x - g_state.dragStartX;
        const float dy = ev->mouse_y - g_state.dragStartY;
        if (g_state.panning) {
            glm::vec3 currentWorld{0.0f};
            g_camera.target = g_state.startTarget;
            if (g_state.panHasAnchor && pickGroundPlaneAt(ev->mouse_x, ev->mouse_y, currentWorld)) {
                g_camera.target = g_state.startTarget + (g_state.panAnchorWorld - currentWorld);
                g_camera.target.y = 0.0f;
            } else {
                const float scale = g_camera.orthoScale / std::max(1.0f, (float)sapp_height());
                glm::vec3 groundRight = g_camera.right();
                glm::vec3 groundUp = g_camera.up();
                groundRight.y = 0.0f;
                groundUp.y = 0.0f;
                if (glm::length(groundRight) > 0.0001f) {
                    groundRight = glm::normalize(groundRight);
                }
                if (glm::length(groundUp) > 0.0001f) {
                    groundUp = glm::normalize(groundUp);
                }
                g_camera.target = g_state.startTarget + (-groundRight * dx + groundUp * dy) * scale * 2.0f;
            }
            updateBrushHover(ev->mouse_x, ev->mouse_y);
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_SCROLL: {
        if (ev->scroll_y == 0.0f) break;
        glm::vec3 beforeWorld{0.0f};
        const bool hasBefore = pickGroundPlaneAt(ev->mouse_x, ev->mouse_y, beforeWorld);

        const float zoom = (ev->scroll_y > 0.0f) ? 0.90f : 1.10f;
        g_camera.orthoScale = std::clamp(g_camera.orthoScale * zoom, 4.0f, 90.0f);
        applyFixedIsoCameraAngles();

        glm::vec3 afterWorld{0.0f};
        if (hasBefore && pickGroundPlaneAt(ev->mouse_x, ev->mouse_y, afterWorld)) {
            g_camera.target += beforeWorld - afterWorld;
            g_camera.target.y = 0.0f;
        }
        updateBrushHover(ev->mouse_x, ev->mouse_y);
        break;
    }
    default:
        break;
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
    desc.window_title = "Landscape3dPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}

