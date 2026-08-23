#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <topology_core/diamond_isometry.h>

#include "NodeField.h"
#include "StoneGen.h"

// Cluster composition of the playground: the painted node silhouette becomes a
// packed set of lobes (multi-block rocks). Site size follows a distance
// transform of the silhouette (deep inside = big monolith, border = pebbles),
// a greedy spacing filter guarantees interpenetrating-but-packed placement
// (no CSG — the z-buffer resolves the contact creases), and a per-vertex
// contact-AO pass bakes the "rocks pressed against each other" darkening.

struct StoneClusterParams {
    float overlap = 0.75f;   // accept site if dist > overlap*(r_i+r_j): <1 = interpenetrate
    float radiusMul = 1.0f;  // global size multiplier on the DT-derived radius
    int maxLobes = 12;
    bool mixForms = true;    // hash-picked frustum/box/prism (+oval pebbles); off = all use gen.form
    float aoStrength = 0.35f; // 0 = AO pass is a no-op
    float aoFalloff = 0.6f;  // influence radius = r*(1+falloff) around each lobe sphere
};

struct StoneClusterResult {
    StoneMesh mesh;                    // world-space (worldPos = 0 for the renderer)
    std::vector<glm::vec3> centers;    // debug/test: accepted lobe centers
    std::vector<float> radii;          // debug/test: accepted lobe radii
};

StoneClusterResult buildCluster(
    const NodeField& nodes,
    const topology_core::DiamondIsometry& iso,
    const StoneGenParams& gen,
    const StoneClusterParams& cl);

inline StoneMesh generateCluster(
    const NodeField& nodes,
    const topology_core::DiamondIsometry& iso,
    const StoneGenParams& gen,
    const StoneClusterParams& cl) {
    return buildCluster(nodes, iso, gen, cl).mesh;
}
