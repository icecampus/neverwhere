#pragma once

// Playground-local stone generator. The iso-grid scaffold (camera, node
// brush, GridRenderer) is copied from B-repGeneratedLandscape; the generator
// itself is the experiment — start here. Do not reach into `stone_gen` or
// the B-rep fork until something graduates.
//
// Painted vertex-nodes are the seed silhouette the generator will consume.
struct StoneGenParams {
    int seed = 1;
};
