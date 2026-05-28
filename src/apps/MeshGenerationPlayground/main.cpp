#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <mutex>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include <FastNoise/FastNoise.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

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

std::mutex g_stateMutex;
std::mutex g_modelMutex;

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

struct MeshBoundarySegment {
    Vec3 a;
    Vec3 b;
    Vec3 normal;
    Vec3 startNormal;
    Vec3 endNormal;
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
    float cornerBevel = 0.3f;
    bool rockEnabled = true;
    int rockSeed = 1337;
    float rockScale = 2.75f;
    float rockAmplitude = 0.28f;
    int wallHorizontalSubdivisions = 5;
    int wallVerticalSubdivisions = 6;
    int terraceSteps = 4;
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

enum class LandscapeZone : std::uint8_t {
    Lowland,
    Clearing,
    Slope,
    HighGround,
    Hill,
};

struct LandscapeBowlSettings {
    int gridWidth = 32;
    int gridHeight = 24;
    int seed = 2027;
    float clearingRadius = 5.5f;
    float clearingSoftness = 2.2f;
    float highGroundRadius = 9.5f;
    float highGroundWidth = 3.5f;
    float highGroundHeight = 3.2f;
    int heightLevels = 4;
    float arcNoiseScale = 4.0f;
    float arcNoiseAmplitude = 1.6f;
    int hillCount = 5;
    float hillHeight = 1.2f;
    float hillRadius = 2.6f;
    bool showTopFaces = true;
    bool showCliffWalls = true;
    bool showMeshWireframe = true;
    bool showHeightValues = false;
};

struct LandscapeBowlModel {
    std::vector<float> heights;
    std::vector<std::uint8_t> heightLevels;
    std::vector<LandscapeZone> zones;
    std::vector<MeshQuad> meshQuads;
    std::vector<int> levelCellCounts;
    int clearingCellCount = 0;
    int highGroundCellCount = 0;
    int hillCellCount = 0;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
};

struct AppState {
    std::uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;
    bool gfxOk = false;
    bool imguiOk = false;
    bool gfxFailureLogged = false;
};

struct MeshPreviewCamera {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
};

AppState g_state;
RectangleCliffSettings g_rectSettings;
RectangleCliffModel g_rectModel;
MeshPreviewCamera g_meshCamera;
LandscapeBowlSettings g_landscapeSettings;
LandscapeBowlModel g_landscapeModel;
MeshPreviewCamera g_landscapeCamera;

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
    settings.cornerBevel = clampFloat(settings.cornerBevel, 0.0f, 0.45f);
    settings.rockScale = clampFloat(settings.rockScale, 0.25f, 24.0f);
    settings.rockAmplitude = clampFloat(settings.rockAmplitude, 0.0f, 1.25f);
    settings.wallHorizontalSubdivisions = clampInt(settings.wallHorizontalSubdivisions, 1, 16);
    settings.wallVerticalSubdivisions = clampInt(settings.wallVerticalSubdivisions, 1, 16);
    settings.terraceSteps = clampInt(settings.terraceSteps, 0, 12);
}

