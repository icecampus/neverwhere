#pragma once

#include "IvtMeshGpuRenderer.h"
#include "IvtScene.h"

#include <cstdint>
#include <imgui.h>

namespace ivt_view {

struct IvtCamera {
    Vec3 target{0.0f, 0.0f, 0.0f};
    float yaw = 0.785398163f;
    float pitch = 0.55f;
    float zoom = 1.0f;
};

class IvtRenderer {
public:
    bool handleMeshViewInput(const ImVec2& viewportOrigin, const ImVec2& viewportSize);
    void renderMeshGpu(const IvtModel& model, const ImVec2& viewportSize);
    void drawMeshViewOverlay(const IvtModel& model, const ImVec2& viewportOrigin, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage);
    void handleInput(const ImVec2& viewportOrigin, const ImVec2& viewportSize, bool hovered, bool orbiting, bool panning, float wheel);

    IvtCamera& camera() { return m_camera; }
    const IvtCamera& camera() const { return m_camera; }

    IvtShading& shading() { return m_shading; }
    const IvtShading& shading() const { return m_shading; }

    void resetView() {
        m_camera.target = {0.0f, 0.0f, 0.0f};
        m_camera.yaw = 0.785398163f;
        m_camera.pitch = 0.55f;
        m_camera.zoom = 1.0f;
    }

    void resetViewForModel(const IvtModel& model);

    void initGpu();
    void shutdownGpu();
    void invalidateGpuMeshCache();

private:
    IvtCamera m_camera;
    IvtShading m_shading;
    IvtMeshGpuRenderer m_gpuMesh;
    bool m_showWorldGrid = true;
    bool m_lastGpuOk = false;
};

} // namespace ivt_view
