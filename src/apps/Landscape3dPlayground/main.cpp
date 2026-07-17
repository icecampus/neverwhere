#include "pch.h"

#include "LandscapeCellCatalog.h"
#include "LandscapeModel.h"
#include "LandscapePreview.h"
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

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

namespace {

using landscape3d::GridPoint;
using landscape3d::LandscapeCellCatalog;
using landscape3d::LandscapeCellCatalogSettings;
using landscape3d::LandscapeModel;
using landscape3d::LandscapePreview;
using landscape3d::LandscapePreviewSettings;

struct AppState {
    std::uint64_t lastTime = 0;
    float frameSeconds = 1.0f / 60.0f;
    int frameIndex = 0;
    bool graphicsReady = false;
    bool imguiReady = false;
    std::optional<GridPoint> hoveredNode;
    bool lastPreviewRendered = false;
};

AppState g_state;
LandscapeModel g_model;
LandscapeCellCatalog g_catalog;
LandscapeCellCatalogSettings g_catalogSettings;
LandscapePreview g_preview;
LandscapePreviewSettings g_previewSettings;

constexpr ImU32 kNodeLowColor = IM_COL32(196, 214, 226, 170);
constexpr ImU32 kNodeHighColor = IM_COL32(255, 220, 102, 245);
constexpr ImU32 kNodeLockedColor = IM_COL32(103, 114, 130, 120);
constexpr ImU32 kNodeHoverColor = IM_COL32(255, 132, 82, 255);

void resetLandscape(int width, int height, bool sample) {
    g_model.reset(width, height);
    if (sample) {
        g_model.loadSample();
    }
}

void rebuildCatalog() {
    g_catalog.rebuild(g_catalogSettings);
    spdlog::info(
        "Landscape3dPlayground: rebuilt 16 templates, highgroundHeight={}, wallSubdivisions={}x{}",
        g_catalogSettings.highgroundHeight,
        g_catalogSettings.wallHorizontalSubdivisions,
        g_catalogSettings.wallVerticalSubdivisions);
}

void drawNodeOverlay(
    ImDrawList* drawList,
    const ImVec2& origin,
    const ImVec2& viewportSize,
    const std::optional<GridPoint>& hovered) {

    const glm::vec2 size{viewportSize.x, viewportSize.y};
    for (int y = 0; y <= g_model.height(); ++y) {
        for (int x = 0; x <= g_model.width(); ++x) {
            const GridPoint node{x, y};
            const glm::vec2 local = g_preview.projectNode(g_model, g_previewSettings, size, node);
            if (local.x < -12.0f || local.y < -12.0f || local.x > viewportSize.x + 12.0f ||
                local.y > viewportSize.y + 12.0f) {
                continue;
            }

            const bool editable = g_model.isNodeEditable(node);
            const bool high = g_model.nodeIsHigh(node);
            const bool isHovered = hovered.has_value() && *hovered == node;
            const ImU32 color = isHovered ? kNodeHoverColor :
                !editable ? kNodeLockedColor :
                high ? kNodeHighColor : kNodeLowColor;
            const float radius = isHovered ? 5.5f : high ? 4.0f : 2.6f;
            const ImVec2 point{origin.x + local.x, origin.y + local.y};
            drawList->AddCircleFilled(point, radius, color);
            if (isHovered) {
                drawList->AddCircle(point, radius + 2.0f, IM_COL32(255, 255, 255, 235), 0, 1.2f);
            }
        }
    }
}

void drawPreviewCanvas(const ImVec2& origin, const ImVec2& viewportSize) {
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##VertexLandscapePreview", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 localMouse{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
    const glm::vec2 size{viewportSize.x, viewportSize.y};

    g_state.hoveredNode = hovered
        ? g_preview.pickNode(g_model, g_previewSettings, size, localMouse)
        : std::nullopt;

    if (hovered && io.MouseWheel != 0.0f) {
        const float previousZoom = g_previewSettings.zoom;
        const float nextZoom = std::clamp(previousZoom * (1.0f + io.MouseWheel * 0.12f), 0.28f, 7.0f);
        const float ratio = nextZoom / previousZoom;
        const glm::vec2 anchor{viewportSize.x * 0.5f, std::min(170.0f, std::max(96.0f, viewportSize.y * 0.34f))};
        g_previewSettings.pan += (localMouse - anchor - g_previewSettings.pan) * (1.0f - ratio);
        g_previewSettings.zoom = nextZoom;
    }

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        g_previewSettings.pan += glm::vec2{io.MouseDelta.x, io.MouseDelta.y};
    }

    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_state.hoveredNode.has_value()) {
        const bool erase = io.KeyCtrl;
        if (g_model.setNodeHigh(*g_state.hoveredNode, !erase)) {
            spdlog::debug(
                "Landscape3dPlayground: {} node ({}, {})",
                erase ? "erased" : "painted",
                g_state.hoveredNode->x,
                g_state.hoveredNode->y);
        }
    }

    g_state.lastPreviewRendered = g_preview.render(
        g_model,
        g_catalog,
        g_previewSettings,
        (int)std::max(1.0f, viewportSize.x),
        (int)std::max(1.0f, viewportSize.y));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);
    if (g_state.lastPreviewRendered && g_preview.validOutput()) {
        const ImTextureID texture = (ImTextureID)simgui_imtextureid_with_sampler(
            g_preview.outputView(),
            g_preview.outputSampler());
        drawList->AddImage(texture, origin, {origin.x + viewportSize.x, origin.y + viewportSize.y});
    } else {
        drawList->AddText(
            {origin.x + 16.0f, origin.y + 16.0f},
            IM_COL32(255, 138, 110, 255),
            "Sokol preview renderer is unavailable");
    }

    drawNodeOverlay(drawList, origin, viewportSize, g_state.hoveredNode);
    drawList->AddText(
        {origin.x + 12.0f, origin.y + 12.0f},
        IM_COL32(226, 232, 241, 255),
        "Vertex-centric 3D highground — 16 logical cell templates");
    drawList->AddText(
        {origin.x + 12.0f, origin.y + 32.0f},
        IM_COL32(164, 179, 196, 255),
        "LMB drag: paint highground   Ctrl+LMB: erase   RMB drag: pan   Wheel: zoom");
    drawList->AddText(
        {origin.x + 12.0f, origin.y + 52.0f},
        g_state.lastPreviewRendered ? IM_COL32(126, 220, 150, 255) : IM_COL32(235, 186, 90, 255),
        g_state.lastPreviewRendered ? "Renderer: Sokol offscreen" : "Renderer: unavailable");
    drawList->PopClipRect();
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(76, 87, 104, 255));
}

