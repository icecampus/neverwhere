#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <landscape_core/landscape_logic.h>

namespace landscape3d {

struct GridPoint {
    int x = -1;
    int y = -1;

    friend bool operator==(const GridPoint&, const GridPoint&) = default;
};

class LandscapeModel {
public:
    void reset(int width, int height);
    void clear();
    void loadSample();

    int width() const { return m_width; }
    int height() const { return m_height; }

    bool isNodeEditable(GridPoint node) const;
    bool nodeIsHigh(GridPoint node) const;
    bool setNodeHigh(GridPoint node, bool high);

    landscape_core::LandscapeTileType cellTypeAt(GridPoint cell) const;
    std::array<bool, 4> nodeMaskAt(GridPoint cell) const;
    std::array<GridPoint, 4> affectedCells(GridPoint node) const;

    int highNodeCount() const;
    int cellTypeCount(landscape_core::LandscapeTileType type) const;

private:
    bool nodeInBounds(GridPoint node) const;
    bool cellInBounds(GridPoint cell) const;
    int nodeIndex(GridPoint node) const;
    int cellIndex(GridPoint cell) const;
    void refreshCell(GridPoint cell);
    void refreshAllCells();

    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_nodes;
    std::vector<landscape_core::LandscapeTileType> m_cellTypes;
};

} // namespace landscape3d
