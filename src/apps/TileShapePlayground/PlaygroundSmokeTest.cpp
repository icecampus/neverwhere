#include "PlaygroundSmokeTest.h"

#include <cmath>
#include <cstring>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include "DiamondIso.h"
#include "FlatAtlasGenerator.h"
#include "LandBrush.h"
#include "RaisedGeometry.h"
#include "RockWalls.h"

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

    const auto fullMask = landscape_core::tileTypeToNodeMask(landscape_core::LandscapeTileType::Full);
    if (!fullMask[0] || !fullMask[1] || !fullMask[2] || !fullMask[3]) {
        spdlog::error("TEST FAIL TileShape: Full mask is not all-true");
        return false;
    }

    const FlatAtlasImage flat = generateFlatAtlas();
    if (flat.width != 256 || flat.height != 384 || flat.rgba.size() != 256u * 384u * 4u) {
        spdlog::error("TEST FAIL TileShape: flat atlas size {}x{} (bytes {})",
            flat.width,
            flat.height,
            flat.rgba.size());
        return false;
    }
    if (flatAtlasOpaquePixelCount(flat, 0) <= 0) {
        spdlog::error("TEST FAIL TileShape: Full tile (index 0) has no opaque pixels");
        return false;
    }
    if (flatAtlasOpaquePixelCount(flat, 22) != 0) {
        spdlog::error("TEST FAIL TileShape: unused slot 22 should be empty");
        return false;
    }
    if (flatAtlasOpaquePixelCount(flat, 8) <= 0) {
        spdlog::error("TEST FAIL TileShape: UpCorner tile (index 8) has no opaque pixels");
        return false;
    }

    // --- Raised (3D) contour geometry: axis-parallel segments meeting at the
    // cell center ("corner built from edge-parallel lines", not a diagonal).
    DiamondIso iso;
    const glm::ivec2 cell{2, 2};
    const glm::vec2 center = iso.mapToField(cell);
    const auto near2 = [](const glm::vec2& a, const glm::vec2& b) {
        return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f;
    };
    const auto axisParallelOk = [](const ContourSegment& seg) {
        const glm::vec2 dir = seg.center - seg.edgeMid;
        const glm::vec2 axisDir = (seg.axis == 0) ? glm::vec2(64.0f, 32.0f) : glm::vec2(-64.0f, 32.0f);
        const float cross = dir.x * axisDir.y - dir.y * axisDir.x;
        return std::abs(cross) < 1e-3f;
    };

    using T = landscape_core::LandscapeTileType;
    const auto fullSegs = cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::Full));
    if (!fullSegs.empty()) {
        spdlog::error("TEST FAIL TileShape: Full cell must have no contour segments");
        return false;
    }
    const auto emptySegs = cellContourSegments(iso, cell, {false, false, false, false});
    if (!emptySegs.empty()) {
        spdlog::error("TEST FAIL TileShape: empty cell must have no contour segments");
        return false;
    }

    const auto cornerSegs = cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::LeftCorner));
    if (cornerSegs.size() != 2) {
        spdlog::error("TEST FAIL TileShape: LeftCorner must have 2 contour segments, got {}", cornerSegs.size());
        return false;
    }
    for (const ContourSegment& seg : cornerSegs) {
        if (!near2(seg.center, center)) {
            spdlog::error("TEST FAIL TileShape: contour segment does not end at cell center");
            return false;
        }
        if (!axisParallelOk(seg)) {
            spdlog::error("TEST FAIL TileShape: contour segment not parallel to a grid axis");
            return false;
        }
    }
    if (near2(cornerSegs[0].edgeMid, cornerSegs[1].edgeMid)) {
        spdlog::error("TEST FAIL TileShape: corner segments must start at two distinct edge midpoints");
        return false;
    }

    const auto lineSegs = cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::RightUpLine));
    if (lineSegs.size() != 2) {
        spdlog::error("TEST FAIL TileShape: RightUpLine must have 2 contour segments, got {}", lineSegs.size());
        return false;
    }
    const auto saddleSegs =
        cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::LeftRightCorners));
    if (saddleSegs.size() != 4) {
        spdlog::error(
            "TEST FAIL TileShape: LeftRightCorners must have 4 contour segments, got {}",
            saddleSegs.size());
        return false;
    }

    // Layers are independent: painting raised nodes must not change flat cells.
    LandBrush flatLayer;
    LandBrush raisedLayer;
    flatLayer.reset(8, 8);
    raisedLayer.reset(8, 8);
    raisedLayer.setNode({4, 4}, true);
    if (flatLayer.cellTypeAt({4, 4}) != T::Unknown || raisedLayer.cellTypeAt({4, 4}) == T::Unknown) {
        spdlog::error("TEST FAIL TileShape: flat/raised layers are not independent");
        return false;
    }

    // --- Rock walls: boundary chains ---
    {
        LandBrush b;
        b.reset(8, 8);
        b.setNode({4, 4}, true);
        const auto chains = analyzeRockBoundaries(b);
        // The tuft boundary is the 1x1 square around the node: 8 segments
        // (2 per adjacent cell), 4 convex corners at cell centers; chain
        // passes straight (180 deg) through edge midpoints.
        if (chains.size() != 1 || !chains[0].closed || chains[0].segments != 8 ||
            chains[0].convexCorners != 4 || chains[0].concaveCorners != 0) {
            spdlog::error(
                "TEST FAIL TileShape: single node chain (n={}, closed={}, segs={}, convex={}, concave={})",
                chains.size(),
                chains.empty() ? -1 : (int)chains[0].closed,
                chains.empty() ? -1 : chains[0].segments,
                chains.empty() ? -1 : chains[0].convexCorners,
                chains.empty() ? -1 : chains[0].concaveCorners);
            return false;
        }
    }
    {
        LandBrush b;
        b.reset(8, 8);
        for (int y = 3; y <= 4; ++y) {
            for (int x = 3; x <= 4; ++x) {
                b.setNode({x, y}, true);
            }
        }
        const auto chains = analyzeRockBoundaries(b);
        if (chains.size() != 1 || !chains[0].closed || chains[0].convexCorners != 4 ||
            chains[0].concaveCorners != 0) {
            spdlog::error(
                "TEST FAIL TileShape: 2x2 block chain (n={}, closed={}, convex={}, concave={})",
                chains.size(),
                chains.empty() ? -1 : (int)chains[0].closed,
                chains.empty() ? -1 : chains[0].convexCorners,
                chains.empty() ? -1 : chains[0].concaveCorners);
            return false;
        }
    }
    {
        LandBrush b;
        b.reset(8, 8);
        b.setNode({3, 4}, true);
        b.setNode({4, 3}, true); // diagonal saddle at cell (3,3)
        const auto chains = analyzeRockBoundaries(b);
        int joins = 0;
        for (const RockChainInfo& c : chains) {
            joins += c.diagonalJoins;
        }
        if (joins < 1) {
            spdlog::error("TEST FAIL TileShape: saddle diagonal join not found");
            return false;
        }
    }
    {
        LandBrush b;
        b.reset(8, 8);
        b.setNode({4, 4}, true);
        const RockWallParams params;
        const RockWallBuild w1 = buildRockWalls(b, 32.0f, params);
        const RockWallBuild w2 = buildRockWalls(b, 32.0f, params);
        if (w1.verts.empty() || w1.quadCount == 0) {
            spdlog::error("TEST FAIL TileShape: rock walls are empty");
            return false;
        }
        if (w1.verts.size() != w2.verts.size() ||
            std::memcmp(w1.verts.data(), w2.verts.data(), w1.verts.size() * sizeof(ColorVertex)) != 0) {
            spdlog::error("TEST FAIL TileShape: rock walls are not deterministic");
            return false;
        }
        if (!(w1.maxAbsOffset > 0.0f && w1.maxAbsOffset <= params.amplitude + 1e-4f)) {
            spdlog::error("TEST FAIL TileShape: displacement clamp ({})", w1.maxAbsOffset);
            return false;
        }
        if (std::abs(rockWallOffset(params, 4.5f, 4.0f, 1.0f)) > 1e-6f ||
            std::abs(rockWallOffset(params, 4.5f, 4.0f, 0.0f)) > 1e-6f) {
            spdlog::error("TEST FAIL TileShape: seam envelope is not zero at edges");
            return false;
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + flat atlas generator + raised contour + rock walls");
    return true;
}
