#pragma once

#include <algorithm>
#include <cmath>
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
    float sunShadow = 1.0f;
    BoundarySide boundarySide = BoundarySide::Top;
    Vec3 outwardHint{0.0f, 1.0f, 0.0f};
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

struct QuadLabPreviewCamera {
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
    float orbitYawDegrees = 35.0f;
    float orbitPitchDegrees = 28.0f;
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
    SunShadow = 8,
    NormalVectors = 9,
};

struct ProductionPreviewSettings {
    float ambient = 0.62f;
    float diffuseStrength = 0.70f;
    float wallBrightness = 1.08f;
    float rimStrength = 0.18f;
    float rimPower = 2.5f;
    float specularStrength = 0.12f;
    float shininess = 24.0f;
    float sunShadowStrength = 0.48f;
    float shadowTintStrength = 0.22f;
    float shadowAmbientFloor = 0.42f;
    float shadowSoftness = 1.65f;
    float cliffDarkeningRadius = 1.35f;
    float cliffDarkeningStrength = 0.22f;
    float minTopBrightness = 0.78f;
    float edgeDarkness = 0.08f;
    float textureScale = 1.0f;
    float macroScale = 0.35f;
    float macroStrength = 0.08f;
    float wallAoStrength = 0.35f;      // A3: darken wall base and crevices.
    float wallEdgeWearStrength = 0.22f; // C1: lighten/desaturate protruding facet ridges.
    float wallCreviceStrength = 0.24f; // C2: darken recessed facets (dirt/moss).
    float wallGrainStrength = 0.22f;   // B2: procedural fbm rock grain amount.
    // C3 (reserved): extra mask channel plumbed to the shader (options4) for a future facet effect.
    float wallFacetWearStrength = 0.0f;
    float wallFacetWearWidth = 1.4f;
    int debugMode = (int)ProductionPreviewDebugMode::Lit;
    float normalVectorScale = 0.5f;
    bool useGpuRenderer = true;
    // Throwaway test: overlay 2D environment sprites on top of the 3D landscape.
    bool showEnvSprites = false;
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

inline bool meshQuadIsTrianglePanel(const MeshQuad& quad) {
    const float dx = quad.c.x - quad.d.x;
    const float dy = quad.c.y - quad.d.y;
    const float dz = quad.c.z - quad.d.z;
    return dx * dx + dy * dy + dz * dz <= 1e-8f;
}

inline Vec3 meshTriangleNormalRaw(const Vec3& a, const Vec3& b, const Vec3& c) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float abz = b.z - a.z;
    const float acx = c.x - a.x;
    const float acy = c.y - a.y;
    const float acz = c.z - a.z;
    return {
        aby * acz - abz * acy,
        abz * acx - abx * acz,
        abx * acy - aby * acx,
    };
}

inline Vec3 meshTriangleNormalOriented(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& outward) {
    Vec3 normal = meshTriangleNormalRaw(a, b, c);
    const float lengthSq = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (lengthSq <= 1e-8f) {
        return outward;
    }
    const float invLength = 1.0f / std::sqrt(lengthSq);
    normal.x *= invLength;
    normal.y *= invLength;
    normal.z *= invLength;
    if (normal.x * outward.x + normal.y * outward.y + normal.z * outward.z < 0.0f) {
        normal.x = -normal.x;
        normal.y = -normal.y;
        normal.z = -normal.z;
    }
    return normal;
}

inline bool meshQuadIsAcDiagonalCornerPair(int lhs, int rhs) {
    return (lhs == 0 && rhs == 2) || (lhs == 2 && rhs == 0);
}

inline bool meshQuadIsBdDiagonalCornerPair(int lhs, int rhs) {
    return (lhs == 1 && rhs == 3) || (lhs == 3 && rhs == 1);
}

inline void meshQuadFindTwoExtremeCornerIndices(const float heights[4], bool highest, int outIndices[2]) {
    int first = 0;
    int second = 1;
    if (heights[1] > heights[first]) {
        first = 1;
        second = 0;
    }
    if (heights[2] > heights[first]) {
        second = first;
        first = 2;
    } else if (heights[2] > heights[second]) {
        second = 2;
    }
    if (heights[3] > heights[first]) {
        second = first;
        first = 3;
    } else if (heights[3] > heights[second]) {
        second = 3;
    }

    if (highest) {
        outIndices[0] = first;
        outIndices[1] = second;
        return;
    }

    int minFirst = 0;
    int minSecond = 1;
    if (heights[1] < heights[minFirst]) {
        minFirst = 1;
        minSecond = 0;
    }
    if (heights[2] < heights[minFirst]) {
        minSecond = minFirst;
        minFirst = 2;
    } else if (heights[2] < heights[minSecond]) {
        minSecond = 2;
    }
    if (heights[3] < heights[minFirst]) {
        minSecond = minFirst;
        minFirst = 3;
    } else if (heights[3] < heights[minSecond]) {
        minSecond = 3;
    }
    outIndices[0] = minFirst;
    outIndices[1] = minSecond;
}

inline bool meshQuadPreferAcDiagonalFromHeights(const float heights[4]) {
    int extremeIndices[2];
    meshQuadFindTwoExtremeCornerIndices(heights, true, extremeIndices);
    if (meshQuadIsAcDiagonalCornerPair(extremeIndices[0], extremeIndices[1])) {
        return true;
    }
    if (meshQuadIsBdDiagonalCornerPair(extremeIndices[0], extremeIndices[1])) {
        return false;
    }

    int lowIndices[2];
    meshQuadFindTwoExtremeCornerIndices(heights, false, lowIndices);
    if (meshQuadIsAcDiagonalCornerPair(lowIndices[0], lowIndices[1])) {
        return true;
    }
    if (meshQuadIsBdDiagonalCornerPair(lowIndices[0], lowIndices[1])) {
        return false;
    }

    return heights[0] + heights[2] >= heights[1] + heights[3];
}

inline bool meshQuadPreferAcDiagonal(
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& outward) {
    const float centerX = (a.x + b.x + c.x + d.x) * 0.25f;
    const float centerY = (a.y + b.y + c.y + d.y) * 0.25f;
    const float centerZ = (a.z + b.z + c.z + d.z) * 0.25f;
    const float heights[4] = {
        (a.x - centerX) * outward.x + (a.y - centerY) * outward.y + (a.z - centerZ) * outward.z,
        (b.x - centerX) * outward.x + (b.y - centerY) * outward.y + (b.z - centerZ) * outward.z,
        (c.x - centerX) * outward.x + (c.y - centerY) * outward.y + (c.z - centerZ) * outward.z,
        (d.x - centerX) * outward.x + (d.y - centerY) * outward.y + (d.z - centerZ) * outward.z,
    };
    return meshQuadPreferAcDiagonalFromHeights(heights);
}

} // namespace meshgen_playground
