#include "SceneSpec.h"

namespace render_playground {

void sanitizeSceneSpec(SceneSpec& spec) {
    spec.cubeSize = clampFloat(spec.cubeSize, 4.0f, 120.0f);
    spec.scenePadding = clampFloat(spec.scenePadding, 1.0f, 20.0f);

    const float domain = spec.cubeSize + 2.0f * spec.scenePadding;
    spec.sceneSizeX = domain;
    spec.sceneSizeY = domain;
    spec.sceneSizeZ = domain;

    const int faceIdx = (int)spec.cliffFace;
    if (faceIdx < 0 || faceIdx > 3) {
        spec.cliffFace = CliffFace::NegX;
    }
    const int modeIdx = (int)spec.replicationMode;
    if (modeIdx < 0 || modeIdx > 1) {
        spec.replicationMode = CliffReplicationMode::AllVerticalFaces;
    }
    spec.mcResolution = clampInt(spec.mcResolution, 32, 160);
    spec.maxSlope = clampFloat(spec.maxSlope, 0.05f, 0.95f);
    spec.surfaceBand = clampFloat(spec.surfaceBand, 0.5f, 8.0f);
    spec.gapFill = clampFloat(spec.gapFill, 0.0f, 0.5f);
}

} // namespace render_playground
