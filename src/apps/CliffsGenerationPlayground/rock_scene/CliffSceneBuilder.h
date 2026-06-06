#pragma once

#include "RockFractureScene.h"
#include "rock_scene/SceneSpec.h"
#include "rock_scene/TileLibrary.h"

namespace render_playground {

struct CliffBuildResult {
    RockFractureModel model;
    bool ok = false;
};

class CliffSceneBuilder {
public:
    static CliffBuildResult build(const RockFractureSettings& settings, TileLibrary& tileLibrary);

    static bool runTestScenario();
    static bool runCliffReplicationBuildTest();
};

} // namespace render_playground
