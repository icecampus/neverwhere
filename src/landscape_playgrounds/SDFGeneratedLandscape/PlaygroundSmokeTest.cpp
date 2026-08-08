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
#include "BoxField.h"
#include "CircleField.h"
#include "MaskField.h"
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

        // --- Box layer pipeline (boxfield::BoxField — the minimal teaching
        // sample the "Box 3D" layer renders with): one axis-aligned box per
        // painted cell. A merged 3x3-cell block plus a detached cluster
        // touching it at a single point (a pinch: saddle faces for
        // regularizeSigns to resolve). Watertight at fill = 1 (neighbours
        // merge) and at fill = 0.6 (gaps between cells).
        for (const float fill : {1.0f, 0.6f}) {
            const std::uint8_t boxNodes[8][8] = {
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 1, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
            };
            boxfield::BoxFieldParams bp;
            bp.cellSize = 0.09f; // coarse: smoke speed
            bp.fill = fill;
            boxfield::BoxField boxField(bp, &boxNodes[0][0], 8, 8);
            cliff::ScalarFieldView boxView = boxField.view();
            std::vector<float> boxSamples;
            boxField.sample(boxSamples);
            cliff::RegularizeStats boxReg;
            cliff::regularizeSigns(boxView, boxSamples, &boxReg);
            const cliff::Mesh boxMesh = cliff::extractSurfaceNets(boxView, boxSamples, nullptr);
            if (boxReg.remaining != 0) {
                spdlog::error("TEST FAIL TileShape: box sign regularization left {} saddles (fill {:.2f})",
                    boxReg.remaining,
                    fill);
                return false;
            }
            if (boxMesh.vertices.empty() || boxMesh.indices.empty()) {
                spdlog::error("TEST FAIL TileShape: box mesh is empty (fill {:.2f})", fill);
                return false;
            }
            const cliff::WatertightReport boxReport = cliff::checkWatertight(boxMesh);
            if (!boxReport.ok()) {
                spdlog::error("TEST FAIL TileShape: box mesh not watertight ({} bad of {} edges, fill {:.2f})",
                    boxReport.badEdges,
                    boxReport.undirectedEdges,
                    fill);
                return false;
            }
            float bpyMin = 1e9f;
            float bpyMax = -1e9f;
            for (const cliff::MeshVertex& v : boxMesh.vertices) {
                bpyMin = std::min(bpyMin, v.py);
                bpyMax = std::max(bpyMax, v.py);
            }
            if (std::fabs(bpyMax - bp.boxHeight) > 2.0f * bp.cellSize) {
                spdlog::error("TEST FAIL TileShape: box top {:.3f}, expected ~{:.3f} (fill {:.2f})",
                    bpyMax,
                    bp.boxHeight,
                    fill);
                return false;
            }
            if (std::fabs(bpyMin) > 2.0f * bp.cellSize) {
                spdlog::error("TEST FAIL TileShape: box bottom {:.3f}, expected ~0 (fill {:.2f})",
                    bpyMin,
                    fill);
                return false;
            }
            if (fill < 1.0f) {
                // The gap between cells is real: outside between two boxes.
                const float gapF = boxField.eval(glm::vec3(1.5f, 0.5f * bp.boxHeight, 2.0f));
                if (gapF <= 0.0f) {
                    spdlog::error("TEST FAIL TileShape: box fill {:.2f} still merged at the gap (f {:.4f})",
                        fill,
                        gapF);
                    return false;
                }
            }
        }

        // --- Circle layer pipeline (circlefield::CircleField — the second
        // teaching sample the "Circle 3D" layer renders with): a cylinder per
        // painted node plus a full parallelepiped per fully-painted cell.
        // A 2x2 node block (its centre cell is fully painted) plus a
        // detached node that stays a lone cylinder.
        {
            const std::uint8_t circleNodes[8][8] = {
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 1, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
            };
            circlefield::CircleFieldParams cp;
            cp.cellSize = 0.09f; // coarse: smoke speed
            circlefield::CircleField circleField(cp, &circleNodes[0][0], 8, 8);
            cliff::ScalarFieldView circleView = circleField.view();
            std::vector<float> circleSamples;
            circleField.sample(circleSamples);
            cliff::RegularizeStats circleReg;
            cliff::regularizeSigns(circleView, circleSamples, &circleReg);
            const cliff::Mesh circleMesh = cliff::extractSurfaceNets(circleView, circleSamples, nullptr);
            if (circleReg.remaining != 0) {
                spdlog::error("TEST FAIL TileShape: circle sign regularization left {} saddles",
                    circleReg.remaining);
                return false;
            }
            if (circleMesh.vertices.empty() || circleMesh.indices.empty()) {
                spdlog::error("TEST FAIL TileShape: circle mesh is empty");
                return false;
            }
            const cliff::WatertightReport circleReport = cliff::checkWatertight(circleMesh);
            if (!circleReport.ok()) {
                spdlog::error("TEST FAIL TileShape: circle mesh not watertight ({} bad of {} edges)",
                    circleReport.badEdges,
                    circleReport.undirectedEdges);
                return false;
            }
            float cpyMin = 1e9f;
            float cpyMax = -1e9f;
            for (const cliff::MeshVertex& v : circleMesh.vertices) {
                cpyMin = std::min(cpyMin, v.py);
                cpyMax = std::max(cpyMax, v.py);
            }
            if (std::fabs(cpyMax - cp.height) > 2.0f * cp.cellSize) {
                spdlog::error("TEST FAIL TileShape: circle top {:.3f}, expected ~{:.3f}",
                    cpyMax,
                    cp.height);
                return false;
            }
            if (std::fabs(cpyMin) > 2.0f * cp.cellSize) {
                spdlog::error("TEST FAIL TileShape: circle bottom {:.3f}, expected ~0",
                    cpyMin);
                return false;
            }
            // The fully painted cell (2,2): its centre is covered by the
            // box — the four corner circles (r 0.55 < ~0.707) would leave
            // it open on their own.
            const float cellF = circleField.eval(glm::vec3(2.5f, 0.5f * cp.height, 2.5f));
            if (cellF >= 0.0f) {
                spdlog::error("TEST FAIL TileShape: full cell centre not covered (f {:.4f})", cellF);
                return false;
            }
            // The detached node grows a cylinder: inside within the radius,
            // outside past it.
            const float inF = circleField.eval(glm::vec3(5.0f, 0.5f * cp.height, 5.0f));
            const float outF = circleField.eval(glm::vec3(5.0f + cp.nodeRadius + 0.15f, 0.5f * cp.height, 5.0f));
            if (inF >= 0.0f || outF <= 0.0f) {
                spdlog::error("TEST FAIL TileShape: detached cylinder wrong (in {:.4f}, out {:.4f})",
                    inF,
                    outF);
                return false;
            }
        }

        // --- Mask layer pipeline (maskfield::MaskField — the third teaching
        // sample the "Mask 3D" layer renders with): the Texture 2D mask
        // silhouette (bilinear node fill, iso 0.5) extruded into a thin
        // plate standing half its height below the node grid plane (slab
        // y = -height/2..+height/2). A 2x2 node block plus a detached node
        // (a small blob).
        {
            const std::uint8_t maskNodes[8][8] = {
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 1, 1, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 1, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
                {0, 0, 0, 0, 0, 0, 0, 0},
            };
            maskfield::MaskFieldParams mp;
            mp.cellSize = 0.09f; // coarse: smoke speed
            maskfield::MaskField maskField(mp, &maskNodes[0][0], 8, 8);
            cliff::ScalarFieldView maskView = maskField.view();
            std::vector<float> maskSamples;
            maskField.sample(maskSamples);
            cliff::RegularizeStats maskReg;
            cliff::regularizeSigns(maskView, maskSamples, &maskReg);
            const cliff::Mesh maskMesh = cliff::extractSurfaceNets(maskView, maskSamples, nullptr);
            if (maskReg.remaining != 0) {
                spdlog::error("TEST FAIL TileShape: mask sign regularization left {} saddles",
                    maskReg.remaining);
                return false;
            }
            if (maskMesh.vertices.empty() || maskMesh.indices.empty()) {
                spdlog::error("TEST FAIL TileShape: mask mesh is empty");
                return false;
            }
            const cliff::WatertightReport maskReport = cliff::checkWatertight(maskMesh);
            if (!maskReport.ok()) {
                spdlog::error("TEST FAIL TileShape: mask mesh not watertight ({} bad of {} edges)",
                    maskReport.badEdges,
                    maskReport.undirectedEdges);
                return false;
            }
            float mpyMin = 1e9f;
            float mpyMax = -1e9f;
            for (const cliff::MeshVertex& v : maskMesh.vertices) {
                mpyMin = std::min(mpyMin, v.py);
                mpyMax = std::max(mpyMax, v.py);
            }
            if (std::fabs(mpyMax - 0.5f * mp.height) > 2.0f * mp.cellSize) {
                spdlog::error("TEST FAIL TileShape: mask top {:.3f}, expected ~{:.3f}",
                    mpyMax,
                    0.5f * mp.height);
                return false;
            }
            if (std::fabs(mpyMin + 0.5f * mp.height) > 2.0f * mp.cellSize) {
                spdlog::error("TEST FAIL TileShape: mask bottom {:.3f}, expected ~{:.3f}",
                    mpyMin,
                    -0.5f * mp.height);
                return false;
            }
            // Deep inside the block and on the detached node: solid at the
            // slab mid-plane (y = 0 — the node grid plane). The shared edge
            // of two on-nodes is inside too (no crack: the fill is C0
            // across cells).
            const float midF = maskField.eval(glm::vec3(2.5f, 0.0f, 2.5f));
            const float nodeF = maskField.eval(glm::vec3(5.0f, 0.0f, 5.0f));
            const float edgeF = maskField.eval(glm::vec3(2.5f, 0.0f, 2.0f));
            if (midF >= 0.0f || nodeF >= 0.0f || edgeF >= 0.0f) {
                spdlog::error("TEST FAIL TileShape: mask interior wrong (mid {:.4f}, node {:.4f}, edge {:.4f})",
                    midF,
                    nodeF,
                    edgeF);
                return false;
            }
            // The iso-0.5 contour: the wall cuts an on->off edge at its
            // midpoint — inside at 0.4 of the way, outside at 0.6.
            const float inW = maskField.eval(glm::vec3(3.0f + 0.4f, 0.0f, 2.2f));
            const float outW = maskField.eval(glm::vec3(3.0f + 0.6f, 0.0f, 2.2f));
            if (inW >= 0.0f || outW <= 0.0f) {
                spdlog::error("TEST FAIL TileShape: mask wall not at the edge midpoint (in {:.4f}, out {:.4f})",
                    inW,
                    outW);
                return false;
            }

            // --- Spread: the XZ term is a true signed distance to the core
            // contour, and the spread band is a skirt whose height ramps
            // linearly from +hh at the wall (s = 0) down to -hh at
            // s = spreadDistance, crossing the node grid plane mid-band.
            // Here spread 0.5, height 0.2 (hh 0.1): top(s) = 0.1 - 0.4*s,
            // the foot reaches the bottom plane at the off-node line
            // (3.5 + 0.5), and the skirt foot rounds the convex corners
            // (the bilinear contour cuts the block's corner through
            // ~(3.29, 3.29) on the diagonal).
            {
                maskfield::MaskFieldParams mps;
                mps.cellSize = 0.09f; // coarse: smoke speed
                mps.spreadDistance = 0.5f;
                maskfield::MaskField spreadField(mps, &maskNodes[0][0], 8, 8);
                // The core keeps the full height right up to the wall
                // (top at y = +hh).
                const float coreIn = spreadField.eval(glm::vec3(2.5f, 0.08f, 2.5f));
                const float coreOut = spreadField.eval(glm::vec3(2.5f, 0.12f, 2.5f));
                // The ramp: at x = 3.6 (s = 0.1) the top is at 0.06, at
                // x = 3.9 (s = 0.4) it is at -0.06 — below/above probes.
                const float rampAIn = spreadField.eval(glm::vec3(3.6f, 0.04f, 2.2f));
                const float rampAOut = spreadField.eval(glm::vec3(3.6f, 0.08f, 2.2f));
                const float rampBIn = spreadField.eval(glm::vec3(3.9f, -0.08f, 2.2f));
                const float rampBOut = spreadField.eval(glm::vec3(3.9f, -0.04f, 2.2f));
                // The skirt crosses the node grid plane halfway across the
                // band: at x = 3.75 (s = 0.25) the top sits exactly at y=0.
                const float gridIn = spreadField.eval(glm::vec3(3.75f, -0.02f, 2.2f));
                const float gridOut = spreadField.eval(glm::vec3(3.75f, 0.02f, 2.2f));
                // The foot at the bottom plane (y = -hh): inside just
                // before the off-node line, outside past it; the corner
                // diagonal rounds off (~0.36 vs ~0.72 from the contour,
                // spread 0.5 between).
                const float footIn = spreadField.eval(glm::vec3(3.9f, -0.09f, 2.2f));
                const float footOut = spreadField.eval(glm::vec3(4.1f, -0.09f, 2.2f));
                const float cornerIn = spreadField.eval(glm::vec3(3.55f, -0.09f, 3.55f));
                const float cornerOut = spreadField.eval(glm::vec3(3.8f, -0.09f, 3.8f));
                if (coreIn >= 0.0f || coreOut <= 0.0f ||
                    rampAIn >= 0.0f || rampAOut <= 0.0f ||
                    rampBIn >= 0.0f || rampBOut <= 0.0f ||
                    gridIn >= 0.0f || gridOut <= 0.0f ||
                    footIn >= 0.0f || footOut <= 0.0f ||
                    cornerIn >= 0.0f || cornerOut <= 0.0f) {
                    spdlog::error(
                        "TEST FAIL TileShape: mask spread wrong (core {:.4f}/{:.4f}, ramp {:.4f}/{:.4f} {:.4f}/{:.4f}, grid {:.4f}/{:.4f}, foot {:.4f}/{:.4f}, corner {:.4f}/{:.4f})",
                        coreIn, coreOut, rampAIn, rampAOut, rampBIn, rampBOut,
                        gridIn, gridOut, footIn, footOut, cornerIn, cornerOut);
                    return false;
                }
                // The skirted silhouette must still close inside the field:
                // same extraction, same watertight contract.
                cliff::ScalarFieldView spreadView = spreadField.view();
                std::vector<float> spreadSamples;
                spreadField.sample(spreadSamples);
                cliff::RegularizeStats spreadReg;
                cliff::regularizeSigns(spreadView, spreadSamples, &spreadReg);
                const cliff::Mesh spreadMesh = cliff::extractSurfaceNets(spreadView, spreadSamples, nullptr);
                const cliff::WatertightReport spreadReport = cliff::checkWatertight(spreadMesh);
                if (spreadReg.remaining != 0 || !spreadReport.ok()) {
                    spdlog::error(
                        "TEST FAIL TileShape: mask spread mesh broken ({} saddles left, {} bad of {} edges)",
                        spreadReg.remaining,
                        spreadReport.badEdges,
                        spreadReport.undirectedEdges);
                    return false;
                }
            }
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + node tags + flat atlas generator + cliff/stone/tech/box/circle/mask field pipelines");
    return true;
}
