#include "PlaygroundUi.h"

#include "CliffWallScenario.h"
#include "LandscapeBowlScenario.h"
#include "MeshPreview.h"
#include "PlaygroundLog.h"
#include "PlaygroundState.h"
#include "PlaygroundVisualCapture.h"
#include "RectangleCliffScenario.h"
#include "SingleQuadLabScenario.h"
#include "WallSeriesLabScenario.h"

#include <algorithm>
#include <mutex>

#include <imgui.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr float kViewportSplitterThickness = 10.0f;
constexpr float kMinStackedViewportHeight = 72.0f;
constexpr float kPanelHorizontalPadding = 24.0f;

struct PanelControls {
    float labelColumnX = 0.0f;
    float widgetWidth = 0.0f;

    void begin(float panelWidth) {
        const float contentWidth = std::max(1.0f, panelWidth - kPanelHorizontalPadding);
        const float labelWidth = std::clamp(contentWidth * 0.50f, 132.0f, 190.0f);
        labelColumnX = labelWidth;
        widgetWidth = std::max(72.0f, contentWidth - labelWidth - ImGui::GetStyle().ItemInnerSpacing.x);
    }

    void drawLabel(const char* text) const {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(labelColumnX);
        ImGui::SetNextItemWidth(widgetWidth);
    }

    bool sliderFloat(const char* label, float* value, float minValue, float maxValue) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::SliderFloat("##value", value, minValue, maxValue);
        ImGui::PopID();
        return changed;
    }

    bool sliderInt(const char* label, int* value, int minValue, int maxValue) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::SliderInt("##value", value, minValue, maxValue);
        ImGui::PopID();
        return changed;
    }

    bool inputInt(const char* label, int* value) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::InputInt("##value", value);
        ImGui::PopID();
        return changed;
    }

    bool combo(const char* label, int* value, const char* const items[], int itemCount) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::Combo("##value", value, items, itemCount);
        ImGui::PopID();
        return changed;
    }

    bool combo(const char* label, int* value, const char* items) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::Combo("##value", value, items);
        ImGui::PopID();
        return changed;
    }

    bool checkbox(const char* label, bool* value) const {
        drawLabel(label);
        ImGui::PushID(label);
        const bool changed = ImGui::Checkbox("##value", value);
        ImGui::PopID();
        return changed;
    }
};

struct StackedViewportLayout {
    float topHeight = 0.0f;
    float bottomHeight = 0.0f;
};

float clampViewportTopFraction(float fraction, float availableHeight) {
    if (availableHeight <= kMinStackedViewportHeight * 2.0f) {
        return 0.5f;
    }

    const float minFraction = kMinStackedViewportHeight / availableHeight;
    const float maxFraction = 1.0f - minFraction;
    return std::clamp(fraction, minFraction, maxFraction);
}