void sanitizeSettings(LandscapeBowlSettings& settings) {
    settings.gridWidth = clampInt(settings.gridWidth, 12, 72);
    settings.gridHeight = clampInt(settings.gridHeight, 10, 56);
    settings.clearingRadius = clampFloat(settings.clearingRadius, 2.0f, (float)std::min(settings.gridWidth, settings.gridHeight) * 0.45f);
    settings.clearingSoftness = clampFloat(settings.clearingSoftness, 0.25f, 8.0f);
    settings.highGroundRadius = clampFloat(settings.highGroundRadius, settings.clearingRadius + 1.0f, (float)std::max(settings.gridWidth, settings.gridHeight));
    settings.highGroundWidth = clampFloat(settings.highGroundWidth, 1.0f, 10.0f);
    settings.highGroundHeight = clampFloat(settings.highGroundHeight, 0.5f, 8.0f);
    settings.heightLevels = clampInt(settings.heightLevels, 2, 6);
    settings.arcNoiseScale = clampFloat(settings.arcNoiseScale, 0.5f, 18.0f);
    settings.arcNoiseAmplitude = clampFloat(settings.arcNoiseAmplitude, 0.0f, 5.0f);
    settings.hillCount = clampInt(settings.hillCount, 0, 12);
    settings.hillHeight = clampFloat(settings.hillHeight, 0.0f, 5.0f);
    settings.hillRadius = clampFloat(settings.hillRadius, 0.75f, 8.0f);
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

void addMeshQuad(LandscapeBowlModel& model, MeshQuad quad) {
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

Vec3 outwardNormalForBoundarySide(BoundarySide side) {
    switch (side) {
    case BoundarySide::Top:
        return {0.0f, 0.0f, -1.0f};
    case BoundarySide::Right:
        return {1.0f, 0.0f, 0.0f};
    case BoundarySide::Bottom:
        return {0.0f, 0.0f, 1.0f};
    case BoundarySide::Left:
    default:
        return {-1.0f, 0.0f, 0.0f};
    }
}

bool samePoint(const Int2& a, const Int2& b) {
    return a.x == b.x && a.y == b.y;
}

Vec3 normalizeHorizontal(const Vec3& value, const Vec3& fallback) {
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length < 0.0001f) {
        return fallback;
    }

    return {value.x / length, 0.0f, value.z / length};
}

bool tryBoundaryVertexNormal(const RectangleCliffModel& model, const Int2& vertex, Vec3& outNormal) {
    Vec3 sum{};
    bool found = false;
    for (const BoundarySegment& segment : model.boundarySegments) {
        if (!samePoint(segment.a, vertex) && !samePoint(segment.b, vertex)) {
            continue;
        }

        const Vec3 normal = outwardNormalForBoundarySide(segment.side);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }

    if (!found) {
        return false;
    }

    outNormal = normalizeHorizontal(sum, {0.0f, 0.0f, 1.0f});
    return true;
}

Vec3 boundaryVertexNormal(const RectangleCliffModel& model, const Int2& vertex, const Vec3& fallback) {
    Vec3 normal;
    if (tryBoundaryVertexNormal(model, vertex, normal)) {
        return normal;
    }

    return fallback;
}

bool isBoundaryCorner(const RectangleCliffModel& model, const Int2& vertex) {
    int firstSide = -1;
    for (const BoundarySegment& segment : model.boundarySegments) {
        if (!samePoint(segment.a, vertex) && !samePoint(segment.b, vertex)) {
            continue;
        }

        const int side = (int)segment.side;
        if (firstSide < 0) {
            firstSide = side;
        } else if (firstSide != side) {
            return true;
        }
    }

    return false;
}

Vec3 subtractVec3(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 addVec3(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scaleVec3(const Vec3& value, float scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

float horizontalLength(const Vec3& value) {
    return std::sqrt(value.x * value.x + value.z * value.z);
}

Vec3 gridPointToVec3(const Int2& point, float y) {
    return {(float)point.x, y, (float)point.y};
}

std::vector<MeshBoundarySegment> buildBeveledBoundarySegments(const RectangleCliffModel& model, const RectangleCliffSettings& settings) {
    struct TrimmedEndpoint {
        Int2 corner;
        Vec3 point;
        BoundarySide side = BoundarySide::Top;
        Vec3 sideNormal;
        Vec3 bevelNormal;
    };

    std::vector<MeshBoundarySegment> segments;
    std::vector<TrimmedEndpoint> endpoints;
    const float bevel = clampFloat(settings.cornerBevel, 0.0f, 0.45f);

    for (const BoundarySegment& segment : model.boundarySegments) {
        const Vec3 a = gridPointToVec3(segment.a, 0.0f);
        const Vec3 b = gridPointToVec3(segment.b, 0.0f);
        const Vec3 delta = subtractVec3(b, a);
        const float length = horizontalLength(delta);
        if (length < 0.0001f) {
            continue;
        }

        const Vec3 direction = scaleVec3(delta, 1.0f / length);
        const bool trimStart = bevel > 0.0f && isBoundaryCorner(model, segment.a);
        const bool trimEnd = bevel > 0.0f && isBoundaryCorner(model, segment.b);
        const float trim = std::min(bevel, length * 0.45f);
        const Vec3 start = trimStart ? addVec3(a, scaleVec3(direction, trim)) : a;
        const Vec3 end = trimEnd ? addVec3(b, scaleVec3(direction, -trim)) : b;
        const Vec3 sideNormal = outwardNormalForBoundarySide(segment.side);
        const Vec3 startBevelNormal = trimStart ? boundaryVertexNormal(model, segment.a, sideNormal) : sideNormal;
        const Vec3 endBevelNormal = trimEnd ? boundaryVertexNormal(model, segment.b, sideNormal) : sideNormal;
        const Vec3 startNormal = trimStart ? normalizeHorizontal(addVec3(sideNormal, startBevelNormal), sideNormal) : sideNormal;
        const Vec3 endNormal = trimEnd ? normalizeHorizontal(addVec3(sideNormal, endBevelNormal), sideNormal) : sideNormal;

        if (horizontalLength(subtractVec3(end, start)) > 0.0001f) {
            segments.push_back({start, end, sideNormal, startNormal, endNormal, segment.side});
        }

        if (trimStart) {
            endpoints.push_back({segment.a, start, segment.side, sideNormal, startBevelNormal});
        }
        if (trimEnd) {
            endpoints.push_back({segment.b, end, segment.side, sideNormal, endBevelNormal});
        }
    }

    std::vector<std::uint8_t> consumed(endpoints.size(), 0);
    for (std::size_t i = 0; i < endpoints.size(); i++) {
        if (consumed[i]) {
            continue;
        }

        std::vector<std::size_t> group;
        for (std::size_t j = i; j < endpoints.size(); j++) {
            if (!consumed[j] && samePoint(endpoints[i].corner, endpoints[j].corner)) {
                group.push_back(j);
                consumed[j] = 1;
            }
        }

        if (group.size() != 2) {
            continue;
        }

        const TrimmedEndpoint& a = endpoints[group[0]];
        const TrimmedEndpoint& b = endpoints[group[1]];
        if (a.side == b.side) {
            continue;
        }

        const Vec3 normal = normalizeHorizontal(
            addVec3(outwardNormalForBoundarySide(a.side), outwardNormalForBoundarySide(b.side)),
            outwardNormalForBoundarySide(a.side));
        const Vec3 startNormal = normalizeHorizontal(addVec3(normal, a.sideNormal), normal);
        const Vec3 endNormal = normalizeHorizontal(addVec3(normal, b.sideNormal), normal);
        if (horizontalLength(subtractVec3(b.point, a.point)) > 0.0001f) {
            segments.push_back({a.point, b.point, normal, startNormal, endNormal, a.side});
        }
    }

    return segments;
}

Vec3 lerpVec3(const Vec3& a, const Vec3& b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

FastNoise::SmartNode<> makeRockNoiseNode(const RectangleCliffSettings& settings) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    if (!simplex) {
        spdlog::error("makeRockNoiseNode: failed to create Simplex node");
        return nullptr;
    }

    simplex->SetScale(settings.rockScale);

    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    if (!fractal) {
        spdlog::warn("makeRockNoiseNode: failed to create FractalRidged, falling back to simplex");
        return simplex;
    }

    fractal->SetSource(simplex);
    fractal->SetOctaveCount(4);
    fractal->SetLacunarity(2.15f);
    fractal->SetGain(0.55f);
    fractal->SetWeightedStrength(0.35f);
    return fractal;
}

void sampleRockNoiseBatch(
    const FastNoise::SmartNode<>& noise,
    const RectangleCliffSettings& settings,
    const std::vector<Vec3>& points,
    std::vector<float>& outNoise) {

    outNoise.assign(points.size(), 0.0f);
    if (!noise || points.empty()) {
        return;
    }

    std::vector<float> xs(points.size());
    std::vector<float> ys(points.size());
    std::vector<float> zs(points.size());
    for (std::size_t i = 0; i < points.size(); i++) {
        xs[i] = points[i].x;
        ys[i] = points[i].y;
        zs[i] = points[i].z;
    }

    noise->GenPositionArray3D(
        outNoise.data(),
        (int)points.size(),
        xs.data(),
        ys.data(),
        zs.data(),
        0.0f,
        0.0f,
        0.0f,
        settings.rockSeed);

    for (float& value : outNoise) {
        value = clampFloat(value, -1.0f, 1.0f);
    }
}

FastNoise::SmartNode<> makeLandscapeNoiseNode(const LandscapeBowlSettings& settings) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    if (!simplex) {
        spdlog::error("makeLandscapeNoiseNode: failed to create Simplex node");
        return nullptr;
    }

    simplex->SetScale(settings.arcNoiseScale);

    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    if (!fractal) {
        spdlog::warn("makeLandscapeNoiseNode: failed to create FractalRidged, falling back to simplex");
        return simplex;
    }

    fractal->SetSource(simplex);
    fractal->SetOctaveCount(4);
    fractal->SetLacunarity(2.05f);
    fractal->SetGain(0.5f);
    return fractal;
}

void sampleLandscapeNoiseBatch(
    const FastNoise::SmartNode<>& noise,
    const LandscapeBowlSettings& settings,
    const std::vector<Vec3>& points,
    std::vector<float>& outNoise) {

    outNoise.assign(points.size(), 0.0f);
    if (!noise || points.empty()) {
        return;
    }

    std::vector<float> xs(points.size());
    std::vector<float> ys(points.size());
    std::vector<float> zs(points.size());
    for (std::size_t i = 0; i < points.size(); i++) {
        xs[i] = points[i].x;
        ys[i] = points[i].y;
        zs[i] = points[i].z;
    }

    noise->GenPositionArray3D(
        outNoise.data(),
        (int)points.size(),
        xs.data(),
        ys.data(),
        zs.data(),
        0.0f,
        0.0f,
        0.0f,
        settings.seed);

    for (float& value : outNoise) {
        value = clampFloat(value, -1.0f, 1.0f);
    }
}

float terraceValue(float value, int steps) {
    if (steps <= 1) {
        return value;
    }

    const float normalized = (value + 1.0f) * 0.5f;
    const float terraced = (float)((int)(normalized * (float)steps)) / (float)steps;
    return terraced * 2.0f - 1.0f;
}

Vec3 displaceRockPoint(
    const RectangleCliffSettings& settings,
    const Vec3& point,
    const Vec3& outwardNormal,
    float heightT,
    float rawNoise) {

    if (!settings.rockEnabled || settings.rockAmplitude <= 0.0f) {
        return point;
    }

    const float steppedNoise = terraceValue(rawNoise, settings.terraceSteps);
    const float topFade = 1.0f - clampFloat(heightT, 0.0f, 1.0f);
    const float lowerBias = topFade * (0.35f + topFade * 0.65f);
    const float offset = steppedNoise * settings.rockAmplitude * lowerBias;

    return {
        point.x + outwardNormal.x * offset,
        point.y,
        point.z + outwardNormal.z * offset,
    };
}

Vec3 displaceLandscapeWallPoint(
    const RectangleCliffSettings& settings,
    const Vec3& point,
    const Vec3& outwardNormal,
    float heightT,
    float rawNoise) {

    if (!settings.rockEnabled || settings.rockAmplitude <= 0.0f) {
        return point;
    }

    const float clampedHeightT = clampFloat(heightT, 0.0f, 1.0f);
    const float edgeFade = std::sin(clampedHeightT * 3.14159265f);
    if (edgeFade <= 0.0001f) {
        return point;
    }

    const float steppedNoise = terraceValue(rawNoise, settings.terraceSteps);
    const float offset = steppedNoise * settings.rockAmplitude * edgeFade;
    return {
        point.x + outwardNormal.x * offset,
        point.y,
        point.z + outwardNormal.z * offset,
    };
}

ImU32 rockWallColor(BoundarySide side, float heightT, float noiseValue) {
    const int sideBias = side == BoundarySide::Bottom ? 16 : (side == BoundarySide::Right ? -8 : 0);
    const int shade = (int)(noiseValue * 24.0f) + (int)((1.0f - heightT) * 38.0f) + sideBias;
    const int r = clampInt(92 + shade, 46, 170);
    const int g = clampInt(86 + shade, 44, 160);
    const int b = clampInt(78 + shade, 40, 150);
    return IM_COL32(r, g, b, 255);
}

ImU32 rockTopColor(float noiseValue) {
    const int shade = (int)(noiseValue * 10.0f);
    const int r = clampInt(88 + shade, 58, 122);
    const int g = clampInt(143 + shade, 104, 172);
    const int b = clampInt(82 + shade, 52, 116);
    return IM_COL32(r, g, b, 255);
}

bool applyTopCornerBevel(
    const RectangleCliffModel& model,
    const RectangleCliffSettings& settings,
    int cellX,
    int cellY,
    Vec3& point,
    Vec3& outNormal) {

    struct CornerRule {
        Int2 vertex;
        bool enabled = false;
        float axisAX = 0.0f;
        float axisAZ = 0.0f;
        float axisBX = 0.0f;
        float axisBZ = 0.0f;
    };

    const float bevel = settings.cornerBevel;
    if (bevel <= 0.0f) {
        return false;
    }

    const CornerRule rules[] = {
        {{cellX, cellY},
            !isSolidCell(model, settings, cellX, cellY - 1) && !isSolidCell(model, settings, cellX - 1, cellY),
            1.0f, 0.0f, 0.0f, 1.0f},
        {{cellX + 1, cellY},
            !isSolidCell(model, settings, cellX, cellY - 1) && !isSolidCell(model, settings, cellX + 1, cellY),
            -1.0f, 0.0f, 0.0f, 1.0f},
        {{cellX + 1, cellY + 1},
            !isSolidCell(model, settings, cellX + 1, cellY) && !isSolidCell(model, settings, cellX, cellY + 1),
            -1.0f, 0.0f, 0.0f, -1.0f},
        {{cellX, cellY + 1},
            !isSolidCell(model, settings, cellX - 1, cellY) && !isSolidCell(model, settings, cellX, cellY + 1),
            1.0f, 0.0f, 0.0f, -1.0f},
    };

    for (const CornerRule& rule : rules) {
        if (!rule.enabled) {
            continue;
        }

        const float fromCornerX = point.x - (float)rule.vertex.x;
        const float fromCornerZ = point.z - (float)rule.vertex.y;
        float distanceA = fromCornerX * rule.axisAX + fromCornerZ * rule.axisAZ;
        float distanceB = fromCornerX * rule.axisBX + fromCornerZ * rule.axisBZ;
        if (distanceA < -0.0001f || distanceB < -0.0001f || distanceA > bevel || distanceB > bevel) {
            continue;
        }

        const float sum = distanceA + distanceB;
        if (sum >= bevel) {
            continue;
        }

        if (sum < 0.0001f) {
            distanceA = bevel * 0.5f;
            distanceB = bevel * 0.5f;
        } else {
            const float scale = bevel / sum;
            distanceA *= scale;
            distanceB *= scale;
        }

        point.x = (float)rule.vertex.x + rule.axisAX * distanceA + rule.axisBX * distanceB;
        point.z = (float)rule.vertex.y + rule.axisAZ * distanceA + rule.axisBZ * distanceB;
        outNormal = boundaryVertexNormal(model, rule.vertex, {0.0f, 0.0f, 1.0f});
        return true;
    }

    return false;
}

bool topBoundaryNormalForCellPoint(
    const RectangleCliffModel& model,
    const RectangleCliffSettings& settings,
    int cellX,
    int cellY,
    int subX,
    int subY,
    int subdivisions,
    Vec3& outNormal) {

    const bool onLeft = subX == 0;
    const bool onRight = subX == subdivisions;
    const bool onTop = subY == 0;
    const bool onBottom = subY == subdivisions;

    if ((onLeft || onRight) && (onTop || onBottom)) {
        const Int2 vertex{
            cellX + (onRight ? 1 : 0),
            cellY + (onBottom ? 1 : 0),
        };
        if (tryBoundaryVertexNormal(model, vertex, outNormal)) {
            return true;
        }
    }

    Vec3 sum{};
    bool found = false;
    if (onTop && !isSolidCell(model, settings, cellX, cellY - 1)) {
        const Vec3 normal = outwardNormalForBoundarySide(BoundarySide::Top);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onRight && !isSolidCell(model, settings, cellX + 1, cellY)) {
        const Vec3 normal = outwardNormalForBoundarySide(BoundarySide::Right);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onBottom && !isSolidCell(model, settings, cellX, cellY + 1)) {
        const Vec3 normal = outwardNormalForBoundarySide(BoundarySide::Bottom);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }
    if (onLeft && !isSolidCell(model, settings, cellX - 1, cellY)) {
        const Vec3 normal = outwardNormalForBoundarySide(BoundarySide::Left);
        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    }

    if (!found) {
        return false;
    }

    outNormal = normalizeHorizontal(sum, {0.0f, 0.0f, 1.0f});
    return true;
}

Vec3 displaceRockTopPoint(
    const RectangleCliffSettings& settings,
    const Vec3& point,
    const Vec3& normal,
    bool boundaryPoint,
    float rawNoise) {

    if (!boundaryPoint) {
        return point;
    }

    return displaceRockPoint(settings, point, normal, 1.0f, rawNoise);
}

void addRockTopCell(
    RectangleCliffModel& model,
    const RectangleCliffSettings& settings,
    const FastNoise::SmartNode<>& rockNoise,
    int cellX,
    int cellY) {

    const float h = settings.cliffHeight;
    const int subdivisions = settings.wallHorizontalSubdivisions;
    const int vertexColumns = subdivisions + 1;
    std::vector<Vec3> points((std::size_t)vertexColumns * (std::size_t)vertexColumns);
    std::vector<Vec3> normals(points.size());
    std::vector<std::uint8_t> boundaryFlags(points.size(), 0);
    std::vector<float> noise;

    for (int y = 0; y <= subdivisions; y++) {
        const float v = (float)y / (float)subdivisions;
        for (int x = 0; x <= subdivisions; x++) {
            const float u = (float)x / (float)subdivisions;
            const std::size_t index = (std::size_t)y * (std::size_t)vertexColumns + (std::size_t)x;
            points[index] = {(float)cellX + u, h, (float)cellY + v};
            if (applyTopCornerBevel(model, settings, cellX, cellY, points[index], normals[index])) {
                boundaryFlags[index] = 1;
            } else {
                boundaryFlags[index] = topBoundaryNormalForCellPoint(
                    model,
                    settings,
                    cellX,
                    cellY,
                    x,
                    y,
                    subdivisions,
                    normals[index]) ? 1 : 0;
            }
        }
    }

    sampleRockNoiseBatch(rockNoise, settings, points, noise);

    for (int y = 0; y < subdivisions; y++) {
        for (int x = 0; x < subdivisions; x++) {
            const std::size_t i00 = (std::size_t)y * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = (std::size_t)(y + 1) * (std::size_t)vertexColumns + (std::size_t)x;
            const std::size_t i11 = i01 + 1;

            MeshQuad top;
            top.a = displaceRockTopPoint(settings, points[i00], normals[i00], boundaryFlags[i00] != 0, noise[i00]);
            top.b = displaceRockTopPoint(settings, points[i10], normals[i10], boundaryFlags[i10] != 0, noise[i10]);
            top.c = displaceRockTopPoint(settings, points[i11], normals[i11], boundaryFlags[i11] != 0, noise[i11]);
            top.d = displaceRockTopPoint(settings, points[i01], normals[i01], boundaryFlags[i01] != 0, noise[i01]);

            const float panelNoise = (noise[i00] + noise[i10] + noise[i11] + noise[i01]) * 0.25f;
            top.color = rockTopColor(panelNoise);
            top.cliffWall = false;
            addMeshQuad(model, top);
        }
    }
}

void addInnerCornerBevelCaps(RectangleCliffModel& model, const RectangleCliffSettings& settings) {
    struct CornerEndpoint {
        Int2 corner;
        Vec3 point;
        BoundarySide side = BoundarySide::Top;
    };

    const float bevel = clampFloat(settings.cornerBevel, 0.0f, 0.45f);
    if (bevel <= 0.0f) {
        return;
    }

    std::vector<CornerEndpoint> endpoints;
    endpoints.reserve(model.boundarySegments.size() * 2);
    for (const BoundarySegment& segment : model.boundarySegments) {
        const Vec3 a = gridPointToVec3(segment.a, settings.cliffHeight);
        const Vec3 b = gridPointToVec3(segment.b, settings.cliffHeight);
        const Vec3 delta = subtractVec3(b, a);
        const float length = horizontalLength(delta);
        if (length < 0.0001f) {
            continue;
        }

        const Vec3 direction = scaleVec3(delta, 1.0f / length);
        const float trim = std::min(bevel, length * 0.45f);
        if (isBoundaryCorner(model, segment.a)) {
            endpoints.push_back({segment.a, addVec3(a, scaleVec3(direction, trim)), segment.side});
        }
        if (isBoundaryCorner(model, segment.b)) {
            endpoints.push_back({segment.b, addVec3(b, scaleVec3(direction, -trim)), segment.side});
        }
    }

    std::vector<std::uint8_t> consumed(endpoints.size(), 0);
    int capCount = 0;
    for (std::size_t i = 0; i < endpoints.size(); i++) {
        if (consumed[i]) {
            continue;
        }

        std::vector<std::size_t> group;
        for (std::size_t j = i; j < endpoints.size(); j++) {
            if (!consumed[j] && samePoint(endpoints[i].corner, endpoints[j].corner)) {
                group.push_back(j);
                consumed[j] = 1;
            }
        }

        if (group.size() != 2) {
            continue;
        }

        const VertexKind kind = classifyVertex(model, settings, endpoints[i].corner.x, endpoints[i].corner.y);
        if (kind != VertexKind::InnerCorner) {
            continue;
        }

        const CornerEndpoint& a = endpoints[group[0]];
        const CornerEndpoint& b = endpoints[group[1]];
        if (a.side == b.side) {
            continue;
        }

        const Vec3 corner = gridPointToVec3(a.corner, settings.cliffHeight);
        const Vec3 mid = scaleVec3(addVec3(a.point, b.point), 0.5f);

        MeshQuad cap;
        cap.a = corner;
        cap.b = a.point;
        cap.c = mid;
        cap.d = b.point;
        cap.color = rockTopColor(0.0f);
        cap.cliffWall = false;
        addMeshQuad(model, cap);
        capCount++;
    }

    if (capCount > 0) {
        spdlog::info("addInnerCornerBevelCaps: added {} concave top bevel caps", capCount);
    }
}

void generateSimpleCliffMesh(RectangleCliffModel& model, const RectangleCliffSettings& settings) {
    const float h = settings.cliffHeight;
    const FastNoise::SmartNode<> rockNoise = settings.rockEnabled ? makeRockNoiseNode(settings) : nullptr;

    spdlog::info("generateSimpleCliffMesh: grid={}x{}, cliffHeight={}, rockEnabled={}, boundarySegments={}",
        settings.gridWidth, settings.gridHeight, h, settings.rockEnabled, model.boundarySegments.size());

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            if (!isSolidCell(model, settings, x, y)) {
                continue;
            }

            if (settings.rockEnabled) {
                addRockTopCell(model, settings, rockNoise, x, y);
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

    if (settings.rockEnabled) {
        addInnerCornerBevelCaps(model, settings);
    }

    const std::vector<MeshBoundarySegment> meshBoundarySegments = buildBeveledBoundarySegments(model, settings);

    for (const MeshBoundarySegment& segment : meshBoundarySegments) {
        if (!settings.rockEnabled) {
            MeshQuad wall;
            wall.a = {segment.a.x, h, segment.a.z};
            wall.b = {segment.b.x, h, segment.b.z};
            wall.c = {segment.b.x, 0.0f, segment.b.z};
            wall.d = {segment.a.x, 0.0f, segment.a.z};
            wall.color = colorForBoundarySide(segment.side);
            wall.cliffWall = true;
            addMeshQuad(model, wall);
            continue;
        }

        const Vec3 topA{segment.a.x, h, segment.a.z};
        const Vec3 topB{segment.b.x, h, segment.b.z};
        const Vec3 bottomA{segment.a.x, 0.0f, segment.a.z};
        const Vec3 bottomB{segment.b.x, 0.0f, segment.b.z};
        const int vertexColumns = settings.wallHorizontalSubdivisions + 1;
        const int vertexRows = settings.wallVerticalSubdivisions + 1;
        std::vector<Vec3> wallPoints((std::size_t)vertexColumns * (std::size_t)vertexRows);
        std::vector<Vec3> wallNormals((std::size_t)vertexColumns * (std::size_t)vertexRows);
        std::vector<float> wallNoise;

        for (int row = 0; row < vertexRows; row++) {
            const float v = (float)row / (float)settings.wallVerticalSubdivisions;
            const float topT = 1.0f - v;
            const Vec3 rowA = lerpVec3(bottomA, topA, topT);
            const Vec3 rowB = lerpVec3(bottomB, topB, topT);

            for (int column = 0; column < vertexColumns; column++) {
                const float u = (float)column / (float)settings.wallHorizontalSubdivisions;
                const std::size_t index = (std::size_t)row * (std::size_t)vertexColumns + (std::size_t)column;
                wallPoints[index] = lerpVec3(rowA, rowB, u);
                if (column == 0) {
                    wallNormals[index] = segment.startNormal;
                } else if (column == vertexColumns - 1) {
                    wallNormals[index] = segment.endNormal;
                } else {
                    wallNormals[index] = segment.normal;
                }
            }
        }

        sampleRockNoiseBatch(rockNoise, settings, wallPoints, wallNoise);

        for (int sy = 0; sy < settings.wallVerticalSubdivisions; sy++) {
            const float v0 = (float)sy / (float)settings.wallVerticalSubdivisions;
            const float v1 = (float)(sy + 1) / (float)settings.wallVerticalSubdivisions;
            const float topT0 = 1.0f - v0;
            const float topT1 = 1.0f - v1;

            for (int sx = 0; sx < settings.wallHorizontalSubdivisions; sx++) {
                const std::size_t i00 = (std::size_t)sy * (std::size_t)vertexColumns + (std::size_t)sx;
                const std::size_t i10 = i00 + 1;
                const std::size_t i01 = (std::size_t)(sy + 1) * (std::size_t)vertexColumns + (std::size_t)sx;
                const std::size_t i11 = i01 + 1;

                MeshQuad wall;
                wall.a = displaceRockPoint(settings, wallPoints[i00], wallNormals[i00], topT0, wallNoise[i00]);
                wall.b = displaceRockPoint(settings, wallPoints[i10], wallNormals[i10], topT0, wallNoise[i10]);
                wall.c = displaceRockPoint(settings, wallPoints[i11], wallNormals[i11], topT1, wallNoise[i11]);
                wall.d = displaceRockPoint(settings, wallPoints[i01], wallNormals[i01], topT1, wallNoise[i01]);

                const float panelNoise = (wallNoise[i00] + wallNoise[i10] + wallNoise[i11] + wallNoise[i01]) * 0.25f;
                wall.color = rockWallColor(segment.side, (topT0 + topT1) * 0.5f, panelNoise);
                wall.cliffWall = true;
                addMeshQuad(model, wall);
            }
        }
    }

    spdlog::info("generateSimpleCliffMesh: result topQuads={}, cliffWallQuads={}, total={}",
        model.topQuadCount, model.cliffWallQuadCount, model.meshQuads.size());
}

void rebuildRectangleCliffModel() {
    spdlog::info("rebuildRectangleCliffModel: start");

    RectangleCliffSettings settings = g_rectSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rectSettings = settings;
    }

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

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rectModel = std::move(model);
    }

    spdlog::info("rebuildRectangleCliffModel: done, solidCells={}, boundarySegments={}, vertices E={}/O={}/I={}/D={}",
        g_rectModel.solidCellCount,
        g_rectModel.boundarySegments.size(),
        g_rectModel.edgeVertexCount,
        g_rectModel.outerCornerCount,
        g_rectModel.innerCornerCount,
        g_rectModel.diagonalJoinCount);
}

int landscapeIndex(int x, int y, int width) {
    return y * width + x;
}

float smoothStep(float edge0, float edge1, float value) {
    if (edge0 == edge1) {
        return value < edge0 ? 0.0f : 1.0f;
    }

    const float t = clampFloat((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float deterministicFloat01(int seed, int salt) {
    const float value = std::sin((float)(seed * 37 + salt * 101) * 12.9898f) * 43758.5453f;
    return value - std::floor(value);
}

float landscapeHeightAtCell(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y) {
    x = clampInt(x, 0, settings.gridWidth - 1);
    y = clampInt(y, 0, settings.gridHeight - 1);
    return model.heights[(std::size_t)landscapeIndex(x, y, settings.gridWidth)];
}

LandscapeZone landscapeZoneAtCell(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y) {
    x = clampInt(x, 0, settings.gridWidth - 1);
    y = clampInt(y, 0, settings.gridHeight - 1);
    return model.zones[(std::size_t)landscapeIndex(x, y, settings.gridWidth)];
}

int landscapeLevelAtCell(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y) {
    x = clampInt(x, 0, settings.gridWidth - 1);
    y = clampInt(y, 0, settings.gridHeight - 1);
    return (int)model.heightLevels[(std::size_t)landscapeIndex(x, y, settings.gridWidth)];
}

float landscapeHeightAtVertex(const LandscapeBowlModel& model, const LandscapeBowlSettings& settings, int x, int y) {
    const float h00 = landscapeHeightAtCell(model, settings, x - 1, y - 1);
    const float h10 = landscapeHeightAtCell(model, settings, x, y - 1);
    const float h01 = landscapeHeightAtCell(model, settings, x - 1, y);
    const float h11 = landscapeHeightAtCell(model, settings, x, y);
    return (h00 + h10 + h01 + h11) * 0.25f;
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

ImU32 landscapeWallColor(float heightT, float noiseValue) {
    const int shade = (int)(noiseValue * 22.0f) + (int)((1.0f - heightT) * 34.0f);
    return IM_COL32(
        clampInt(92 + shade, 52, 162),
        clampInt(88 + shade, 50, 154),
        clampInt(76 + shade, 44, 136),
        255);
}

bool verticalRangesOverlap(float a0, float a1, float b0, float b1) {
    const float aMin = std::min(a0, a1);
    const float aMax = std::max(a0, a1);
    const float bMin = std::min(b0, b1);
    const float bMax = std::max(b0, b1);
    return std::min(aMax, bMax) - std::max(aMin, bMin) > 0.0001f;
}

Vec3 landscapeBoundaryVertexNormal(
    const LandscapeBowlModel& model,
    const LandscapeBowlSettings& settings,
    const Int2& vertex,
    float lowHeight,
    float highHeight,
    const Vec3& fallback) {

    Vec3 sum{};
    bool found = false;
    auto addCandidate = [&](const Int2& a, const Int2& b, float firstHeight, float secondHeight, const Vec3& normal) {
        if (!samePoint(vertex, a) && !samePoint(vertex, b)) {
            return;
        }
        if (!verticalRangesOverlap(lowHeight, highHeight, firstHeight, secondHeight)) {
            return;
        }

        sum.x += normal.x;
        sum.z += normal.z;
        found = true;
    };

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const float currentHeight = landscapeHeightAtCell(model, settings, x, y);
            const int currentLevel = landscapeLevelAtCell(model, settings, x, y);
            if (x + 1 < settings.gridWidth) {
                const float neighborHeight = landscapeHeightAtCell(model, settings, x + 1, y);
                const int neighborLevel = landscapeLevelAtCell(model, settings, x + 1, y);
                if (currentLevel != neighborLevel) {
                    const Vec3 normal = currentHeight > neighborHeight ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{-1.0f, 0.0f, 0.0f};
                    addCandidate({x + 1, y}, {x + 1, y + 1}, currentHeight, neighborHeight, normal);
                }
            }

            if (y + 1 < settings.gridHeight) {
                const float neighborHeight = landscapeHeightAtCell(model, settings, x, y + 1);
                const int neighborLevel = landscapeLevelAtCell(model, settings, x, y + 1);
                if (currentLevel != neighborLevel) {
                    const Vec3 normal = currentHeight > neighborHeight ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 0.0f, -1.0f};
                    addCandidate({x, y + 1}, {x + 1, y + 1}, currentHeight, neighborHeight, normal);
                }
            }
        }
    }

    if (!found) {
        return fallback;
    }

    return normalizeHorizontal(sum, fallback);
}

void addLandscapeCliffWall(
    LandscapeBowlModel& model,
    const LandscapeBowlSettings& settings,
    const RectangleCliffSettings& rockSettings,
    const FastNoise::SmartNode<>& rockNoise,
    const Vec3& topA,
    const Vec3& topB,
    const Vec3& bottomA,
    const Vec3& bottomB,
    const Vec3& normal,
    const Vec3& startNormal,
    const Vec3& endNormal) {

    const int horizontalSubdivisions = 3;
    const int verticalSubdivisions = 4;
    const int vertexColumns = horizontalSubdivisions + 1;
    const int vertexRows = verticalSubdivisions + 1;
    std::vector<Vec3> wallPoints((std::size_t)vertexColumns * (std::size_t)vertexRows);
    std::vector<Vec3> wallNormals((std::size_t)vertexColumns * (std::size_t)vertexRows);
    std::vector<float> wallNoise;

    for (int row = 0; row < vertexRows; row++) {
        const float v = (float)row / (float)verticalSubdivisions;
        const float topT = 1.0f - v;
        const Vec3 rowA = lerpVec3(bottomA, topA, topT);
        const Vec3 rowB = lerpVec3(bottomB, topB, topT);
        for (int column = 0; column < vertexColumns; column++) {
            const float u = (float)column / (float)horizontalSubdivisions;
            const std::size_t index = (std::size_t)row * (std::size_t)vertexColumns + (std::size_t)column;
            wallPoints[index] = lerpVec3(rowA, rowB, u);
            if (column == 0) {
                wallNormals[index] = startNormal;
            } else if (column == vertexColumns - 1) {
                wallNormals[index] = endNormal;
            } else {
                wallNormals[index] = normal;
            }
        }
    }

    sampleRockNoiseBatch(rockNoise, rockSettings, wallPoints, wallNoise);

    for (int sy = 0; sy < verticalSubdivisions; sy++) {
        const float v0 = (float)sy / (float)verticalSubdivisions;
        const float v1 = (float)(sy + 1) / (float)verticalSubdivisions;
        const float topT0 = 1.0f - v0;
        const float topT1 = 1.0f - v1;
        for (int sx = 0; sx < horizontalSubdivisions; sx++) {
            const std::size_t i00 = (std::size_t)sy * (std::size_t)vertexColumns + (std::size_t)sx;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = (std::size_t)(sy + 1) * (std::size_t)vertexColumns + (std::size_t)sx;
            const std::size_t i11 = i01 + 1;

            MeshQuad wall;
            wall.a = displaceLandscapeWallPoint(rockSettings, wallPoints[i00], wallNormals[i00], topT0, wallNoise[i00]);
            wall.b = displaceLandscapeWallPoint(rockSettings, wallPoints[i10], wallNormals[i10], topT0, wallNoise[i10]);
            wall.c = displaceLandscapeWallPoint(rockSettings, wallPoints[i11], wallNormals[i11], topT1, wallNoise[i11]);
            wall.d = displaceLandscapeWallPoint(rockSettings, wallPoints[i01], wallNormals[i01], topT1, wallNoise[i01]);
            wall.color = landscapeWallColor((topT0 + topT1) * 0.5f, (wallNoise[i00] + wallNoise[i10] + wallNoise[i11] + wallNoise[i01]) * 0.25f);
            wall.cliffWall = true;
            addMeshQuad(model, wall);
        }
    }
}

void generateLandscapeBowlMesh(LandscapeBowlModel& model, const LandscapeBowlSettings& settings) {
    RectangleCliffSettings rockSettings;
    rockSettings.rockEnabled = true;
    rockSettings.rockSeed = settings.seed + 7919;
    rockSettings.rockScale = std::max(0.5f, settings.arcNoiseScale * 1.25f);
    rockSettings.rockAmplitude = 0.18f;
    rockSettings.terraceSteps = 3;
    sanitizeSettings(rockSettings);

    const FastNoise::SmartNode<> rockNoise = makeRockNoiseNode(rockSettings);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const float currentHeight = landscapeHeightAtCell(model, settings, x, y);

            MeshQuad top;
            top.a = {(float)x, currentHeight, (float)y};
            top.b = {(float)(x + 1), currentHeight, (float)y};
            top.c = {(float)(x + 1), currentHeight, (float)(y + 1)};
            top.d = {(float)x, currentHeight, (float)(y + 1)};
            top.color = colorForLandscapeZone(landscapeZoneAtCell(model, settings, x, y), currentHeight, model.minHeight, model.maxHeight);
            top.cliffWall = false;
            addMeshQuad(model, top);

            if (x + 1 < settings.gridWidth) {
                const float neighborHeight = landscapeHeightAtCell(model, settings, x + 1, y);
                if (landscapeLevelAtCell(model, settings, x, y) != landscapeLevelAtCell(model, settings, x + 1, y)) {
                    const float high = std::max(currentHeight, neighborHeight);
                    const float low = std::min(currentHeight, neighborHeight);
                    const Vec3 normal = currentHeight > neighborHeight ? Vec3{1.0f, 0.0f, 0.0f} : Vec3{-1.0f, 0.0f, 0.0f};
                    const Vec3 startNormal = landscapeBoundaryVertexNormal(model, settings, {x + 1, y}, low, high, normal);
                    const Vec3 endNormal = landscapeBoundaryVertexNormal(model, settings, {x + 1, y + 1}, low, high, normal);
                    addLandscapeCliffWall(
                        model,
                        settings,
                        rockSettings,
                        rockNoise,
                        {(float)(x + 1), high, (float)y},
                        {(float)(x + 1), high, (float)(y + 1)},
                        {(float)(x + 1), low, (float)y},
                        {(float)(x + 1), low, (float)(y + 1)},
                        normal,
                        startNormal,
                        endNormal);
                }
            }

            if (y + 1 < settings.gridHeight) {
                const float neighborHeight = landscapeHeightAtCell(model, settings, x, y + 1);
                if (landscapeLevelAtCell(model, settings, x, y) != landscapeLevelAtCell(model, settings, x, y + 1)) {
                    const float high = std::max(currentHeight, neighborHeight);
                    const float low = std::min(currentHeight, neighborHeight);
                    const Vec3 normal = currentHeight > neighborHeight ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 0.0f, -1.0f};
                    const Vec3 startNormal = landscapeBoundaryVertexNormal(model, settings, {x, y + 1}, low, high, normal);
                    const Vec3 endNormal = landscapeBoundaryVertexNormal(model, settings, {x + 1, y + 1}, low, high, normal);
                    addLandscapeCliffWall(
                        model,
                        settings,
                        rockSettings,
                        rockNoise,
                        {(float)x, high, (float)(y + 1)},
                        {(float)(x + 1), high, (float)(y + 1)},
                        {(float)x, low, (float)(y + 1)},
                        {(float)(x + 1), low, (float)(y + 1)},
                        normal,
                        startNormal,
                        endNormal);
                }
            }
        }
    }
}

