#pragma once

#include "PlaygroundTypes.h"

#include <vector>

namespace meshgen_playground {

// FastNoise2 port of the Blender "NW_CliffWall" Geometry Nodes prototype
// (prototypes/blender/clifs/cyclopean_walls/clifs.blend).
//
// The Blender graph displaces a tall subdivided grid with a stack of noise
// fields: a per-course block offset, layered fBm bulges, a fine micro grain, a
// Voronoi crack mask and a noise-driven jagged crown. The exposed settings map
// 1:1 to the Geometry Nodes modifier inputs so the look can be matched directly.
struct CliffWallSettings {
    float width = 16.0f;     // wall span along X (Blender "Width")
    float height = 18.0f;    // wall span along Y / up (Blender "Height")
    int seed = 3;            // Blender "Seed"
    float depth = 3.0f;      // forward (-Z) displacement scale (Blender "Depth")
    int layers = 26;         // stacked stone courses (Blender "Layers")
    float topJag = 3.5f;     // downward erosion of the crown (Blender "Top Jag")
    float crackDepth = 0.8f; // Voronoi crack ridge strength (Blender "Crack Depth")
    float micro = 0.10f;     // fine grain amplitude (Blender "Micro")

    // Tessellation. Blender used 220x280; this is lighter but still tunable.
    int resolutionX = 140;
    int resolutionY = 170;

    bool showWireframe = false;
};

struct CliffWallModel {
    std::vector<MeshQuad> quads;
    int vertexCount = 0;
    int quadCount = 0;
    float minDepth = 0.0f; // most recessed (smallest outward push)
    float maxDepth = 0.0f; // most protruding
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
};

void sanitizeCliffWallSettings(CliffWallSettings& settings);
void resetCliffWallCamera(QuadLabPreviewCamera& camera);
void rebuildCliffWallModel();
bool runCliffWallSmokeTest();

} // namespace meshgen_playground
