#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <landscape_core/landscape_logic.h>

// --- Sub-cell shape of a partially filled cell ---------------------------
// A cell is a diamond with a node in each corner, and a half-lit cell is
// filled only on the side of the nodes that are on. Anything derived from the
// footprint (contact AO, scatter rings) has to use this rule too, otherwise it
// works with whole cell squares and stops following the silhouette that is
// actually on screen.

// Diamond coordinates: Left (-1, 0), Up (0, -1), Right (1, 0), Down (0, 1);
// the cell is the L1 ball |x| + |y| <= 1.
//
// Coverage in [0, 1]: the node flags weighted by how close the point is to
// each corner, >= 0.5 counts as solid. Constant along rays from the cell
// centre, so the fill is a wedge — exactly what the preview atlas rasterizes
// (FlatAtlasGenerator). nodeMask is in LandBrush::nodeMaskAt order.
float diamondNodeFill(const std::array<bool, 4>& nodeMask, glm::vec2 diamond);

// The world-space unit square of a cell rotated into those coordinates. uv is
// the position inside the square, and node (cx, cy) — the Up corner — sits at
// its origin, which is what puts the diamond axes on the square's diagonals.
inline glm::vec2 cellSquareToDiamond(glm::vec2 uv) {
    return {uv.x - uv.y, uv.x + uv.y - 1.0f};
}

// Vertex-centric binary land brush — same contract as LandscapePencil / LandscapeModel.
class LandBrush {
public:
    void reset(int width, int height);
    void clear();

    int width() const { return m_width; }
    int height() const { return m_height; }

    bool isNodeEditable(glm::ivec2 node) const;
    bool nodeIsOn(glm::ivec2 node) const;
    bool setNode(glm::ivec2 node, bool on);

    landscape_core::LandscapeTileType cellTypeAt(glm::ivec2 cell) const;
    std::array<bool, 4> nodeMaskAt(glm::ivec2 cell) const;
    std::array<glm::ivec2, 4> affectedCells(glm::ivec2 node) const;

    // Atlas tile index matching SliceAsset::subTileIndexByType (Grass 4x6 atlas).
    static int atlasIndexByType(landscape_core::LandscapeTileType type);

    int onNodeCount() const;
    int cellTypeCount(landscape_core::LandscapeTileType type) const;

    // Monotonic content version: bumped by setNode/clear/reset — cheap change
    // detection for cached derivatives (e.g. the cliff-field mesh cache).
    std::uint64_t version() const { return m_version; }

private:
    bool nodeInBounds(glm::ivec2 node) const;
    bool cellInBounds(glm::ivec2 cell) const;
    int nodeIndex(glm::ivec2 node) const;
    int cellIndex(glm::ivec2 cell) const;
    void refreshCell(glm::ivec2 cell);
    void refreshAllCells();

    int m_width = 0;
    int m_height = 0;
    std::uint64_t m_version = 0;
    std::vector<std::uint8_t> m_nodes;
    std::vector<landscape_core::LandscapeTileType> m_cellTypes;
};
