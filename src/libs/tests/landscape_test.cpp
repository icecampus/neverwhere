#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <QUuid>
#include "topology/staggered_tiled_landscape.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"
#include "assets_library/assets/slice_asset.h"
#include "topology/staggered_isometry.h"
#include <landscape_core/landscape_logic.h>
#include <landscape_core/sun_shadow.h>
#include <landscape_mesh/landscape_mesh.h>

// Mock for SliceAsset to avoid loading real files
class MockSliceAsset : public SliceAsset {
public:
    explicit MockSliceAsset(QObject* parent = nullptr) : SliceAsset(parent) {}
};

class LandscapeTest : public ::testing::Test {
protected:
    void SetUp() override {
        layerModel = new LayerModel(nullptr);
        isoView = new StaggeredIsometryView(nullptr);
        mockAsset = new MockSliceAsset(nullptr);
    }

    void TearDown() override {
        delete layerModel;
        delete isoView;
        delete mockAsset;
    }

    void updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset, const math::ivec2& cellPosition, TileSet::TileType tileType)
    {
        layerModel->removeAll(cellPosition);
        if (tileType != TileSet::Unknown)
        {
            std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
            landObject->setName(QString("Landscape"));
            landObject->setPosition(cellPosition);
            landObject->setAssetUiid(sliceAsset->uuid());
            landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
            layerModel->addGameObject(std::move(landObject));
        }
    }

    LayerModel* layerModel;
    StaggeredIsometryView* isoView;
    MockSliceAsset* mockAsset;
};

int maxAdjacentLevelDelta(const landscape_core::LandscapeLevelGrid& grid) {
    int maxDelta = 0;
    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const int level = (int)grid.cellLevelAt(x, y);
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                        continue;
                    }
                    const int otherLevel = (int)grid.cellLevelAt(nx, ny);
                    const int delta = level > otherLevel ? level - otherLevel : otherLevel - level;
                    maxDelta = std::max(maxDelta, delta);
                }
            }
        }
    }
    return maxDelta;
}

TEST_F(LandscapeTest, NodeDrawingLogic) {
    math::ivec2 nodePos(10, 10);
    LandNodes landNodes;
    landNodes.init(100, 100);
    landNodes[nodePos] = 1;

    StaggeredIsometry::Neighbours neighbours = StaggeredIsometry::nodeNeighboursCell(nodePos);
    for (const math::ivec2& curCellPosition : neighbours)
    {
        TileSet::TileType tileType = landNodes.getTileType(curCellPosition);
        updateLandscapeCell(layerModel, mockAsset, curCellPosition, tileType);
    }

    for (const math::ivec2& curCellPosition : neighbours)
    {
        std::vector<GameObject*> objects = layerModel->getObjectsAt(curCellPosition);
        EXPECT_FALSE(objects.empty());
        Landscape* landObject = dynamic_cast<Landscape*>(objects.back());
        ASSERT_NE(landObject, nullptr);
        TileSet::TileType expectedType = landNodes.getTileType(curCellPosition);
        EXPECT_EQ(landObject->getTileIndex(), mockAsset->subTileIndexByType(expectedType));
    }
}

TEST_F(LandscapeTest, PencilConnectivityTest) {
    // 1. Рисуем первую точку (узел)
    math::ivec2 node1(10, 10);
    LandNodes nodes1;
    nodes1.init(100, 100);
    nodes1[node1] = 1;

    // Обновляем клетки вокруг node1
    auto cells1 = StaggeredIsometry::nodeNeighboursCell(node1);
    for (const auto& cellPos : cells1) {
        updateLandscapeCell(layerModel, mockAsset, cellPos, nodes1.getTileType(cellPos));
    }

    // 2. Рисуем ВТОРУЮ точку рядом (которая делит клетки с node1)
    math::ivec2 node2 = node1 + math::ivec2(0, 2); 
    
    // СИМУЛИРУЕМ РАБОТУ КИСТИ (которая должна обновить клетки вокруг НОВОГО узла)
    LandNodes nodes2 = LandNodes::createByMap(*layerModel);
    nodes2[node2] = 1;
    
    // Обновляем клетки вокруг НОВОГО узла
    auto cells2 = StaggeredIsometry::nodeNeighboursCell(node2);
    for (const auto& cellPos : cells2) {
        updateLandscapeCell(layerModel, mockAsset, cellPos, nodes2.getTileType(cellPos));
    }

    // 3. ПРОВЕРКА:
    // Мы ожидаем, что общие клетки теперь имеют правильный тип (например, UpAndDownCorners)
    // в LayerModel, так как Pencil обновил их при обработке node2.
    for (const auto& cellPos : cells1) {
        auto objects = layerModel->getObjectsAt(cellPos);
        if (objects.empty()) continue; 
        
        Landscape* land = dynamic_cast<Landscape*>(objects.back());
        TileSet::TileType currentTypeInMap = nodes2.getTileType(cellPos);
        
        EXPECT_EQ(land->getTileIndex(), mockAsset->subTileIndexByType(currentTypeInMap))
            << "Cell at " << cellPos.x << "," << cellPos.y << " should be updated by Pencil connectivity logic!";
    }
}