StackedViewportLayout layoutStackedViewports(
    const ImVec2& origin,
    float width,
    float totalHeight,
    float& topFraction,
    const char* splitterId) {

    const float availableHeight = std::max(1.0f, totalHeight - kViewportSplitterThickness);
    topFraction = clampViewportTopFraction(topFraction, availableHeight);

    auto computeLayout = [&](float fraction) {
        StackedViewportLayout result;
        result.topHeight = availableHeight * fraction;
        result.bottomHeight = availableHeight - result.topHeight;
        return result;
    };

    StackedViewportLayout layout = computeLayout(topFraction);

    ImGui::SetCursorScreenPos({origin.x, origin.y + layout.topHeight});
    ImGui::PushID(splitterId);
    ImGui::InvisibleButton("##viewport_splitter", {width, kViewportSplitterThickness});
    const bool splitterHovered = ImGui::IsItemHovered();
    const bool splitterActive = ImGui::IsItemActive();
    if (splitterActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const float mouseY = ImGui::GetIO().MousePos.y;
        const float newTopHeight = mouseY - origin.y;
        topFraction = clampViewportTopFraction(newTopHeight / availableHeight, availableHeight);
        layout = computeLayout(topFraction);
    }
    if (splitterHovered || splitterActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    ImGui::PopID();

    const ImVec2 splitterMin{origin.x, origin.y + layout.topHeight};
    const ImVec2 splitterMax{origin.x + width, splitterMin.y + kViewportSplitterThickness};
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 splitterColor = splitterActive
        ? IM_COL32(96, 132, 196, 255)
        : splitterHovered
            ? IM_COL32(74, 86, 108, 255)
            : IM_COL32(52, 58, 70, 255);
    drawList->AddRectFilled(splitterMin, splitterMax, splitterColor);
    drawList->AddLine(
        {splitterMin.x + 8.0f, splitterMin.y + kViewportSplitterThickness * 0.5f},
        {splitterMax.x - 8.0f, splitterMin.y + kViewportSplitterThickness * 0.5f},
        IM_COL32(150, 162, 180, splitterHovered || splitterActive ? 220 : 140),
        1.5f);
    const float gripCenterX = splitterMin.x + width * 0.5f;
    const float gripCenterY = splitterMin.y + kViewportSplitterThickness * 0.5f;
    for (int i = -1; i <= 1; i++) {
        drawList->AddCircleFilled(
            {gripCenterX + (float)i * 5.0f, gripCenterY},
            1.6f,
            IM_COL32(196, 206, 220, splitterHovered || splitterActive ? 255 : 180));
    }

    return layout;
}

void drawFrameStats() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
}

