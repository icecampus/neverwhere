#pragma once

#include "LandscapeBowlScenario.h"
#include "RectangleCliffScenario.h"
#include "SingleQuadLabScenario.h"

#include <cstdint>
#include <mutex>

namespace meshgen_playground {

struct AppState {
    std::uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;
    bool gfxFailureLogged = false;
};

extern std::mutex g_stateMutex;
extern std::mutex g_modelMutex;

extern AppState g_state;
extern RectangleCliffSettings g_rectSettings;
extern RectangleCliffModel g_rectModel;
extern MeshPreviewCamera g_meshCamera;
extern LandscapeBowlSettings g_landscapeSettings;
extern LandscapeBowlModel g_landscapeModel;
extern MeshPreviewCamera g_landscapeCamera;
extern Vec3 g_productionLightDirection;
extern ProductionPreviewSettings g_productionPreviewSettings;
extern SingleQuadLabSettings g_singleQuadLabSettings;
extern SingleQuadLabModel g_singleQuadLabModel;
extern QuadLabPreviewCamera g_singleQuadLabCamera;

} // namespace meshgen_playground
