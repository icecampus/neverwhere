#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>

#include <imgui.h>
#include <spdlog/spdlog.h>

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

namespace {

struct Int2 {
    int x = 0;
    int y = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class BoundarySide : int {
    Top = 0,
    Right = 1,
    Bottom = 2,
    Left = 3,
};

enum class VertexKind : int {
    Empty,
    Edge,
    OuterCorner,
    InnerCorner,
    SolidInterior,
    DiagonalJoin,
};

struct BoundarySegment {
    Int2 a;
    Int2 b;
    BoundarySide side = BoundarySide::Top;
};

struct VertexMarker {
    Int2 position;
    VertexKind kind = VertexKind::Empty;
};

struct MeshQuad {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 d;
    ImU32 color = IM_COL32(255, 255, 255, 255);
    bool cliffWall = false;
    float depth = 0.0f;
};

struct RectangleCliffSettings {
    int gridWidth = 18;
    int gridHeight = 12;
    int rectX = 3;
    int rectY = 2;
    int rectWidth = 11;
    int rectHeight = 8;
    bool enableCutout = true;
    int cutoutX = 7;
    int cutoutY = 4;
    int cutoutWidth = 4;
    int cutoutHeight = 3;
    float cliffHeight = 2.5f;
    bool showTopFaces = true;
    bool showCliffWalls = true;
    bool showMeshWireframe = true;
    bool showCellLabels = true;
    bool showVertexLabels = true;
};

struct RectangleCliffModel {
    std::vector<std::uint8_t> solidCells;
    std::vector<BoundarySegment> boundarySegments;
    std::vector<VertexMarker> vertexMarkers;
    int solidCellCount = 0;
    int edgeVertexCount = 0;
    int outerCornerCount = 0;
    int innerCornerCount = 0;
    int diagonalJoinCount = 0;
    std::vector<MeshQuad> meshQuads;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
};

struct AppState {
    std::uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;
};

struct MeshPreviewCamera {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
};

AppState g_state;
RectangleCliffSettings g_rectSettings;
RectangleCliffModel g_rectModel;
MeshPreviewCamera g_meshCamera;

int cellIndex(int x, int y, int width) {
    return y * width + x;
}

int clampInt(int value, int minValue, int maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

float clampFloat(float value, float minValue, float maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

bool pointInRect(int x, int y, int rectX, int rectY, int rectWidth, int rectHeight) {
    return x >= rectX && y >= rectY && x < rectX + rectWidth && y < rectY + rectHeight;
}

void sanitizeSettings(RectangleCliffSettings& settings) {
    settings.gridWidth = clampInt(settings.gridWidth, 4, 48);
    settings.gridHeight = clampInt(settings.gridHeight, 4, 32);
    settings.rectWidth = clampInt(settings.rectWidth, 1, settings.gridWidth);
    settings.rectHeight = clampInt(settings.rectHeight, 1, settings.gridHeight);
    settings.rectX = clampInt(settings.rectX, 0, settings.gridWidth - settings.rectWidth);
    settings.rectY = clampInt(settings.rectY, 0, settings.gridHeight - settings.rectHeight);
    settings.cutoutWidth = clampInt(settings.cutoutWidth, 1, settings.gridWidth);
    settings.cutoutHeight = clampInt(settings.cutoutHeight, 1, settings.gridHeight);
    settings.cutoutX = clampInt(settings.cutoutX, 0, settings.gridWidth - settings.cutoutWidth);
    settings.cutoutY = clampInt(settings.cutoutY, 0, settings.gridHeight - settings.cutoutHeight);
    settings.cliffHeight = clampFloat(settings.cliffHeight, 0.25f, 8.0f);
}

bool isSolidCell(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y) {
    if (x < 0 || y < 0 || x >= settings.gridWidth || y >= settings.gridHeight) {
        return false;
    }

    return model.solidCells[cellIndex(x, y, settings.gridWidth)] != 0;
}

VertexKind classifyVertex(const RectangleCliffModel& model, const RectangleCliffSettings& settings, int x, int y) {
    const bool topLeft = isSolidCell(model, settings, x - 1, y - 1);
    const bool topRight = isSolidCell(model, settings, x, y - 1);
    const bool bottomLeft = isSolidCell(model, settings, x - 1, y);
    const bool bottomRight = isSolidCell(model, settings, x, y);
    const int solidCount = (topLeft ? 1 : 0) + (topRight ? 1 : 0) + (bottomLeft ? 1 : 0) + (bottomRight ? 1 : 0);

    if (solidCount == 0) return VertexKind::Empty;
    if (solidCount == 1) return VertexKind::OuterCorner;
    if (solidCount == 3) return VertexKind::InnerCorner;
    if (solidCount == 4) return VertexKind::SolidInterior;

    const bool diagonal = (topLeft && bottomRight) || (topRight && bottomLeft);
    return diagonal ? VertexKind::DiagonalJoin : VertexKind::Edge;
}

void addBoundarySegment(RectangleCliffModel& model, int x, int y, BoundarySide side) {
    BoundarySegment segment;
    segment.side = side;

    switch (side) {
    case BoundarySide::Top:
        segment.a = {x, y};
        segment.b = {x + 1, y};
        break;
    case BoundarySide::Right:
        segment.a = {x + 1, y};
        segment.b = {x + 1, y + 1};
        break;
    case BoundarySide::Bottom:
        segment.a = {x + 1, y + 1};
        segment.b = {x, y + 1};
        break;
    case BoundarySide::Left:
        segment.a = {x, y + 1};
        segment.b = {x, y};
        break;
    }

    model.boundarySegments.push_back(segment);
}

float meshQuadDepth(const MeshQuad& quad) {
    const float x = (quad.a.x + quad.b.x + quad.c.x + quad.d.x) * 0.25f;
    const float y = (quad.a.y + quad.b.y + quad.c.y + quad.d.y) * 0.25f;
    const float z = (quad.a.z + quad.b.z + quad.c.z + quad.d.z) * 0.25f;
    return x + z - y * 0.25f;
}

void addMeshQuad(RectangleCliffModel& model, MeshQuad quad) {
    quad.depth = meshQuadDepth(quad);
    model.meshQuads.push_back(quad);
    if (quad.cliffWall) {
        model.cliffWallQuadCount++;
    } else {
        model.topQuadCount++;
    }
}

ImU32 colorForBoundarySide(BoundarySide side) {
    switch (side) {
    case BoundarySide::Top:
        return IM_COL32(104, 92, 76, 255);
    case BoundarySide::Right:
        return IM_COL32(86, 82, 74, 255);
    case BoundarySide::Bottom:
        return IM_COL32(130, 108, 84, 255);
    case BoundarySide::Left:
    default:
        return IM_COL32(96, 88, 78, 255);
    }
}

void generateSimpleCliffMesh(RectangleCliffModel& model, const RectangleCliffSettings& settings) {
    const float h = settings.cliffHeight;

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            if (!isSolidCell(model, settings, x, y)) {
                continue;
            }

            MeshQuad top;
            top.a = {(float)x, h, (float)y};
            top.b = {(float)(x + 1), h, (float)y};
            top.c = {(float)(x + 1), h, (float)(y + 1)};
            top.d = {(float)x, h, (float)(y + 1)};
            top.color = IM_COL32(88, 143, 82, 255);
            top.cliffWall = false;
            addMeshQuad(model, top);
        }
    }

    for (const BoundarySegment& segment : model.boundarySegments) {
        MeshQuad wall;
        wall.a = {(float)segment.a.x, h, (float)segment.a.y};
        wall.b = {(float)segment.b.x, h, (float)segment.b.y};
        wall.c = {(float)segment.b.x, 0.0f, (float)segment.b.y};
        wall.d = {(float)segment.a.x, 0.0f, (float)segment.a.y};
        wall.color = colorForBoundarySide(segment.side);
        wall.cliffWall = true;
        addMeshQuad(model, wall);
    }
}

void rebuildRectangleCliffModel() {
    RectangleCliffSettings settings = g_rectSettings;
    sanitizeSettings(settings);
    g_rectSettings = settings;

    RectangleCliffModel model;
    model.solidCells.assign((std::size_t)settings.gridWidth * (std::size_t)settings.gridHeight, 0);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const bool inMainRect = pointInRect(x, y, settings.rectX, settings.rectY, settings.rectWidth, settings.rectHeight);
            const bool inCutout = settings.enableCutout &&
                pointInRect(x, y, settings.cutoutX, settings.cutoutY, settings.cutoutWidth, settings.cutoutHeight);
            if (inMainRect && !inCutout) {
                model.solidCells[cellIndex(x, y, settings.gridWidth)] = 1;
                model.solidCellCount++;
            }
        }
    }

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            if (!isSolidCell(model, settings, x, y)) {
                continue;
            }

