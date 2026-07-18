#include "LandBrush.h"

#include <algorithm>

#include "DiamondIso.h"

void LandBrush::reset(int width, int height) {
    m_width = std::clamp(width, 4, 96);
    m_height = std::clamp(height, 4, 96);
    m_nodes.assign(static_cast<std::size_t>(m_width + 1) * static_cast<std::size_t>(m_height + 1), 0);
    m_cellTypes.assign(
        static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height),
        landscape_core::LandscapeTileType::Unknown);
}

void LandBrush::clear() {
    std::fill(m_nodes.begin(), m_nodes.end(), 0);
    refreshAllCells();
}

bool LandBrush::isNodeEditable(glm::ivec2 node) const {
    return node.x > 0 && node.y > 0 && node.x < m_width && node.y < m_height;
}

bool LandBrush::nodeIsOn(glm::ivec2 node) const {
    return nodeInBounds(node) && m_nodes[static_cast<std::size_t>(nodeIndex(node))] != 0;
}

bool LandBrush::setNode(glm::ivec2 node, bool on) {
    if (!isNodeEditable(node)) {
        return false;
    }

    std::uint8_t& current = m_nodes[static_cast<std::size_t>(nodeIndex(node))];
    const std::uint8_t next = on ? 1 : 0;
    if (current == next) {
        return false;
    }

    current = next;
    for (const glm::ivec2 cell : affectedCells(node)) {
        refreshCell(cell);
    }
    return true;
}

landscape_core::LandscapeTileType LandBrush::cellTypeAt(glm::ivec2 cell) const {
    if (!cellInBounds(cell)) {
        return landscape_core::LandscapeTileType::Unknown;
    }
    return m_cellTypes[static_cast<std::size_t>(cellIndex(cell))];
}

std::array<bool, 4> LandBrush::nodeMaskAt(glm::ivec2 cell) const {
    if (!cellInBounds(cell)) {
        return {};
    }

    const auto corners = DiamondIso::cellCornerNodes(cell);
    return {
        nodeIsOn(corners[0]), // Left
        nodeIsOn(corners[1]), // Up
        nodeIsOn(corners[2]), // Right
        nodeIsOn(corners[3]), // Down
    };
}

std::array<glm::ivec2, 4> LandBrush::affectedCells(glm::ivec2 node) const {
    return DiamondIso::nodeNeighbourCells(node);
}

int LandBrush::atlasIndexByType(landscape_core::LandscapeTileType type) {
    using T = landscape_core::LandscapeTileType;
    switch (type) {
    case T::Full:
        return 0;
    case T::DownLack:
        return 4;
    case T::LeftLack:
        return 5;
    case T::UpLack:
        return 6;
    case T::RightLack:
        return 7;
    case T::UpCorner:
        return 8;
    case T::RightCorner:
        return 9;
    case T::DownCorner:
        return 10;
    case T::LeftCorner:
        return 11;
    case T::RightUpLine:
        return 12;
    case T::RightDownLine:
        return 13;
    case T::LeftDownLine:
        return 14;
    case T::LeftUpLine:
        return 15;
    case T::UpAndDownCorners:
        return 20;
    case T::LeftRightCorners:
        return 21;
    case T::Unknown:
    default:
        return -1;
    }
}

int LandBrush::onNodeCount() const {
    return static_cast<int>(std::count_if(m_nodes.begin(), m_nodes.end(), [](std::uint8_t v) {
        return v != 0;
    }));
}

int LandBrush::cellTypeCount(landscape_core::LandscapeTileType type) const {
    return static_cast<int>(std::count(m_cellTypes.begin(), m_cellTypes.end(), type));
}

bool LandBrush::nodeInBounds(glm::ivec2 node) const {
    return node.x >= 0 && node.y >= 0 && node.x <= m_width && node.y <= m_height;
}

bool LandBrush::cellInBounds(glm::ivec2 cell) const {
    return cell.x >= 0 && cell.y >= 0 && cell.x < m_width && cell.y < m_height;
}

int LandBrush::nodeIndex(glm::ivec2 node) const {
    return node.y * (m_width + 1) + node.x;
}

int LandBrush::cellIndex(glm::ivec2 cell) const {
    return cell.y * m_width + cell.x;
}

void LandBrush::refreshCell(glm::ivec2 cell) {
    if (!cellInBounds(cell)) {
        return;
    }
    m_cellTypes[static_cast<std::size_t>(cellIndex(cell))] =
        landscape_core::nodeMaskToTileType(nodeMaskAt(cell));
}

void LandBrush::refreshAllCells() {
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            refreshCell({x, y});
        }
    }
}
