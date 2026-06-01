#pragma once

#include "RenderTypes.h"
#include "RockFractureScene.h"

#include <imgui.h>

namespace render_playground {

// Camera state shared by both viewports.
struct RockFractureCamera {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
};

// Shading parameters (Tier 1 CPU lighting; GPU path can read the same struct later).
struct RockFractureShading {
    Vec3 lightDir{-0.45f, 0.85f, -0.35f};
    Vec3 skyColor{0.42f, 0.46f, 0.52f};
    Vec3 groundColor{0.18f, 0.16f, 0.14f};
    Vec3 rockTint{0.66f, 0.61f, 0.54f};
    float ambientStrength = 0.35f;
    float diffuseStrength = 0.75f;
    float specularStrength = 0.35f;
    float shininess = 24.0f;
    float rimStrength = 0.35f;
    float rimPower = 3.0f;
    float groundShadowStrength = 0.35f;
    float fogStrength = 0.18f;
    bool showWireframe = true;
    bool showSamples2d = true;
    bool showFractures2d = true;
};

// Self-contained CPU renderer. The plan is to swap this for a sokol-gfx implementation
// later while keeping the same drawDebugView / drawMeshView / handleInput contract.
class RockFractureRenderer {
public:
    RockFractureRenderer() = default;

    void drawDebugView(const RockFractureModel& model, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage);
    void drawMeshView(const RockFractureModel& model, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage);
    void handleInput(const ImVec2& viewportSize, bool hovered, bool dragging, float wheel);

    RockFractureCamera& camera() { return m_camera; }
    const RockFractureCamera& camera() const { return m_camera; }

    RockFractureShading& shading() { return m_shading; }
    const RockFractureShading& shading() const { return m_shading; }

    void resetView() { m_camera.zoom = 1.0f; m_camera.pan = {0.0f, 0.0f}; }

private:
    RockFractureCamera m_camera;
    RockFractureShading m_shading;
};

} // namespace render_playground
