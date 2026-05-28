#include <gtest/gtest.h>
#include <algorithm>
#include <QUuid>
#include "topology/staggered_tiled_landscape.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"
#include "assets_library/assets/slice_asset.h"
#include "topology/staggered_isometry.h"
#include <landscape_core/landscape_logic.h>
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
}