void rebuildLandscapeBowlModel() {
    spdlog::info("rebuildLandscapeBowlModel: start");

    LandscapeBowlSettings settings = g_landscapeSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_landscapeSettings = settings;
    }

    LandscapeBowlModel model;
    model.heights.assign((std::size_t)settings.gridWidth * (std::size_t)settings.gridHeight, 0.0f);
    model.heightLevels.assign(model.heights.size(), 0);
    model.zones.assign(model.heights.size(), LandscapeZone::Lowland);
    model.levelCellCounts.assign((std::size_t)settings.heightLevels, 0);
    model.minHeight = 100000.0f;
    model.maxHeight = -100000.0f;

    const FastNoise::SmartNode<> noiseNode = makeLandscapeNoiseNode(settings);
    std::vector<Vec3> samplePoints;
    samplePoints.reserve(model.heights.size());
    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            samplePoints.push_back({(float)x * 0.42f, 0.0f, (float)y * 0.42f});
        }
    }

    std::vector<float> noiseValues;
    sampleLandscapeNoiseBatch(noiseNode, settings, samplePoints, noiseValues);

    const float centerX = (float)settings.gridWidth * 0.5f;
    const float centerY = (float)settings.gridHeight * 0.58f;

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const std::size_t index = (std::size_t)landscapeIndex(x, y, settings.gridWidth);
            const float px = (float)x + 0.5f;
            const float py = (float)y + 0.5f;
            const float dx = px - centerX;
            const float dy = py - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            const float topHalfMask = smoothStep(0.0f, settings.highGroundRadius * 0.5f, -dy + settings.clearingRadius * 0.2f);
            const float distortedRadius = settings.highGroundRadius + noiseValues[index] * settings.arcNoiseAmplitude;
            const float ringDistance = std::abs(distance - distortedRadius);
            const float ringMask = 1.0f - smoothStep(settings.highGroundWidth * 0.35f, settings.highGroundWidth, ringDistance);
            const float highGroundMask = clampFloat(topHalfMask * ringMask, 0.0f, 1.0f);
            const float clearingMask = 1.0f - smoothStep(
                settings.clearingRadius,
                settings.clearingRadius + settings.clearingSoftness,
                distance);

            float hillContribution = 0.0f;
            for (int hill = 0; hill < settings.hillCount; hill++) {
                const float angleT = 0.12f + deterministicFloat01(settings.seed, hill * 7 + 1) * 0.76f;
                const float angle = 3.14159265f * angleT;
                const float radius = settings.highGroundRadius + (deterministicFloat01(settings.seed, hill * 7 + 2) - 0.5f) * settings.highGroundWidth;
                const float hillX = centerX + std::cos(angle) * radius;
                const float hillY = centerY - std::sin(angle) * radius;
                const float hillDx = px - hillX;
                const float hillDy = py - hillY;
                const float hillDistanceSq = hillDx * hillDx + hillDy * hillDy;
                const float hillRadius = settings.hillRadius * (0.75f + deterministicFloat01(settings.seed, hill * 7 + 3) * 0.65f);
                const float local = std::exp(-hillDistanceSq / std::max(0.001f, hillRadius * hillRadius));
                hillContribution += local * settings.hillHeight * (0.7f + deterministicFloat01(settings.seed, hill * 7 + 4) * 0.6f);
            }

            const int maxLevel = settings.heightLevels - 1;
            int level = 0;
            if (clearingMask <= 0.62f) {
                const float hillLevelBoost = clampFloat(hillContribution / std::max(0.001f, settings.hillHeight), 0.0f, 1.5f);
                const float levelScore = highGroundMask * (float)maxLevel + hillLevelBoost;
                level = clampInt((int)std::round(levelScore), 0, maxLevel);
                if (highGroundMask > 0.18f) {
                    level = std::max(level, 1);
                }
                if (highGroundMask > 0.55f) {
                    level = std::max(level, std::min(maxLevel, 2));
                }
                if (highGroundMask > 0.82f || hillLevelBoost > 1.05f) {
                    level = std::max(level, maxLevel);
                }
            }

            const float levelHeight = settings.highGroundHeight / (float)std::max(1, maxLevel);
            const float height = (float)level * levelHeight;

            LandscapeZone zone = LandscapeZone::Lowland;
            if (clearingMask > 0.7f) {
                zone = LandscapeZone::Clearing;
                model.clearingCellCount++;
            } else if (hillContribution > settings.hillHeight * 0.35f && level > 0) {
                zone = LandscapeZone::Hill;
                model.hillCellCount++;
            } else if (level >= std::max(1, maxLevel - 1)) {
                zone = LandscapeZone::HighGround;
                model.highGroundCellCount++;
            } else if (level > 0) {
                zone = LandscapeZone::Slope;
            }

            model.heights[index] = height;
            model.heightLevels[index] = (std::uint8_t)level;
            model.zones[index] = zone;
            if (level >= 0 && level < (int)model.levelCellCounts.size()) {
                model.levelCellCounts[(std::size_t)level]++;
            }
            model.minHeight = std::min(model.minHeight, height);
            model.maxHeight = std::max(model.maxHeight, height);
        }
    }

    generateLandscapeBowlMesh(model, settings);

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_landscapeModel = std::move(model);
    }

    spdlog::info("rebuildLandscapeBowlModel: done, grid={}x{}, heightRange={:.2f}-{:.2f}, cells clearing/high/hill={}/{}/{}, quads top/walls/total={}/{}/{}",
        settings.gridWidth,
        settings.gridHeight,
        g_landscapeModel.minHeight,
        g_landscapeModel.maxHeight,
        g_landscapeModel.clearingCellCount,
        g_landscapeModel.highGroundCellCount,
        g_landscapeModel.hillCellCount,
        g_landscapeModel.topQuadCount,
        g_landscapeModel.cliffWallQuadCount,
        g_landscapeModel.meshQuads.size());
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
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "2D level map: clearing + terraced noisy upper high-ground arc");
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

    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "Landscape Bowl 3D preview: flat terraces + rocky cliff strips");
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, mouse wheel: zoom, double click: reset");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", g_landscapeCamera.zoom, g_landscapeCamera.pan.x, g_landscapeCamera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    drawList->PopClipRect();
}

