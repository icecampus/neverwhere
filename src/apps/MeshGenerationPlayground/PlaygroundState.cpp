#include "PlaygroundState.h"

namespace meshgen_playground {

std::mutex g_stateMutex;
std::mutex g_modelMutex;

AppState g_state;
RectangleCliffSettings g_rectSettings;
RectangleCliffModel g_rectModel;
MeshPreviewCamera g_meshCamera;
LandscapeBowlSettings g_landscapeSettings;
LandscapeBowlModel g_landscapeModel;
MeshPreviewCamera g_landscapeCamera;
ObjectGenerationSettings g_objectSettings;
ObjectGenerationModel g_objectModel;
MeshPreviewCamera g_objectCamera;
Vec3 g_productionLightDirection{-0.35f, 0.82f, -0.45f};
ProductionPreviewSettings g_productionPreviewSettings;

} // namespace meshgen_playground