TEST(LandscapePipelineTest, SharedTileResolverMatchesTileSetMasks) {
    TileSet tileSet;
    for (const auto& [mask, tileType] : tileSet.tileset) {
        const landscape_core::LandscapeTileType sharedType = landscape_core::nodeMaskToTileType(mask);
        EXPECT_EQ((int)sharedType, (int)tileType)
            << "Shared no-Qt tile resolver must preserve editor TileSet mask semantics";
    }
}

TEST(LandscapePipelineTest, BowlGeneratorKeepsClearingLowAndUsesDiscreteLevels) {
    landscape_core::LandscapeBowlSettings settings;
    settings.gridWidth = 32;
    settings.gridHeight = 24;
    settings.heightLevels = 4;
    settings.clearingRadius = 5.5f;

    landscape_core::BowlGenerationStats stats;
    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(settings, &stats);

    ASSERT_FALSE(grid.empty());
    ASSERT_EQ((int)stats.levelCellCounts.size(), settings.heightLevels);
    EXPECT_GT(stats.clearingCellCount, 0);
    EXPECT_GT(stats.highGroundCellCount + stats.hillCellCount, 0);
    EXPECT_EQ(grid.cellLevelAt(settings.gridWidth / 2, settings.gridHeight / 2), 0);
    for (std::uint8_t level : grid.cellLevels) {
        EXPECT_LT(level, settings.heightLevels);
    }
    EXPECT_LE(stats.maxAdjacentLevelDelta, 1);
    EXPECT_LE(maxAdjacentLevelDelta(grid), 1);
}

TEST(LandscapePipelineTest, BowlGeneratorStacksHighGroundAsPyramid) {
    landscape_core::LandscapeBowlSettings settings;
    settings.gridWidth = 36;
    settings.gridHeight = 28;
    settings.heightLevels = 5;
    settings.clearingRadius = 4.5f;
    settings.highGroundRadius = 8.5f;
    settings.highGroundWidth = 2.2f;
    settings.hillCount = 8;
    settings.hillHeight = 2.8f;

    landscape_core::BowlGenerationStats stats;
    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(settings, &stats);

    ASSERT_FALSE(grid.empty());
    EXPECT_LE(stats.maxAdjacentLevelDelta, 1);
    EXPECT_LE(maxAdjacentLevelDelta(grid), 1);

    for (int y = 0; y < grid.height; y++) {
        for (int x = 0; x < grid.width; x++) {
            const int level = (int)grid.cellLevelAt(x, y);
            if (level < 2) {
                continue;
            }

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= grid.width || ny >= grid.height) {
                        continue;
                    }
                    EXPECT_GT(grid.cellLevelAt(nx, ny), 0)
                        << "High ground level " << level << " must have at least one logical cell of slope before level 0";
                }
            }
        }
    }
}

