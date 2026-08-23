#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "StonePoly.h"

// Single-stone generator of the playground: base-form plane factory -> clipper
// -> chamfer/jitter/noise -> ground clip -> flat-shaded mesh with baked light.
// Pure C++ (no sokol/Qt), deterministic by seed, so it can graduate into a lib
// later. One rock comes out centered on the XZ origin with its base on y=0
// (minus sink), explicit extents included — the cluster layout (phase 2) needs
// them for packing.
//
// Painted vertex-nodes are the seed silhouette the cluster sampler will
// consume; phase 1 places the single rock at the silhouette centroid.

enum class StoneBaseForm {
    Box,     // parallelepiped (prism family: 4 sides, no taper)
    Frustum, // truncated pyramid (4 sides + taper)
    Prism,   // N-gonal prism/frustum
    Sphere,  // tangent-plane ball
    Oval,    // ball + anisotropic scale
};

struct StoneGenParams {
    int seed = 1;
    StoneBaseForm form = StoneBaseForm::Frustum;

    float radius = 1.2f;    // XZ extent (world units, 1 cell = 1)
    float height = 1.6f;    // prism family
    float taper = 0.25f;    // Frustum/Prism: top radius shrink (0..0.95)
    int sides = 6;          // Prism
    float yawDeg = 0.0f;    // prism family base rotation
    int ballPlanes = 24;    // Sphere/Oval facet count
    float ovalScaleX = 1.3f; // Oval anisotropic scale
    float ovalScaleZ = 0.8f;

    float chamferWidth = 0.12f;  // 0 = off; distance cut along each face
    bool chamferTopOnly = true;  // buried bottom edges stay sharp

    float planeTiltDeg = 3.0f;   // per-plane tilt jitter (locked bottom excepted)
    float planeOffset = 0.08f;   // per-plane offset jitter (fraction of radius)
    float noiseAmp = 0.015f;     // vertex micro-noise (fraction of radius)
    float sink = 0.05f;          // base pushed below y=0
    float tintJitter = 0.10f;    // per-face albedo variation
    // Seeded spread of the shape itself (elongation, yaw, taper/height nudges)
    // around the slider values: 0 = sliders are exact, 1 = every seed gets its
    // own proportions. This is what makes "New seed" produce a new rock.
    float shapeVariance = 1.0f;
    int blocks = 1;              // 1 = single block; 2 = + top block; 3 = + leaning side block
};

struct StoneMesh {
    std::vector<float> pos; // xyz triplets, world units
    std::vector<float> nrm; // per-vertex flat (per-face) normals
    std::vector<float> col; // rgba, light baked in
    int triCount = 0;
    glm::vec3 extentMin{0.0f};
    glm::vec3 extentMax{0.0f};
};

StoneMesh generateStone(const StoneGenParams& params);

// Multi-block lobe: block 0 is the base rock; blocks 2..3 stack a smaller
// block on top / lean a side block against it (interpenetration, no CSG — the
// contact line reads as a crease). Each block is a full buildStonePoly with a
// derived sub-seed, merged into one mesh in lobe-local world coordinates.
// generateStone(params) == generateLobe(params) with blocks=1 (bit-identical).
StoneMesh generateLobe(const StoneGenParams& params);

// Mesh emission, shared by all generators of the playground: fan triangulation,
// flat per-face Newell normals (flipped by the generating plane when the
// winding faces in), light baked into the vertex color. Appends to `out`;
// extents accumulate across calls.
void appendStoneMesh(
    StoneMesh& out,
    const StonePoly& poly,
    const std::vector<StonePlane>& planes,
    int seed,
    float tintJitter);

// Debug/test entry: the polyhedron + the full plane set that built it
// (base planes, then chamfer planes, then the ground plane last).
void buildStonePoly(
    const StoneGenParams& params, StonePoly& outPoly, std::vector<StonePlane>& outPlanes);
