#include "PlaygroundUi.h"

#include "LandscapeBowlScenario.h"
#include "MeshPreview.h"
#include "PlaygroundState.h"
#include "RectangleCliffScenario.h"

#include <algorithm>
#include <mutex>

#include <imgui.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

void drawFrameStats() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
}

void drawRectangleScenarioControls(float panelWidth) {
    ImGui::PushItemWidth(panelWidth - 24.0f);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Debug Scene");
    ImGui::TextWrapped("Debug scenario for boundary vertices, cutouts, bevels and rocky wall stitching.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= ImGui::SliderInt("Grid Width", &g_rectSettings.gridWidth, 4, 48);
        changed |= ImGui::SliderInt("Grid Height", &g_rectSettings.gridHeight, 4, 32);
        changed |= ImGui::SliderInt("Rect X", &g_rectSettings.rectX, 0, g_rectSettings.gridWidth - 1);
        changed |= ImGui::SliderInt("Rect Y", &g_rectSettings.rectY, 0, g_rectSettings.gridHeight - 1);
        changed |= ImGui::SliderInt("Rect Width", &g_rectSettings.rectWidth, 1, g_rectSettings.gridWidth);
        changed |= ImGui::SliderInt("Rect Height", &g_rectSettings.rectHeight, 1, g_rectSettings.gridHeight);
        changed |= ImGui::Checkbox("Enable Cutout (inner corners)", &g_rectSettings.enableCutout);
        changed |= ImGui::SliderInt("Cutout X", &g_rectSettings.cutoutX, 0, g_rectSettings.gridWidth - 1);
        changed |= ImGui::SliderInt("Cutout Y", &g_rectSettings.cutoutY, 0, g_rectSettings.gridHeight - 1);
        changed |= ImGui::SliderInt("Cutout Width", &g_rectSettings.cutoutWidth, 1, g_rectSettings.gridWidth);
        changed |= ImGui::SliderInt("Cutout Height", &g_rectSettings.cutoutHeight, 1, g_rectSettings.gridHeight);
        changed |= ImGui::SliderFloat("Cliff Height", &g_rectSettings.cliffHeight, 0.25f, 8.0f);
        changed |= ImGui::SliderFloat("Corner Bevel", &g_rectSettings.cornerBevel, 0.0f, 0.45f);
        changed |= ImGui::Checkbox("Rock Noise Enabled", &g_rectSettings.rockEnabled);
        changed |= ImGui::InputInt("Rock Seed", &g_rectSettings.rockSeed);
        changed |= ImGui::SliderFloat("Rock Scale", &g_rectSettings.rockScale, 0.25f, 24.0f);
        changed |= ImGui::SliderFloat("Rock Amplitude", &g_rectSettings.rockAmplitude, 0.0f, 1.25f);
        changed |= ImGui::SliderInt("Wall Horizontal Subdivs", &g_rectSettings.wallHorizontalSubdivisions, 1, 16);
        changed |= ImGui::SliderInt("Wall Vertical Subdivs", &g_rectSettings.wallVerticalSubdivisions, 1, 16);
        changed |= ImGui::SliderInt("Terrace Steps", &g_rectSettings.terraceSteps, 0, 12);
        ImGui::Checkbox("Show Top Faces", &g_rectSettings.showTopFaces);
        ImGui::Checkbox("Show Cliff Walls", &g_rectSettings.showCliffWalls);
        ImGui::Checkbox("Show Mesh Wireframe", &g_rectSettings.showMeshWireframe);
        ImGui::Checkbox("Show Cell Labels", &g_rectSettings.showCellLabels);
        ImGui::Checkbox("Show Vertex Labels", &g_rectSettings.showVertexLabels);
    }
    if (changed) {
        rebuildRectangleCliffModel();
        rebuildLandscapeBowlModel();
    }

    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::Text("Solid cells: %d", g_rectModel.solidCellCount);
        ImGui::Text("Boundary segments: %d", (int)g_rectModel.boundarySegments.size());
        ImGui::Text("3D quads top/walls/total: %d / %d / %d",
            g_rectModel.topQuadCount,
            g_rectModel.cliffWallQuadCount,
            (int)g_rectModel.meshQuads.size());
        ImGui::Text("Shared bevel segments/caps: %d / %d",
            g_rectModel.beveledSegmentCount,
            g_rectModel.cornerCapCount);
        ImGui::Text("Rock mode: %s", g_rectSettings.rockEnabled ? "FastNoise2 ridged" : "flat walls");
        ImGui::Text("Vertices E/O/I/D: %d / %d / %d / %d",
            g_rectModel.edgeVertexCount,
            g_rectModel.outerCornerCount,
            g_rectModel.innerCornerCount,
            g_rectModel.diagonalJoinCount);
    }
    ImGui::TextColored(ImVec4(0.36f, 0.70f, 1.0f, 1.0f), "E = straight edge vertex");
    ImGui::TextColored(ImVec4(0.98f, 0.77f, 0.28f, 1.0f), "O = outer corner");
    ImGui::TextColored(ImVec4(0.91f, 0.36f, 0.36f, 1.0f), "I = inner corner");
    ImGui::TextColored(ImVec4(0.73f, 0.46f, 1.0f, 1.0f), "D = diagonal join");
    ImGui::PopItemWidth();
}

