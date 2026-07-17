#include "pch.h"

#include "LandscapeModel.h"

namespace landscape3d {

void LandscapeModel::reset(int width, int height) {
    m_width = std::clamp(width, 4, 96);
    m_height = std::clamp(height, 4, 96);
    m_nodes.assign((std::size_t)(m_width + 1) * (std::size_t)(m_height + 1), 0);
    m_cellTypes.assign(
        (std::size_t)m_width * (std::size_t)m_height,
        landscape_core::LandscapeTileType::Unknown);
}

void LandscapeModel::clear() {
    std::fill(m_nodes.begin(), m_nodes.end(), 0);
    refreshAllCells();
}

void LandscapeModel::loadSample() {
    clear();
    const float centerX = (float)m_width * 0.5f;
    const float centerY = (float)m_height * 0.5f;
    const float radiusX = std::max(2.0f, (float)m_width * 0.22f);
    const float radiusY = std::max(2.0f, (float)m_height * 0.28f);

    for (int y = 1; y < m_height; ++y) {
        for (int x = 1; x < m_width; ++x) {
            const float dx = ((float)x - centerX) / radiusX;
            const float dy = ((float)y - centerY) / radiusY;
            const bool insideMainPlateau = dx * dx + dy * dy < 1.0f;
            const bool addRidge = x > m_width / 2 && y > m_height / 3 &&
                y < (m_height * 2) / 3 && x < (m_width * 3) / 4;
            m_nodes[(std::size_t)nodeIndex({x, y})] = insideMainPlateau || addRidge ? 1 : 0;
        }
    }
    refreshAllCells();
}

bool LandscapeModel::isNodeEditable(GridPoint node) const {
    return node.x > 0 && node.y > 0 && node.x < m_width && node.y < m_height;
}

bool LandscapeModel::nodeIsHigh(GridPoint node) const {
    return nodeInBounds(node) && m_nodes[(std::size_t)nodeIndex(node)] != 0;
}

bool LandscapeModel::setNodeHigh(GridPoint node, bool high) {
    if (!isNodeEditable(node)) {
        return false;
    }

    std::uint8_t& current = m_nodes[(std::size_t)nodeIndex(node)];
    const std::uint8_t next = high ? 1 : 0;
    if (current == next) {
        return false;
    }

    current = next;
    for (const GridPoint cell : affectedCells(node)) {
        refreshCell(cell);
    }
    return true;
}

landscape_core::LandscapeTileType LandscapeModel::cellTypeAt(GridPoint cell) const {
    if (!cellInBounds(cell)) {
        return landscape_core::LandscapeTileType::Unknown;
    }
    return m_cellTypes[(std::size_t)cellIndex(cell)];
}

std::array<bool, 4> LandscapeModel::nodeMaskAt(GridPoint cell) const {
    if (!cellInBounds(cell)) {
        return {};
    }

    return {
        nodeIsHigh({cell.x, cell.y + 1}), // Left
        nodeIsHigh({cell.x, cell.y}),     // Up
        nodeIsHigh({cell.x + 1, cell.y}), // Right
        nodeIsHigh({cell.x + 1, cell.y + 1}), // Down
    };
}

std::array<GridPoint, 4> LandscapeModel::affectedCells(GridPoint node) const {
    return {
        GridPoint{node.x, node.y},
        GridPoint{node.x - 1, node.y},
        GridPoint{node.x, node.y - 1},
        GridPoint{node.x - 1, node.y - 1},
    };
}

int LandscapeModel::highNodeCount() const {
    return (int)std::count_if(m_nodes.begin(), m_nodes.end(), [](std::uint8_t value) {
        return value != 0;
    });
}

int LandscapeModel::cellTypeCount(landscape_core::LandscapeTileType type) const {
    return (int)std::count(m_cellTypes.begin(), m_cellTypes.end(), type);
}

bool LandscapeModel::nodeInBounds(GridPoint node) const {
    return node.x >= 0 && node.y >= 0 && node.x <= m_width && node.y <= m_height;
}

bool LandscapeModel::cellInBounds(GridPoint cell) const {
    return cell.x >= 0 && cell.y >= 0 && cell.x < m_width && cell.y < m_height;
}

int LandscapeModel::nodeIndex(GridPoint node) const {
    return node.y * (m_width + 1) + node.x;
}

int LandscapeModel::cellIndex(GridPoint cell) const {
    return cell.y * m_width + cell.x;
}

void LandscapeModel::refreshCell(GridPoint cell) {
    if (!cellInBounds(cell)) {
        return;
    }
    m_cellTypes[(std::size_t)cellIndex(cell)] = landscape_core::nodeMaskToTileType(nodeMaskAt(cell));
}

void LandscapeModel::refreshAllCells() {
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            refreshCell({x, y});
        }
    }
}

} // namespace landscape3d
