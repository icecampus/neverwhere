#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <glm/glm.hpp>

// Vertex-node seed field of the playground: a dense (mapW+1)x(mapH+1) byte
// grid (0 = empty, 1 = stone seed) with a monotonic content version — the
// same vertex-centric contract the editor's LandscapeModel and the B-rep
// playground's NodeField expose, reduced to binary on/off for stone seeds.
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

    bool isOn(glm::ivec2 node) const {
        return inBounds(node) && nodes[static_cast<std::size_t>(node.y) * width + node.x] != 0;
    }

    bool setNode(glm::ivec2 node, bool on) {
        if (!inBounds(node)) {
            return false;
        }
        std::uint8_t& slot = nodes[static_cast<std::size_t>(node.y) * width + node.x];
        const std::uint8_t value = on ? std::uint8_t{1} : std::uint8_t{0};
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
};

// Paint the node line between two drag events: Bresenham, so a fast stroke
// leaves no holes in the node field (the "holey dashes" gotcha of the
// per-event painting).
inline void paintNodeLine(NodeField& field, glm::ivec2 a, glm::ivec2 b, bool on) {
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
        field.setNode({x0, y0}, on);
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
