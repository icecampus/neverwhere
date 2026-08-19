#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <glm/glm.hpp>

// Vertex-node land field of the playground: a dense (mapW+1)x(mapH+1) byte
// grid of PLATEAU LEVELS (0 = empty, 1..kMaxNodeLevel = stacked terraces)
// with a monotonic content version — the same vertex-centric contract the
// editor's LandscapeModel and the SDF playgrounds' LandBrush expose, reduced
// to the minimum the B-rep pipeline needs.
constexpr std::uint8_t kMaxNodeLevel = 3;

struct NodeField {
    int width = 0;  // nodes along x (cells + 1)
    int height = 0; // nodes along y (cells + 1)
    std::vector<std::uint8_t> nodes;
    std::uint64_t version = 0;

    void reset(int w, int h) {
        width = w;
        height = h;
        nodes.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        ++version;
    }

    void clear() {
        std::fill(nodes.begin(), nodes.end(), std::uint8_t{0});
        ++version;
    }

    bool inBounds(glm::ivec2 node) const {
        return node.x >= 0 && node.y >= 0 && node.x < width && node.y < height;
    }

    std::uint8_t levelAt(glm::ivec2 node) const {
        return inBounds(node) ? nodes[static_cast<std::size_t>(node.y) * width + node.x] : std::uint8_t{0};
    }

    bool isOn(glm::ivec2 node) const {
        return levelAt(node) != 0;
    }

    bool setNode(glm::ivec2 node, int level) {
        if (!inBounds(node)) {
            return false;
        }
        std::uint8_t& slot = nodes[static_cast<std::size_t>(node.y) * width + node.x];
        const std::uint8_t value = static_cast<std::uint8_t>(std::clamp(level, 0, (int)kMaxNodeLevel));
        if (slot == value) {
            return false;
        }
        slot = value;
        ++version;
        return true;
    }

    int onNodeCount() const {
        int count = 0;
        for (std::uint8_t node : nodes) {
            count += node != 0 ? 1 : 0;
        }
        return count;
    }

    // Highest painted level in the field (0 = empty): the composer builds one
    // plateau band per level up to this.
    std::uint8_t maxLevel() const {
        std::uint8_t level = 0;
        for (std::uint8_t node : nodes) {
            level = std::max(level, node);
        }
        return level;
    }
};

// Paint the node line between two drag events: Bresenham, so a fast stroke
// leaves no holes in the node field (the "holey dashes" gotcha of the
// per-event painting).
inline void paintNodeLine(NodeField& field, glm::ivec2 a, glm::ivec2 b, int level) {
    int x0 = a.x;
    int y0 = a.y;
    const int x1 = b.x;
    const int y1 = b.y;
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        field.setNode({x0, y0}, level);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
