#include "MeshPreview.h"

#include "PlaygroundState.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace meshgen_playground {

namespace {

ImU32 colorForVertex(VertexKind kind) {
    switch (kind) {
    case VertexKind::OuterCorner:
        return IM_COL32(250, 196, 72, 255);
    case VertexKind::InnerCorner:
        return IM_COL32(232, 92, 92, 255);
    case VertexKind::DiagonalJoin:
        return IM_COL32(186, 118, 255, 255);
    case VertexKind::Edge:
        return IM_COL32(92, 180, 255, 255);
    case VertexKind::Empty:
    case VertexKind::SolidInterior:
    default:
        return IM_COL32(160, 160, 160, 255);
    }
}

const char* labelForVertex(VertexKind kind) {
    switch (kind) {
    case VertexKind::OuterCorner:
        return "O";
    case VertexKind::InnerCorner:
        return "I";
    case VertexKind::DiagonalJoin:
        return "D";
    case VertexKind::Edge:
        return "E";
    case VertexKind::Empty:
    case VertexKind::SolidInterior:
    default:
        return "";
    }
}

ImVec2 gridPointToScreen(const ImVec2& origin, float cellSize, const Int2& point) {
    return {origin.x + (float)point.x * cellSize, origin.y + (float)point.y * cellSize};
}

ImVec2 meshProjectionAnchor(const ImVec2& origin, const ImVec2& canvasSize, const MeshPreviewCamera& camera) {
    return {
        origin.x + canvasSize.x * 0.5f + camera.pan.x,
        origin.y + 130.0f + camera.pan.y,
    };
}

ImVec2 projectMeshPoint(
    const RectangleCliffSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const Vec3& point) {

    const float centeredX = point.x - (float)settings.gridWidth * 0.5f;
    const float centeredZ = point.z - (float)settings.gridHeight * 0.5f;
    const float isoX = (centeredX - centeredZ) * 28.0f * camera.zoom;
    const float isoY = ((centeredX + centeredZ) * 14.0f - point.y * 34.0f) * camera.zoom;
    const ImVec2 anchor = meshProjectionAnchor(origin, canvasSize, camera);
    return {
        anchor.x + isoX,
        anchor.y + isoY,
    };
}

void drawProjectedQuad(
    ImDrawList* drawList,
    const RectangleCliffSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const MeshQuad& quad) {

    const ImVec2 a = projectMeshPoint(settings, camera, origin, canvasSize, quad.a);
    const ImVec2 b = projectMeshPoint(settings, camera, origin, canvasSize, quad.b);
    const ImVec2 c = projectMeshPoint(settings, camera, origin, canvasSize, quad.c);
    const ImVec2 d = projectMeshPoint(settings, camera, origin, canvasSize, quad.d);

    drawList->AddQuadFilled(a, b, c, d, quad.color);
    if (settings.showMeshWireframe) {
        drawList->AddQuad(a, b, c, d, IM_COL32(24, 24, 24, 210), 1.25f);
    }
}

ImVec2 projectLandscapeMeshPoint(
    const LandscapeBowlSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const Vec3& point) {

    const float centeredX = point.x - (float)settings.gridWidth * 0.5f;
    const float centeredZ = point.z - (float)settings.gridHeight * 0.5f;
    const float isoX = (centeredX - centeredZ) * 19.0f * camera.zoom;
    const float isoY = ((centeredX + centeredZ) * 9.5f - point.y * 25.0f) * camera.zoom;
    const ImVec2 anchor = meshProjectionAnchor(origin, canvasSize, camera);
    return {
        anchor.x + isoX,
        anchor.y + isoY,
    };
}

void drawLandscapeProjectedQuad(
    ImDrawList* drawList,
    const LandscapeBowlSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const MeshQuad& quad) {

    const ImVec2 a = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.a);
    const ImVec2 b = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.b);
    const ImVec2 c = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.c);
    const ImVec2 d = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.d);

    drawList->AddQuadFilled(a, b, c, d, quad.color);
    if (settings.showMeshWireframe) {
        drawList->AddQuad(a, b, c, d, IM_COL32(24, 24, 24, 190), 1.0f);
    }
}

ImU32 colorForLandscapeZone(LandscapeZone zone, float height, float minHeight, float maxHeight) {
    const float t = clampFloat((height - minHeight) / std::max(0.001f, maxHeight - minHeight), 0.0f, 1.0f);
    switch (zone) {
    case LandscapeZone::Clearing:
        return IM_COL32(110, 161, 89, 255);
    case LandscapeZone::HighGround:
        return IM_COL32(
            clampInt(105 + (int)(t * 55.0f), 80, 180),
            clampInt(120 + (int)(t * 46.0f), 90, 180),
            clampInt(88 + (int)(t * 34.0f), 70, 150),
            255);
    case LandscapeZone::Hill:
        return IM_COL32(126, 151, 91, 255);
    case LandscapeZone::Slope:
        return IM_COL32(96, 136, 84, 255);
    case LandscapeZone::Lowland:
    default:
        return IM_COL32(78, 128, 82, 255);
    }
}

} // namespace

