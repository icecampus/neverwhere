#pragma once

#include <vector>

#include "StoneGen.h"
#include "StonePoly.h"

// Cut-based rock generator ("how boulders really form — by chipping off"):
// start from a parallelepiped, then repeatedly pick a corner and slice it with
// a random plane (clipByPlane, so the result stays watertight by construction).
// Each cut plane is aimed through the corner: normal within a cone around the
// outward corner direction, offset at a fraction of the corner's incident edge
// lengths — small chips, never slicing the body in half. Bottom corners are
// left alone so the base stays flat on the ground.
//
// Isolated from the phase-1/2 machinery on purpose: only the clipper core
// (StonePoly) and the shared mesh emission (appendStoneMesh) are reused.

struct StoneCutParams {
    int seed = 1;
    float sizeX = 1.3f;      // box half-extent in x (world units)
    float sizeZ = 1.1f;      // box half-extent in z
    float height = 1.5f;     // box height (base sits on y=0)
    int cuts = 12;           // corner cuts applied
    float cutDepth = 0.35f;  // fraction of the corner's mean incident edge length
    float cutTiltDeg = 25.0f; // cone half-angle around the corner direction
    float sink = 0.05f;      // base pushed below y=0
    float tintJitter = 0.10f;
    float noiseAmp = 0.0f;   // vertex micro-noise; off = clean cuts (fraction of size)
};

StoneMesh generateCutStone(const StoneCutParams& params);

// Debug/test entry: the polyhedron + the full plane set (6 box planes, then
// one per cut, then the ground plane last).
void buildCutPoly(
    const StoneCutParams& params, StonePoly& outPoly, std::vector<StonePlane>& outPlanes);
