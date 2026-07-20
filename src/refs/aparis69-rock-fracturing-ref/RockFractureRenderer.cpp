#include "RockFractureRenderer.h"
#include "RockMeshGpuRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #endif
#endif
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <util/sokol_imgui.h>

#include <spdlog/spdlog.h>

namespace render_playground {

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
    if (len < 1e-6f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

struct CameraBasis {
    Vec3 right;
    Vec3 up;
    Vec3 forward; // from camera toward the scene (world space)
};

// Z-up turntable: yaw around +Z, pitch lifts camera above the XY plane.
inline CameraBasis turntableBasisZUp(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);

    CameraBasis basis{};
    basis.forward = normalizeVec({-cp * cy, -cp * sy, -sp});

    const Vec3 worldUp{0.0f, 0.0f, 1.0f};
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

void computeMeshBounds(const RockFractureModel& model, Vec3& outMin, Vec3& outMax) {
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

void RockFractureRenderer::resetViewForModel(const RockFractureModel& model) {
    Vec3 bmin{};
    Vec3 bmax{};
    computeMeshBounds(model, bmin, bmax);

    m_camera.target = {
        (bmin.x + bmax.x) * 0.5f,
        (bmin.y + bmax.y) * 0.5f,
        (bmin.z + bmax.z) * 0.5f,
    };
    m_camera.yaw = 0.785398163f;
    m_camera.pitch = 0.698131700f;

    const float spanX = bmax.x - bmin.x;
    const float spanY = bmax.y - bmin.y;
    const float spanZ = bmax.z - bmin.z;
    const float maxSpan = std::max({spanX, spanY, spanZ, 1.0f});
    m_camera.zoom = clampFloat(30.0f / maxSpan, 0.05f, 16.0f);
}

void RockFractureRenderer::initGpu() {
    m_gpuMesh.init();
}

void RockFractureRenderer::shutdownGpu() {
    m_gpuMesh.shutdown();
}

void RockFractureRenderer::invalidateGpuMeshCache() {
    m_gpuMesh.invalidateMeshCache();
    m_gpuMeshRevision = 0;
}

namespace {

inline float pointViewDepth(const Vec3& p, const Vec3& cameraPos, const Vec3& cameraForward) {
    return dotVec(subVec(p, cameraPos), cameraForward);
}

inline float triangleViewDepthCam(const Vec3& v0, const Vec3& v1, const Vec3& v2,
    const Vec3& cameraPos, const Vec3& cameraForward) {
    const Vec3 centroid{
        (v0.x + v1.x + v2.x) / 3.0f,
        (v0.y + v1.y + v2.y) / 3.0f,
        (v0.z + v1.z + v2.z) / 3.0f,
    };
    return pointViewDepth(centroid, cameraPos, cameraForward);
}

enum class SceneDrawKind : std::uint8_t {
    MeshTriangle = 0,
};

struct SceneDrawItem {
    float depth = 0.0f;
    int triIndex = -1;
};

template<typename ProjectFn>
void drawWorldGridImmediate(ImDrawList* drawList, const ProjectFn& project,
    float extent, float cellSize, int majorEvery, const Vec3& center, float groundZ) {
    if (cellSize <= 0.0f || extent <= 0.0f || majorEvery < 1) return;

    const int minCell = (int)std::floor(-extent / cellSize);
    const int maxCell = (int)std::ceil(extent / cellSize);

    const auto drawLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
        drawList->AddLine(project(a), project(b), color, thickness);
    };

    for (int i = minCell; i <= maxCell; i++) {
        if (i == 0) continue;
        const float coord = i * cellSize;
        const bool major = (i % majorEvery) == 0;
        const ImU32 color = major ? IM_COL32(88, 98, 112, 210) : IM_COL32(40, 46, 56, 150);
        const float thickness = major ? 1.15f : 0.75f;
        drawLine({center.x + coord, center.y - extent, groundZ}, {center.x + coord, center.y + extent, groundZ}, color, thickness);
        drawLine({center.x - extent, center.y + coord, groundZ}, {center.x + extent, center.y + coord, groundZ}, color, thickness);
    }

    drawLine({center.x - extent, center.y, groundZ}, {center.x + extent, center.y, groundZ}, IM_COL32(220, 92, 92, 235), 1.8f);
    drawLine({center.x, center.y - extent, groundZ}, {center.x, center.y + extent, groundZ}, IM_COL32(96, 140, 230, 235), 1.8f);
    drawLine({center.x, center.y, groundZ}, {center.x, center.y, groundZ + extent * 0.45f}, IM_COL32(118, 210, 118, 235), 1.8f);
}

template<typename ProjectFn>
void drawGroundShadowPlate(ImDrawList* drawList, const ProjectFn& project, const RockFractureModel& model) {
    if (model.meshVertices.empty()) {
        return;
    }
    const float z = model.boundsMin.z - 0.02f;
    const float pad = 0.35f;
    const Vec3 p0{model.boundsMin.x - pad, model.boundsMin.y - pad, z};
    const Vec3 p1{model.boundsMax.x + pad, model.boundsMin.y - pad, z};
    const Vec3 p2{model.boundsMax.x + pad, model.boundsMax.y + pad, z};
    const Vec3 p3{model.boundsMin.x - pad, model.boundsMax.y + pad, z};
    drawList->AddQuadFilled(project(p0), project(p1), project(p2), project(p3), IM_COL32(14, 16, 20, 255));
}

struct WorldGridParams {
    float extent = 20.0f;
    float cellSize = 1.0f;
    int majorEvery = 5;
};

// Match grid footprint to generated mesh; fixed ±24 m was too small for fracture radii up to ~15 m.
WorldGridParams computeWorldGridParams(const RockFractureModel& model) {
    constexpr float kMinExtent = 16.0f;
    constexpr float kPadding = 1.25f;
    constexpr int kMajorEvery = 5;

    auto roundUpToCell = [kMinExtent](float halfExtent, float cellSize) {
        return std::max(kMinExtent, std::ceil(halfExtent / cellSize) * cellSize);
    };

    const float halfSpanX = (model.boundsMax.x - model.boundsMin.x) * 0.5f;
    const float halfSpanY = (model.boundsMax.y - model.boundsMin.y) * 0.5f;
    float halfFootprint = std::max(halfSpanX, halfSpanY);

    if (!model.meshVertices.empty()) {
        float minX = model.meshVertices[0].x;
        float maxX = minX;
        float minY = model.meshVertices[0].y;
        float maxY = minY;
        for (const Vec3& v : model.meshVertices) {
            minX = std::min(minX, v.x);
            maxX = std::max(maxX, v.x);
            minY = std::min(minY, v.y);
            maxY = std::max(maxY, v.y);
        }
        halfFootprint = std::max(
            std::max(maxX - minX, maxY - minY) * 0.5f,
            halfFootprint);
    }

    if (halfFootprint < 1.0f) {
        halfFootprint = 10.0f;
    }

    float cellSize = 1.0f;
    if (halfFootprint > 35.0f) cellSize = 2.0f;
    if (halfFootprint > 70.0f) cellSize = 5.0f;

    const float extent = roundUpToCell(halfFootprint * kPadding, cellSize);
    return {extent, cellSize, kMajorEvery};
}

inline Vec3 triNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 e0 = subVec(b, a);
    const Vec3 e1 = subVec(c, a);
    Vec3 n{
        e0.y * e1.z - e0.z * e1.y,
        e0.z * e1.x - e0.x * e1.z,
        e0.x * e1.y - e0.y * e1.x,
    };
    const float len = lengthVec(n);
    if (len < 1e-6f) return {0.0f, 0.0f, 0.0f};
    return {n.x / len, n.y / len, n.z / len};
}

inline ImU32 packColor(const Vec3& c) {
    return IM_COL32(
        std::clamp((int)(c.x * 255.0f), 0, 255),
        std::clamp((int)(c.y * 255.0f), 0, 255),
        std::clamp((int)(c.z * 255.0f), 0, 255),
        255);
}

inline Vec3 triangleNormal(const RockFractureModel& model, int triIndex, bool flatNormals) {
    const std::uint32_t i0 = model.meshIndices[(std::size_t)triIndex * 3 + 0];
    const std::uint32_t i1 = model.meshIndices[(std::size_t)triIndex * 3 + 1];
    const std::uint32_t i2 = model.meshIndices[(std::size_t)triIndex * 3 + 2];
    const Vec3& v0 = model.meshVertices[(std::size_t)i0];
    const Vec3& v1 = model.meshVertices[(std::size_t)i1];
    const Vec3& v2 = model.meshVertices[(std::size_t)i2];
    Vec3 n = triNormal(v0, v1, v2);
    if (!flatNormals && i0 < model.meshNormals.size() && i1 < model.meshNormals.size() && i2 < model.meshNormals.size()) {
        const Vec3 n0 = model.meshNormals[(std::size_t)i0];
        const Vec3 n1 = model.meshNormals[(std::size_t)i1];
        const Vec3 n2 = model.meshNormals[(std::size_t)i2];
        const Vec3 avg = normalizeVec({(n0.x + n1.x + n2.x) / 3.0f, (n0.y + n1.y + n2.y) / 3.0f, (n0.z + n1.z + n2.z) / 3.0f});
        if (lengthVec(avg) > 0.0f) {
            n = avg;
        }
    }
    return n;
}

} // namespace

void RockFractureRenderer::handleInput(const ImVec2& viewportSize, bool hovered, bool orbiting, bool panning, float wheel) {
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && wheel != 0.0f) {
        m_camera.zoom = clampFloat(m_camera.zoom * (1.0f + wheel * 0.12f), 0.05f, 16.0f);
    }

