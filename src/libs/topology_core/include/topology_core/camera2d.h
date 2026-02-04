#pragma once

#include <glm/glm.hpp>

namespace topology_core {

struct Camera2D {
    glm::vec2 offset{0.0f, 0.0f}; // screen-space pixels
    float zoom = 1.0f;

    glm::vec2 screenToWorld(const glm::vec2& screen) const {
        return (screen - offset) / zoom;
    }

    glm::vec2 worldToScreen(const glm::vec2& world) const {
        return world * zoom + offset;
    }
};

} // namespace topology_core

