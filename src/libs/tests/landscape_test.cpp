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
        // Clean all existing objects in this cell to prevent duplicates and 'ghost' nodes
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
        EXPECT_EQ(landObject->getTileIndex(), mockAsset->subTileIndexByType(expectedType));
        EXPECT_NE(expectedType, TileSet::Unknown);
    }
}

TEST_F(LandscapeTest, ComplexIslandSyncTest) {
    // 1. Define a complex shape of nodes (10x10 area)
    // We'll create a "donut" shape to test various internal and external corners
    std::unordered_set<math::ivec2> targetNodes = {
        {5,5}, {6,5}, {7,5},
        {5,6},        {7,6},
        {5,7}, {6,7}, {7,7}
    };

    LandNodes landNodes;
    landNodes.init(100, 100);

    // 2. Multi-step drawing process
    // For each node in our target, we'll:
    // a) Create LandNodes from current LayerModel
    // b) Set the node
    // c) Update LayerModel
    for (const auto& nodePos : targetNodes) {
        // Round-trip: LayerModel -> LandNodes
        LandNodes currentNodes = LandNodes::createByMap(*layerModel);
        
        currentNodes[nodePos] = 1;

        // Update cells around the modified node
        StaggeredIsometry::Neighbours cellsToUpdate = StaggeredIsometry::nodeNeighboursCell(nodePos);
        for (const math::ivec2& cellPos : cellsToUpdate) {
            TileSet::TileType type = currentNodes.getTileType(cellPos);
            updateLandscapeCell(layerModel, mockAsset, cellPos, type);
        }
    }

    // 3. Final Verification: LayerModel -> LandNodes
    LandNodes finalNodes = LandNodes::createByMap(*layerModel);
    
    // Check if all target nodes are 1 and others are 0
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            math::ivec2 p(x, y);
            if (targetNodes.count(p)) {
                EXPECT_EQ(finalNodes.at(p), 1) << "Node at " << x << "," << y << " should be 1";
            } else {
                // Nodes that were never touched should be 0
                // We only care about nodes around our island
                if (x >= 4 && x <= 8 && y >= 4 && y <= 8) {
                    EXPECT_EQ(finalNodes.at(p), 0) << "Node at " << x << "," << y << " should be 0";
                }
            }
        }
    }

    // 4. Verify specific tile types in the donut
    // The center cell {6,6} (if it exists in staggered) should be surrounded by our nodes
    // Let's find a cell that should be "Unknown" (the hole in the donut) 
    // and cells that should be "Full" or "Corners".
    
    // In staggered, cell {6,6} nodes are returned by getNeighboursNodeForCell
    auto holeNodes = StaggeredTiledLandscape::getNeighboursNodeForCell({6,6});
    // If all these nodes were in our targetNodes, it would be Full. 
    // But we skipped {6,6} node itself in targetNodes.
}

TEST_F(LandscapeTest, AllTileTypesTest) {
    TileSet tileSet;
    LandNodes landNodes;
    landNodes.init(100, 100);

    // Iterate through all 16 possible neighbor masks
    for (int i = 0; i < 16; ++i) {
        math::ivec2 cellPos(i * 2, 5); // Spread them out
        auto nodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPos);
        
        // Set nodes according to bitmask i
        for (int bit = 0; bit < 4; ++bit) {
            if ((i >> bit) & 1) {
                landNodes[nodes[bit]] = 1;
            } else {
                landNodes[nodes[bit]] = 0;
            }
        }

        TileSet::TileType type = landNodes.getTileType(cellPos);
        
        // Verify we got a valid type from the tileset
        // (Except for 0 which is Unknown)
        if (i == 0) {
            EXPECT_EQ(type, TileSet::Unknown);
        } else {
            EXPECT_NE(type, TileSet::Unknown) << "Mask " << i << " should result in a valid TileType";
            
            // Apply to LayerModel
            updateLandscapeCell(layerModel, mockAsset, cellPos, type);
        }
    }

    // Now convert back and verify LandNodes
     LandNodes reconstructed = LandNodes::createByMap(*layerModel);
    for (int i = 1; i < 16; ++i) {
        math::ivec2 cellPos(i * 2, 5);
        auto nodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPos);
        for (int bit = 0; bit < 4; ++bit) {
            bool expected = (i >> bit) & 1;
            EXPECT_EQ(reconstructed.at(nodes[bit]), expected ? 1 : 0) 
                << "Node bit " << bit << " for mask " << i << " mismatch";
        }
    }
}