    const CameraBasis basis = turntableBasisZUp(m_camera.yaw, m_camera.pitch);
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
    (void)viewportSize;
}

void RockFractureRenderer::drawDebugView(const RockFractureModel& model, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(20, 23, 28, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    if (model.sampleCount == 0 && model.fractureCount == 0 && model.clusterCount == 0) {
        if (building) {
            char line[192];
            std::snprintf(line, sizeof(line), "Building mesh, please wait... %.1fs (%s)", buildElapsed, buildStage ? buildStage : "");
            drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(220, 168, 90, 255), line);
        } else {
            drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(220, 168, 90, 255),
                "No data. Click 'Regenerate' in the control panel.");
        }
        drawList->PopClipRect();
        ImGui::Dummy(viewportSize);
        return;
    }

    const float halfSpanX = (model.boundsMax.x - model.boundsMin.x) * 0.5f;
    const float halfSpanZ = (model.boundsMax.z - model.boundsMin.z) * 0.5f;
    const float centerX = (model.boundsMin.x + model.boundsMax.x) * 0.5f;
    const float centerZ = (model.boundsMin.z + model.boundsMax.z) * 0.5f;
    float worldHalf = std::max(halfSpanX, halfSpanZ);
    if (worldHalf < 1.0f) {
        worldHalf = 10.0f;
    }
    const float padTop = 32.0f;
    const float padLeft = 12.0f;
    const float availableW = std::max(1.0f, viewportSize.x - padLeft * 2.0f);
    const float availableH = std::max(1.0f, viewportSize.y - padTop - 12.0f);
    const float cellSize = std::min(availableW / (worldHalf * 2.0f), availableH / (worldHalf * 2.0f));
    const ImVec2 gridOrigin{
        origin.x + padLeft + (availableW - cellSize * worldHalf * 2.0f) * 0.5f,
        origin.y + padTop + (availableH - cellSize * worldHalf * 2.0f) * 0.5f,
    };
    auto worldToScreen = [&](float x, float z) {
        return ImVec2{
            gridOrigin.x + (x - centerX + worldHalf) * cellSize,
            gridOrigin.y + (z - centerZ + worldHalf) * cellSize,
        };
    };

    const ImVec2 a = worldToScreen(centerX - worldHalf, centerZ - worldHalf);
    const ImVec2 b = worldToScreen(centerX + worldHalf, centerZ - worldHalf);
    const ImVec2 c = worldToScreen(centerX + worldHalf, centerZ + worldHalf);
    const ImVec2 d = worldToScreen(centerX - worldHalf, centerZ + worldHalf);
    drawList->AddQuad(a, b, c, d, IM_COL32(54, 60, 70, 200), 1.2f);
    const ImVec2 ox0 = worldToScreen(centerX - worldHalf, centerZ);
    const ImVec2 ox1 = worldToScreen(centerX + worldHalf, centerZ);
    const ImVec2 oz0 = worldToScreen(centerX, centerZ - worldHalf);
    const ImVec2 oz1 = worldToScreen(centerX, centerZ + worldHalf);
    drawList->AddLine(ox0, ox1, IM_COL32(58, 64, 78, 180), 1.0f);
    drawList->AddLine(oz0, oz1, IM_COL32(58, 64, 78, 180), 1.0f);

    if (m_shading.showFractures2d && model.generationMode != GenerationMode::CliffScene) {
        for (int i = 0; i < model.fractureCount; i++) {
            const Vec3& p = model.fractureCenters[(std::size_t)i];
            drawList->AddCircleFilled(worldToScreen(p.x, p.z), 2.0f, IM_COL32(220, 168, 90, 220));
        }
    }
    if (m_shading.showSamples2d && model.generationMode != GenerationMode::CliffScene) {
        for (int i = 0; i < model.sampleCount; i++) {
            const Vec3& p = model.samples[(std::size_t)i];
            drawList->AddCircleFilled(worldToScreen(p.x, p.z), 1.3f, IM_COL32(150, 200, 255, 200));
        }
    }
    if (model.generationMode == GenerationMode::CliffScene) {
        const ImU32 cliffColor = IM_COL32(255, 120, 90, 230);
        const float z0 = model.boundsMin.z;
        const float z1 = model.boundsMax.z;

        if (model.replicationMode == CliffReplicationMode::AllVerticalFaces) {
            const ImVec2 negX0 = worldToScreen(model.boundsMin.x, z0);
            const ImVec2 negX1 = worldToScreen(model.boundsMin.x, z1);
            const ImVec2 posX0 = worldToScreen(model.boundsMax.x, z0);
            const ImVec2 posX1 = worldToScreen(model.boundsMax.x, z1);
            drawList->AddLine(negX0, negX1, cliffColor, 2.0f);
            drawList->AddLine(posX0, posX1, cliffColor, 2.0f);
            drawList->AddText({origin.x + 12.0f, origin.y + 48.0f}, IM_COL32(255, 180, 120, 255),
                "Orange lines = stone cliff target walls (4 vertical faces, XZ profile)");
        } else {
            float cliffLineX = model.cliffInset;
            if (model.cliffFace == CliffFace::PosX) {
                cliffLineX = model.boundsMax.x;
            }
            if (model.cliffFace == CliffFace::NegX || model.cliffFace == CliffFace::PosX) {
                const ImVec2 wallBase = worldToScreen(cliffLineX, z0);
                const ImVec2 wallTop = worldToScreen(cliffLineX, z1);
                drawList->AddLine(wallBase, wallTop, cliffColor, 2.0f);
            }
            drawList->AddText({origin.x + 12.0f, origin.y + 48.0f}, IM_COL32(255, 180, 120, 255),
                "Orange line = single debug cliff face (XZ profile)");
        }
    }
    if (model.generationMode == GenerationMode::CliffScene && (m_shading.showSamples2d || m_shading.showFractures2d)) {
        drawList->AddText({origin.x + 12.0f, origin.y + 64.0f}, IM_COL32(150, 162, 180, 255),
            "Top view: tile samples hidden in Cliff scene mode");
    }
    if (model.generationMode != GenerationMode::CliffScene) {
        for (int i = 0; i < model.clusterCount; i++) {
            const Vec3& p = model.clusterCenters[(std::size_t)i];
            drawList->AddCircle(worldToScreen(p.x, p.z), 8.0f, IM_COL32(126, 220, 150, 220), 12, 1.4f);
        }
    }

    char legend[192];
    std::snprintf(legend, sizeof(legend),
        "cyan=samples, orange=fractures, green rings=clusters  |  samples=%d, fractures=%d, clusters=%d",
        model.sampleCount, model.fractureCount, model.clusterCount);
    drawList->AddText({origin.x + 12.0f, origin.y + viewportSize.y - 18.0f}, IM_COL32(150, 162, 180, 220), legend);

    drawList->PopClipRect();
    ImGui::Dummy(viewportSize);
}

