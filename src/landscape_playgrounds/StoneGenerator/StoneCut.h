#pragma once

#include "StoneMesh.h"

// Cut-based rock generator on CGAL exact booleans: start from a
// parallelepiped, then repeatedly pick a corner and slice it with a random
// plane; concave features (V-grooves and pits) subtract convex wedges.
// All cutting happens on CGAL::Nef_polyhedron_3<CGAL::Epeck> (exact
// arithmetic) — the output is watertight by construction, no hand-rolled
// clipping anywhere in this file.
//
// Corner cuts aim through a corner: normal within a cone around the outward
// corner direction, offset at a fraction of the corner's incident edge
// lengths — small chips, never slicing the body in half. Bottom corners are
// excluded so the base stays flat. A groove enters a face ~perpendicularly
// and runs along a face-edge direction (parallel to the adjacent face's
// plane); a pit is a trihedral cone dent. Wedge depth is clamped by the exact
// ray-exit distance so channels never punch through as tunnels.

struct StoneCutParams {
    int seed = 1;
    float sizeX = 1.3f;      // box half-extent in x (world units)
    float sizeZ = 1.1f;      // box half-extent in z
    float height = 1.5f;     // box height (base sits on y=0)
    int cuts = 12;           // corner cuts applied
    float cutDepth = 0.35f;  // fraction of the corner's mean incident edge length
    float cutTiltDeg = 25.0f; // cone half-angle around the corner direction

    int grooves = 2;          // V-channels into random faces
    float grooveDepth = 0.22f; // axis depth below the surface (fraction of min extent)
    float grooveAngleDeg = 35.0f; // V half-angle (channel width)
    float grooveLen = 1.0f;   // 1.0 = full chord across the face, less = capped segment

    int pits = 2;             // trihedral dents on random faces
    float pitDepth = 0.18f;   // apex depth (fraction of min extent)
    float pitAngleDeg = 30.0f; // cone half-angle (pit width)

    float sink = 0.05f;      // base pushed below y=0
    float tintJitter = 0.10f; // per-plane-facet albedo variation
};

StoneMesh generateCutStone(const StoneCutParams& params);