void drawControls(float width) {
    ImGui::Text("Binary Vertex Landscape");
    ImGui::TextWrapped(
        "Nodes are the only authored data. Each cell resolves one of the shared 16 "
        "Marching Squares types and instantiates a matching 3D cliff template.");
    ImGui::Separator();

    int gridWidth = g_model.width();
    int gridHeight = g_model.height();
    bool resetRequested = false;
    resetRequested |= ImGui::SliderInt("Grid Width", &gridWidth, 8, 64);
    resetRequested |= ImGui::SliderInt("Grid Height", &gridHeight, 8, 56);
    if (resetRequested) {
        resetLandscape(gridWidth, gridHeight, false);
    }
    if (ImGui::Button("Load sample highground", {width * 0.48f, 0.0f})) {
        g_model.loadSample();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", {width * 0.44f, 0.0f})) {
        g_model.clear();
    }

    ImGui::Separator();
    ImGui::Text("Cell template geometry");
    bool catalogDirty = false;
    catalogDirty |= ImGui::SliderFloat("Highground Height", &g_catalogSettings.highgroundHeight, 0.4f, 4.0f);
    catalogDirty |= ImGui::SliderInt(
        "Wall Horizontal Subdivisions",
        &g_catalogSettings.wallHorizontalSubdivisions,
        1,
        12);
    catalogDirty |= ImGui::SliderInt(
        "Wall Vertical Subdivisions",
        &g_catalogSettings.wallVerticalSubdivisions,
        1,
        12);
    if (catalogDirty) {
        rebuildCatalog();
    }

    ImGui::Separator();
    ImGui::Text("Production Preview lighting");
    ImGui::SliderFloat("Ambient", &g_previewSettings.ambient, 0.35f, 1.35f);
    ImGui::SliderFloat("Diffuse", &g_previewSettings.diffuseStrength, 0.0f, 0.85f);
    ImGui::SliderFloat("Wall Brightness", &g_previewSettings.wallBrightness, 0.45f, 1.65f);
    ImGui::SliderFloat("Texture Scale", &g_previewSettings.textureScale, 0.25f, 4.0f);
    ImGui::SliderFloat("Wall AO", &g_previewSettings.wallAoStrength, 0.0f, 0.85f);
    ImGui::SliderFloat("Wall Grain", &g_previewSettings.wallGrainStrength, 0.0f, 0.6f);
    if (ImGui::Button("Reset View")) {
        g_previewSettings.zoom = 1.0f;
        g_previewSettings.pan = {0.0f, 0.0f};
    }

    ImGui::Separator();
    ImGui::Text("Brush");
    if (g_state.hoveredNode.has_value()) {
        const GridPoint node = *g_state.hoveredNode;
        ImGui::Text(
            "Node: %d, %d  [%s]%s",
            node.x,
            node.y,
            g_model.nodeIsHigh(node) ? "high" : "low",
            g_model.isNodeEditable(node) ? "" : " (locked border)");
        const auto cells = g_model.affectedCells(node);
        ImGui::Text(
            "Affects: (%d,%d) (%d,%d) (%d,%d) (%d,%d)",
            cells[0].x,
            cells[0].y,
            cells[1].x,
            cells[1].y,
            cells[2].x,
            cells[2].y,
            cells[3].x,
            cells[3].y);
    } else {
        ImGui::TextDisabled("Hover a visible node to paint it.");
    }

    ImGui::Separator();
    ImGui::Text("Diagnostics");
    ImGui::Text("High nodes: %d", g_model.highNodeCount());
    ImGui::Text("Render quads: %d", g_preview.renderedQuadCount());
    ImGui::Text(
        "Unknown / Full / Corner / Lack / Line / Opposite: %d / %d / %d / %d / %d / %d",
        g_model.cellTypeCount(landscape_core::LandscapeTileType::Unknown),
        g_model.cellTypeCount(landscape_core::LandscapeTileType::Full),
        g_model.cellTypeCount(landscape_core::LandscapeTileType::RightCorner) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::LeftCorner) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::UpCorner) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::DownCorner),
        g_model.cellTypeCount(landscape_core::LandscapeTileType::DownLack) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::UpLack) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::RightLack) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::LeftLack),
        g_model.cellTypeCount(landscape_core::LandscapeTileType::RightDownLine) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::LeftDownLine) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::RightUpLine) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::LeftUpLine),
        g_model.cellTypeCount(landscape_core::LandscapeTileType::UpAndDownCorners) +
            g_model.cellTypeCount(landscape_core::LandscapeTileType::LeftRightCorners));
    ImGui::Text("Grass texture: %s", g_preview.grassTextureLoaded() ? "loaded" : "fallback");
    ImGui::Text("Rock texture: %s", g_preview.rockTextureLoaded() ? "loaded" : "fallback");
    ImGui::Text("Frame: %d (%.2f ms)", g_state.frameIndex, g_state.frameSeconds * 1000.0f);
}