void drawLandscapeScenarioControls(float panelWidth) {
    ImGui::PushItemWidth(panelWidth - 24.0f);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Production Preview");
    ImGui::TextWrapped("Clean landscape preview for visual iteration: flat clearing, terraced high-ground arc and hills with production-style lighting.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= ImGui::SliderInt("Grid Width", &g_landscapeSettings.gridWidth, 12, 72);
        changed |= ImGui::SliderInt("Grid Height", &g_landscapeSettings.gridHeight, 10, 56);
        changed |= ImGui::InputInt("Seed", &g_landscapeSettings.seed);
        changed |= ImGui::SliderFloat("Clearing Radius", &g_landscapeSettings.clearingRadius, 2.0f, 18.0f);
        changed |= ImGui::SliderFloat("Clearing Softness", &g_landscapeSettings.clearingSoftness, 0.25f, 8.0f);
        changed |= ImGui::SliderFloat("High Ground Radius", &g_landscapeSettings.highGroundRadius, 3.0f, 28.0f);
        changed |= ImGui::SliderFloat("High Ground Width", &g_landscapeSettings.highGroundWidth, 1.0f, 10.0f);
        changed |= ImGui::SliderFloat("High Ground Height", &g_landscapeSettings.highGroundHeight, 0.5f, 8.0f);
        changed |= ImGui::SliderInt("Height Levels", &g_landscapeSettings.heightLevels, 2, 6);
        changed |= ImGui::SliderFloat("Arc Noise Scale", &g_landscapeSettings.arcNoiseScale, 0.5f, 18.0f);
        changed |= ImGui::SliderFloat("Arc Noise Amplitude", &g_landscapeSettings.arcNoiseAmplitude, 0.0f, 5.0f);
        changed |= ImGui::SliderInt("Hill Count", &g_landscapeSettings.hillCount, 0, 12);
        changed |= ImGui::SliderFloat("Hill Height", &g_landscapeSettings.hillHeight, 0.0f, 5.0f);
        changed |= ImGui::SliderFloat("Hill Radius", &g_landscapeSettings.hillRadius, 0.75f, 8.0f);
        ImGui::Checkbox("Show Top Faces", &g_landscapeSettings.showTopFaces);
        ImGui::Checkbox("Show Cliff Walls", &g_landscapeSettings.showCliffWalls);
        ImGui::Checkbox("Show Level Labels", &g_landscapeSettings.showHeightValues);
        ImGui::Separator();
        ImGui::Text("Rendering");
        ImGui::Checkbox("Use GPU Renderer", &g_productionPreviewSettings.useGpuRenderer);
        ImGui::SliderFloat("Ambient", &g_productionPreviewSettings.ambient, 0.35f, 1.35f);
        ImGui::SliderFloat("Diffuse Strength", &g_productionPreviewSettings.diffuseStrength, 0.0f, 0.85f);
        ImGui::SliderFloat("Wall Brightness", &g_productionPreviewSettings.wallBrightness, 0.45f, 1.65f);
        ImGui::SliderFloat("Texture Scale", &g_productionPreviewSettings.textureScale, 0.25f, 4.0f);
        ImGui::SliderFloat("Macro Scale", &g_productionPreviewSettings.macroScale, 0.05f, 2.0f);
        ImGui::SliderFloat("Macro Strength", &g_productionPreviewSettings.macroStrength, 0.0f, 0.35f);
        ImGui::SliderFloat("Cliff Dark Radius", &g_productionPreviewSettings.cliffDarkeningRadius, 0.05f, 4.0f);
        ImGui::SliderFloat("Cliff Dark Strength", &g_productionPreviewSettings.cliffDarkeningStrength, 0.0f, 0.75f);
        ImGui::SliderFloat("Min Top Brightness", &g_productionPreviewSettings.minTopBrightness, 0.35f, 1.0f);
        ImGui::SliderFloat("Edge Darkness", &g_productionPreviewSettings.edgeDarkness, 0.0f, 0.45f);
        const char* debugModes[] = {"Lit", "Albedo", "Normals", "UV", "Cliff Proximity", "Depth Order"};
        ImGui::Combo("Debug Mode", &g_productionPreviewSettings.debugMode, debugModes, IM_ARRAYSIZE(debugModes));
    }
    if (changed) {
        rebuildLandscapeBowlModel();
    }

    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::Text("Height range: %.2f - %.2f", g_landscapeModel.minHeight, g_landscapeModel.maxHeight);
        ImGui::Text("Cells clearing/high/hill: %d / %d / %d",
            g_landscapeModel.clearingCellCount,
            g_landscapeModel.highGroundCellCount,
            g_landscapeModel.hillCellCount);
        ImGui::Text("3D quads top/walls/total: %d / %d / %d",
            g_landscapeModel.topQuadCount,
            g_landscapeModel.cliffWallQuadCount,
            (int)g_landscapeModel.meshQuads.size());
        ImGui::Text("Tile meshes surface/walls/unique/reused: %d / %d / %d / %d",
            g_landscapeModel.surfaceTileCount,
            g_landscapeModel.wallTileCount,
            g_landscapeModel.uniqueTileMeshCount,
            g_landscapeModel.reusedTileMeshCount);
        ImGui::Text("Shared tile quality h/v/rock/terrace: %d / %d / %.2f / %d",
            g_rectSettings.wallHorizontalSubdivisions,
            g_rectSettings.wallVerticalSubdivisions,
            g_rectSettings.rockAmplitude,
            g_rectSettings.terraceSteps);
        ImGui::Text("Production textures grass/rock: %s / %s",
            productionGrassTextureLoaded() ? "loaded" : "fallback",
            productionRockTextureLoaded() ? "loaded" : "fallback");
        ImGui::Text("Preview renderer: %s",
            g_productionPreviewSettings.useGpuRenderer ? "Sokol GPU offscreen" : "ImGui fallback");
        ImGui::Text("Cliff top darkening radius/strength/min: %.2f / %.2f / %.2f",
            g_productionPreviewSettings.cliffDarkeningRadius,
            g_productionPreviewSettings.cliffDarkeningStrength,
            g_productionPreviewSettings.minTopBrightness);
        ImGui::Text("Shared bevel segments/caps: %d / %d",
            g_landscapeModel.beveledSegmentCount,
            g_landscapeModel.cornerCapCount);
        ImGui::Text("Pyramid max adjacent level delta: %d",
            g_landscapeModel.maxAdjacentLevelDelta);
        ImGui::Text("Seams checked/mismatches/max gap: %d / %d / %.4f",
            g_landscapeModel.seamCheckedEdges,
            g_landscapeModel.seamMismatchCount,
            g_landscapeModel.seamMaxGap);
        ImGui::Text("Level cells:");
        for (int level = 0; level < (int)g_landscapeModel.levelCellCounts.size(); level++) {
            ImGui::SameLine();
            ImGui::Text("L%d=%d", level, g_landscapeModel.levelCellCounts[(std::size_t)level]);
        }
    }
    ImGui::TextColored(ImVec4(0.43f, 0.63f, 0.35f, 1.0f), "Green = clearing / lowland");
    ImGui::TextColored(ImVec4(0.58f, 0.68f, 0.40f, 1.0f), "Olive = hills");
    ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.50f, 1.0f), "Bright = upper high ground");
    ImGui::PopItemWidth();
}

