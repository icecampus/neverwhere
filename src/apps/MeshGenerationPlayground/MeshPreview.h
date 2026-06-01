#pragma once

#include "LandscapeBowlScenario.h"
#include "ObjectGenerationScenario.h"
#include "RectangleCliffScenario.h"

#include <imgui.h>

namespace meshgen_playground {

void initProductionPreviewTextures();
void shutdownProductionPreviewTextures();
bool productionGrassTextureLoaded();
bool productionRockTextureLoaded();
bool warmupProductionPreviewRenderer();

// Throwaway env-sprite test helpers.
int loadedEnvSpriteCount();
void requestEnvSpriteReseed();

void drawRectangleCliffDebugView(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize);
void drawMesh3dPreview(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize);
void drawObjectMesh3dPreview(const ObjectGenerationSettings& settings, const ObjectGenerationModel& model, const ImVec2& viewportSize);
void drawLandscapeBowlDebugView(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize);
void drawLandscapeMesh3dPreview(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize);

} // namespace meshgen_playground