void drawRectangleScenarioControls(float panelWidth) {
    PanelControls panel;
    panel.begin(panelWidth);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Debug Scene");
    ImGui::TextWrapped("Debug scenario for boundary vertices, cutouts, bevels and rocky wall stitching.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= panel.sliderInt("Grid Width", &g_rectSettings.gridWidth, 4, 48);
        changed |= panel.sliderInt("Grid Height", &g_rectSettings.gridHeight, 4, 32);
        changed |= panel.sliderInt("Rect X", &g_rectSettings.rectX, 0, g_rectSettings.gridWidth - 1);
        changed |= panel.sliderInt("Rect Y", &g_rectSettings.rectY, 0, g_rectSettings.gridHeight - 1);
        changed |= panel.sliderInt("Rect Width", &g_rectSettings.rectWidth, 1, g_rectSettings.gridWidth);
        changed |= panel.sliderInt("Rect Height", &g_rectSettings.rectHeight, 1, g_rectSettings.gridHeight);
        changed |= panel.checkbox("Enable Cutout (inner corners)", &g_rectSettings.enableCutout);
        changed |= panel.sliderInt("Cutout X", &g_rectSettings.cutoutX, 0, g_rectSettings.gridWidth - 1);
        changed |= panel.sliderInt("Cutout Y", &g_rectSettings.cutoutY, 0, g_rectSettings.gridHeight - 1);
        changed |= panel.sliderInt("Cutout Width", &g_rectSettings.cutoutWidth, 1, g_rectSettings.gridWidth);
        changed |= panel.sliderInt("Cutout Height", &g_rectSettings.cutoutHeight, 1, g_rectSettings.gridHeight);
        changed |= panel.sliderFloat("Cliff Height", &g_rectSettings.cliffHeight, 0.25f, 8.0f);
        changed |= panel.sliderFloat("Corner Bevel", &g_rectSettings.cornerBevel, 0.0f, 0.45f);
        changed |= panel.checkbox("Rock Noise Enabled", &g_rectSettings.rockEnabled);
        changed |= panel.inputInt("Rock Seed", &g_rectSettings.rockSeed);
        changed |= panel.sliderFloat("Rock Scale", &g_rectSettings.rockScale, 0.25f, 24.0f);
        changed |= panel.sliderFloat("Rock Amplitude", &g_rectSettings.rockAmplitude, 0.0f, 1.25f);
        changed |= panel.sliderInt("Wall Horizontal Subdivs", &g_rectSettings.wallHorizontalSubdivisions, 1, 16);
        changed |= panel.sliderInt("Wall Vertical Subdivs", &g_rectSettings.wallVerticalSubdivisions, 1, 16);
        changed |= panel.sliderInt("Terrace Steps", &g_rectSettings.terraceSteps, 0, 12);
        panel.checkbox("Show Top Faces", &g_rectSettings.showTopFaces);
        panel.checkbox("Show Cliff Walls", &g_rectSettings.showCliffWalls);
        panel.checkbox("Show Mesh Wireframe", &g_rectSettings.showMeshWireframe);
        panel.checkbox("Show Cell Labels", &g_rectSettings.showCellLabels);
        panel.checkbox("Show Vertex Labels", &g_rectSettings.showVertexLabels);
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
        ImGui::Text("Outward normals fail/warn/minDot: %d / %d / %.4f",
            g_rectModel.outwardFailCount,
            g_rectModel.outwardWarnCount,
            g_rectModel.minWallOutwardDot);
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
}

void drawLandscapeScenarioControls(float panelWidth) {
    PanelControls panel;
    panel.begin(panelWidth);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Production Preview");
    ImGui::TextWrapped("Clean landscape preview for visual iteration: flat clearing, terraced high-ground arc and hills with production-style lighting.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= panel.sliderInt("Grid Width", &g_landscapeSettings.gridWidth, 12, 72);
        changed |= panel.sliderInt("Grid Height", &g_landscapeSettings.gridHeight, 10, 56);
        changed |= panel.inputInt("Seed", &g_landscapeSettings.seed);
        changed |= panel.sliderFloat("Clearing Radius", &g_landscapeSettings.clearingRadius, 2.0f, 18.0f);
        changed |= panel.sliderFloat("Clearing Softness", &g_landscapeSettings.clearingSoftness, 0.25f, 8.0f);
        changed |= panel.sliderFloat("High Ground Radius", &g_landscapeSettings.highGroundRadius, 3.0f, 28.0f);
        changed |= panel.sliderFloat("High Ground Width", &g_landscapeSettings.highGroundWidth, 1.0f, 10.0f);
        changed |= panel.sliderFloat("High Ground Height", &g_landscapeSettings.highGroundHeight, 0.5f, 8.0f);
        changed |= panel.sliderInt("Height Levels", &g_landscapeSettings.heightLevels, 2, 6);
        changed |= panel.sliderFloat("Arc Noise Scale", &g_landscapeSettings.arcNoiseScale, 0.5f, 18.0f);
        changed |= panel.sliderFloat("Arc Noise Amplitude", &g_landscapeSettings.arcNoiseAmplitude, 0.0f, 5.0f);
        changed |= panel.sliderInt("Hill Count", &g_landscapeSettings.hillCount, 0, 12);
        changed |= panel.sliderFloat("Hill Height", &g_landscapeSettings.hillHeight, 0.0f, 5.0f);
        changed |= panel.sliderFloat("Hill Radius", &g_landscapeSettings.hillRadius, 0.75f, 8.0f);
        panel.checkbox("Show Top Faces", &g_landscapeSettings.showTopFaces);
        panel.checkbox("Show Cliff Walls", &g_landscapeSettings.showCliffWalls);
        panel.checkbox("Show Level Labels", &g_landscapeSettings.showHeightValues);
        ImGui::Separator();
        ImGui::Text("Wall Style");
        const char* wallStyles[] = {"Block Cliff", "Cyclopean"};
        changed |= panel.combo("Wall Style", &g_productionWallStyle, wallStyles, IM_ARRAYSIZE(wallStyles));
        ImGui::Separator();
        ImGui::Text("Rendering");
        panel.checkbox("Use GPU Renderer", &g_productionPreviewSettings.useGpuRenderer);
        panel.sliderFloat("Ambient", &g_productionPreviewSettings.ambient, 0.35f, 1.35f);
        panel.sliderFloat("Diffuse Strength", &g_productionPreviewSettings.diffuseStrength, 0.0f, 0.85f);
        panel.sliderFloat("Rim Strength", &g_productionPreviewSettings.rimStrength, 0.0f, 0.5f);
        panel.sliderFloat("Specular Strength", &g_productionPreviewSettings.specularStrength, 0.0f, 0.4f);
        panel.sliderFloat("Sun Shadow Strength", &g_productionPreviewSettings.sunShadowStrength, 0.0f, 1.0f);
        panel.sliderFloat("Shadow Tint", &g_productionPreviewSettings.shadowTintStrength, 0.0f, 1.0f);
        panel.sliderFloat("Shadow Ambient Floor", &g_productionPreviewSettings.shadowAmbientFloor, 0.15f, 0.75f);
        panel.sliderFloat("Shadow Softness", &g_productionPreviewSettings.shadowSoftness, 0.6f, 3.0f);
        panel.sliderFloat("Wall Brightness", &g_productionPreviewSettings.wallBrightness, 0.45f, 1.65f);
        panel.sliderFloat("Texture Scale", &g_productionPreviewSettings.textureScale, 0.25f, 4.0f);
        panel.sliderFloat("Macro Scale", &g_productionPreviewSettings.macroScale, 0.05f, 2.0f);
        panel.sliderFloat("Macro Strength", &g_productionPreviewSettings.macroStrength, 0.0f, 0.35f);
        panel.sliderFloat("Cliff Dark Radius", &g_productionPreviewSettings.cliffDarkeningRadius, 0.05f, 4.0f);
        panel.sliderFloat("Cliff Dark Strength", &g_productionPreviewSettings.cliffDarkeningStrength, 0.0f, 0.75f);
        panel.sliderFloat("Min Top Brightness", &g_productionPreviewSettings.minTopBrightness, 0.35f, 1.0f);
        panel.sliderFloat("Edge Darkness", &g_productionPreviewSettings.edgeDarkness, 0.0f, 0.45f);
        ImGui::Text("Cliff Surface");
        panel.sliderFloat("Wall AO", &g_productionPreviewSettings.wallAoStrength, 0.0f, 0.85f);
        panel.sliderFloat("Edge Wear", &g_productionPreviewSettings.wallEdgeWearStrength, 0.0f, 1.0f);
        panel.sliderFloat("Crevice Darken", &g_productionPreviewSettings.wallCreviceStrength, 0.0f, 1.0f);
        panel.sliderFloat("Wall Grain", &g_productionPreviewSettings.wallGrainStrength, 0.0f, 0.6f);
        const char* debugModes[] = {
            "Lit",
            "Albedo",
            "Raw Normals",
            "Stable Normals",
            "Blended Normals",
            "UV",
            "Cliff Proximity",
            "Depth Order",
            "Sun Shadow",
            "Normal Vectors",
        };
        panel.combo("Debug Mode", &g_productionPreviewSettings.debugMode, debugModes, IM_ARRAYSIZE(debugModes));
        if (g_productionPreviewSettings.debugMode == (int)ProductionPreviewDebugMode::NormalVectors) {
            panel.sliderFloat("Normal Arrow Scale", &g_productionPreviewSettings.normalVectorScale, 0.1f, 1.5f);
        }
        ImGui::Separator();
        ImGui::Text("Env Sprites (throwaway test)");
        panel.checkbox("Scatter 2D Env Sprites", &g_productionPreviewSettings.showEnvSprites);
        ImGui::SameLine();
        if (ImGui::Button("Reseed")) {
            requestEnvSpriteReseed();
        }
        ImGui::Text("Loaded sprite kinds: %d", loadedEnvSpriteCount());
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
        ImGui::Text("Outward normals fail/warn/minDot: %d / %d / %.4f",
            g_landscapeModel.outwardFailCount,
            g_landscapeModel.outwardWarnCount,
            g_landscapeModel.minWallOutwardDot);
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
    static float viewportTopFraction = 0.2f;

    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const StackedViewportLayout viewportLayout = layoutStackedViewports(
        {rightX, layoutOrigin.y},
        rightWidth,
        layoutSize.y,
        viewportTopFraction,
        "RectViewportSplit");

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawRectangleScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawRectangleCliffDebugView(g_rectSettings, g_rectModel, {rightWidth, viewportLayout.topHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportLayout.topHeight + kViewportSplitterThickness});
        drawMesh3dPreview(g_rectSettings, g_rectModel, {rightWidth, viewportLayout.bottomHeight});
    }
}

void drawSingleQuadLabControls(float panelWidth) {
    PanelControls panel;
    panel.begin(panelWidth);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Quad Lab");
    ImGui::TextWrapped(
        "Sandbox for learning mesh operations on a single quad. Start with extrude, then add more steps here.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= panel.sliderFloat("Quad Width", &g_singleQuadLabSettings.quadWidth, 0.1f, 4.0f);
        changed |= panel.sliderFloat("Quad Height", &g_singleQuadLabSettings.quadHeight, 0.1f, 4.0f);
        changed |= panel.sliderFloat("Yaw", &g_singleQuadLabSettings.yawDegrees, -180.0f, 180.0f);
        changed |= panel.sliderFloat("Pitch", &g_singleQuadLabSettings.pitchDegrees, -89.0f, 89.0f);
        changed |= panel.sliderFloat("Center X", &g_singleQuadLabSettings.centerX, -4.0f, 4.0f);
        changed |= panel.sliderFloat("Center Y", &g_singleQuadLabSettings.centerY, -2.0f, 4.0f);
        changed |= panel.sliderFloat("Center Z", &g_singleQuadLabSettings.centerZ, -4.0f, 4.0f);

        int operation = (int)g_singleQuadLabSettings.operation;
        changed |= panel.combo("Operation", &operation, "Flat quad\0Extrude shell\0");
        g_singleQuadLabSettings.operation = (QuadLabOperation)operation;

        if (g_singleQuadLabSettings.operation == QuadLabOperation::Extrude) {
            changed |= panel.sliderFloat("Extrude Depth", &g_singleQuadLabSettings.extrudeDepth, 0.0f, 2.0f);
            changed |= panel.sliderFloat("Extrude Top Scale", &g_singleQuadLabSettings.extrudeTopScale, 0.1f, 1.0f);
            changed |= panel.sliderFloat(
                "Top Height Spread",
                &g_singleQuadLabSettings.extrudeTopHeightSpread,
                0.0f,
                1.0f);
            changed |= panel.sliderFloat(
                "Top Scale Spread",
                &g_singleQuadLabSettings.extrudeTopScaleSpread,
                0.0f,
                1.0f);
            changed |= panel.inputInt("Extrude Seed", &g_singleQuadLabSettings.extrudeHeightSeed);
            if (ImGui::Button("Reseed Extrude")) {
                ++g_singleQuadLabSettings.extrudeHeightSeed;
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Per top corner: height uses depth * random(1-hSpread, 1+hSpread), "
                    "pinch scale uses topScale * random(1-sSpread, 1+sSpread).");
            }
        }

        changed |= panel.checkbox("Colorize Faces", &g_singleQuadLabSettings.colorizeFaces);
        panel.checkbox("Show Wireframe", &g_singleQuadLabSettings.showWireframe);

        ImGui::Separator();
        ImGui::Text("Preview Camera");
        ImGui::TextWrapped("GPU mesh preview with depth buffer. Ctrl+LMB drag: orbit. LMB drag: pan.");
        panel.sliderFloat("Camera Yaw", &g_singleQuadLabCamera.orbitYawDegrees, -180.0f, 180.0f);
        panel.sliderFloat("Camera Pitch", &g_singleQuadLabCamera.orbitPitchDegrees, -89.0f, 89.0f);
        if (ImGui::Button("Reset Camera")) {
            g_singleQuadLabCamera.zoom = 1.0f;
            g_singleQuadLabCamera.pan = {0.0f, 0.0f};
            g_singleQuadLabCamera.orbitYawDegrees = 35.0f;
            g_singleQuadLabCamera.orbitPitchDegrees = 28.0f;
        }

        ImGui::Separator();
        ImGui::Text("Panels: %d", g_singleQuadLabModel.panelCount);
        if (g_singleQuadLabSettings.operation == QuadLabOperation::Extrude && g_singleQuadLabSettings.extrudeDepth > 0.0001f) {
            ImGui::TextColored(ImVec4(0.66f, 0.72f, 0.78f, 1.0f), "Gray = base/top, orange = sides");
        }
    }
    if (changed) {
        rebuildSingleQuadLabModel();
    }
}

