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
// lengths — small chips, never slicing the body in half. Cuts whose removed
// wedge reaches the base pass extra filters so the stone keeps standing:
// the plane must be STEEP against the base (dihedral >= baseCutAngleDeg —
// grazing planes that bevel the rim or shave the whole base are rejected and
// resampled), the remaining footprint must keep >= baseMinArea of the
// starting-box area and still contain the body centroid's projection, and at
// most baseCutQuota of all cuts may touch the base. A groove enters a face
// ~perpendicularly and runs along a face-edge direction (parallel to the
// adjacent face's plane); a pit is a trihedral cone dent. Wedge depth is
// clamped by the exact ray-exit distance so channels never punch through as
// tunnels.

struct StoneCutParams {
    int seed = 5434;
    float sizeX = 1.3f;      // box half-extent in x (world units)
    float sizeZ = 1.1f;      // box half-extent in z
    float height = 3.0f;     // box height (base sits on y=0)
    int cuts = 12;           // corner cuts applied
    float cutDepth = 0.093f; // fraction of the corner's mean incident edge length
    float cutTiltDeg = 23.676f; // cone half-angle around the corner direction

    float baseCutAngleDeg = 50.0f; // min dihedral with the base for base-touching cuts
    float baseCutQuota = 0.25f;    // max fraction of cuts allowed to touch the base
    float baseMinArea = 0.55f;     // footprint floor, fraction of the starting box

    int grooves = 0;          // V-channels into random faces
    float grooveDepth = 0.22f; // axis depth below the surface (fraction of min extent)
    float grooveAngleDeg = 35.0f; // V half-angle (channel width)
    float grooveLen = 1.0f;   // 1.0 = full chord across the face, less = capped segment

    int pits = 0;             // trihedral dents on random faces
    float pitDepth = 0.18f;   // apex depth (fraction of min extent)
    float pitAngleDeg = 30.0f; // cone half-angle (pit width)

    float sink = 0.05f;      // base pushed below y=0
    float tintJitter = 0.10f; // per-plane-facet albedo variation
};

StoneMesh generateCutStone(const StoneCutParams& params);

// Two-stone composition: the big stone stands at the origin; the small one is
// placed against one of its sides. The small stone starts as a box overlapping
// the big one, then the big stone (slightly inflated by ~`gap` about its
// centroid) is subtracted from it — the contact face becomes the exact
// negative imprint of the big stone's cut surface, with a narrow slit between
// them. After the carve the small stone gets its own corner-cut pass (seeded
// separately), so its free sides match the big one's look. Both bodies are
// emitted into one mesh; they stay disjoint (no union) because of the gap.
struct StonePairParams {
    StoneCutParams big;
    StoneCutParams small; // sizes = box BEFORE the carve; seed drives its cuts
    int side = 0;         // 0:+x 1:-x 2:+z 3:-z — which side of the big stone
    float gap = 0.06f;    // contact slit width (approximate: inflation-based)
    float overlap = 0.30f; // initial box penetration depth (must exceed gap)
    float shift = 0.0f;   // tangential slide along the side (world units)
};

StoneMesh generateCutStonePair(const StonePairParams& params);