void drawUi() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Landscape3dPlayground", nullptr, windowFlags);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float panelWidth = std::clamp(available.x * 0.31f, 330.0f, 420.0f);
    constexpr float gutter = 12.0f;
    const ImVec2 previewOrigin{origin.x + panelWidth + gutter, origin.y};
    const ImVec2 previewSize{
        std::max(1.0f, available.x - panelWidth - gutter),
        std::max(1.0f, available.y),
    };

    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginChild("LandscapeControls", {panelWidth, available.y}, true);
    drawControls(panelWidth - 18.0f);
    ImGui::EndChild();

    drawPreviewCanvas(previewOrigin, previewSize);
    ImGui::SetCursorScreenPos({origin.x, origin.y + available.y});
    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Landscape3dPlayground: init");
    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc graphicsDescription{};
    graphicsDescription.environment = sglue_environment();
    graphicsDescription.logger.func = slog_func;
    sg_setup(&graphicsDescription);
    g_state.graphicsReady = sg_isvalid();
    if (!g_state.graphicsReady) {
        spdlog::error("Landscape3dPlayground: sg_setup failed");
        return;
    }

    simgui_desc_t imguiDescription{};
    imguiDescription.max_vertices = 2u * 1024u * 1024u;
    imguiDescription.logger.func = slog_func;
    simgui_setup(&imguiDescription);
    g_state.imguiReady = true;

    resetLandscape(32, 24, true);
    rebuildCatalog();
    g_preview.init();
    runTestScenario(g_catalog);
}

void frame() {
    const std::uint64_t now = stm_now();
    g_state.frameSeconds = (float)stm_sec(stm_diff(now, g_state.lastTime));
    g_state.lastTime = now;
    g_state.frameIndex++;
    if (!g_state.graphicsReady) {
        return;
    }

    const int width = sapp_width();
    const int height = sapp_height();
    if (g_state.imguiReady) {
        simgui_frame_desc_t frameDescription{};
        frameDescription.width = width;
        frameDescription.height = height;
        frameDescription.delta_time = g_state.frameSeconds;
        frameDescription.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&frameDescription);
        drawUi();
    }

    sg_pass_action action{};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    sg_pass pass{};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    if (g_state.imguiReady) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("Landscape3dPlayground: cleanup");
    g_preview.shutdown();
    if (g_state.imguiReady) {
        simgui_shutdown();
        g_state.imguiReady = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* event) {
    if (g_state.imguiReady) {
        simgui_handle_event(event);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--smoke") {
            spdlog::set_level(spdlog::level::info);
            rebuildCatalog();
            return runTestScenario(g_catalog) ? 0 : 1;
        }
    }

    sapp_desc description{};
    description.init_cb = init;
    description.frame_cb = frame;
    description.cleanup_cb = cleanup;
    description.event_cb = event;
    description.width = 1280;
    description.height = 720;
    description.sample_count = 1;
    description.high_dpi = true;
    description.window_title = "Landscape3dPlayground — Vertex 3D Painter";
#if defined(_WIN32)
    description.win32.console_utf8 = true;
    description.win32.console_attach = true;
#endif
    description.logger.func = slog_func;
    sapp_run(&description);
    return 0;
}