TEST(LandscapePipelineTest, MeshComposerUsesReusableTilesAndPassesSeamContract) {
    landscape_core::LandscapeBowlSettings settings;
    settings.gridWidth = 24;
    settings.gridHeight = 18;
    settings.heightLevels = 4;

    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(settings);
    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.wallHorizontalSubdivisions = 3;
    meshSettings.wallVerticalSubdivisions = 4;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeLandscapeMesh(grid, meshSettings);

    EXPECT_GT(result.stats.surfaceTileCount, 0);
    EXPECT_GT(result.stats.beveledSegmentCount, 0);
    EXPECT_GT(result.stats.cliffWallQuadCount, 0);
    EXPECT_TRUE(result.seams.passed);
    EXPECT_EQ(result.seams.mismatches, 0);
    EXPECT_EQ(result.normalOrientation.outwardFailCount, 0);
}

TEST(LandscapePipelineTest, BaseLevelSurfaceReachesHigherLevelCliffFoot) {
    landscape_core::LandscapeLevelGrid grid;
    grid.width = 2;
    grid.height = 1;
    grid.levelCount = 2;
    grid.levelHeight = 1.0f;
    grid.cellLevels = {0, 1};
    grid.nodeLevels.assign(6, 0);
    grid.zones.assign(2, landscape_core::LandscapeZone::Lowland);

    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cornerBevel = 0.3f;
    meshSettings.wallHorizontalSubdivisions = 4;
    meshSettings.wallVerticalSubdivisions = 3;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeLandscapeMesh(grid, meshSettings);

    bool baseTopTouchesCliffFoot = false;
    for (const landscape_mesh::MeshQuad& quad : result.quads) {
        if (quad.cliffWall || quad.a.y != 0.0f || quad.b.y != 0.0f || quad.c.y != 0.0f || quad.d.y != 0.0f) {
            continue;
        }

        const float maxX = std::max({quad.a.x, quad.b.x, quad.c.x, quad.d.x});
        if (maxX >= 0.999f) {
            baseTopTouchesCliffFoot = true;
            break;
        }
    }

    EXPECT_TRUE(baseTopTouchesCliffFoot)
        << "Base level top must not bevel away from the foot of a higher-level cliff";
}

TEST(LandscapePipelineTest, LowerLevelSurfaceFillsBeveledHigherLevelOuterCorner) {
    landscape_core::LandscapeLevelGrid grid;
    grid.width = 2;
    grid.height = 2;
    grid.levelCount = 2;
    grid.levelHeight = 1.0f;
    grid.cellLevels = {
        0, 0,
        0, 1,
    };
    grid.nodeLevels.assign(9, 0);
    grid.zones.assign(4, landscape_core::LandscapeZone::Lowland);

    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cornerBevel = 0.3f;
    meshSettings.rockEnabled = false;
    meshSettings.wallHorizontalSubdivisions = 4;
    meshSettings.wallVerticalSubdivisions = 3;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeLandscapeMesh(grid, meshSettings);

    auto isBasePointInsideHighCellCorner = [](const landscape_mesh::Vec3& point) {
        return point.y >= -0.0001f && point.y <= 0.0001f &&
            point.x >= 0.999f && point.x <= 1.451f &&
            point.z >= 0.999f && point.z <= 1.451f;
    };

    bool foundFootCap = false;
    for (const landscape_mesh::MeshQuad& quad : result.quads) {
        if (quad.cliffWall) {
            continue;
        }

        if (isBasePointInsideHighCellCorner(quad.a) &&
            isBasePointInsideHighCellCorner(quad.b) &&
            isBasePointInsideHighCellCorner(quad.c) &&
            isBasePointInsideHighCellCorner(quad.d)) {
            foundFootCap = true;
            break;
        }
    }

    EXPECT_TRUE(foundFootCap)
        << "Lower level surface must fill the triangular foot gap created by a beveled higher-level outer corner";
}

