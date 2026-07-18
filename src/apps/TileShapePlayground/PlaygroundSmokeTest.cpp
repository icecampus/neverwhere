#include "PlaygroundSmokeTest.h"

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "LandBrush.h"

bool runTileShapeSmokeTest() {
    LandBrush brush;
    brush.reset(8, 8);

    const glm::ivec2 nodeA{3, 3};
    const glm::ivec2 nodeB{4, 3};

    if (!brush.setNode(nodeA, true) || brush.onNodeCount() != 1) {
        spdlog::error("TEST FAIL TileShape: paint first node");
        return false;
    }

    for (const glm::ivec2 cell : brush.affectedCells(nodeA)) {
        if (cell.x < 0 || cell.y < 0 || cell.x >= brush.width() || cell.y >= brush.height()) {
            continue;
        }
        const auto type = brush.cellTypeAt(cell);
        if (type == landscape_core::LandscapeTileType::Unknown ||
            !landscape_core::tileTypeHasSurface(type)) {
            spdlog::error("TEST FAIL TileShape: expected surface at cell ({},{}) after single node",
                cell.x,
                cell.y);
            return false;
        }
    }

    if (!brush.setNode(nodeB, true)) {
        spdlog::error("TEST FAIL TileShape: paint second node");
        return false;
    }

    // Same contract as Landscape3dPlayground smoke: adjacent high nodes along +X
    // produce RightUpLine on cell (3,3).
    if (brush.cellTypeAt({3, 3}) != landscape_core::LandscapeTileType::RightUpLine) {
        spdlog::error(
            "TEST FAIL TileShape: expected RightUpLine at (3,3), got {}",
            landscape_core::tileTypeName(brush.cellTypeAt({3, 3})));
        return false;
    }

    if (LandBrush::atlasIndexByType(landscape_core::LandscapeTileType::Full) != 0 ||
        LandBrush::atlasIndexByType(landscape_core::LandscapeTileType::RightUpLine) != 12 ||
        LandBrush::atlasIndexByType(landscape_core::LandscapeTileType::LeftCorner) != 11) {
        spdlog::error("TEST FAIL TileShape: atlas index mapping mismatch");
        return false;
    }

    if (!brush.setNode(nodeA, false) || !brush.setNode(nodeB, false)) {
        spdlog::error("TEST FAIL TileShape: erase nodes");
        return false;
    }
    if (brush.onNodeCount() != 0 || brush.cellTypeCount(landscape_core::LandscapeTileType::Unknown) != 64) {
        spdlog::error("TEST FAIL TileShape: map not cleared after erase");
        return false;
    }

    // Four corners of one cell -> Full
    brush.setNode({2, 3}, true); // Left of cell (2,2)
    brush.setNode({2, 2}, true); // Up
    brush.setNode({3, 2}, true); // Right
    brush.setNode({3, 3}, true); // Down
    if (brush.cellTypeAt({2, 2}) != landscape_core::LandscapeTileType::Full) {
        spdlog::error(
            "TEST FAIL TileShape: expected Full at (2,2), got {}",
            landscape_core::tileTypeName(brush.cellTypeAt({2, 2})));
        return false;
    }

    spdlog::info("TEST PASS TileShape: LandBrush + atlas mapping");
    return true;
}