void drawScenarioPanelBackground(const ImVec2& layoutOrigin, const ImVec2& layoutSize, float leftPanelWidth) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(25, 28, 34, 255));
    drawList->AddRect(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(65, 72, 84, 255));
}

void drawRectangleScenarioTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const float viewportHeight = std::max(1.0f, (layoutSize.y - gutter) * 0.5f);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawRectangleScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawRectangleCliffDebugView(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportHeight + gutter});
        drawMesh3dPreview(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});
    }
}

void drawLandscapeScenarioTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const float viewportHeight = std::max(1.0f, (layoutSize.y - gutter) * 0.5f);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawLandscapeScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawLandscapeBowlDebugView(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportHeight + gutter});
        drawLandscapeMesh3dPreview(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportHeight});
    }
}

} // namespace

void drawUi() {
    static bool layoutLogged = false;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MeshGenerationPlayground", nullptr, rootFlags);

    if (ImGui::BeginTabBar("ScenarioTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Debug Scene")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            if (!layoutLogged) {
                spdlog::info("drawUi: viewport pos=({}, {}), workSize={}x{}, first tab layoutSize={}x{}",
                    viewport->WorkPos.x,
                    viewport->WorkPos.y,
                    viewport->WorkSize.x,
                    viewport->WorkSize.y,
                    layoutSize.x,
                    layoutSize.y);
                layoutLogged = true;
            }
            drawRectangleScenarioTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Production Preview")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            drawLandscapeScenarioTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace meshgen_playground
