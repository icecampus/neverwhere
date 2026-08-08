#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <landscape_core/landscape_logic.h>

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
    // Tagged paint: the node carries an opaque payload (multi-texture layer:
    // tiling slot + 1, 0 = untagged). Repainting an on-node with another tag
    // displaces it (counts as a change); erasing clears the tag.
    bool setNode(glm::ivec2 node, bool on, std::uint8_t tag);
    std::uint8_t nodeTag(glm::ivec2 node) const;

    landscape_core::LandscapeTileType cellTypeAt(glm::ivec2 cell) const;
    std::array<bool, 4> nodeMaskAt(glm::ivec2 cell) const;
    std::array<glm::ivec2, 4> affectedCells(glm::ivec2 node) const;

    // Majority raw tag among the cell's on-nodes (ties broken in corner order
    // Left/Up/Right/Down); 0 when no on-node carries a tag.
    int cellTagAt(glm::ivec2 cell) const;

    // Multi-texture blend data of a cell: the distinct tags of its on-nodes
    // as texture array layers (tag - 1) in first-seen corner order
    // [Left, Up, Right, Down], plus a one-hot weight vector per corner
    // (weight[k] = 1 when the corner's tag matches candidate k). Corners
    // that are off or untagged take the first candidate's slot so the
    // interpolation stays inside the blend (their region is masked away
    // anyway). Returns the candidate count; 0 = untagged cell, render as
    // mask-color fallback. Layers/weights are constant-per-cell except the
    // corner weights, so neighboring cells sharing a node stay continuous.
    int cellTextureBlend(glm::ivec2 cell, float layers[4], glm::vec4 cornerWeights[4]) const;

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
    std::vector<std::uint8_t> m_tags; // parallel to m_nodes, meaningful where on
    std::vector<landscape_core::LandscapeTileType> m_cellTypes;
};
