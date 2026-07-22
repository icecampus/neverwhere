// Debug utility: dumps non-manifold edges of an extracted cliff mesh with the
// shared voxel-face diagnostics. Lives in the playground (not in
// highground_core) because it logs through spdlog.
#pragma once

#include <highground_core/surface_nets.h>

#include <vector>

namespace cliff {

// Logs up to maxCount bad edges of each kind with half-edge counts, vertex
// positions and the shared voxel face diagnostics (corner signs, center value).
void debugDumpBadEdges(const CliffField& field, const std::vector<float>& samples,
    const Mesh& mesh, int maxCount);

} // namespace cliff
