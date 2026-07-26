#include "PlaygroundSmokeTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <highground_core/cliff_field.h>
#include <highground_core/highground.h>
#include <highground_core/inspect.h>
#include <highground_core/surface_nets.h>
#include <landscape_mesh/landscape_mesh.h>
#include <topology_core/diamond_isometry.h>

#include "FlatAtlasGenerator.h"
#include "LandBrush.h"

namespace {

// Collect the "on" nodes of a brush into a highground grid.
highground::Grid brushGrid(const LandBrush& brush) {
    std::vector<glm::ivec2> onNodes;
    for (int y = 0; y < brush.height(); ++y) {
        for (int x = 0; x < brush.width(); ++x) {
            const auto mask = brush.nodeMaskAt({x, y});
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes({x, y});
            for (int i = 0; i < 4; ++i) {
                if (mask[i]) {
                    onNodes.push_back(corners[i]);
                }
            }
        }
    }
    return highground::makeGrid(onNodes.data(), onNodes.size());
}

std::size_t primCount(const highground::Mesh& mesh, highground::Material material) {
    std::size_t n = 0;
    for (const highground::Primitive& prim : mesh.primitives) {
        if (prim.material == material) {
            ++n;
        }
    }
    return n;
}

} // namespace

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

    // --- Raised (3D) contour geometry (highground_core): axis-parallel
    // segments meeting at the cell center ("corner built from edge-parallel
    // lines", not a diagonal).
    topology_core::DiamondIsometry iso;
    const glm::ivec2 cell{2, 2};
    const glm::vec2 center = iso.mapToField(cell);
    const auto near2 = [](const glm::vec2& a, const glm::vec2& b) {
        return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f;
    };
    const auto axisParallelOk = [](const highground::ContourSegment& seg) {
        const glm::vec2 dir = seg.center - seg.edgeMid;
        const glm::vec2 axisDir = (seg.axis == 0) ? glm::vec2(64.0f, 32.0f) : glm::vec2(-64.0f, 32.0f);
        const float cross = dir.x * axisDir.y - dir.y * axisDir.x;
        return std::abs(cross) < 1e-3f;
    };

    using T = landscape_core::LandscapeTileType;
    const auto fullSegs = highground::cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::Full));
    if (!fullSegs.empty()) {
        spdlog::error("TEST FAIL TileShape: Full cell must have no contour segments");
        return false;
    }
    const auto emptySegs = highground::cellContourSegments(iso, cell, {false, false, false, false});
    if (!emptySegs.empty()) {
        spdlog::error("TEST FAIL TileShape: empty cell must have no contour segments");
        return false;
    }

    const auto cornerSegs =
        highground::cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::LeftCorner));
    if (cornerSegs.size() != 2) {
        spdlog::error("TEST FAIL TileShape: LeftCorner must have 2 contour segments, got {}", cornerSegs.size());
        return false;
    }
    for (const highground::ContourSegment& seg : cornerSegs) {
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

    const auto lineSegs =
        highground::cellContourSegments(iso, cell, landscape_core::tileTypeToNodeMask(T::RightUpLine));
    if (lineSegs.size() != 2) {
        spdlog::error("TEST FAIL TileShape: RightUpLine must have 2 contour segments, got {}", lineSegs.size());
        return false;
    }
    const auto saddleSegs = highground::cellContourSegments(
        iso,
        cell,
        landscape_core::tileTypeToNodeMask(T::LeftRightCorners));
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

    // --- highground_core::generate: content, determinism, sort order ---
    {
        LandBrush b;
        b.reset(8, 8);
        b.setNode({4, 4}, true);
        const highground::Params params;
        const highground::Mesh m1 = highground::generate(brushGrid(b), params);
        const highground::Mesh m2 = highground::generate(brushGrid(b), params);
        if (m1.vertices.empty() || primCount(m1, highground::Material::Wall) == 0 ||
            primCount(m1, highground::Material::Top) == 0) {
            spdlog::error("TEST FAIL TileShape: single node mesh is empty or incomplete");
            return false;
        }
        if (m1.vertices.size() != m2.vertices.size() || m1.primitives.size() != m2.primitives.size() ||
            std::memcmp(
                m1.vertices.data(),
                m2.vertices.data(),
                m1.vertices.size() * sizeof(highground::Vertex)) != 0) {
            spdlog::error("TEST FAIL TileShape: highground generation is not deterministic");
            return false;
        }
        for (std::size_t i = 1; i < m1.primitives.size(); ++i) {
            if (m1.primitives[i].depth < m1.primitives[i - 1].depth) {
                spdlog::error("TEST FAIL TileShape: primitives are not sorted back-to-front");
                return false;
            }
        }
    }
    {
        // Diagonal saddle + a diagonal-neck bridge: contour loops and the top
        // triangulation must hold (regression for the "square lids" bug).
        LandBrush b;
        b.reset(12, 12);
        b.setNode({3, 4}, true);
        b.setNode({4, 3}, true);
        b.setNode({7, 7}, true);
        b.setNode({8, 7}, true);
        b.setNode({7, 8}, true);
        b.setNode({8, 8}, true);
        b.setNode({9, 9}, true);
        const highground::Mesh m = highground::generate(brushGrid(b), highground::Params{});
        if (m.vertices.empty() || primCount(m, highground::Material::Top) == 0) {
            spdlog::error("TEST FAIL TileShape: saddle/bridge shapes produce no geometry");
            return false;
        }
    }

    // --- Cliff layer pipeline (cliff::CliffField + surface nets): brush
    // nodes -> bbox grid (1-cell margin) -> sampled field -> watertight mesh
    // with the groove attribute. Coarser cellSize keeps the smoke fast.
    {
        LandBrush b;
        b.reset(24, 24);
        for (int y = 14; y <= 17; ++y) {
            for (int x = 14; x <= 17; ++x) {
                b.setNode({x, y}, true);
            }
        }
        std::vector<glm::ivec2> onNodes;
        for (int y = 0; y <= b.height(); ++y) {
            for (int x = 0; x <= b.width(); ++x) {
                if (b.nodeIsOn({x, y})) {
                    onNodes.emplace_back(x, y);
                }
            }
        }
        if (onNodes.empty()) {
            spdlog::error("TEST FAIL TileShape: cliff brush has no on nodes");
            return false;
        }
        int minX = onNodes[0].x, minY = onNodes[0].y;
        int maxX = onNodes[0].x, maxY = onNodes[0].y;
        for (const glm::ivec2& n : onNodes) {
            minX = std::min(minX, n.x);
            minY = std::min(minY, n.y);
            maxX = std::max(maxX, n.x);
            maxY = std::max(maxY, n.y);
        }
        minX -= 1;
        minY -= 1;
        maxX += 1;
        maxY += 1;
        const int nodesX = maxX - minX + 1;
        const int nodesY = maxY - minY + 1;
        std::vector<std::uint8_t> nodes(static_cast<std::size_t>(nodesX) * nodesY, 0);
        for (const glm::ivec2& n : onNodes) {
            nodes[static_cast<std::size_t>(n.y - minY) * nodesX + (n.x - minX)] = 1;
        }

        cliff::FieldParams fp;
        fp.cellSize = 0.09f; // ~8x fewer voxels than the UI default: smoke speed
        cliff::CliffField field(fp, nodes.data(), nodesX, nodesY);
        std::vector<float> samples;
        field.sample(samples);
        cliff::RegularizeStats regStats;
        cliff::regularizeSigns(field, samples, &regStats);
        const cliff::Mesh mesh = cliff::extractSurfaceNets(field, samples, nullptr);
        if (regStats.remaining != 0) {
            spdlog::error("TEST FAIL TileShape: cliff sign regularization left {} saddles",
                regStats.remaining);
            return false;
        }
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            spdlog::error("TEST FAIL TileShape: cliff mesh is empty");
            return false;
        }
        const cliff::WatertightReport report = cliff::checkWatertight(mesh);
        if (!report.ok()) {
            spdlog::error("TEST FAIL TileShape: cliff mesh not watertight ({} bad of {} edges)",
                report.badEdges, report.undirectedEdges);
            return false;
        }
        float gMax = -1e9f;
        for (const cliff::MeshVertex& v : mesh.vertices) {
            gMax = std::max(gMax, v.groove);
        }
        if (gMax <= 0.02f) {
            spdlog::error("TEST FAIL TileShape: cliff groove attribute range max {:.4f} — no carve",
                gMax);
            return false;
        }
    }

    // --- Wall-mesh ("Cyclopean 3D") layer pipeline (landscape_mesh): brush
    // nodes -> dense node grid -> cell solid-mask -> single-level Cyclopean
    // plateau with valid seams (the same path the renderer's cache drives).
    {
        LandBrush b;
        b.reset(24, 24);
        for (int y = 10; y <= 13; ++y) {
            for (int x = 10; x <= 13; ++x) {
                b.setNode({x, y}, true);
            }
        }
        const int nodesX = b.width() + 1;
        const int nodesY = b.height() + 1;
        std::vector<std::uint8_t> nodes(static_cast<std::size_t>(nodesX) * nodesY, 0);
        for (int y = 0; y < nodesY; ++y) {
            for (int x = 0; x < nodesX; ++x) {
                if (b.nodeIsOn({x, y})) {
                    nodes[static_cast<std::size_t>(y) * nodesX + x] = 1;
                }
            }
        }

        landscape_mesh::MeshBuildSettings settings;
        settings.wallStyle = landscape_mesh::WallStyleId::Cyclopean;
        settings.wallHorizontalSubdivisions = 16;
        settings.wallVerticalSubdivisions = 16;
        settings.levelHeight = 3.0f;

        landscape_mesh::SolidMeshBuildRequest request;
        request.mask = landscape_mesh::solidMaskFromNodes(nodes.data(), nodesX, nodesY);
        request.baseHeight = 0.0f;
        request.topHeight = 3.0f;
        request.level = 1;
        request.maxLevel = 1;
        request.includeWalls = true;
        request.fadeWallDisplacementAtBottom = false;

        const landscape_mesh::CompositionResult result =
            landscape_mesh::composeSolidMaskMesh(request, settings);
        if (result.quads.empty() || result.stats.topQuadCount <= 0 || result.stats.cliffWallQuadCount <= 0) {
            spdlog::error("TEST FAIL TileShape: wall-mesh plateau is empty or incomplete");
            return false;
        }
        if (!result.seams.passed) {
            spdlog::error("TEST FAIL TileShape: wall-mesh seam validation ({} of {} edges)",
                result.seams.mismatches,
                result.seams.checkedEdges);
            return false;
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + flat atlas generator + highground_core generation + cliff field pipeline + landscape_mesh wall-mesh pipeline");
    return true;
}