            if (!isSolidCell(model, settings, x, y - 1)) addBoundarySegment(model, x, y, BoundarySide::Top);
            if (!isSolidCell(model, settings, x + 1, y)) addBoundarySegment(model, x, y, BoundarySide::Right);
            if (!isSolidCell(model, settings, x, y + 1)) addBoundarySegment(model, x, y, BoundarySide::Bottom);
            if (!isSolidCell(model, settings, x - 1, y)) addBoundarySegment(model, x, y, BoundarySide::Left);
        }
    }

    for (int y = 0; y <= settings.gridHeight; y++) {
        for (int x = 0; x <= settings.gridWidth; x++) {
            const VertexKind kind = classifyVertex(model, settings, x, y);
            if (kind == VertexKind::Empty || kind == VertexKind::SolidInterior) {
                continue;
            }

            model.vertexMarkers.push_back({{x, y}, kind});
            switch (kind) {
            case VertexKind::Edge:
                model.edgeVertexCount++;
                break;
            case VertexKind::OuterCorner:
                model.outerCornerCount++;
                break;
            case VertexKind::InnerCorner:
                model.innerCornerCount++;
                break;
            case VertexKind::DiagonalJoin:
                model.diagonalJoinCount++;
                break;
            case VertexKind::Empty:
            case VertexKind::SolidInterior:
                break;
            }
        }
    }

    generateSimpleCliffMesh(model, settings);
    g_rectModel = std::move(model);
}

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