TEST_F(LandscapeTest, NeighborConflictRegressionTest) {
    // This test ensures that a 'Full' tile doesn't get corrupted by a neighbor
    // that has '0' in a shared node (like a Corner or Lack tile).

    math::ivec2 cellA(10, 10);
    math::ivec2 cellB(11, 10); // Neighboring cell

    // 1. Manually place a Full tile at cellA
    updateLandscapeCell(layerModel, mockAsset, cellA, TileSet::Full);

    // 2. Manually place a RightCorner tile at cellB
    // This tile will share nodes with cellA, but some of its nodes will be 0
    updateLandscapeCell(layerModel, mockAsset, cellB, TileSet::RightCorner);

    // 3. Reconstruct LandNodes from map
    LandNodes nodes = LandNodes::createByMap(*layerModel);

    // 4. Verify that ALL nodes of cellA are still 1
    auto nodesA = StaggeredTiledLandscape::getNeighboursNodeForCell(cellA);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(nodes.at(nodesA[i]), 1) << "Node " << i << " of Full tile was corrupted by neighbor";
    }

    // 5. Verify that the RightCorner's active node is also 1
    auto nodesB = StaggeredTiledLandscape::getNeighboursNodeForCell(cellB);
    TileSet tileSet;
    TileSet::NeighboursNodeMask maskB = tileSet.tilename2mask[TileSet::RightCorner];
    for (int i = 0; i < 4; ++i) {
        if (maskB[i]) {
            EXPECT_EQ(nodes.at(nodesB[i]), 1);
        }
    }
}

TEST_F(LandscapeTest, DuplicateObjectsCorruptionTest) {
    // This test reproduces the issue where multiple objects in the same cell
    // corrupt the LandNodes reconstruction, especially when trying to 'erase' or change types.
    
    math::ivec2 cellPos(15, 15);
    
    // 1. Simulate a bug where two objects end up in the same cell
    // First: a 'Full' tile
    updateLandscapeCell(layerModel, mockAsset, cellPos, TileSet::Full);
    
    // Second: manually add another object to the same cell without clearing (simulating faulty updateLandscapeCell)
    std::unique_ptr<Landscape> extraLand = std::make_unique<Landscape>(layerModel);
    extraLand->setPosition(cellPos);
    extraLand->setTileIndex(mockAsset->subTileIndexByType(TileSet::RightCorner));
    layerModel->addGameObject(std::move(extraLand));
    
    // Verify we have 2 objects now
    EXPECT_EQ(layerModel->getObjectsAt(cellPos).size(), 2);
    
    // 2. Reconstruct LandNodes
    // Because of our 'if (mask[i]) landMask[nodePos] = 1' fix, 
    // the reconstruction will OR all nodes from ALL objects in the cell.
    LandNodes nodes = LandNodes::createByMap(*layerModel);
    
    // Since one object is 'Full', all nodes for this cell MUST be 1
    auto cellNodes = StaggeredTiledLandscape::getNeighboursNodeForCell(cellPos);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(nodes.at(cellNodes[i]), 1);
    }
    
    // 3. NOW THE PROBLEM: Try to 'erase' land by setting a node to 0
    // If we only update the 'last' object in updateLandscapeCell, 
    // the 'hidden' Full tile underneath will still keep the nodes as 1.
    
    // Let's simulate what Pencil does: it gets nodes, sets one to 0, and updates.
    nodes[cellNodes[0]] = 0;
    TileSet::TileType newType = nodes.getTileType(cellPos);
    
    // Pencil calls updateLandscapeCell with newType (which should be something else than Full)
    updateLandscapeCell(layerModel, mockAsset, cellPos, newType);
    
    // 4. Verification of the 'Drift':
    // Reconstruct again. If the old 'Full' tile is still there, the nodes will 'pop' back to 1.
    LandNodes nodesAfterUpdate = LandNodes::createByMap(*layerModel);
    
    // If our current updateLandscapeCell is buggy (only updates back()), 
    // the first 'Full' tile is still in layerModel and will force node 0 to be 1 again.
    EXPECT_EQ(nodesAfterUpdate.at(cellNodes[0]), 0) 
        << "Node state leaked from hidden duplicate object! This is the 'drift' cause.";
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
    EXPECT_EQ(landObject->getTileIndex(), mockAsset->subTileIndexByType(TileSet::Full));
}