void drawFrameStats() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    ImGui::Text("Frame: %d", g_state.frameIndex);
    ImGui::Text("dt: %.3f ms", 1000.0f * g_state.dt);
}

void drawRectangleScenarioControls(float panelWidth) {
    ImGui::PushItemWidth(panelWidth - 24.0f);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Rectangle Cliff Debug");
    ImGui::TextWrapped("Debug scenario for boundary vertices, cutouts, bevels and rocky wall stitching.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
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
        changed |= ImGui::SliderFloat("Corner Bevel", &g_rectSettings.cornerBevel, 0.0f, 0.45f);
        changed |= ImGui::Checkbox("Rock Noise Enabled", &g_rectSettings.rockEnabled);
        changed |= ImGui::InputInt("Rock Seed", &g_rectSettings.rockSeed);
        changed |= ImGui::SliderFloat("Rock Scale", &g_rectSettings.rockScale, 0.25f, 24.0f);
        changed |= ImGui::SliderFloat("Rock Amplitude", &g_rectSettings.rockAmplitude, 0.0f, 1.25f);
        changed |= ImGui::SliderInt("Wall Horizontal Subdivs", &g_rectSettings.wallHorizontalSubdivisions, 1, 16);
        changed |= ImGui::SliderInt("Wall Vertical Subdivs", &g_rectSettings.wallVerticalSubdivisions, 1, 16);
        changed |= ImGui::SliderInt("Terrace Steps", &g_rectSettings.terraceSteps, 0, 12);
        ImGui::Checkbox("Show Top Faces", &g_rectSettings.showTopFaces);
        ImGui::Checkbox("Show Cliff Walls", &g_rectSettings.showCliffWalls);
        ImGui::Checkbox("Show Mesh Wireframe", &g_rectSettings.showMeshWireframe);
        ImGui::Checkbox("Show Cell Labels", &g_rectSettings.showCellLabels);
        ImGui::Checkbox("Show Vertex Labels", &g_rectSettings.showVertexLabels);
    }
    if (changed) {
        rebuildRectangleCliffModel();
    }

    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::Text("Solid cells: %d", g_rectModel.solidCellCount);
        ImGui::Text("Boundary segments: %d", (int)g_rectModel.boundarySegments.size());
        ImGui::Text("3D quads top/walls/total: %d / %d / %d",
            g_rectModel.topQuadCount,
            g_rectModel.cliffWallQuadCount,
            (int)g_rectModel.meshQuads.size());
        ImGui::Text("Rock mode: %s", g_rectSettings.rockEnabled ? "FastNoise2 ridged" : "flat walls");
        ImGui::Text("Vertices E/O/I/D: %d / %d / %d / %d",
            g_rectModel.edgeVertexCount,
            g_rectModel.outerCornerCount,
            g_rectModel.innerCornerCount,
            g_rectModel.diagonalJoinCount);
    }
    ImGui::TextColored(ImVec4(0.36f, 0.70f, 1.0f, 1.0f), "E = straight edge vertex");
    ImGui::TextColored(ImVec4(0.98f, 0.77f, 0.28f, 1.0f), "O = outer corner");
    ImGui::TextColored(ImVec4(0.91f, 0.36f, 0.36f, 1.0f), "I = inner corner");
    ImGui::TextColored(ImVec4(0.73f, 0.46f, 1.0f, 1.0f), "D = diagonal join");
    ImGui::PopItemWidth();
}

