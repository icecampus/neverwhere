#pragma once

#include "StoneMesh.h"

// Cut-based rock generator on CGAL exact booleans. The macro form is built in
// three stages, largest feature first, so the silhouette is decided before any
// detail touches it:
//
//   1. MASSIF — the starting body is a union of `lobes` yaw-rotated boxes, all
//      resting on the ground, all overlapping the main one and all flush with
//      its top. That reads as a rock mass with shoulders instead of a single
//      prism, and the off-axis yaw gives facets that do not line up with the
//      isometric grid, while `height` stays the stone's true height for every
//      downstream consumer. Shoulders deliberately do not vary in height: a
//      lower shoulder shows its own top as a horizontal shelf jutting out of the
//      wall, which reads as a pulled-out drawer, not as rock. Differences in
//      height are the cluster's job — that is what separate stones are for.
//      Each shoulder's protrusion and width are solved for rather than sampled,
//      so two walls never cross at a shallow angle — see buildMassif() for why a
//      grazing lobe leaves a razor fin instead of a step.
//   2. TAPER — `taperCuts` large planes around the azimuth circle, each tilted
//      so it bites `inset` at the top and nothing at the base: wide bottom,
//      narrower top, a few big flat flanks. This is the batter that makes a
//      block read as a boulder. Base-safe by construction (see below), so it
//      never fights the footprint filters. The tilt is deliberately not uniform:
//      a `taperWalls` share of the flanks is skipped and keeps the massif's own
//      vertical wall, because tapering every side equally yields a symmetric
//      frustum — a tent, not a rock.
//   3. TOP — the plateau is tilted by `topSlantDeg` and its rim is chamfered by
//      `rimBevelCuts` 45-degree facets, the wide bevel band that separates a
//      rock's top face from its walls.
//
// Only then come the small features: corner cuts (chips), and the optional
// concavities (V-grooves, pits) which ship off.
//
// All cutting happens on CGAL::Nef_polyhedron_3<CGAL::Epeck> (exact
// arithmetic) — the output is watertight by construction, no hand-rolled
// clipping anywhere in this file.
//
// The three macro stages cannot undercut the stone: a taper plane reaches the
// base only if `inset > height*tan(taperDeg)`, and `inset` is defined as
// `taperReach` (<= 1) times exactly that product; the top slant and rim bevels
// are anchored at the plateau and clamped so their traces stay above y=0.
// Corner cuts, which aim through an arbitrary vertex, do need the guards:
// a cut whose removed wedge reaches the base must be STEEP against it
// (dihedral >= baseCutAngleDeg — grazing planes that bevel the rim or shave
// the whole base are rejected and resampled), must leave >= baseMinArea of the
// starting footprint, must keep the body centroid's projection inside it, and
// at most baseCutQuota of all cuts may touch the base at all.
//
// A groove enters a face ~perpendicularly and runs along a face-edge direction
// (parallel to the adjacent face's plane); a pit is a trihedral cone dent.
// Wedge depth is clamped by the exact ray-exit distance so channels never
// punch through as tunnels.

struct StoneCutParams {
    int seed = 5434;
    float sizeX = 1.3f;      // main box half-extent in x (world units)
    float sizeZ = 1.1f;      // main box half-extent in z
    float height = 3.0f;     // stone height (base sits on y=0; lobes never exceed it)

    // --- Massif: the starting body is a union of yawed boxes ----------------
    int lobes = 3;             // 1 = plain box; 2..4 add shoulders
    float lobeSize = 0.70f;    // lobe half-extents, fraction of the main box
    float lobeSpread = 0.30f;  // how far a shoulder sticks out, in main-radius units
    float lobeYawDeg = 32.0f;  // per-lobe yaw about Y (kept away from zero)

    // --- Taper: large flank planes, wide base -> narrow top -----------------
    int taperCuts = 5;        // planes spread around the azimuth circle
    float taperDeg = 15.0f;   // plane tilt off vertical (the batter angle)
    float taperReach = 0.80f; // 1.0 = the plane grazes the base corner
    float taperWalls = 0.40f; // share of flanks left vertical (0 = even frustum)

    // --- Top: slanted plateau with a chamfered rim --------------------------
    float topSlantDeg = 9.0f; // plateau tilt (0 = dead flat)
    int rimBevelCuts = 4;     // 45-degree facets around the top edge
    float rimBevel = 0.28f;   // chamfer depth, fraction of the min extent

    int cuts = 4;            // corner cuts applied (chips on the macro form)
    float cutDepth = 0.32f;  // fraction of the corner's mean incident edge length
    float cutTiltDeg = 24.0f; // cone half-angle around the corner direction

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

// Stone cluster: a root stone plus recursively spawned companions arranged as a
// FAN of azimuths around it, not a stair along the axes. Each child picks a
// direction on the circle, is pushed out to the parent's exact support radius
// along it (minus `overlap`), then has the inflated parent subtracted — the
// contact face becomes a tight imprint with a ~gap slit, same trick as the
// pair. Candidates that miss the parent's body (carve removed ~nothing) or
// interpenetrate an already-placed stone are resampled.
//
// Composition rules, all in service of the silhouette:
//   * Ordinary companions go in the CAMERA-FACING half (+X/+Z read as "front"
//     and "nearer" in the diamond projection) and are always shorter than their
//     parent, so they pile against its foot without hiding it.
//   * With `spireChance` a companion is a SPIRE instead: taller than its parent
//     and placed BEHIND it. That is what turns a decaying stair into a massif
//     with a second peak, and it is the only case where the back half of the
//     circle is used — a shorter stone back there would just disappear.
//   * Ordinary heights scatter by `heightVar` around `decay`, so tops do not
//     line up into a staircase. Each child's footprint is then derived from its
//     height and a per-role target aspect — companions squat, spires chunky —
//     rather than decayed alongside it, which is what keeps a tall root from
//     breeding thin fangs.
//   * `pebbles` small blocks are scattered on the ground outside the cluster
//     silhouette, the debris that plants the group in the terrain. They are
//     placed beyond the cluster's own support radius, so they need no boolean
//     collision test at all.
struct StoneClusterParams {
    StoneCutParams base;    // root recipe; children inherit it with own seeds
    int seed = 777;         // layout rng: azimuths, heights, child seeds
    int levels = 2;         // recursion depth (0 = root only)
    int maxChildren = 3;    // per stone; actual count is 1..maxChildren
    float decay = 0.62f;    // child/parent HEIGHT ratio (footprint follows the aspect)
    float gap = 0.06f;      // contact slit width (inflation-based, as the pair)
    float overlap = 0.30f;  // initial box penetration into the parent
    float spread = 0.85f;   // azimuth scatter within the chosen half (0 = axis)
    float heightVar = 0.35f; // child height jitter around decay*parent
    float spireChance = 0.28f; // chance a child is a taller stone placed behind
    int pebbles = 6;        // ground debris scattered around the cluster
    float pebbleSize = 0.26f; // pebble half-extent, fraction of the root box
};

StoneMesh generateCutStoneCluster(const StoneClusterParams& params);