void drawWallLabControls(float panelWidth) {
    PanelControls panel;
    panel.begin(panelWidth);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Wall Lab");
    ImGui::TextWrapped(
        "Rectangle split into 4x4 quads. Shared grid vertices get chaotic forward push, then each facet gets Quad Lab extrude.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= panel.sliderFloat("Width", &g_wallSeriesLabSettings.wallWidth, 0.5f, 12.0f);
        changed |= panel.sliderFloat("Height", &g_wallSeriesLabSettings.wallHeight, 0.5f, 8.0f);

        changed |= panel.sliderFloat("Center X", &g_wallSeriesLabSettings.wallCenterX, -4.0f, 4.0f);
        changed |= panel.sliderFloat("Center Y", &g_wallSeriesLabSettings.wallCenterY, -2.0f, 4.0f);
        changed |= panel.sliderFloat("Center Z", &g_wallSeriesLabSettings.wallCenterZ, -4.0f, 4.0f);
        changed |= panel.sliderFloat("Yaw", &g_wallSeriesLabSettings.yawDegrees, -180.0f, 180.0f);
        changed |= panel.sliderFloat("Pitch", &g_wallSeriesLabSettings.pitchDegrees, -89.0f, 89.0f);
        changed |= panel.sliderFloat("Vertex Push Max", &g_wallSeriesLabSettings.vertexPushMax, 0.0f, 1.5f);
        changed |= panel.inputInt("Vertex Push Seed", &g_wallSeriesLabSettings.vertexPushSeed);
        if (ImGui::Button("Reseed Vertex Push")) {
            ++g_wallSeriesLabSettings.vertexPushSeed;
            changed = true;
        }

        ImGui::Separator();
        ImGui::Text("Quad Lab Extrude");
        int quadLabOperation = (int)g_wallSeriesLabSettings.quadLabOperation;
        changed |= panel.combo("Operation", &quadLabOperation, "Flat quad\0Extrude shell\0");
        g_wallSeriesLabSettings.quadLabOperation = (QuadLabOperation)quadLabOperation;

        if (g_wallSeriesLabSettings.quadLabOperation == QuadLabOperation::Extrude) {
            changed |= panel.sliderFloat("Extrude Depth", &g_wallSeriesLabSettings.extrudeDepth, 0.0f, 2.0f);
            changed |= panel.sliderFloat("Extrude Top Scale", &g_wallSeriesLabSettings.extrudeTopScale, 0.1f, 1.0f);
            changed |= panel.sliderFloat(
                "Top Height Spread",
                &g_wallSeriesLabSettings.extrudeTopHeightSpread,
                0.0f,
                1.0f);
            changed |= panel.sliderFloat(
                "Top Scale Spread",
                &g_wallSeriesLabSettings.extrudeTopScaleSpread,
                0.0f,
                1.0f);
            changed |= panel.inputInt("Extrude Seed", &g_wallSeriesLabSettings.extrudeHeightSeed);
            changed |= panel.checkbox("Vary Seed Per Quad", &g_wallSeriesLabSettings.extrudeVaryPerQuad);
            if (ImGui::Button("Reseed Extrude")) {
                ++g_wallSeriesLabSettings.extrudeHeightSeed;
                changed = true;
            }
        }

        changed |= panel.checkbox("Colorize Faces", &g_wallSeriesLabSettings.colorizeFaces);
        panel.checkbox("Show Wireframe", &g_wallSeriesLabSettings.showWireframe);

        ImGui::Separator();
        ImGui::Text("Preview Camera");
        panel.sliderFloat("Camera Yaw", &g_wallSeriesLabCamera.orbitYawDegrees, -180.0f, 180.0f);
        panel.sliderFloat("Camera Pitch", &g_wallSeriesLabCamera.orbitPitchDegrees, -89.0f, 89.0f);
        if (ImGui::Button("Reset Camera")) {
            resetWallSeriesLabCamera(g_wallSeriesLabCamera);
        }

        ImGui::Separator();
        ImGui::Text(
            "Base quads: %d (%d x %d) | mesh panels: %d",
            g_wallSeriesLabModel.baseQuadCount,
            g_wallSeriesLabModel.gridColumns,
            g_wallSeriesLabModel.gridRows,
            g_wallSeriesLabModel.meshPanelCount);
        if (g_wallSeriesLabSettings.quadLabOperation == QuadLabOperation::Extrude
            && g_wallSeriesLabSettings.extrudeDepth > 0.0001f) {
            ImGui::TextColored(ImVec4(0.66f, 0.72f, 0.78f, 1.0f), "Gray = base/top, orange = sides");
        }
    }
    if (changed) {
        rebuildWallSeriesLabModel();
    }
}

void drawSingleQuadLabTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawSingleQuadLabControls(leftPanelWidth);
    ImGui::EndGroup();

    MeshQuadsPreviewOptions previewOptions;
    std::vector<MeshQuad> quads;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        previewOptions.projectionCenterX = g_singleQuadLabSettings.centerX;
        previewOptions.projectionCenterY = g_singleQuadLabSettings.centerY;
        previewOptions.projectionCenterZ = g_singleQuadLabSettings.centerZ;
        previewOptions.showWireframe = g_singleQuadLabSettings.showWireframe;
        previewOptions.previewTitle = "Quad Lab: GPU mesh preview";
        quads = g_singleQuadLabModel.quads;
    }

    ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
    drawMeshQuadsPreview(quads, g_singleQuadLabCamera, previewOptions, {rightWidth, layoutSize.y});
}

void drawWallLabTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawWallLabControls(leftPanelWidth);
    ImGui::EndGroup();

    MeshQuadsPreviewOptions previewOptions;
    std::vector<MeshQuad> quads;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        previewOptions.projectionCenterX = g_wallSeriesLabSettings.wallCenterX;
        previewOptions.projectionCenterY = g_wallSeriesLabSettings.wallCenterY;
        previewOptions.projectionCenterZ = g_wallSeriesLabSettings.wallCenterZ;
        previewOptions.showWireframe = g_wallSeriesLabSettings.showWireframe;
        previewOptions.orbitResetYawDegrees = 180.0f;
        previewOptions.orbitResetPitchDegrees = 18.0f;
        previewOptions.previewTitle = "Wall Lab: GPU mesh preview";
        quads = g_wallSeriesLabModel.quads;
    }

    ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
    drawMeshQuadsPreview(quads, g_wallSeriesLabCamera, previewOptions, {rightWidth, layoutSize.y});
}