void RockFractureRenderer::drawMeshView(const RockFractureModel& model, const ImVec2& viewportSize, bool building, double buildElapsed, const char* buildStage) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##RockFractureMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const bool shiftHeld = io.KeyShift;
    const bool orbiting = hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !shiftHeld;
    const bool panning = hovered && (
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)
        || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && shiftHeld)
        || ImGui::IsMouseDragging(ImGuiMouseButton_Right));
    const float wheel = io.MouseWheel;
    handleInput(viewportSize, hovered, orbiting, panning, wheel);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    // First-call visibility log: confirm the renderer is being invoked and with what data.
    static int s_callCount = 0;
    if (s_callCount < 1) {
        spdlog::info("drawMeshView: first call viewport={}x{} building={} | model: tri={} v={} samples={} frac={} clu={} failed={}",
            (int)viewportSize.x, (int)viewportSize.y, building ? "y" : "n",
            model.triangleCount, model.vertexCount,
            model.sampleCount, model.fractureCount, model.clusterCount,
            model.generationFailed ? "y" : "n");
    }
    s_callCount++;

    const ImVec2 viewCenter{origin.x + viewportSize.x * 0.5f, origin.y + viewportSize.y * 0.5f};
    const CameraBasis basis = turntableBasisZUp(m_camera.yaw, m_camera.pitch);
    const float zoom = m_camera.zoom;

    auto project = [&](const Vec3& point) {
        const Vec3 local = subVec(point, m_camera.target);
        const float viewX = dotVec(local, basis.right);
        const float viewY = dotVec(local, basis.up);
        return ImVec2{
            viewCenter.x + viewX * kOrthoScale * zoom,
            viewCenter.y - viewY * kOrthoScale * zoom,
        };
    };

    const Vec3 viewDir = normalizeVec({-basis.forward.x, -basis.forward.y, -basis.forward.z});
    const Vec3 lightDir = normalizeVec(m_shading.lightDir);
    const float orbitDistance = std::max(8.0f, 48.0f / std::max(0.1f, zoom));
    const Vec3 cameraPos{
        m_camera.target.x - basis.forward.x * orbitDistance,
        m_camera.target.y - basis.forward.y * orbitDistance,
        m_camera.target.z - basis.forward.z * orbitDistance,
    };

    // Pass 1: background (grid + shadow plate). Never interleave with mesh — that caused see-through.
    if (m_shading.showWorldGrid) {
        const WorldGridParams grid = computeWorldGridParams(model);
        const float groundZ = model.meshVertices.empty() ? 0.0f : model.boundsMin.z;
        drawWorldGridImmediate(drawList, project, grid.extent, grid.cellSize, grid.majorEvery,
            m_camera.target, groundZ);
    }
    drawGroundShadowPlate(drawList, project, model);

    const int triCount = model.meshIndices.size() >= 3
        ? static_cast<int>(model.meshIndices.size() / 3)
        : 0;

    const bool flatNormals = model.generationMode == GenerationMode::CliffScene && model.enableBlockReplication;
    (void)flatNormals;

    static double s_lastMeshBuildSeconds = -1.0;
    if (model.buildSeconds != s_lastMeshBuildSeconds) {
        m_gpuMesh.invalidateMeshCache();
        s_lastMeshBuildSeconds = model.buildSeconds;
    }

    RockMeshGpuCamera gpuCam{};
    gpuCam.target[0] = m_camera.target.x;
    gpuCam.target[1] = m_camera.target.y;
    gpuCam.target[2] = m_camera.target.z;
    gpuCam.cameraPos[0] = cameraPos.x;
    gpuCam.cameraPos[1] = cameraPos.y;
    gpuCam.cameraPos[2] = cameraPos.z;
    gpuCam.orthoHalfHeight = viewportSize.y * 0.5f / std::max(1.0f, kOrthoScale * zoom);
    gpuCam.aspect = viewportSize.x / std::max(1.0f, viewportSize.y);

    const int vpW = std::max(1, (int)viewportSize.x);
    const int vpH = std::max(1, (int)viewportSize.y);
    const bool gpuOk = m_gpuMesh.render(model, m_shading, gpuCam, vpW, vpH);

    if (gpuOk && m_gpuMesh.validOutput()) {
        const ImTextureID texId = (ImTextureID)simgui_imtextureid_with_sampler(
            m_gpuMesh.outputView(), m_gpuMesh.outputSampler());
        drawList->AddImage(texId, origin, {origin.x + viewportSize.x, origin.y + viewportSize.y});
    } else {
        static bool s_loggedGpuFallback = false;
        if (!s_loggedGpuFallback) {
            spdlog::warn("drawMeshView: GPU mesh renderer unavailable (init={} pipeline={}), using CPU painter — expect see-through on dense meshes",
                m_gpuMesh.validOutput() ? "ok" : "no-output",
                gpuOk ? "render-failed" : "not-ready");
            s_loggedGpuFallback = true;
        }
        // Fallback: CPU painter (may show sorting artifacts on dense cliff meshes).
        std::vector<SceneDrawItem> meshItems;
        meshItems.reserve((std::size_t)triCount);
        for (int i = 0; i < triCount; i++) {
            const std::uint32_t i0 = model.meshIndices[(std::size_t)i * 3 + 0];
            const std::uint32_t i1 = model.meshIndices[(std::size_t)i * 3 + 1];
            const std::uint32_t i2 = model.meshIndices[(std::size_t)i * 3 + 2];
            if (i0 >= model.meshVertices.size() || i1 >= model.meshVertices.size() || i2 >= model.meshVertices.size()) {
                continue;
            }
            const Vec3& v0 = model.meshVertices[(std::size_t)i0];
            const Vec3& v1 = model.meshVertices[(std::size_t)i1];
            const Vec3& v2 = model.meshVertices[(std::size_t)i2];

            const Vec3 n = triangleNormal(model, i, flatNormals);
            if (dotVec(n, viewDir) <= 1e-5f) {
                continue;
            }

            SceneDrawItem item{};
            item.depth = triangleViewDepthCam(v0, v1, v2, cameraPos, basis.forward);
            item.triIndex = i;
            meshItems.push_back(item);
        }

        std::sort(meshItems.begin(), meshItems.end(), [](const SceneDrawItem& a, const SceneDrawItem& b) {
            return a.depth > b.depth;
        });

        const float ambientK = m_shading.ambientStrength;
        const float diffuseK = m_shading.diffuseStrength;
        const float specK = m_shading.specularStrength;
        const float shininess = std::max(1.0f, m_shading.shininess);
        const float rimK = m_shading.rimStrength;
        const float rimP = std::max(0.5f, m_shading.rimPower);
        const float groundShadow = m_shading.groundShadowStrength;
        const float fog = m_shading.fogStrength;
        const Vec3 sky = m_shading.skyColor;
        const Vec3 ground = m_shading.groundColor;

        for (const SceneDrawItem& item : meshItems) {
            const int index = item.triIndex;
            const std::uint32_t i0 = model.meshIndices[(std::size_t)index * 3 + 0];
            const std::uint32_t i1 = model.meshIndices[(std::size_t)index * 3 + 1];
            const std::uint32_t i2 = model.meshIndices[(std::size_t)index * 3 + 2];
            const Vec3& v0 = model.meshVertices[(std::size_t)i0];
            const Vec3& v1 = model.meshVertices[(std::size_t)i1];
            const Vec3& v2 = model.meshVertices[(std::size_t)i2];
            const Vec3 n = triangleNormal(model, index, flatNormals);
            const float upDot = std::max(0.0f, n.z * 0.5f + 0.5f);
            Vec3 ambient{sky.x * upDot + ground.x * (1.0f - upDot),
                         sky.y * upDot + ground.y * (1.0f - upDot),
                         sky.z * upDot + ground.z * (1.0f - upDot)};
            const float ndotl = std::max(0.0f, dotVec(n, lightDir));
            Vec3 diffuse{m_shading.rockTint.x * diffuseK * ndotl,
                         m_shading.rockTint.y * diffuseK * ndotl,
                         m_shading.rockTint.z * diffuseK * ndotl};
            const Vec3 halfVec = normalizeVec({lightDir.x + viewDir.x, lightDir.y + viewDir.y, lightDir.z + viewDir.z});
            const float ndoth = std::max(0.0f, dotVec(n, halfVec));
            const float specAmount = std::pow(ndoth, shininess) * specK;
            Vec3 specular{specAmount, specAmount, specAmount};
            const float rim = std::pow(1.0f - std::max(0.0f, dotVec(n, viewDir)), rimP) * rimK;
            const float avgZ = (v0.z + v1.z + v2.z) / 3.0f;
            const float boundsMidZ = (model.boundsMin.z + model.boundsMax.z) * 0.5f;
            const float boundsHalfZ = std::max(0.5f, (model.boundsMax.z - model.boundsMin.z) * 0.5f);
            const float groundFactor = clampFloat((avgZ - boundsMidZ + boundsHalfZ) / (2.0f * boundsHalfZ), 0.0f, 1.0f);
            const float shadowFactor = (1.0f - groundFactor) * groundShadow;
            Vec3 shaded{
                ambient.x * ambientK + diffuse.x + specular.x + rim,
                ambient.y * ambientK + diffuse.y + specular.y + rim,
                ambient.z * ambientK + diffuse.z + specular.z + rim,
            };
            shaded = {
                shaded.x * (1.0f - shadowFactor) + ground.x * shadowFactor,
                shaded.y * (1.0f - shadowFactor) + ground.y * shadowFactor,
                shaded.z * (1.0f - shadowFactor) + ground.z * shadowFactor,
            };
            const ImVec2 s0 = project(v0);
            const ImVec2 s1 = project(v1);
            const ImVec2 s2 = project(v2);
            drawList->AddTriangleFilled(s0, s1, s2, packColor(shaded));
        }
    }

    if (triCount == 0 && !m_shading.showWorldGrid) {
        if (building) {
            char line[192];
            std::snprintf(line, sizeof(line), "Building mesh, please wait... %.1fs (%s)", buildElapsed, buildStage ? buildStage : "");
            drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 168, 90, 255), line);
            drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255),
                "MC at res=100 typically takes 5-15 seconds.");
        } else {
            const char* msg = model.generationFailed
                ? "Generation failed. See log."
                : "No mesh. Click 'Regenerate' in the control panel.";
            drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 168, 90, 255), msg);
        }
        drawList->PopClipRect();
        return;
    }

    static int s_drawnCount = 0;
    if (s_drawnCount < 1) {
        spdlog::info("drawMeshView: gpu={} tri={} zoom={} target=({:.1f},{:.1f},{:.1f})",
            gpuOk ? "yes" : "no", triCount, m_camera.zoom,
            m_camera.target.x, m_camera.target.y, m_camera.target.z);
    }
    s_drawnCount++;

    char titleText[160];
    std::snprintf(titleText, sizeof(titleText), "Rock mesh (tri=%d, v=%d, renderer=%s)",
        model.triangleCount, model.vertexCount, gpuOk ? "GPU+depth" : "CPU painter");
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

} // namespace render_playground
