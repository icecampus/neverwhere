#pragma once

#include "LandscapeBowlScenario.h"
#include "PlaygroundTypes.h"
#include "RectangleCliffScenario.h"

#include <imgui.h>

namespace meshgen_playground {

void initProductionPreviewTextures();
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

} // namespace meshgen_playground