void drawLandscapeScenarioControls(float panelWidth) {
    ImGui::PushItemWidth(panelWidth - 24.0f);
    drawFrameStats();
    ImGui::Separator();

    ImGui::Text("Landscape Bowl Preview");
    ImGui::TextWrapped("Discrete level scenario: flat clearing in the center, terraced upper high-ground arc and deterministic hills.");

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        changed |= ImGui::SliderInt("Grid Width", &g_landscapeSettings.gridWidth, 12, 72);
        changed |= ImGui::SliderInt("Grid Height", &g_landscapeSettings.gridHeight, 10, 56);
        changed |= ImGui::InputInt("Seed", &g_landscapeSettings.seed);
        changed |= ImGui::SliderFloat("Clearing Radius", &g_landscapeSettings.clearingRadius, 2.0f, 18.0f);
        changed |= ImGui::SliderFloat("Clearing Softness", &g_landscapeSettings.clearingSoftness, 0.25f, 8.0f);
        changed |= ImGui::SliderFloat("High Ground Radius", &g_landscapeSettings.highGroundRadius, 3.0f, 28.0f);
        changed |= ImGui::SliderFloat("High Ground Width", &g_landscapeSettings.highGroundWidth, 1.0f, 10.0f);
        changed |= ImGui::SliderFloat("High Ground Height", &g_landscapeSettings.highGroundHeight, 0.5f, 8.0f);
        changed |= ImGui::SliderInt("Height Levels", &g_landscapeSettings.heightLevels, 2, 6);
        changed |= ImGui::SliderFloat("Arc Noise Scale", &g_landscapeSettings.arcNoiseScale, 0.5f, 18.0f);
        changed |= ImGui::SliderFloat("Arc Noise Amplitude", &g_landscapeSettings.arcNoiseAmplitude, 0.0f, 5.0f);
        changed |= ImGui::SliderInt("Hill Count", &g_landscapeSettings.hillCount, 0, 12);
        changed |= ImGui::SliderFloat("Hill Height", &g_landscapeSettings.hillHeight, 0.0f, 5.0f);
        changed |= ImGui::SliderFloat("Hill Radius", &g_landscapeSettings.hillRadius, 0.75f, 8.0f);
        ImGui::Checkbox("Show Top Faces", &g_landscapeSettings.showTopFaces);
        ImGui::Checkbox("Show Cliff Walls", &g_landscapeSettings.showCliffWalls);
        ImGui::Checkbox("Show Mesh Wireframe", &g_landscapeSettings.showMeshWireframe);
        ImGui::Checkbox("Show Level Labels", &g_landscapeSettings.showHeightValues);
    }
    if (changed) {
        rebuildLandscapeBowlModel();
    }

    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::Text("Height range: %.2f - %.2f", g_landscapeModel.minHeight, g_landscapeModel.maxHeight);
        ImGui::Text("Cells clearing/high/hill: %d / %d / %d",
            g_landscapeModel.clearingCellCount,
            g_landscapeModel.highGroundCellCount,
            g_landscapeModel.hillCellCount);
        ImGui::Text("3D quads top/walls/total: %d / %d / %d",
            g_landscapeModel.topQuadCount,
            g_landscapeModel.cliffWallQuadCount,
            (int)g_landscapeModel.meshQuads.size());
        ImGui::Text("Level cells:");
        for (int level = 0; level < (int)g_landscapeModel.levelCellCounts.size(); level++) {
            ImGui::SameLine();
            ImGui::Text("L%d=%d", level, g_landscapeModel.levelCellCounts[(std::size_t)level]);
        }
    }
    ImGui::TextColored(ImVec4(0.43f, 0.63f, 0.35f, 1.0f), "Green = clearing / lowland");
    ImGui::TextColored(ImVec4(0.58f, 0.68f, 0.40f, 1.0f), "Olive = hills");
    ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.50f, 1.0f), "Bright = upper high ground");
    ImGui::PopItemWidth();
}