TEST(LandscapePipelineTest, SharedSolidMaskBuilderCreatesBevelCapsForCutout) {
    landscape_mesh::SolidMaskGrid mask;
    mask.width = 8;
    mask.height = 8;
    mask.solidCells.assign((std::size_t)mask.width * (std::size_t)mask.height, 0);
    mask.topCells.assign(mask.solidCells.size(), 0);

    for (int y = 1; y < 7; y++) {
        for (int x = 1; x < 7; x++) {
            const bool cutout = x >= 3 && x < 5 && y >= 3 && y < 5;
            const std::size_t index = (std::size_t)mask.cellIndex(x, y);
            mask.solidCells[index] = cutout ? 0 : 1;
            mask.topCells[index] = mask.solidCells[index];
        }
    }

    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cornerBevel = 0.3f;
    meshSettings.wallHorizontalSubdivisions = 5;
    meshSettings.wallVerticalSubdivisions = 6;

    landscape_mesh::SolidMeshBuildRequest request;
    request.mask = mask;
    request.baseHeight = 0.0f;
    request.topHeight = 2.5f;
    request.includeWalls = true;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeSolidMaskMesh(request, meshSettings);

    EXPECT_GT(result.stats.boundarySegmentCount, 0);
    EXPECT_GT(result.stats.beveledSegmentCount, result.stats.boundarySegmentCount);
    EXPECT_GT(result.stats.cornerCapCount, 0);
    EXPECT_GT(result.stats.cliffWallQuadCount, 0);
    EXPECT_EQ(result.normalOrientation.outwardFailCount, 0);
}

