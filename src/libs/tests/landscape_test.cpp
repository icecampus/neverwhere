#include <gtest/gtest.h>
#include <QUuid>
#include "topology/staggered_tiled_landscape.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"
#include "assets_library/assets/slice_asset.h"
#include "topology/staggered_isometry.h"

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