void drawScenarioPanelBackground(const ImVec2& layoutOrigin, const ImVec2& layoutSize, float leftPanelWidth) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(25, 28, 34, 255));
    drawList->AddRect(
        layoutOrigin,
        {layoutOrigin.x + leftPanelWidth, layoutOrigin.y + layoutSize.y},
        IM_COL32(65, 72, 84, 255));
}

void drawRectangleScenarioTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const float viewportHeight = std::max(1.0f, (layoutSize.y - gutter) * 0.5f);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawRectangleScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawRectangleCliffDebugView(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportHeight + gutter});
        drawMesh3dPreview(g_rectSettings, g_rectModel, {rightWidth, viewportHeight});
    }
}

void drawLandscapeScenarioTab(const ImVec2& layoutOrigin, const ImVec2& layoutSize) {
    const float gutter = 12.0f;
    const float leftPanelWidth = std::min(420.0f, std::max(320.0f, layoutSize.x * 0.34f));
    const float rightX = layoutOrigin.x + leftPanelWidth + gutter;
    const float rightWidth = std::max(1.0f, layoutSize.x - leftPanelWidth - gutter);
    const float viewportHeight = std::max(1.0f, (layoutSize.y - gutter) * 0.5f);

    drawScenarioPanelBackground(layoutOrigin, layoutSize, leftPanelWidth);
    ImGui::SetCursorScreenPos({layoutOrigin.x + 12.0f, layoutOrigin.y + 12.0f});
    ImGui::BeginGroup();
    drawLandscapeScenarioControls(leftPanelWidth);
    ImGui::EndGroup();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y});
        drawLandscapeBowlDebugView(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportHeight});

        ImGui::SetCursorScreenPos({rightX, layoutOrigin.y + viewportHeight + gutter});
        drawLandscapeMesh3dPreview(g_landscapeSettings, g_landscapeModel, {rightWidth, viewportHeight});
    }
}