namespace {

float dot3(const landscape_mesh::Vec3& a, const landscape_mesh::Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

landscape_mesh::Vec3 normalizeHorizontal(const landscape_mesh::Vec3& value, const landscape_mesh::Vec3& fallback) {
    const float length = std::sqrt(value.x * value.x + value.z * value.z);
    if (length < 0.0001f) {
        return fallback;
    }
    return {value.x / length, 0.0f, value.z / length};
}

bool isCardinalOutwardHint(const landscape_mesh::Vec3& hint) {
    const float horizontalLength = std::sqrt(hint.x * hint.x + hint.z * hint.z);
    if (horizontalLength < 0.999f) {
        return false;
    }
    return std::abs(std::abs(hint.x) - 1.0f) < 0.01f || std::abs(std::abs(hint.z) - 1.0f) < 0.01f;
}

} // namespace

TEST(LandscapePipelineTest, LitWallNormalUsesOutwardHint) {
    landscape_mesh::MeshQuad synthetic;
    synthetic.cliffWall = true;
    synthetic.boundarySide = landscape_mesh::BoundarySide::Right;
    synthetic.outwardHint = {1.0f, 0.0f, 0.0f};
    const landscape_mesh::Vec3 syntheticLit = landscape_mesh::litWallNormal(synthetic);
    EXPECT_NEAR(syntheticLit.x, 1.0f, 1e-4f);
    EXPECT_NEAR(syntheticLit.y, 0.0f, 1e-4f);
    EXPECT_NEAR(syntheticLit.z, 0.0f, 1e-4f);

    synthetic.outwardHint = {0.0f, 0.0f, 0.0f};
    const landscape_mesh::Vec3 fallbackLit = landscape_mesh::litWallNormal(synthetic);
    EXPECT_GT(dot3(fallbackLit, landscape_mesh::Vec3{1.0f, 0.0f, 0.0f}), 0.99f);

    landscape_core::LandscapeBowlSettings settings;
    settings.gridWidth = 24;
    settings.gridHeight = 18;
    settings.heightLevels = 4;

    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(settings);
    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cornerBevel = 0.3f;
    meshSettings.wallHorizontalSubdivisions = 5;
    meshSettings.wallVerticalSubdivisions = 6;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeLandscapeMesh(grid, meshSettings);
    ASSERT_GT(result.stats.cliffWallQuadCount, 0);

    std::map<int, landscape_mesh::Vec3> cardinalReferenceBySide;
    for (const landscape_mesh::MeshQuad& quad : result.quads) {
        if (!quad.cliffWall) {
            continue;
        }

        const landscape_mesh::Vec3 lit = landscape_mesh::litWallNormal(quad);
        const landscape_mesh::Vec3 hint = normalizeHorizontal(
            quad.outwardHint,
            normalizeHorizontal(quad.normal, {0.0f, 0.0f, 1.0f}));

        EXPECT_GT(dot3(lit, hint), 0.99f) << "lit must align with outwardHint";
        EXPECT_NEAR(lit.y, 0.0f, 1e-3f);

        if (!isCardinalOutwardHint(quad.outwardHint)) {
            continue;
        }

        const int sideKey = static_cast<int>(quad.boundarySide);
        const auto existing = cardinalReferenceBySide.find(sideKey);
        if (existing == cardinalReferenceBySide.end()) {
            cardinalReferenceBySide.emplace(sideKey, lit);
            continue;
        }

        EXPECT_NEAR(existing->second.x, lit.x, 1e-4f);
        EXPECT_NEAR(existing->second.y, lit.y, 1e-4f);
        EXPECT_NEAR(existing->second.z, lit.z, 1e-4f);
    }

    EXPECT_GE(cardinalReferenceBySide.size(), 2u);
}

TEST(LandscapePipelineTest, SolidMaskBuilderOutwardNormalsPassWithoutRockDisplacement) {
    landscape_mesh::SolidMaskGrid mask;
    mask.width = 8;
    mask.height = 8;
    mask.solidCells.assign((std::size_t)mask.width * (std::size_t)mask.height, 0);
    mask.topCells.assign(mask.solidCells.size(), 0);

    for (int y = 1; y < 7; y++) {
        for (int x = 1; x < 7; x++) {
            const bool cutout = x >= 3 && x < 5 && y >= 3 && y < 5;
            const std::size_t index = (std::size_t)mask.cellIndex(x, y);
            mask.solidCells[index] = cutout ? 0 : 1;
            mask.topCells[index] = mask.solidCells[index];
        }
    }

    landscape_mesh::MeshBuildSettings meshSettings;
    meshSettings.cornerBevel = 0.3f;
    meshSettings.rockEnabled = false;
    meshSettings.wallHorizontalSubdivisions = 5;
    meshSettings.wallVerticalSubdivisions = 6;

    landscape_mesh::SolidMeshBuildRequest request;
    request.mask = mask;
    request.baseHeight = 0.0f;
    request.topHeight = 2.5f;
    request.includeWalls = true;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeSolidMaskMesh(request, meshSettings);

    EXPECT_EQ(result.normalOrientation.outwardFailCount, 0);
    EXPECT_EQ(result.normalOrientation.outwardWarnCount, 0);
}

TEST(SunShadowTest, LowerCellFallsIntoShadowFromHigherNeighborTowardSun) {
    const int width = 3;
    const int height = 3;
    std::vector<std::uint8_t> cellLevels((std::size_t)width * (std::size_t)height, 0);
    cellLevels[(std::size_t)1 * (std::size_t)width + 0] = 2;

    landscape_core::SunShadowSettings settings;
    settings.lightDirectionX = -1.0f;
    settings.lightDirectionY = 0.8f;
    settings.lightDirectionZ = 0.0f;

    const std::vector<float> field =
        landscape_core::computeSunShadowField(width, height, cellLevels, settings);

    ASSERT_EQ(field.size(), cellLevels.size());
    // Adjacent lit cell keeps most of the sunlight but may receive soft penumbra.
    EXPECT_GT(field[(std::size_t)2 * (std::size_t)width + 2], 0.82f);
    EXPECT_LT(field[(std::size_t)1 * (std::size_t)width + 2], 0.5f);
}

TEST(SunShadowTest, GeneratedBowlProducesVariedShadowField) {
    landscape_core::LandscapeBowlSettings settings;
    settings.gridWidth = 24;
    settings.gridHeight = 18;
    settings.heightLevels = 4;
    const landscape_core::LandscapeLevelGrid grid = landscape_core::generateLandscapeBowl(settings);

    landscape_core::SunShadowSettings shadowSettings;
    shadowSettings.lightDirectionX = -0.35f;
    shadowSettings.lightDirectionY = 0.82f;
    shadowSettings.lightDirectionZ = -0.45f;
    const std::vector<float> field = landscape_core::computeSunShadowField(grid, shadowSettings);

    ASSERT_FALSE(field.empty());
    float minShadow = 1.0f;
    float maxShadow = 0.0f;
    for (float value : field) {
        EXPECT_GE(value, 0.0f);
        EXPECT_LE(value, 1.0f);
        minShadow = std::min(minShadow, value);
        maxShadow = std::max(maxShadow, value);
    }
    EXPECT_LT(minShadow, 1.0f);
    EXPECT_GT(maxShadow, 0.0f);
}
