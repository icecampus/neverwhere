#pragma once

#include "RenderTypes.h"

namespace render_playground {

enum class CliffFace : int {
    NegX = 0,
    PosX = 1,
    NegY = 2,
    PosY = 3,
};

enum class CliffReplicationMode : int {
    SingleFace = 0,
    AllVerticalFaces = 1,
};

struct SceneSpec {
    // MC sampling domain (auto-synced from cubeSize + padding in sanitizeSceneSpec).
    float sceneSizeX = 24.0f;
    float sceneSizeY = 24.0f;
    float sceneSizeZ = 24.0f;
    // Solid macro rock = axis-aligned cube centered in the scene domain.
    float cubeSize = 20.0f;
    float scenePadding = 2.0f;
    CliffFace cliffFace = CliffFace::NegX;
    CliffReplicationMode replicationMode = CliffReplicationMode::AllVerticalFaces;
    int mcResolution = 140;
    float maxSlope = 0.35f;
    // Thickness (m) of the cliff wall slab where block tiles are applied (inward from each vertical face).
    float surfaceBand = 8.0f;
    // How far block SDF may extend outward past the macro cube face (avoids flat clipping).
    float protrusionMargin = 2.0f;
    // Optional shrink of tile voids before max(f,t); 0 = full fracture depth.
    float gapFill = 0.0f;

    // Legacy fields kept for debug overlay compatibility (derived from macro box).
    float plateauHeight = 20.0f;
    float cliffInset = 2.0f;
};

void sanitizeSceneSpec(SceneSpec& spec);

} // namespace render_playground
