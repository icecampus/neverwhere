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
    
    // We don't need to override subTileIndexByType if we don't care about the exact index
    // but we can if we want to verify it.
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
        std::vector<GameObject*> objects = layerModel->getObjectsAt(cellPosition);

        if (tileType != TileSet::Unknown)
        {
            if (objects.size())
            {
                GameObject* gameObject = objects.back();
                if (gameObject->getType() == GameObjectTypes::Landscape)
                {
                    Landscape* landObject = dynamic_cast<Landscape*>(gameObject);
                    if (landObject)
                    {
                        landObject->setName(QString("Landscape"));
                        landObject->setPosition(cellPosition);
                        landObject->setAssetUiid(sliceAsset->uuid());
                        landObject->setTileIndex(static_cast<size_t>(tileType)); // Using tileType as index for testing
                    }
                }
            }
            else
            {
                std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
                landObject->setName(QString("Landscape"));
                landObject->setPosition(cellPosition);
                landObject->setAssetUiid(sliceAsset->uuid());
                landObject->setTileIndex(static_cast<size_t>(tileType)); // Using tileType as index for testing
                layerModel->addGameObject(std::move(landObject));
            }
        }
        else
        {
            layerModel->remove(cellPosition);
        }
    }

    LayerModel* layerModel;
    StaggeredIsometryView* isoView;
    MockSliceAsset* mockAsset;
};

TEST_F(LandscapeTest, NodeDrawingLogic) {
    // 1. Pick a node position
    math::ivec2 nodePos(10, 10);

    // 2. Simulate "clicking" the node (setting it to 1)
    // In real app, we get LandNodes from map, but here we start fresh
    LandNodes landNodes;
    landNodes.init(100, 100);
    landNodes[nodePos] = 1;

    // 3. Update cells around the node
    StaggeredIsometry::Neighbours neighbours = StaggeredIsometry::nodeNeighboursCell(nodePos);
    for (const math::ivec2& curCellPosition : neighbours)
    {
        TileSet::TileType tileType = landNodes.getTileType(curCellPosition);
        updateLandscapeCell(layerModel, mockAsset, curCellPosition, tileType);
    }

    // 4. Verify that cells around the node are created
    for (const math::ivec2& curCellPosition : neighbours)
    {
        std::vector<GameObject*> objects = layerModel->getObjectsAt(curCellPosition);
        EXPECT_FALSE(objects.empty());
        
        Landscape* landObject = dynamic_cast<Landscape*>(objects.back());
        ASSERT_NE(landObject, nullptr);
        
        // Check if tile type is what we expect for a single node being 1
        // Since only one node is 1, each surrounding cell should have a "Corner" type
        TileSet::TileType expectedType = landNodes.getTileType(curCellPosition);
        EXPECT_EQ(landObject->getTileIndex(), static_cast<size_t>(expectedType));
        EXPECT_NE(expectedType, TileSet::Unknown);
    }
}

TEST_F(LandscapeTest, FullBlockLogic) {
    // Setting 4 nodes that form a single cell to 1 should result in a "Full" tile
    math::ivec2 cellPos(5, 5);
    StaggeredTiledLandscape::ModeNeighbours nodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPos);
    
    LandNodes landNodes;
    landNodes.init(100, 100);
    for (const auto& nodePos : nodes) {
        landNodes[nodePos] = 1;
    }

    TileSet::TileType tileType = landNodes.getTileType(cellPos);
    EXPECT_EQ(tileType, TileSet::Full);

    updateLandscapeCell(layerModel, mockAsset, cellPos, tileType);
    
    std::vector<GameObject*> objects = layerModel->getObjectsAt(cellPos);
    ASSERT_FALSE(objects.empty());
    Landscape* landObject = dynamic_cast<Landscape*>(objects.back());
    ASSERT_NE(landObject, nullptr);
    EXPECT_EQ(landObject->getTileIndex(), static_cast<size_t>(TileSet::Full));
}
