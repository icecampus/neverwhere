#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "landscape_core/landscape_logic.h"

namespace landscape_mesh {

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
    ColorRgba color;
    bool cliffWall = false;
};

struct TileMesh {
    std::vector<MeshQuad> quads;
};

struct MeshBuildSettings {
    float cellSize = 1.0f;
    float levelHeight = 1.0f;
    float rockAmplitude = 0.16f;
    int wallHorizontalSubdivisions = 3;
    int wallVerticalSubdivisions = 4;
};

struct CompositionStats {
    int surfaceTileCount = 0;
    int wallTileCount = 0;
    int reusedTileMeshCount = 0;
    int generatedTileMeshCount = 0;
    int uniqueTileMeshCount = 0;
    int topQuadCount = 0;
    int cliffWallQuadCount = 0;
};

struct SeamValidation {
    bool passed = true;
    int checkedEdges = 0;
    int mismatches = 0;
    float maxGap = 0.0f;
};

struct CompositionResult {
    std::vector<MeshQuad> quads;
    CompositionStats stats;
    SeamValidation seams;
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

CompositionResult composeLandscapeMesh(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings);
SeamValidation validateLandscapeSeams(const landscape_core::LandscapeLevelGrid& grid, const MeshBuildSettings& settings);

} // namespace landscape_mesh
