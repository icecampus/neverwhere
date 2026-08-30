#pragma once

// Mesh island splitting and rigid piece merge (stage E7: spec §5.4 foreach,
// §8.3 islands, §8.11 fracture) — pure geometry, no engine dependencies.
//
// Piece order is a contract: components are ordered by ascending minimum face
// index, and @piece_index inside a foreach body is the position in this order.
// Split gathers attributes/groups of every domain onto the piece (detail is
// shared by pointer); merge is the rigid N-way concatenation in piece order
// with the binary merge's union column semantics (no welding).

#include "geometry.h"

namespace pgg {

// Connected components of a mesh: when the faces carry an int attribute
// @island_id ("already tagged pieces", §5.4) the components are the groups by
// id, otherwise the union-find components by shared points (cornerVerts).
// Returns the dense 0-based piece id per face; outCount = number of pieces.
std::vector<int32_t> computeIslands(const Geo& mesh, size_t& outCount);

// Splits a mesh into its pieces in computeIslands order. Each piece is a
// compact sub-mesh: face/corner subset, points reindexed, attributes and
// groups of all domains gathered. Empty mesh -> no pieces.
std::vector<GeoPtr> splitMeshPieces(const Geo& mesh);

// Rigid N-way merge of meshes in piece order (no welding): union of column
// names in first-appearance order, zero-fill for missing pieces, the first
// seen type wins (others convert numerically), detail leftmost wins.
// Empty list -> empty mesh.
GeoPtr mergeMeshPieces(const std::vector<GeoPtr>& pieces);

}  // namespace pgg