void drawCliffWallControls(float panelWidth) {
    PanelControls panel;
    panel.begin(panelWidth);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Cliff Wall (FastNoise2)");
    ImGui::TextWrapped(
        "FastNoise2 port of the Blender NW_CliffWall geometry-nodes prototype: a tall subdivided grid "
        "displaced by stacked stone courses, fBm bulges, micro grain, Voronoi cracks and a jagged crown.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::Text("Shape (maps 1:1 to the Blender modifier)");
        changed |= panel.sliderFloat("Width", &g_cliffWallSettings.width, 4.0f, 48.0f);
        changed |= panel.sliderFloat("Height", &g_cliffWallSettings.height, 4.0f, 48.0f);
        changed |= panel.inputInt("Seed", &g_cliffWallSettings.seed);
        changed |= panel.sliderFloat("Depth", &g_cliffWallSettings.depth, 0.0f, 8.0f);
        changed |= panel.sliderInt("Layers", &g_cliffWallSettings.layers, 1, 80);
        changed |= panel.sliderFloat("Top Jag", &g_cliffWallSettings.topJag, 0.0f, 8.0f);
        changed |= panel.sliderFloat("Crack Depth", &g_cliffWallSettings.crackDepth, 0.0f, 3.0f);
        changed |= panel.sliderFloat("Micro", &g_cliffWallSettings.micro, 0.0f, 1.0f);

        ImGui::Separator();
        ImGui::Text("Tessellation");
        changed |= panel.sliderInt("Resolution X", &g_cliffWallSettings.resolutionX, 8, 320);
        changed |= panel.sliderInt("Resolution Y", &g_cliffWallSettings.resolutionY, 8, 360);
        panel.checkbox("Show Wireframe", &g_cliffWallSettings.showWireframe);

        ImGui::Separator();
        ImGui::Text("Preview Camera");
        panel.sliderFloat("Camera Yaw", &g_cliffWallCamera.orbitYawDegrees, -180.0f, 180.0f);
        panel.sliderFloat("Camera Pitch", &g_cliffWallCamera.orbitPitchDegrees, -89.0f, 89.0f);
        if (ImGui::Button("Reset Camera")) {
            resetCliffWallCamera(g_cliffWallCamera);
        }

        ImGui::Separator();
        ImGui::Text("Verts/Quads: %d / %d", g_cliffWallModel.vertexCount, g_cliffWallModel.quadCount);
        ImGui::Text("Relief depth: %.2f - %.2f", g_cliffWallModel.minDepth, g_cliffWallModel.maxDepth);
    }
    if (changed) {
        rebuildCliffWallModel();
    }
}

void drawCliffWallTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawCliffWallControls(leftPanelWidth);
    ImGui::EndGroup();

    MeshQuadsPreviewOptions previewOptions;
    std::vector<MeshQuad> quads;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        previewOptions.projectionCenterX = 0.0f;
        previewOptions.projectionCenterY = 0.0f;
        previewOptions.projectionCenterZ = 0.0f;
        previewOptions.showWireframe = g_cliffWallSettings.showWireframe;
        previewOptions.orbitResetYawDegrees = 180.0f;
        previewOptions.orbitResetPitchDegrees = 6.0f;
        previewOptions.previewTitle = "Cliff Wall: FastNoise2 GPU preview";
        quads = g_cliffWallModel.quads;
    }

    ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
    drawMeshQuadsPreview(quads, g_cliffWallCamera, previewOptions, {rightWidth, layoutSize.y});
}

void drawLandscapeScenarioTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    static float viewportTopFraction = 0.2f;

    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const StackedViewportLayout viewportLayout = layoutStackedViewports(
        {rightX, layoutOrigin.y},
        rightWidth,
        layoutSize.y,
        viewportTopFraction,
        "LandscapeViewportSplit");

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawLandscapeScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawLandscapeBowlDebugView(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportLayout.topHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportLayout.topHeight + kViewportSplitterThickness});
        drawLandscapeMesh3dPreview(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportLayout.bottomHeight});
    }
}

} // namespace

void drawVisualCaptureUi() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MeshGenerationVisualCapture", nullptr, rootFlags);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        drawLandscapeMesh3dPreview(g_landscapeSettings, g_landscapeModel, viewport->WorkSize);
    }
    ImGui::End();
}

void drawUi() {
    if (visualCaptureEnabled()) {
        drawVisualCaptureUi();
        return;
    }

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

    ImGui::Begin("PolygonalGeneratedLandscapePlayground", nullptr, rootFlags);

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

        if (ImGui::BeginTabItem("Quad Lab")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            drawSingleQuadLabTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Wall Lab")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            drawWallLabTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cliff Wall")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            drawCliffWallTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace meshgen_playground
