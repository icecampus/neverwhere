#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "landscape_core/landscape_logic.h"

namespace landscape_mesh {

using BoundarySide = landscape_core::EdgeSide;

enum class VertexKind : std::uint8_t {
    Empty,
    Edge,
    OuterCorner,
    InnerCorner,
    SolidInterior,
    DiagonalJoin,
};

struct Int2 {
    int x = 0;
    int y = 0;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ColorRgba {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    std::uint8_t a = 255;
};

struct MeshQuad {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    Vec3 d;
    Vec3 normal{0.0f, 1.0f, 0.0f};
    ColorRgba color;
    bool cliffWall = false;
    // Signed rock displacement of the panel: >0 protruding ridge, <0 recessed crevice.
    float relief = 0.0f;
    // Vertical position of the wall panel: 0 at the base, 1 at the top.
    float heightFraction = 1.0f;
    BoundarySide boundarySide = BoundarySide::Top;
    Vec3 outwardHint{0.0f, 1.0f, 0.0f};
};

struct TileMesh {
    std::vector<MeshQuad> quads;
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

struct SolidMaskGrid {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> solidCells;
    std::vector<std::uint8_t> topCells;
    std::vector<landscape_core::LandscapeZone> zones;

    bool empty() const;
    int cellIndex(int x, int y) const;
    bool isSolid(int x, int y) const;
    bool hasTop(int x, int y) const;
    landscape_core::LandscapeZone zoneAt(int x, int y) const;
};

struct MeshBuildSettings {
    float cellSize = 1.0f;
    float levelHeight = 1.0f;
    float cornerBevel = 0.3f;
    bool rockEnabled = true;
    int rockSeed = 1337;
    float rockScale = 2.75f;
    float rockAmplitude = 0.28f;
    int wallHorizontalSubdivisions = 5;
    int wallVerticalSubdivisions = 6;
    int terraceSteps = 4;
};

struct CompositionStats {
    int surfaceTileCount = 0;
    int wallTileCount = 0;
    int reusedTileMeshCount = 0;
    int generatedTileMeshCount = 0;
    int uniqueTileMeshCount = 0;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
    int boundarySegmentCount = 0;
    int beveledSegmentCount = 0;
    int cornerCapCount = 0;
};

struct SeamValidation {
    bool passed = true;
    int checkedEdges = 0;
    int mismatches = 0;
    float maxGap = 0.0f;
};

struct NormalOrientationStats {
    int wallQuadsChecked = 0;
    int topQuadsChecked = 0;
    int outwardFailCount = 0;
    int outwardWarnCount = 0;
    float minWallOutwardDot = 1.0f;
};

struct CompositionResult {
    std::vector<MeshQuad> quads;
    CompositionStats stats;
    SeamValidation seams;
    NormalOrientationStats normalOrientation;
};

struct SolidMeshBuildRequest {
    SolidMaskGrid mask;
    float baseHeight = 0.0f;
    float topHeight = 1.0f;
    std::uint8_t level = 1;
    std::uint8_t maxLevel = 1;
    bool includeWalls = true;
    bool fadeWallDisplacementAtBottom = false;
};

struct WallPanelBuildRequest {
    float width = 4.0f;
    float height = 3.0f;
    bool fadeDisplacementAtBottom = true;
};

struct WallPanelMesh {
    std::vector<MeshQuad> baseQuads;
    std::vector<MeshQuad> displacedQuads;
};

struct BeveledBoundaryResult {
    std::vector<BoundarySegment> boundarySegments;
    std::vector<MeshBoundarySegment> beveledSegments;
};

class TileMeshCatalog {
public:
    explicit TileMeshCatalog(MeshBuildSettings settings);

    const TileMesh& meshFor(const landscape_core::LandscapeTileKey& key);
    int generatedCount() const { return m_generatedCount; }
    int reusedCount() const { return m_reusedCount; }
    int uniqueCount() const { return (int)m_meshes.size(); }

private:
    TileMesh buildMesh(const landscape_core::LandscapeTileKey& key) const;
    TileMesh buildSurfaceMesh(const landscape_core::LandscapeTileKey& key) const;
    TileMesh buildWallMesh(const landscape_core::LandscapeTileKey& key) const;

    MeshBuildSettings m_settings;
    std::map<landscape_core::LandscapeTileKey, TileMesh> m_meshes;
    int m_generatedCount = 0;
    int m_reusedCount = 0;
};

VertexKind classifyVertex(const SolidMaskGrid& mask, int x, int y);
std::vector<BoundarySegment> buildBoundarySegments(const SolidMaskGrid& mask);
BeveledBoundaryResult buildBeveledBoundary(const SolidMaskGrid& mask, const MeshBuildSettings& settings);
NormalOrientationStats validateNormalOrientation(const std::vector<MeshQuad>& quads);
// Lambert lighting normal for wall quads: mesh-authoritative outwardHint, not displaced facet geometry.
Vec3 litWallNormal(const MeshQuad& quad);
CompositionResult composeSolidMaskMesh(const SolidMeshBuildRequest& request, const MeshBuildSettings& settings);
CompositionResult composeLandscapeMesh(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings);
SeamValidation validateLandscapeSeams(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings);
// Production cliff wall mesh for one straight boundary segment (same code path as composeSolidMaskMesh).
std::vector<MeshQuad> buildWallQuadsFromBoundarySegment(
    const MeshBoundarySegment& segment,
    float baseHeight,
    float topHeight,
    bool fadeWallDisplacementAtBottom,
    float wallMaxOffset,
    const MeshBuildSettings& settings,
    std::vector<MeshQuad>* outBaseQuads = nullptr);
// Lab helper: one rectangular cliff face built through buildWallQuadsFromBoundarySegment.
std::vector<MeshQuad> buildDisplacedWallPanel(const WallPanelBuildRequest& request, const MeshBuildSettings& settings);
// Flat undisplaced quads plus displaced cliff quads for the same wall panel grid.
WallPanelMesh buildWallPanelMesh(const WallPanelBuildRequest& request, const MeshBuildSettings& settings);

} // namespace landscape_mesh
