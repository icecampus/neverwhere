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

} // namespace meshgen_playground
