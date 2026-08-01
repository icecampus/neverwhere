// landscape_mesh::solidMaskFromNodes — vertex-node grid -> cell solid-mask,
// plus the single-level composeSolidMaskMesh pipeline it feeds (the
// SDFGeneratedLandscape "Cyclopean 3D" layer path).
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <landscape_mesh/landscape_mesh.h>

namespace {

std::vector<std::uint8_t> makeNodes(int nodesX, int nodesY) {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(nodesX) * nodesY, 0);
}

void setNode(std::vector<std::uint8_t>& nodes, int nodesX, int x, int y) {
    nodes[static_cast<std::size_t>(y) * nodesX + x] = 1;
}

int solidCellCount(const landscape_mesh::SolidMaskGrid& mask) {
    int count = 0;
    for (std::uint8_t cell : mask.solidCells) {
        count += cell != 0 ? 1 : 0;
    }
    return count;
}

} // namespace

TEST(SolidMaskFromNodes, DegenerateInputYieldsEmptyMask) {
    EXPECT_TRUE(landscape_mesh::solidMaskFromNodes(nullptr, 4, 4).empty());
    std::vector<std::uint8_t> nodes(1, 1);
    EXPECT_TRUE(landscape_mesh::solidMaskFromNodes(nodes.data(), 1, 1).empty());
    EXPECT_TRUE(landscape_mesh::solidMaskFromNodes(nodes.data(), 0, 0).empty());
    EXPECT_TRUE(landscape_mesh::solidMaskFromNodes(nodes.data(), 1, 8).empty());
}

TEST(SolidMaskFromNodes, SingleCornerNodeSolidifiesOneCell) {
    auto nodes = makeNodes(4, 4);
    setNode(nodes, 4, 0, 0);
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 4, 4);
    ASSERT_EQ(mask.width, 3);
    ASSERT_EQ(mask.height, 3);
    EXPECT_EQ(solidCellCount(mask), 1);
    EXPECT_TRUE(mask.isSolid(0, 0));
}

TEST(SolidMaskFromNodes, SingleInteriorNodeSolidifiesFourCells) {
    auto nodes = makeNodes(4, 4);
    setNode(nodes, 4, 1, 1);
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 4, 4);
    EXPECT_EQ(solidCellCount(mask), 4);
    EXPECT_TRUE(mask.isSolid(0, 0));
    EXPECT_TRUE(mask.isSolid(1, 0));
    EXPECT_TRUE(mask.isSolid(0, 1));
    EXPECT_TRUE(mask.isSolid(1, 1));
    EXPECT_FALSE(mask.isSolid(2, 2));
}

TEST(SolidMaskFromNodes, TwoByTwoNodeBlockSolidifiesOneCell) {
    // A 2x2 node grid has exactly one cell; all four corner nodes on -> solid.
    auto nodes = makeNodes(2, 2);
    setNode(nodes, 2, 0, 0);
    setNode(nodes, 2, 1, 0);
    setNode(nodes, 2, 0, 1);
    setNode(nodes, 2, 1, 1);
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 2, 2);
    ASSERT_EQ(mask.width, 1);
    ASSERT_EQ(mask.height, 1);
    EXPECT_EQ(solidCellCount(mask), 1);
    EXPECT_TRUE(mask.isSolid(0, 0));
}

TEST(SolidMaskFromNodes, TwoByTwoNodeBlockInsideGridSolidifiesThreeByThree) {
    auto nodes = makeNodes(5, 5);
    setNode(nodes, 5, 2, 2);
    setNode(nodes, 5, 3, 2);
    setNode(nodes, 5, 2, 3);
    setNode(nodes, 5, 3, 3);
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 5, 5);
    EXPECT_EQ(solidCellCount(mask), 9);
    EXPECT_TRUE(mask.isSolid(1, 1));
    EXPECT_TRUE(mask.isSolid(3, 3));
    EXPECT_FALSE(mask.isSolid(0, 0));
}

TEST(SolidMaskFromNodes, FullNodeGridSolidifiesAllCells) {
    auto nodes = makeNodes(5, 5);
    for (std::uint8_t& node : nodes) {
        node = 1;
    }
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 5, 5);
    EXPECT_EQ(solidCellCount(mask), 16);
    EXPECT_EQ(mask.topCells, mask.solidCells);
    ASSERT_EQ(mask.zones.size(), mask.solidCells.size());
    EXPECT_EQ(mask.zoneAt(0, 0), landscape_core::LandscapeZone::Lowland);
}

TEST(SolidMaskFromNodes, CyclopeanPipelineProducesSeamedPlateau) {
    auto nodes = makeNodes(12, 12);
    for (int y = 3; y <= 8; ++y) {
        for (int x = 3; x <= 8; ++x) {
            setNode(nodes, 12, x, y);
        }
    }
    const landscape_mesh::SolidMaskGrid mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 12, 12);
    ASSERT_FALSE(mask.empty());
    ASSERT_GT(solidCellCount(mask), 0);

    landscape_mesh::MeshBuildSettings settings;
    settings.cellSize = 1.0f;
    settings.wallStyle = landscape_mesh::WallStyleId::Cyclopean;
    settings.wallHorizontalSubdivisions = 16;
    settings.wallVerticalSubdivisions = 16;

    landscape_mesh::SolidMeshBuildRequest request;
    request.mask = mask;
    request.baseHeight = 0.0f;
    request.topHeight = 3.0f;
    request.level = 1;
    request.maxLevel = 1;
    request.includeWalls = true;
    request.fadeWallDisplacementAtBottom = false;

    const landscape_mesh::CompositionResult result = landscape_mesh::composeSolidMaskMesh(request, settings);
    EXPECT_FALSE(result.quads.empty());
    EXPECT_GT(result.stats.topQuadCount, 0);
    EXPECT_GT(result.stats.cliffWallQuadCount, 0);
    EXPECT_TRUE(result.seams.passed)
        << "seam mismatches: " << result.seams.mismatches << " of " << result.seams.checkedEdges;
}
