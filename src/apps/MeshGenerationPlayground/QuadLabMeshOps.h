#pragma once

#include "PlaygroundTypes.h"

#include <cstdint>
#include <vector>

namespace meshgen_playground {

struct OrientedQuadParams {
    float width = 1.0f;
    float height = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    ImU32 color = IM_COL32(118, 126, 138, 255);
};

struct ExtrudeQuadColors {
    ImU32 bottom = IM_COL32(118, 126, 138, 255);
    ImU32 top = IM_COL32(168, 176, 188, 255);
    ImU32 sides[4] = {
        IM_COL32(196, 128, 88, 255),
        IM_COL32(176, 112, 78, 255),
        IM_COL32(156, 98, 70, 255),
        IM_COL32(176, 112, 78, 255),
    };
};

MeshQuad makeOrientedQuad(const OrientedQuadParams& params);
Vec3 quadCentroid(const MeshQuad& quad);
Vec3 projectOntoPlane(const Vec3& point, const Vec3& planeOrigin, const Vec3& planeNormal);

Vec3 liftExtrudeCornerFromSeed(
    const Vec3& corner,
    const Vec3& shellCentroid,
    const Vec3& extrudeDir,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int cornerSeed);

void appendExtrudedQuadWithShell(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    const Vec3& topA,
    const Vec3& topB,
    const Vec3& topC,
    const Vec3& topD,
    const Vec3& shellCentroid,
    bool colorizeFaces,
    const bool emitSides[4],
    const ExtrudeQuadColors* customColors = nullptr);

void appendExtrudedQuad(
    std::vector<MeshQuad>& out,
    const MeshQuad& quad,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int heightSeed,
    bool colorizeFaces,
    const ExtrudeQuadColors* customColors = nullptr);

// Extrude from a flat base quad but keep the displaced cliff face as the bottom panel.
void appendExtrudedQuadWithDisplacedBottom(
    std::vector<MeshQuad>& out,
    const MeshQuad& baseQuad,
    const MeshQuad& displacedBottom,
    const Vec3& extrudeNormal,
    float depth,
    float topCornerScale,
    float topHeightSpread,
    float topScaleSpread,
    int heightSeed,
    bool colorizeFaces,
    const ExtrudeQuadColors* customColors = nullptr);

bool assignTopCapCornersFromPanels(
    const MeshQuad& baseQuad,
    const MeshQuad& topTri0,
    const MeshQuad& topTri1,
    Vec3& outA,
    Vec3& outB,
    Vec3& outC,
    Vec3& outD);

ImU32 quadLabPanelTintColor(int panelIndex, int panelCount);

} // namespace meshgen_playground