void drawMesh3dPreview(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##GeneratedMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

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

void drawUi() {
    rebuildRectangleCliffModel();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr ImGuiWindowFlags rootFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("MeshGenerationPlayground", nullptr, rootFlags);

    const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const float viewportHeight = std::max(1.0f, (layoutSize.y - gutter) * 0.5f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(25, 28, 34, 255));
    drawList->AddRect(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(65, 72, 84, 255));

    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    ImGui::PushItemWidth(leftPanelWidth - 24.0f);
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
    ImGui::Separator();

    ImGui::Text("Logical rectangle cliff prototype");
    ImGui::TextWrapped("Boundary segments are generated from solid cells that touch empty space, like cliff edges in Landscape3dPlayground.");

    bool changed = false;
    changed |= ImGui::SliderInt("Grid Width", &g_rectSettings.gridWidth, 4, 48);
    changed |= ImGui::SliderInt("Grid Height", &g_rectSettings.gridHeight, 4, 32);
    changed |= ImGui::SliderInt("Rect X", &g_rectSettings.rectX, 0, g_rectSettings.gridWidth - 1);
    changed |= ImGui::SliderInt("Rect Y", &g_rectSettings.rectY, 0, g_rectSettings.gridHeight - 1);
    changed |= ImGui::SliderInt("Rect Width", &g_rectSettings.rectWidth, 1, g_rectSettings.gridWidth);
    changed |= ImGui::SliderInt("Rect Height", &g_rectSettings.rectHeight, 1, g_rectSettings.gridHeight);
    changed |= ImGui::Checkbox("Enable Cutout (inner corners)", &g_rectSettings.enableCutout);
    changed |= ImGui::SliderInt("Cutout X", &g_rectSettings.cutoutX, 0, g_rectSettings.gridWidth - 1);
    changed |= ImGui::SliderInt("Cutout Y", &g_rectSettings.cutoutY, 0, g_rectSettings.gridHeight - 1);
    changed |= ImGui::SliderInt("Cutout Width", &g_rectSettings.cutoutWidth, 1, g_rectSettings.gridWidth);
    changed |= ImGui::SliderInt("Cutout Height", &g_rectSettings.cutoutHeight, 1, g_rectSettings.gridHeight);
    changed |= ImGui::SliderFloat("Cliff Height", &g_rectSettings.cliffHeight, 0.25f, 8.0f);
    ImGui::Checkbox("Show Top Faces", &g_rectSettings.showTopFaces);
    ImGui::Checkbox("Show Cliff Walls", &g_rectSettings.showCliffWalls);
    ImGui::Checkbox("Show Mesh Wireframe", &g_rectSettings.showMeshWireframe);
    ImGui::Checkbox("Show Cell Labels", &g_rectSettings.showCellLabels);
    ImGui::Checkbox("Show Vertex Labels", &g_rectSettings.showVertexLabels);
    if (changed) {
        sanitizeSettings(g_rectSettings);
    }

    ImGui::Separator();
    ImGui::Text("Solid cells: %d", g_rectModel.solidCellCount);
    ImGui::Text("Boundary segments: %d", (int)g_rectModel.boundarySegments.size());
    ImGui::Text("3D quads top/walls/total: %d / %d / %d",
        g_rectModel.topQuadCount,
        g_rectModel.cliffWallQuadCount,
        (int)g_rectModel.meshQuads.size());
    ImGui::Text("Vertices E/O/I/D: %d / %d / %d / %d",
        g_rectModel.edgeVertexCount,
        g_rectModel.outerCornerCount,
        g_rectModel.innerCornerCount,
        g_rectModel.diagonalJoinCount);
    ImGui::TextColored(ImVec4(0.36f, 0.70f, 1.0f, 1.0f), "E = straight edge vertex");
    ImGui::TextColored(ImVec4(0.98f, 0.77f, 0.28f, 1.0f), "O = outer corner");
    ImGui::TextColored(ImVec4(0.91f, 0.36f, 0.36f, 1.0f), "I = inner corner");
    ImGui::TextColored(ImVec4(0.73f, 0.46f, 1.0f, 1.0f), "D = diagonal join");
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
    drawRectangleCliffDebugView(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});

    ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportHeight + gutter});
    drawMesh3dPreview(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});

    ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
    ImGui::End();
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("MeshGenerationPlayground: init()");

    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("MeshGenerationPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");

    if (!g_state.gfxOk) {
        return;
    }

    simgui_desc_t imguiDesc = {};
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;
    rebuildRectangleCliffModel();
}

void frame() {
    const std::uint64_t now = stm_now();
    g_state.dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
    g_state.lastTime = now;
    g_state.frameIndex++;

    if (!g_state.gfxOk) {
        return;
    }

    const int width = sapp_width();
    const int height = sapp_height();

    if (g_state.imguiOk) {
        simgui_frame_desc_t frameDesc = {};
        frameDesc.width = width;
        frameDesc.height = height;
        frameDesc.delta_time = g_state.dt;
        frameDesc.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&frameDesc);
        drawUi();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};

    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (g_state.imguiOk) {
        simgui_render();
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("MeshGenerationPlayground: cleanup()");

    if (g_state.imguiOk) {
        simgui_shutdown();
        g_state.imguiOk = false;
    }

    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* ev) {
    if (g_state.imguiOk) {
        simgui_handle_event(ev);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "MeshGenerationPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
