#include "IvtRenderer.h"
#include "IvtGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #endif
#endif
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <util/sokol_imgui.h>

#include <spdlog/spdlog.h>

namespace ivt_view {
namespace {

inline float dotVec(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 subVec(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

inline float lengthVec(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

inline Vec3 crossVec(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline Vec3 normalizeVec(const Vec3& v) {
    const float len = lengthVec(v);
    if (len < 1e-6f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

struct CameraBasis {
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

CameraBasis turntableBasisYUp(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);

    CameraBasis basis{};
    basis.forward = normalizeVec({-cp * sy, -sp, -cp * cy});

    const Vec3 worldUp{0.0f, 1.0f, 0.0f};
    basis.right = crossVec(worldUp, basis.forward);
    const float rightLen = lengthVec(basis.right);
    if (rightLen < 1e-5f) {
        basis.right = {1.0f, 0.0f, 0.0f};
    } else {
        basis.right = {basis.right.x / rightLen, basis.right.y / rightLen, basis.right.z / rightLen};
    }
    basis.up = normalizeVec(crossVec(basis.forward, basis.right));
    return basis;
}

constexpr float kOrthoScale = 42.0f;
constexpr float kMinZoom = 0.02f;
constexpr float kMaxZoom = 32.0f;

Vec3 worldPointOnViewPlane(const IvtCamera& camera, const ImVec2& viewportOrigin, const ImVec2& viewportSize, float screenX, float screenY) {
    const CameraBasis basis = turntableBasisYUp(camera.yaw, camera.pitch);
    const float viewCenterX = viewportOrigin.x + viewportSize.x * 0.5f;
    const float viewCenterY = viewportOrigin.y + viewportSize.y * 0.5f;
    const float scale = std::max(1.0f, kOrthoScale * camera.zoom);
    const float viewX = (screenX - viewCenterX) / scale;
    const float viewY = -(screenY - viewCenterY) / scale;
    return {
        camera.target.x + basis.right.x * viewX + basis.up.x * viewY,
        camera.target.y + basis.right.y * viewX + basis.up.y * viewY,
        camera.target.z + basis.right.z * viewX + basis.up.z * viewY,
    };
}

void computeMeshBounds(const IvtModel& model, Vec3& outMin, Vec3& outMax) {
    if (model.meshVertices.empty()) {
        outMin = model.boundsMin;
        outMax = model.boundsMax;
        return;
    }

    outMin = model.meshVertices[0];
    outMax = model.meshVertices[0];
    for (const Vec3& v : model.meshVertices) {
        outMin.x = std::min(outMin.x, v.x);
        outMin.y = std::min(outMin.y, v.y);
        outMin.z = std::min(outMin.z, v.z);
        outMax.x = std::max(outMax.x, v.x);
        outMax.y = std::max(outMax.y, v.y);
        outMax.z = std::max(outMax.z, v.z);
    }
}

} // namespace

void IvtRenderer::resetViewForModel(const IvtModel& model) {
    Vec3 bmin{};
    Vec3 bmax{};
    computeMeshBounds(model, bmin, bmax);

    m_camera.target = {
        (bmin.x + bmax.x) * 0.5f,
        (bmin.y + bmax.y) * 0.5f,
        (bmin.z + bmax.z) * 0.5f,
    };
    m_camera.yaw = 0.785398163f;
    m_camera.pitch = 0.55f;

    const float spanX = bmax.x - bmin.x;
    const float spanY = bmax.y - bmin.y;
    const float spanZ = bmax.z - bmin.z;
    const float maxSpan = std::max({spanX, spanY, spanZ, 1.0f});
    m_camera.zoom = clampFloat(30.0f / maxSpan, kMinZoom, kMaxZoom);
}

void IvtRenderer::initGpu() {
    m_gpuMesh.init();
}

void IvtRenderer::shutdownGpu() {
    m_gpuMesh.shutdown();
}

void IvtRenderer::invalidateGpuMeshCache() {
    m_gpuMesh.invalidateMeshCache();
}

void IvtRenderer::handleInput(const ImVec2& viewportOrigin, const ImVec2& viewportSize, bool hovered, bool orbiting, bool panning, float wheel) {
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && wheel != 0.0f) {
        const ImVec2& mouse = io.MousePos;
        const Vec3 anchorBefore = worldPointOnViewPlane(m_camera, viewportOrigin, viewportSize, mouse.x, mouse.y);
        const float newZoom = clampFloat(m_camera.zoom * std::exp(wheel * 0.14f), kMinZoom, kMaxZoom);
        IvtCamera zoomCam = m_camera;
        zoomCam.zoom = newZoom;
        const Vec3 anchorAfter = worldPointOnViewPlane(zoomCam, viewportOrigin, viewportSize, mouse.x, mouse.y);
        m_camera.target.x += anchorBefore.x - anchorAfter.x;
        m_camera.target.y += anchorBefore.y - anchorAfter.y;
        m_camera.target.z += anchorBefore.z - anchorAfter.z;
        m_camera.zoom = newZoom;
    }

    const CameraBasis basis = turntableBasisYUp(m_camera.yaw, m_camera.pitch);
    const float worldPerPixel = 1.0f / std::max(1.0f, kOrthoScale * m_camera.zoom);

    if (hovered && panning) {
        const float dx = io.MouseDelta.x * worldPerPixel;
        const float dy = io.MouseDelta.y * worldPerPixel;
        m_camera.target.x += basis.right.x * (-dx) + basis.up.x * dy;
        m_camera.target.y += basis.right.y * (-dx) + basis.up.y * dy;
        m_camera.target.z += basis.right.z * (-dx) + basis.up.z * dy;
    }

    if (hovered && orbiting) {
        constexpr float kOrbitSensitivity = 0.008f;
        m_camera.yaw += io.MouseDelta.x * kOrbitSensitivity;
        m_camera.pitch = clampFloat(
            m_camera.pitch - io.MouseDelta.y * kOrbitSensitivity, -1.25f, 1.35f);
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        resetView();
    }
}

void IvtRenderer::renderMeshGpu(const IvtModel& model, const ImVec2& viewportSize) {
    static double s_lastMeshBuildSeconds = -1.0;
    if (model.buildSeconds != s_lastMeshBuildSeconds) {
        m_gpuMesh.invalidateMeshCache();
        s_lastMeshBuildSeconds = model.buildSeconds;
    }

    const CameraBasis basis = turntableBasisYUp(m_camera.yaw, m_camera.pitch);
    const float zoom = m_camera.zoom;
    const float orbitDistance = std::max(8.0f, 48.0f / std::max(0.1f, zoom));
    const Vec3 cameraPos{
        m_camera.target.x - basis.forward.x * orbitDistance,
        m_camera.target.y - basis.forward.y * orbitDistance,
        m_camera.target.z - basis.forward.z * orbitDistance,
    };

    IvtMeshGpuCamera gpuCam{};
    gpuCam.target[0] = m_camera.target.x;
    gpuCam.target[1] = m_camera.target.y;
    gpuCam.target[2] = m_camera.target.z;
    gpuCam.cameraPos[0] = cameraPos.x;
    gpuCam.cameraPos[1] = cameraPos.y;
    gpuCam.cameraPos[2] = cameraPos.z;
    gpuCam.orthoHalfHeight = viewportSize.y * 0.5f / std::max(1.0f, kOrthoScale * zoom);
    gpuCam.aspect = viewportSize.x / std::max(1.0f, viewportSize.y);

    const WorldGridParams grid = computeWorldGridParams(model, m_camera.target);
    const int vpW = std::max(1, (int)viewportSize.x);
    const int vpH = std::max(1, (int)viewportSize.y);
    m_lastGpuOk = m_gpuMesh.render(model, m_shading, gpuCam, grid, m_showWorldGrid, vpW, vpH)
        && m_gpuMesh.validOutput();
}

bool IvtRenderer::handleMeshViewInput(const ImVec2& viewportOrigin, const ImVec2& viewportSize) {
    ImGui::InvisibleButton("##IvtMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const bool shiftHeld = io.KeyShift;
    const bool orbiting = hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !shiftHeld;
    const bool panning = hovered && (
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
        || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && shiftHeld)
        || ImGui::IsMouseDragging(ImGuiMouseButton_Right));
    handleInput(viewportOrigin, viewportSize, hovered, orbiting, panning, io.MouseWheel);
    return hovered;
}

void IvtRenderer::drawMeshViewOverlay(const IvtModel& model, const ImVec2& viewportOrigin, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage) {
    const ImVec2 origin = viewportOrigin;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    if (!m_lastGpuOk) {
        drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    } else {
        const ImTextureID texId = (ImTextureID)simgui_imtextureid_with_sampler(
            m_gpuMesh.outputView(), m_gpuMesh.outputSampler());
        drawList->AddImage(texId, origin, {origin.x + viewportSize.x, origin.y + viewportSize.y});
    }

    const int triCount = model.triangleCount;
    if (triCount == 0) {
        if (building) {
            char line[192];
            std::snprintf(line, sizeof(line), "Building mesh, please wait... %.1fs (%s)", buildElapsed, buildStage ? buildStage : "");
            drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 168, 90, 255), line);
            drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255),
                "Marching cubes can take several seconds at high resolution.");
        } else {
            const char* msg = model.generationFailed
                ? "Generation failed. See log."
                : "No mesh. Click 'Regenerate' in the control panel.";
            drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 168, 90, 255), msg);
        }
        drawList->PopClipRect();
        return;
    }

    char titleText[160];
    std::snprintf(titleText, sizeof(titleText), "IVT terrain (tri=%d, v=%d, renderer=%s)",
        model.triangleCount, model.vertexCount, m_lastGpuOk ? "GPU+depth" : "unavailable");
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), titleText);
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255),
        "LMB drag: orbit | Shift+LMB / MMB / RMB: pan | wheel: zoom | dbl-click: reset pivot");
    char statsText[256];
    std::snprintf(statsText, sizeof(statsText),
        "build=%.2fs, zoom=%.2fx, target=(%.1f, %.1f, %.1f), yaw=%.0f pitch=%.0f",
        model.buildSeconds, m_camera.zoom,
        m_camera.target.x, m_camera.target.y, m_camera.target.z,
        m_camera.yaw * 57.2958f, m_camera.pitch * 57.2958f);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), statsText);
    drawList->PopClipRect();
}

} // namespace ivt_view