void drawRectangleCliffDebugView(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize) {
    const float cellSize = 34.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridOrigin{origin.x + 12.0f, origin.y + 36.0f};

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(20, 23, 28, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "2D logical boundary view");
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const ImVec2 min{gridOrigin.x + (float)x * cellSize, gridOrigin.y + (float)y * cellSize};
            const ImVec2 max{min.x + cellSize, min.y + cellSize};

            if (isSolidCell(model, settings, x, y)) {
                drawList->AddRectFilled({min.x + 1.0f, min.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f}, IM_COL32(74, 114, 74, 255));
                if (settings.showCellLabels) {
                    drawList->AddText({min.x + 9.0f, min.y + 8.0f}, IM_COL32(215, 235, 205, 255), "land");
                }
            }

            drawList->AddRect(min, max, IM_COL32(58, 64, 74, 255));
        }
    }

    for (const BoundarySegment& segment : model.boundarySegments) {
        const ImVec2 a = gridPointToScreen(gridOrigin, cellSize, segment.a);
        const ImVec2 b = gridPointToScreen(gridOrigin, cellSize, segment.b);
        drawList->AddLine(a, b, IM_COL32(235, 106, 72, 255), 4.0f);
    }

    for (const VertexMarker& marker : model.vertexMarkers) {
        const ImVec2 center = gridPointToScreen(gridOrigin, cellSize, marker.position);
        const ImU32 color = colorForVertex(marker.kind);
        drawList->AddCircleFilled(center, 6.5f, color, 16);
        drawList->AddCircle(center, 7.5f, IM_COL32(10, 10, 10, 220), 16, 1.5f);

        if (settings.showVertexLabels) {
            drawList->AddText({center.x + 8.0f, center.y - 8.0f}, color, labelForVertex(marker.kind));
        }
    }

    drawList->PopClipRect();
    ImGui::Dummy(viewportSize);
}

void drawMesh3dPreview(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##GeneratedMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (hovered && io.MouseWheel != 0.0f) {
            const float previousZoom = g_meshCamera.zoom;
            const float nextZoom = clampFloat(previousZoom * (1.0f + io.MouseWheel * 0.12f), 0.35f, 4.0f);
            if (nextZoom != previousZoom) {
                const ImVec2 anchor = meshProjectionAnchor(origin, viewportSize, g_meshCamera);
                const ImVec2 mouseLocal{
                    io.MousePos.x - anchor.x,
                    io.MousePos.y - anchor.y,
                };
                const float zoomRatio = nextZoom / previousZoom;
                g_meshCamera.pan.x += mouseLocal.x * (1.0f - zoomRatio);
                g_meshCamera.pan.y += mouseLocal.y * (1.0f - zoomRatio);
                g_meshCamera.zoom = nextZoom;
            }
        }

        if (dragging) {
            g_meshCamera.pan.x += io.MouseDelta.x;
            g_meshCamera.pan.y += io.MouseDelta.y;
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_meshCamera.zoom = 1.0f;
            g_meshCamera.pan = {0.0f, 0.0f};
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    const ImVec2 groundCenter{origin.x + viewportSize.x * 0.5f, origin.y + 165.0f};
    drawList->AddEllipse(groundCenter, {360.0f, 150.0f}, IM_COL32(36, 40, 48, 255), 0.0f, 48, 2.0f);

    std::vector<int> drawOrder;
    drawOrder.reserve(model.meshQuads.size());
    for (int i = 0; i < (int)model.meshQuads.size(); i++) {
        const MeshQuad& quad = model.meshQuads[(std::size_t)i];
        if (quad.cliffWall && !settings.showCliffWalls) {
            continue;
        }
        if (!quad.cliffWall && !settings.showTopFaces) {
            continue;
        }
        drawOrder.push_back(i);
    }

    std::sort(drawOrder.begin(), drawOrder.end(), [&](int lhs, int rhs) {
        return model.meshQuads[(std::size_t)lhs].depth < model.meshQuads[(std::size_t)rhs].depth;
    });

    for (int index : drawOrder) {
        drawProjectedQuad(drawList, settings, g_meshCamera, origin, viewportSize, model.meshQuads[(std::size_t)index]);
    }

    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "Generated 3D mesh: top quads + cliff wall quads");
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, mouse wheel: zoom, double click: reset");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", g_meshCamera.zoom, g_meshCamera.pan.x, g_meshCamera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    drawList->PopClipRect();
}

