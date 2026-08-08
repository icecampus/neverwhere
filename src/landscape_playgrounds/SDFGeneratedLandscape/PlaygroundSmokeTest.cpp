#include "PlaygroundSmokeTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>
#include <highground_core/tech_field.h>
#include <stone_gen/stone_field.h>
#include <topology_core/diamond_isometry.h>

#include "FlatAtlasGenerator.h"
#include "LandBrush.h"
#include "TechOutlineField.h"

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

    // --- Tagged painting (multi-texture layer): per-node tags, displacement,
    // idempotence, majority cell resolution. Cell (2,2) corners in vote
    // order: Left (2,3), Up (2,2), Right (3,2), Down (3,3).
    {
        LandBrush b;
        b.reset(8, 8);
        const std::uint8_t tagA = 1; // raw tags (tiling slot + 1 by convention)
        const std::uint8_t tagB = 3;

        b.setNode({2, 2}, true, tagA); // Up
        if (b.cellTagAt({2, 2}) != tagA || b.nodeTag({2, 2}) != tagA) {
            spdlog::error("TEST FAIL TileShape: single tagged node, cell tag {}",
                b.cellTagAt({2, 2}));
            return false;
        }

        b.setNode({3, 3}, true, tagB); // Down: 1xA vs 1xB tie -> corner order wins (Up)
        if (b.cellTagAt({2, 2}) != tagA) {
            spdlog::error("TEST FAIL TileShape: tag tie must resolve in corner order, got {}",
                b.cellTagAt({2, 2}));
            return false;
        }

        b.setNode({2, 3}, true, tagB); // Left: 2xB vs 1xA -> majority B
        if (b.cellTagAt({2, 2}) != tagB) {
            spdlog::error("TEST FAIL TileShape: majority tag, got {}", b.cellTagAt({2, 2}));
            return false;
        }

        // Repainting an on-node with another tag displaces it (a real change,
        // version bumps) even though the on-state stays the same.
        const std::uint64_t v0 = b.version();
        if (!b.setNode({2, 3}, true, tagA) || b.cellTagAt({2, 2}) != tagA || b.version() <= v0) {
            spdlog::error("TEST FAIL TileShape: tag displacement on a painted node");
            return false;
        }
        // Same state + same tag is a no-op.
        if (b.setNode({2, 3}, true, tagA)) {
            spdlog::error("TEST FAIL TileShape: idempotent tagged paint reported a change");
            return false;
        }
        // Erasing clears the tag; a plain untagged paint resets it to 0.
        if (!b.setNode({3, 3}, false) || b.nodeTag({3, 3}) != 0) {
            spdlog::error("TEST FAIL TileShape: erase must clear the node tag");
            return false;
        }
        b.setNode({3, 3}, true, tagB);
        if (!b.setNode({3, 3}, true) || b.nodeTag({3, 3}) != 0) {
            spdlog::error("TEST FAIL TileShape: untagged repaint must reset the tag");
            return false;
        }
    }

    // --- Multi-texture blend data (LandBrush::cellTextureBlend): candidate
    // layers in first-seen corner order, one-hot corner weights, untagged
    // fallback. Cell (2,2) corners: Left (2,3), Up (2,2), Right (3,2),
    // Down (3,3).
    {
        float layers[4];
        glm::vec4 w[4];
        const glm::vec4 e0{1.0f, 0.0f, 0.0f, 0.0f};
        const glm::vec4 e1{0.0f, 1.0f, 0.0f, 0.0f};

        // Untagged cell: no candidates, zero weights (mask-color fallback).
        LandBrush b0;
        b0.reset(8, 8);
        b0.setNode({2, 2}, true);
        if (b0.cellTextureBlend({2, 2}, layers, w) != 0 ||
            w[0] != glm::vec4(0.0f) || w[1] != glm::vec4(0.0f)) {
            spdlog::error("TEST FAIL TileShape: untagged cell must have no blend candidates");
            return false;
        }

        LandBrush b;
        b.reset(8, 8);
        b.setNode({2, 3}, true, 3); // Left -> layer 2, first-seen candidate 0
        b.setNode({2, 2}, true, 1); // Up -> layer 0, candidate 1
        b.setNode({3, 2}, true, 1); // Right -> layer 0
        // Down is off: masked away, falls back to the first candidate's slot.
        const int nc = b.cellTextureBlend({2, 2}, layers, w);
        if (nc != 2 || layers[0] != 2.0f || layers[1] != 0.0f) {
            spdlog::error("TEST FAIL TileShape: blend candidates {} -> ({}, {})",
                nc, layers[0], layers[1]);
            return false;
        }
        if (w[0] != e0 || w[1] != e1 || w[2] != e1 || w[3] != e0) {
            spdlog::error("TEST FAIL TileShape: corner blend weights mismatch");
            return false;
        }
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

        // --- Stone layer pipeline (stone_gen::StoneField over the same
        // nodes): voronoi stones carved into the slab, generic
        // ScalarFieldView -> surface nets, watertight mesh, groove attr.
        stone_gen::StoneFieldParams sp;
        sp.base.cellSize = 0.09f; // coarse: smoke speed
        sp.base.groundEnabled = false;
        stone_gen::StoneField stoneField(sp, nodes.data(), nodesX, nodesY);
        cliff::ScalarFieldView stoneView = stoneField.view();
        std::vector<float> stoneSamples;
        stoneField.sample(stoneSamples);
        cliff::RegularizeStats stoneReg;
        cliff::regularizeSigns(stoneView, stoneSamples, &stoneReg);
        const cliff::Mesh stoneMesh = cliff::extractSurfaceNets(stoneView, stoneSamples, nullptr);
        if (stoneReg.remaining != 0) {
            spdlog::error("TEST FAIL TileShape: stone sign regularization left {} saddles",
                stoneReg.remaining);
            return false;
        }
        if (stoneMesh.vertices.empty() || stoneMesh.indices.empty()) {
            spdlog::error("TEST FAIL TileShape: stone mesh is empty");
            return false;
        }
        const cliff::WatertightReport stoneReport = cliff::checkWatertight(stoneMesh);
        if (!stoneReport.ok()) {
            spdlog::error("TEST FAIL TileShape: stone mesh not watertight ({} bad of {} edges)",
                stoneReport.badEdges, stoneReport.undirectedEdges);
            return false;
        }
        float sgMax = -1e9f;
        for (const cliff::MeshVertex& v : stoneMesh.vertices) {
            sgMax = std::max(sgMax, v.groove);
        }
        if (sgMax <= 0.01f) {
            spdlog::error("TEST FAIL TileShape: stone groove attribute range max {:.4f} — no carve",
                sgMax);
            return false;
        }

        // --- Tech layer pipeline (tech::TechField over the same nodes):
        // TechnicalGrass ridge/valley heightfield, generic ScalarFieldView ->
        // surface nets, watertight mesh, crease groove attr, plateau top at
        // levelHeight. Both style extremes (ridge / valley).
        for (const float style : {0.0f, 1.0f}) {
            tech::TechFieldParams tp;
            tp.cellSize = 0.09f; // coarse: smoke speed
            tp.style = style;
            tp.creaseWidth = 0.08f;
            tech::TechField techField(tp, nodes.data(), nodesX, nodesY);
            cliff::ScalarFieldView techView = techField.view();
            std::vector<float> techSamples;
            techField.sample(techSamples);
            cliff::RegularizeStats techReg;
            cliff::regularizeSigns(techView, techSamples, &techReg);
            const cliff::Mesh techMesh = cliff::extractSurfaceNets(techView, techSamples, nullptr);
            if (techReg.remaining != 0) {
                spdlog::error("TEST FAIL TileShape: tech sign regularization left {} saddles (style {:.1f})",
                    techReg.remaining,
                    style);
                return false;
            }
            if (techMesh.vertices.empty() || techMesh.indices.empty()) {
                spdlog::error("TEST FAIL TileShape: tech mesh is empty (style {:.1f})", style);
                return false;
            }
            const cliff::WatertightReport techReport = cliff::checkWatertight(techMesh);
            if (!techReport.ok()) {
                spdlog::error(
                    "TEST FAIL TileShape: tech mesh not watertight ({} bad of {} edges, style {:.1f})",
                    techReport.badEdges,
                    techReport.undirectedEdges,
                    style);
                return false;
            }
            float tgMax = -1e9f;
            float tpyMax = -1e9f;
            for (const cliff::MeshVertex& v : techMesh.vertices) {
                tgMax = std::max(tgMax, v.groove);
                tpyMax = std::max(tpyMax, v.py);
            }
            if (tgMax <= 0.5f) {
                spdlog::error("TEST FAIL TileShape: tech groove attribute range max {:.4f} — no outline",
                    tgMax);
                return false;
            }
            if (std::fabs(tpyMax - tp.levelHeight) > 2.0f * tp.cellSize) {
                spdlog::error("TEST FAIL TileShape: tech plateau top {:.3f}, expected ~{:.3f} (style {:.1f})",
                    tpyMax,
                    tp.levelHeight,
                    style);
                return false;
            }
        }

        // --- Tech shoreline (tech_outline::TechOutlineField — the
        // playground-local fork the "Tech 3D Outline" layer renders with):
        // the field derives the outline ring around the land ("yellow around
        // green"), the ramps continue below the water plane. Margin-2 grid:
        // the ring spreads one cell past the land and the border ring stays
        // empty.
        {
            const std::uint8_t shoreNodes[8][8] = {
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 1, 1, 1, 0, 0, 0},
                {0, 0, 1, 0, 1, 0, 0, 0},
                {0, 0, 1, 1, 1, 0, 0, 0},
                {0, 0, 0, 0, 0, 1, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
            };
            tech_outline::TechOutlineFieldParams tp;
            tp.cellSize = 0.09f; // coarse: smoke speed
            tp.outlineDepth = 1.0f;
            tech_outline::TechOutlineField techField(tp, &shoreNodes[0][0], 8, 8);
            cliff::ScalarFieldView techView = techField.view();
            std::vector<float> techSamples;
            techField.sample(techSamples);
            cliff::RegularizeStats techReg;
            cliff::regularizeSigns(techView, techSamples, &techReg);
            const cliff::Mesh techMesh = cliff::extractSurfaceNets(techView, techSamples, nullptr);
            if (techReg.remaining != 0) {
                spdlog::error("TEST FAIL TileShape: tech outline sign regularization left {} saddles",
                    techReg.remaining);
                return false;
            }
            if (techMesh.vertices.empty() || techMesh.indices.empty()) {
                spdlog::error("TEST FAIL TileShape: tech outline mesh is empty");
                return false;
            }
            const cliff::WatertightReport techReport = cliff::checkWatertight(techMesh);
            if (!techReport.ok()) {
                spdlog::error("TEST FAIL TileShape: tech outline mesh not watertight ({} bad of {} edges)",
                    techReport.badEdges,
                    techReport.undirectedEdges);
                return false;
            }
            float tpyMin = 1e9f;
            float tpyMax = -1e9f;
            for (const cliff::MeshVertex& v : techMesh.vertices) {
                tpyMin = std::min(tpyMin, v.py);
                tpyMax = std::max(tpyMax, v.py);
            }
            if (tpyMin > -0.5f * tp.levelHeight) {
                spdlog::error("TEST FAIL TileShape: tech outline foot not underwater (min py {:.3f})",
                    tpyMin);
                return false;
            }
            if (std::fabs(tpyMax - tp.levelHeight) > 2.0f * tp.cellSize) {
                spdlog::error("TEST FAIL TileShape: tech outline plateau top {:.3f}, expected ~{:.3f}",
                    tpyMax,
                    tp.levelHeight);
                return false;
            }
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + node tags + flat atlas generator + cliff/stone/tech field pipelines");
    return true;
}
