#include "RockFractureRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

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

// Standard orbit camera: orthonormal view frame in world space. Vertices are never rotated.
inline CameraBasis orbitCameraBasis(float yaw, float pitch) {
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

inline float triangleViewDepth(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& target, const Vec3& forward) {
    const float z0 = dotVec(subVec(v0, target), forward);
    const float z1 = dotVec(subVec(v1, target), forward);
    const float z2 = dotVec(subVec(v2, target), forward);
    return (z0 + z1 + z2) / 3.0f;
}

inline float segmentViewDepth(const Vec3& a, const Vec3& b, const Vec3& target, const Vec3& forward) {
    return (dotVec(subVec(a, target), forward) + dotVec(subVec(b, target), forward)) * 0.5f;
}

enum class SceneDrawKind : std::uint8_t {
    GridLine = 0,
    MeshTriangle = 1,
};

struct SceneDrawItem {
    float depth = 0.0f;
    SceneDrawKind kind = SceneDrawKind::MeshTriangle;
    int triIndex = -1;
    Vec3 gridA{};
    Vec3 gridB{};
    ImU32 lineColor = 0;
    float lineThickness = 1.0f;
};

template<typename EnqueueFn>
void enqueueWorldGrid(const EnqueueFn& enqueue, const Vec3& target, const Vec3& forward,
                      float extent, float cellSize, int majorEvery) {
    if (cellSize <= 0.0f || extent <= 0.0f || majorEvery < 1) return;

    const int minCell = (int)std::floor(-extent / cellSize);
    const int maxCell = (int)std::ceil(extent / cellSize);

    const auto pushLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
        SceneDrawItem item{};
        item.kind = SceneDrawKind::GridLine;
        item.depth = segmentViewDepth(a, b, target, forward);
        item.gridA = a;
        item.gridB = b;
        item.lineColor = color;
        item.lineThickness = thickness;
        enqueue(item);
    };

    for (int i = minCell; i <= maxCell; i++) {
        if (i == 0) continue;
        const float coord = i * cellSize;
        const bool major = (i % majorEvery) == 0;
        const ImU32 color = major ? IM_COL32(88, 98, 112, 210) : IM_COL32(40, 46, 56, 150);
        const float thickness = major ? 1.15f : 0.75f;
        pushLine({coord, 0.0f, -extent}, {coord, 0.0f, extent}, color, thickness);
        pushLine({-extent, 0.0f, coord}, {extent, 0.0f, coord}, color, thickness);
    }

    pushLine({-extent, 0.0f, 0.0f}, {extent, 0.0f, 0.0f}, IM_COL32(220, 92, 92, 235), 1.8f);
    pushLine({0.0f, 0.0f, -extent}, {0.0f, 0.0f, extent}, IM_COL32(96, 140, 230, 235), 1.8f);
    pushLine({0.0f, 0.0f, 0.0f}, {0.0f, extent * 0.45f, 0.0f}, IM_COL32(118, 210, 118, 235), 1.8f);
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

    auto roundUpToCell = [](float halfExtent, float cellSize) {
        return std::max(kMinExtent, std::ceil(halfExtent / cellSize) * cellSize);
    };

    float halfFootprint = 10.0f; // default tile half-size (tileSize=20)
    if (!model.meshVertices.empty()) {
        float minX = model.meshVertices[0].x;
        float maxX = minX;
        float minZ = model.meshVertices[0].z;
        float maxZ = minZ;
        for (const Vec3& v : model.meshVertices) {
            minX = std::min(minX, v.x);
            maxX = std::max(maxX, v.x);
            minZ = std::min(minZ, v.z);
            maxZ = std::max(maxZ, v.z);
        }
        halfFootprint = std::max(
            std::max(std::abs(minX), std::abs(maxX)),
            std::max(std::abs(minZ), std::abs(maxZ)));
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

} // namespace

void RockFractureRenderer::handleInput(const ImVec2& viewportSize, bool hovered, bool dragging, float wheel) {
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && wheel != 0.0f) {
        m_camera.zoom = clampFloat(m_camera.zoom * (1.0f + wheel * 0.12f), 0.05f, 16.0f);
    }
    if (dragging) {
        if (io.KeyCtrl) {
            constexpr float kOrbitSensitivity = 0.012f;
            m_camera.yaw += io.MouseDelta.x * kOrbitSensitivity;
            m_camera.pitch = clampFloat(
                m_camera.pitch - io.MouseDelta.y * kOrbitSensitivity, -1.35f, 1.35f);
        } else {
            m_camera.pan.x += io.MouseDelta.x;
            m_camera.pan.y += io.MouseDelta.y;
        }
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

    const float halfTile = 10.0f; // default; size of debug view doesn't change with tileSize to keep legend stable.
    const float worldHalf = std::max(halfTile, 0.5f);
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
            gridOrigin.x + (x + worldHalf) * cellSize,
            gridOrigin.y + (z + worldHalf) * cellSize,
        };
    };

    const ImVec2 a = worldToScreen(-worldHalf, -worldHalf);
    const ImVec2 b = worldToScreen(worldHalf, -worldHalf);
    const ImVec2 c = worldToScreen(worldHalf, worldHalf);
    const ImVec2 d = worldToScreen(-worldHalf, worldHalf);
    drawList->AddQuad(a, b, c, d, IM_COL32(54, 60, 70, 200), 1.2f);
    const ImVec2 ox0 = worldToScreen(-worldHalf, 0);
    const ImVec2 ox1 = worldToScreen(worldHalf, 0);
    const ImVec2 oz0 = worldToScreen(0, -worldHalf);
    const ImVec2 oz1 = worldToScreen(0, worldHalf);
    drawList->AddLine(ox0, ox1, IM_COL32(58, 64, 78, 180), 1.0f);
    drawList->AddLine(oz0, oz1, IM_COL32(58, 64, 78, 180), 1.0f);

    if (m_shading.showFractures2d) {
        for (int i = 0; i < model.fractureCount; i++) {
            const Vec3& p = model.fractureCenters[(std::size_t)i];
            drawList->AddCircleFilled(worldToScreen(p.x, p.z), 2.0f, IM_COL32(220, 168, 90, 220));
        }
    }
    if (m_shading.showSamples2d) {
        for (int i = 0; i < model.sampleCount; i++) {
            const Vec3& p = model.samples[(std::size_t)i];
            drawList->AddCircleFilled(worldToScreen(p.x, p.z), 1.3f, IM_COL32(150, 200, 255, 200));
        }
    }
    for (int i = 0; i < model.clusterCount; i++) {
        const Vec3& p = model.clusterCenters[(std::size_t)i];
        drawList->AddCircle(worldToScreen(p.x, p.z), 8.0f, IM_COL32(126, 220, 150, 220), 12, 1.4f);
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
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    const float wheel = ImGui::GetIO().MouseWheel;
    handleInput(viewportSize, hovered, dragging, wheel);

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

    const ImVec2 viewCenter{origin.x + viewportSize.x * 0.5f, origin.y + viewportSize.y * 0.55f};
    constexpr float kOrthoScale = 42.0f;
    const CameraBasis basis = orbitCameraBasis(m_camera.yaw, m_camera.pitch);
    const float zoom = m_camera.zoom;

    auto project = [&](const Vec3& point) {
        const Vec3 local = subVec(point, m_camera.target);
        const float viewX = dotVec(local, basis.right);
        const float viewY = dotVec(local, basis.up);
        return ImVec2{
            viewCenter.x + m_camera.pan.x + viewX * kOrthoScale * zoom,
            viewCenter.y + m_camera.pan.y - viewY * kOrthoScale * zoom,
        };
    };

    const Vec3 viewDir = normalizeVec({-basis.forward.x, -basis.forward.y, -basis.forward.z});
    const Vec3 lightDir = normalizeVec(m_shading.lightDir);

    std::vector<SceneDrawItem> drawItems;
    if (m_shading.showWorldGrid) {
        const WorldGridParams grid = computeWorldGridParams(model);
        enqueueWorldGrid(
            [&](const SceneDrawItem& item) { drawItems.push_back(item); },
            m_camera.target, basis.forward, grid.extent, grid.cellSize, grid.majorEvery);
    }

    const int triCount = model.meshIndices.size() >= 3
        ? static_cast<int>(model.meshIndices.size() / 3)
        : 0;
    drawItems.reserve(drawItems.size() + (std::size_t)std::max(0, triCount));
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
        SceneDrawItem item{};
        item.kind = SceneDrawKind::MeshTriangle;
        item.depth = triangleViewDepth(v0, v1, v2, m_camera.target, basis.forward);
        item.triIndex = i;
        drawItems.push_back(item);
    }

    std::sort(drawItems.begin(), drawItems.end(), [](const SceneDrawItem& a, const SceneDrawItem& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        // Grid before mesh at equal depth so coplanar ground stays under the rock.
        return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
    });

    if (triCount == 0 && drawItems.empty()) {
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

    for (const SceneDrawItem& item : drawItems) {
        if (item.kind == SceneDrawKind::GridLine) {
            drawList->AddLine(
                project(item.gridA), project(item.gridB), item.lineColor, item.lineThickness);
            continue;
        }

        const int index = item.triIndex;
        const std::uint32_t i0 = model.meshIndices[(std::size_t)index * 3 + 0];
        const std::uint32_t i1 = model.meshIndices[(std::size_t)index * 3 + 1];
        const std::uint32_t i2 = model.meshIndices[(std::size_t)index * 3 + 2];
        const Vec3& v0 = model.meshVertices[(std::size_t)i0];
        const Vec3& v1 = model.meshVertices[(std::size_t)i1];
        const Vec3& v2 = model.meshVertices[(std::size_t)i2];

        Vec3 n = triNormal(v0, v1, v2);
        // If MC gave us per-vertex normals, use the mean for a smoother look.
        if (i0 < model.meshNormals.size() && i1 < model.meshNormals.size() && i2 < model.meshNormals.size()) {
            const Vec3 n0 = model.meshNormals[(std::size_t)i0];
            const Vec3 n1 = model.meshNormals[(std::size_t)i1];
            const Vec3 n2 = model.meshNormals[(std::size_t)i2];
            const Vec3 avg = normalizeVec({(n0.x + n1.x + n2.x) / 3.0f, (n0.y + n1.y + n2.y) / 3.0f, (n0.z + n1.z + n2.z) / 3.0f});
            if (lengthVec(avg) > 0.0f) n = avg;
        }

        // Hemispheric ambient: sky color from above, ground color from below.
        const float upDot = std::max(0.0f, n.y * 0.5f + 0.5f);
        Vec3 ambient{sky.x * upDot + ground.x * (1.0f - upDot),
                     sky.y * upDot + ground.y * (1.0f - upDot),
                     sky.z * upDot + ground.z * (1.0f - upDot)};

        // Diffuse (Lambert).
        const float ndotl = std::max(0.0f, dotVec(n, lightDir));
        Vec3 diffuse{m_shading.rockTint.x * diffuseK * ndotl,
                     m_shading.rockTint.y * diffuseK * ndotl,
                     m_shading.rockTint.z * diffuseK * ndotl};

        // Specular (Blinn-Phong). For an orthographic view the half-vector reduces to the light.
        const Vec3 halfVec = normalizeVec({lightDir.x + viewDir.x, lightDir.y + viewDir.y, lightDir.z + viewDir.z});
        const float ndoth = std::max(0.0f, dotVec(n, halfVec));
        const float specAmount = std::pow(ndoth, shininess) * specK;
        Vec3 specular{specAmount, specAmount, specAmount};

        // Rim: bright at silhouette edges.
        const float rim = std::pow(1.0f - std::max(0.0f, dotVec(n, viewDir)), rimP) * rimK;

        // Fake ground shadow: lerp toward ground color in the lower half of the bounding box.
        const float avgY = (v0.y + v1.y + v2.y) / 3.0f;
        const float groundFactor = clampFloat((avgY + 10.0f) / 20.0f, 0.0f, 1.0f);
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

        // Distance fog toward the ground center to fade the silhouette into the plate.
        const ImVec2 s0 = project(v0);
        const ImVec2 s1 = project(v1);
        const ImVec2 s2 = project(v2);
        const float centroidDist = (std::abs((s0.x + s1.x + s2.x) / 3.0f - viewCenter.x)
            + std::abs((s0.y + s1.y + s2.y) / 3.0f - viewCenter.y)) / 600.0f;
        const float fogFactor = clampFloat(centroidDist * fog, 0.0f, 1.0f);
        const Vec3 fogColor = {0.06f, 0.07f, 0.09f};
        shaded = {
            shaded.x * (1.0f - fogFactor) + fogColor.x * fogFactor,
            shaded.y * (1.0f - fogFactor) + fogColor.y * fogFactor,
            shaded.z * (1.0f - fogFactor) + fogColor.z * fogFactor,
        };

        const ImU32 color = packColor(shaded);
        drawList->AddTriangleFilled(s0, s1, s2, color);
        if (m_shading.showWireframe) {
            // Polyline path: 3 unique corners + closing back to s0 = 3 verts/indices per triangle
            drawList->PathLineTo(s0);
            drawList->PathLineTo(s1);
            drawList->PathLineTo(s2);
            drawList->PathStroke(IM_COL32(20, 28, 36, 220), ImDrawFlags_Closed, 0.9f);
        }
    }

    // Per-frame visibility log: confirm triangles were actually issued to the drawlist.
    static int s_drawnCount = 0;
    if (s_drawnCount < 1) {
        spdlog::info("drawMeshView: emitted {} triangle fills (wireframe={}) | zoom={} pan=({}, {})",
            triCount, m_shading.showWireframe ? "on" : "off", m_camera.zoom, m_camera.pan.x, m_camera.pan.y);
    }
    s_drawnCount++;

    char titleText[160];
    std::snprintf(titleText, sizeof(titleText), "Rock Fracture mesh (tri=%d, v=%d)",
        model.triangleCount, model.vertexCount);
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), titleText);
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255),
        "LMB: pan view, Ctrl+LMB: orbit camera, wheel: zoom, dbl-click: reset");
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