void drawUi() {
    static bool layoutLogged = false;

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

    if (ImGui::BeginTabBar("ScenarioTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Rectangle Debug")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            if (!layoutLogged) {
                spdlog::info("drawUi: viewport pos=({}, {}), workSize={}x{}, first tab layoutSize={}x{}",
                    viewport->WorkPos.x,
                    viewport->WorkPos.y,
                    viewport->WorkSize.x,
                    viewport->WorkSize.y,
                    layoutSize.x,
                    layoutSize.y);
                layoutLogged = true;
            }
            drawRectangleScenarioTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Landscape Bowl")) {
            const ImVec2 layoutOrigin = ImGui::GetCursorScreenPos();
            const ImVec2 layoutSize = ImGui::GetContentRegionAvail();
            drawLandscapeScenarioTab(layoutOrigin, layoutSize);
            ImGui::SetCursorScreenPos({layoutOrigin.x, layoutOrigin.y + layoutSize.y});
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

std::string formatTimestamp(const char* dateFmt) {
    auto now = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &timeT);
#else
    localtime_r(&timeT, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, dateFmt);
    return oss.str();
}

std::filesystem::path resolveLogDir(const char* argv0) {
    std::filesystem::path exePath = std::filesystem::absolute(std::filesystem::path(argv0));
    std::filesystem::path dir = exePath.parent_path();
    while (!dir.empty() && dir.has_parent_path()) {
        if (std::filesystem::exists(dir / "CMakeLists.txt") &&
            std::filesystem::exists(dir / "vcpkg.json")) {
            return dir / "logs";
        }
        if (dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return std::filesystem::current_path() / "logs";
}

void setupLogger(const char* argv0) {
    try {
        std::filesystem::path logDir = resolveLogDir(argv0);
        std::filesystem::create_directories(logDir);

        std::string timestamp = formatTimestamp("%Y%m%d_%H%M%S");
        std::string logFileName = "MeshGenerationPlayground_" + timestamp + ".json.log";
        std::filesystem::path logPath = logDir / logFileName;

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), false);

        std::vector<spdlog::sink_ptr> sinks{consoleSink, fileSink};

        auto logger = std::make_shared<spdlog::logger>("meshgen", sinks.begin(), sinks.end());

        logger->set_pattern(
            "{\"ts\":\"%Y-%m-%dT%H:%M:%S.%e\",\"level\":\"%l\",\"msg\":\"%v\"}"
        );

        logger->set_level(spdlog::level::trace);
        logger->flush_on(spdlog::level::info);

        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::trace);

        spdlog::info("logger initialized: file={}", logPath.string());
    } catch (const std::exception& e) {
        spdlog::set_level(spdlog::level::trace);
        spdlog::error("setupLogger failed: {}", e.what());
    }
}