void drawLandscapeBowlDebugView(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float availableWidth = std::max(1.0f, viewportSize.x - 24.0f);
    const float availableHeight = std::max(1.0f, viewportSize.y - 48.0f);
    const float cellSize = std::min(availableWidth / (float)settings.gridWidth, availableHeight / (float)settings.gridHeight);
    const ImVec2 gridSize{cellSize * (float)settings.gridWidth, cellSize * (float)settings.gridHeight};
    const ImVec2 gridOrigin{
        origin.x + (viewportSize.x - gridSize.x) * 0.5f,
        origin.y + 36.0f + (availableHeight - gridSize.y) * 0.5f,
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(20, 23, 28, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "2D logical level map: heightmap quantized before tile composition");
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const std::size_t index = (std::size_t)landscapeIndex(x, y, settings.gridWidth);
            const float height = model.heights[index];
            const LandscapeZone zone = model.zones[index];
            const ImVec2 min{gridOrigin.x + (float)x * cellSize, gridOrigin.y + (float)y * cellSize};
            const ImVec2 max{min.x + cellSize + 0.5f, min.y + cellSize + 0.5f};
            drawList->AddRectFilled(min, max, colorForLandscapeZone(zone, height, model.minHeight, model.maxHeight));
            if (cellSize >= 13.0f) {
                drawList->AddRect(min, max, IM_COL32(25, 30, 30, 90));
            }
            if (settings.showHeightValues && cellSize >= 18.0f) {
                char label[16];
                snprintf(label, sizeof(label), "L%d", landscapeLevelAtCell(model, settings, x, y));
                drawList->AddText({min.x + 2.0f, min.y + 2.0f}, IM_COL32(230, 235, 220, 220), label);
            }
        }
    }

    drawList->PopClipRect();
    ImGui::Dummy(viewportSize);
}

void drawLandscapeMesh3dPreview(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##LandscapeMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (hovered && io.MouseWheel != 0.0f) {
            const float previousZoom = g_landscapeCamera.zoom;
            const float nextZoom = clampFloat(previousZoom * (1.0f + io.MouseWheel * 0.12f), 0.28f, 4.0f);
            if (nextZoom != previousZoom) {
                const ImVec2 anchor = meshProjectionAnchor(origin, viewportSize, g_landscapeCamera);
                const ImVec2 mouseLocal{
                    io.MousePos.x - anchor.x,
                    io.MousePos.y - anchor.y,
                };
                const float zoomRatio = nextZoom / previousZoom;
                g_landscapeCamera.pan.x += mouseLocal.x * (1.0f - zoomRatio);
                g_landscapeCamera.pan.y += mouseLocal.y * (1.0f - zoomRatio);
                g_landscapeCamera.zoom = nextZoom;
            }
        }

        if (dragging) {
            g_landscapeCamera.pan.x += io.MouseDelta.x;
            g_landscapeCamera.pan.y += io.MouseDelta.y;
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_landscapeCamera.zoom = 1.0f;
            g_landscapeCamera.pan = {0.0f, 0.0f};
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    const ImVec2 groundCenter{origin.x + viewportSize.x * 0.5f, origin.y + 170.0f};
    drawList->AddEllipse(groundCenter, {390.0f, 150.0f}, IM_COL32(34, 39, 47, 255), 0.0f, 48, 2.0f);

    std::vector<int> drawOrder;
    drawOrder.reserve(model.meshQuads.size());
    for (int i = 0; i < (int)model.meshQuads.size(); i++) {
        const MeshQuad& quad = model.meshQuads[(std::size_t)i];
        if (quad.cliffWall && !settings.showCliffWalls) {
            continue;
        }
        if (!quad.cliffWall && !settings.showTopFaces) {
            continue;
        }
        drawOrder.push_back(i);
    }

    std::sort(drawOrder.begin(), drawOrder.end(), [&](int lhs, int rhs) {
        return model.meshQuads[(std::size_t)lhs].depth < model.meshQuads[(std::size_t)rhs].depth;
    });

    for (int index : drawOrder) {
        drawLandscapeProjectedQuad(drawList, settings, g_landscapeCamera, origin, viewportSize, model.meshQuads[(std::size_t)index]);
    }

    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "Landscape Bowl 3D preview: composed from reusable surface/wall tile meshes");
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, mouse wheel: zoom, double click: reset");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", g_landscapeCamera.zoom, g_landscapeCamera.pan.x, g_landscapeCamera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    drawList->PopClipRect();
}

} // namespace meshgen_playground
