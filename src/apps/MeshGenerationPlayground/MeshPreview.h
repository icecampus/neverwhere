#pragma once

#include "LandscapeBowlScenario.h"
#include "PlaygroundTypes.h"
#include "RectangleCliffScenario.h"

#include <imgui.h>
#include <vector>

namespace meshgen_playground {

void initProductionPreviewTextures();
void initQuadLabGpuPreview();
bool warmupQuadLabGpuPreview();
void configureProductionPreviewGpuReadback(void* d3d11Device, void* d3d11Context);
void shutdownProductionPreviewTextures();
bool productionGrassTextureLoaded();
bool productionRockTextureLoaded();
bool warmupProductionPreviewRenderer();
bool captureProductionPreviewGpuPng(const char* path, ProductionPreviewDebugMode debugMode, int width = 960, int height = 540);

// Throwaway env-sprite test helpers.
int loadedEnvSpriteCount();
void requestEnvSpriteReseed();

void drawRectangleCliffDebugView(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize);
void drawMesh3dPreview(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize);
void drawLandscapeBowlDebugView(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize);
void drawLandscapeMesh3dPreview(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize);

struct MeshQuadsPreviewOptions {
    float projectionCenterX = 0.0f;
    float projectionCenterY = 0.0f;
    float projectionCenterZ = 0.0f;
    bool showWireframe = true;
    // Match production wall preview: one lighting normal per quad, fixed a-c diagonal (no visible facet split).
    bool flatQuadShading = false;
    float orbitResetYawDegrees = 35.0f;
    float orbitResetPitchDegrees = 28.0f;
    const char* previewTitle = "Quad Lab: GPU mesh preview";
};

void drawMeshQuadsPreview(
    const std::vector<MeshQuad>& quads,
    QuadLabPreviewCamera& camera,
    const MeshQuadsPreviewOptions& options,
    const ImVec2& viewportSize);

} // namespace meshgen_playground