void simguiLog(
    const char* tag,
    std::uint32_t logLevel,
    std::uint32_t logItemId,
    const char* message,
    std::uint32_t lineNumber,
    const char* filename,
    void* userData) {

    (void)userData;

    const char* safeTag = tag ? tag : "simgui";
    const char* safeMessage = message ? message : "<no message>";
    const char* safeFilename = filename ? filename : "<unknown>";

    switch (logLevel) {
    case 0:
    case 1:
        spdlog::error("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    case 2:
        spdlog::warn("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    default:
        spdlog::info("{}: item={}, {} ({}:{})", safeTag, logItemId, safeMessage, safeFilename, lineNumber);
        break;
    }
}

void init() {
    spdlog::info("init: start");
    spdlog::info("init: calling stm_setup()");
    stm_setup();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.lastTime = stm_now();
    }
    spdlog::info("init: stm_setup() done, lastTime recorded");

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;

    spdlog::info("init: calling sg_setup() with environment from sglue_environment()");
    sg_setup(&desc);

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.gfxOk = sg_isvalid();
    }

    spdlog::info("init: sg_setup() result sg_isvalid()={}", g_state.gfxOk);

    if (!g_state.gfxOk) {
        spdlog::error("init: sg_setup FAILED, sg_isvalid()=false, cannot continue graphics init");
        return;
    }

    spdlog::info("init: calling simgui_setup()");
    simgui_desc_t imguiDesc = {};
    imguiDesc.max_vertices = 1048576;
    imguiDesc.logger.func = simguiLog;
    simgui_setup(&imguiDesc);

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_state.imguiOk = true;
    }
    spdlog::info("init: simgui_setup() done, imguiOk=true");

    spdlog::info("init: calling rebuildRectangleCliffModel()");
    rebuildRectangleCliffModel();
    spdlog::info("init: calling rebuildLandscapeBowlModel()");
    rebuildLandscapeBowlModel();
    spdlog::info("init: complete");
}

void frame() {
    const std::uint64_t now = stm_now();
    float dt;
    int frameIndex;
    bool gfxOk;
    bool imguiOk;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        dt = (float)stm_sec(stm_diff(now, g_state.lastTime));
        g_state.lastTime = now;
        g_state.frameIndex++;
        frameIndex = g_state.frameIndex;
        g_state.dt = dt;
        gfxOk = g_state.gfxOk;
        imguiOk = g_state.imguiOk;
    }

    if (frameIndex <= 5) {
        spdlog::info("frame: index={}, dt={:.3f}ms, gfxOk={}, imguiOk={}, window={}x{}, dpi={}",
            frameIndex, dt * 1000.0f, gfxOk, imguiOk,
            sapp_width(), sapp_height(), sapp_dpi_scale());
    }

    if (frameIndex % 600 == 0) {
        spdlog::info("frame: heartbeat index={}, dt={:.3f}ms, gfxOk={}, imguiOk={}",
            frameIndex, dt * 1000.0f, gfxOk, imguiOk);
    }

    if (!gfxOk) {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (!g_state.gfxFailureLogged) {
            spdlog::error("frame: skipping frame #{}, gfxOk=false, no graphics context available", frameIndex);
            g_state.gfxFailureLogged = true;
        }
        return;
    }

    const int width = sapp_width();
    const int height = sapp_height();

    if (imguiOk) {
        simgui_frame_desc_t frameDesc = {};
        frameDesc.width = width;
        frameDesc.height = height;
        frameDesc.delta_time = dt;
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

    if (imguiOk) {
        simgui_render();
        if (frameIndex <= 5) {
            const ImDrawData* drawData = ImGui::GetDrawData();
            spdlog::info("frame: post-simgui_render drawData cmdLists={}, vertices={}, indices={}, display={}x{}, framebufferScale={}x{}",
                drawData ? drawData->CmdListsCount : -1,
                drawData ? drawData->TotalVtxCount : -1,
                drawData ? drawData->TotalIdxCount : -1,
                drawData ? drawData->DisplaySize.x : -1.0f,
                drawData ? drawData->DisplaySize.y : -1.0f,
                drawData ? drawData->FramebufferScale.x : -1.0f,
                drawData ? drawData->FramebufferScale.y : -1.0f);
        }
    }

    sg_end_pass();
    sg_commit();
}

void cleanup() {
    spdlog::info("cleanup: start");

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (g_state.imguiOk) {
            spdlog::info("cleanup: calling simgui_shutdown()");
            simgui_shutdown();
            g_state.imguiOk = false;
        }

        if (sg_isvalid()) {
            spdlog::info("cleanup: calling sg_shutdown()");
            sg_shutdown();
        }
    }

    spdlog::info("cleanup: done, total frames rendered={}", g_state.frameIndex);
    spdlog::default_logger()->flush();
}

void event(const sapp_event* ev) {
    bool imguiOk;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        imguiOk = g_state.imguiOk;
    }

    if (imguiOk) {
        simgui_handle_event(ev);
    }

    if (ev->type == SAPP_EVENTTYPE_RESIZED) {
        spdlog::info("event: window resized to {}x{}", ev->window_width, ev->window_height);
    }
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;

    setupLogger(argv[0]);

    spdlog::info("main: MeshGenerationPlayground starting");
    spdlog::info("main: build config: C++20, SOKOL_D3D11={}, SOKOL_METAL={}, SOKOL_GLCORE={}, SOKOL_GLES3={}",
#if defined(SOKOL_D3D11)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_METAL)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_GLCORE)
        "YES"
#else
        "NO"
#endif
        ,
#if defined(SOKOL_GLES3)
        "YES"
#else
        "NO"
#endif
    );

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

    spdlog::info("main: sapp_desc configured: window={}x{}, sample_count={}, high_dpi={}, window_title=\"{}\"",
        desc.width, desc.height, desc.sample_count, desc.high_dpi, desc.window_title);
    spdlog::info("main: calling sapp_run()");

    sapp_run(&desc);

    spdlog::info("main: sapp_run() returned, exiting");
    spdlog::default_logger()->flush();
    return 0;
}
