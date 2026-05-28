#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include <imgui.h>

namespace meshgen_playground {

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
    Vec3 normal{0.0f, 1.0f, 0.0f};
    ImU32 color = IM_COL32(255, 255, 255, 255);
    bool cliffWall = false;
    float relief = 0.0f;
    float heightFraction = 1.0f;
    float depth = 0.0f;
    float cliffDistance = 1000.0f;
};

enum class LandscapeZone : std::uint8_t {
    Lowland,
    Clearing,
    Slope,
    HighGround,
    Hill,
};

struct MeshPreviewCamera {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
};

enum class ProductionPreviewDebugMode : int {
    Lit = 0,
    Albedo = 1,
    RawNormals = 2,
    StableNormals = 3,
    BlendedNormals = 4,
    Uv = 5,
    CliffProximity = 6,
    DepthOrder = 7,
};

struct ProductionPreviewSettings {
    float ambient = 0.98f;
    float diffuseStrength = 0.30f;
    float wallBrightness = 1.05f;
    float cliffDarkeningRadius = 1.35f;
    float cliffDarkeningStrength = 0.32f;
    float minTopBrightness = 0.68f;
    float edgeDarkness = 0.12f;
    float textureScale = 1.0f;
    float macroScale = 0.35f;
    float macroStrength = 0.08f;
    float wallDetailNormal = 1.0f;     // A1: 0 = flat cliff plane, 1 = full faceted relief.
    float wallAoStrength = 0.35f;      // A3: darken wall base and crevices.
    float wallEdgeWearStrength = 0.4f; // C1: lighten/desaturate protruding facet ridges.
    float wallCreviceStrength = 0.45f; // C2: darken recessed facets (dirt/moss).
    float wallGrainStrength = 0.22f;   // B2: procedural fbm rock grain amount.
    int debugMode = (int)ProductionPreviewDebugMode::Lit;
    bool useGpuRenderer = true;
};

inline int cellIndex(int x, int y, int width) {
    return y * width + x;
}

inline int clampInt(int value, int minValue, int maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    return std::clamp(value, minValue, maxValue);
}

inline float clampFloat(float value, float minValue, float maxValue) {
    if (maxValue < minValue) {
        return minValue;
    }
    return std::clamp(value, minValue, maxValue);
}

inline bool pointInRect(int x, int y, int rectX, int rectY, int rectWidth, int rectHeight) {
    return x >= rectX && y >= rectY && x < rectX + rectWidth && y < rectY + rectHeight;
}

inline float meshQuadDepth(const MeshQuad& quad) {
    const float x = (quad.a.x + quad.b.x + quad.c.x + quad.d.x) * 0.25f;
    const float y = (quad.a.y + quad.b.y + quad.c.y + quad.d.y) * 0.25f;
    const float z = (quad.a.z + quad.b.z + quad.c.z + quad.d.z) * 0.25f;
    return x + z - y * 0.25f;
}

} // namespace meshgen_playground
