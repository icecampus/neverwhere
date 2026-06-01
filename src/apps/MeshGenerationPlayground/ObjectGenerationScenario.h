#pragma once

#include "PlaygroundTypes.h"

#include <vector>

namespace meshgen_playground {

enum class ObjectGeneratorKind : int {
    CliffRock = 0,
};

struct ObjectGeneratorInfo {
    ObjectGeneratorKind kind = ObjectGeneratorKind::CliffRock;
    const char* name = "";
    const char* description = "";
};

struct ObjectGenerationSettings {
    ObjectGeneratorKind generatorKind = ObjectGeneratorKind::CliffRock;
    int seed = 4107;
    float height = 6.0f;
    float radiusX = 1.45f;
    float radiusZ = 1.05f;
    float taper = 0.28f;
    int rings = 32;
    int segments = 40;
    // Big cliff forms: low frequency noise driving the silhouette + terraced strata.
    float cliffScale = 2.0f;
    float cliffStrength = 1.0f;
    // Horizontal cliff walls vs. egg-like radial bulge (1 = steep vertical walls).
    float wallBias = 0.85f;
    // Terraced rock ledges (strata): number of layers and how hard the stepping is.
    int cliffSteps = 9;
    float terraceStrength = 0.55f;
    // Vertical fracture grooves: ridged, Y-stretched noise carved into the walls.
    float cliffYStretch = 0.28f;
    float grooveDepth = 0.45f;
    // Fine rocky surface: high frequency fbm noise.
    float detailScale = 5.5f;
    float detailStrength = 0.14f;
    bool showMeshWireframe = false;
};

struct ObjectGenerationModel {
    std::vector<MeshQuad> meshQuads;
    int faceCount = 0;
    int vertexCount = 0;
    int gridVertexCount = 0;
    int rockQuadCount = 0;
    int recessedQuadCount = 0;
    bool usedFastNoise = false;
};

int objectGeneratorCount();
const ObjectGeneratorInfo& objectGeneratorInfo(int index);
int objectGeneratorIndex(ObjectGeneratorKind kind);
ObjectGeneratorKind objectGeneratorKindAt(int index);
const char* objectGeneratorName(ObjectGeneratorKind kind);
void sanitizeSettings(ObjectGenerationSettings& settings);
void rebuildObjectGenerationModel();

} // namespace meshgen_playground
