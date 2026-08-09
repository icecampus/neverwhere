#include "PlaygroundSmokeTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <highground_core/surface_nets.h>

#include "FlatAtlasGenerator.h"
#include "MaskField.h"
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

    // --- Mask layer pipeline (maskfield::MaskField — the single brush this
    // fork renders with): the 2D mask silhouette (bilinear node fill, iso
    // 0.5) extruded into a thin plate standing half its height below the
    // node grid plane (slab y = -height/2..+height/2). A 2x2 node block
    // plus a detached node (a small blob).
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
                maskReport.badEdges, maskReport.undirectedEdges);
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

        // --- Sink fraction: the share of the plate height standing below
        // the node grid plane (y = 0, the future water). height 0.2,
        // sink 0.75 -> the slab spans y = -0.15..+0.05 (the default 0.5
        // keeps the symmetric +-hh slab the blocks above assert).
        {
            maskfield::MaskFieldParams mpk;
            mpk.cellSize = 0.09f; // coarse: smoke speed
            mpk.sinkFraction = 0.75f;
            maskfield::MaskField sinkField(mpk, &maskNodes[0][0], 8, 8);
            const float topIn = sinkField.eval(glm::vec3(2.5f, 0.03f, 2.5f));
            const float topOut = sinkField.eval(glm::vec3(2.5f, 0.07f, 2.5f));
            const float botIn = sinkField.eval(glm::vec3(2.5f, -0.13f, 2.5f));
            const float botOut = sinkField.eval(glm::vec3(2.5f, -0.17f, 2.5f));
            if (topIn >= 0.0f || topOut <= 0.0f || botIn >= 0.0f || botOut <= 0.0f) {
                spdlog::error(
                    "TEST FAIL TileShape: mask sink wrong (top {:.4f}/{:.4f}, bottom {:.4f}/{:.4f})",
                    topIn, topOut, botIn, botOut);
                return false;
            }
        }

        // --- Micro relief (displacement): a tiling CPU height raster
        // shifts the iso surface by reliefDepth * (sample - 0.5) * 2.
        // A uniform map moves the top by exactly +/- depth; a gradient map
        // pins the bilinear REPEAT sampling at a chosen point.
        {
            const float hh = 0.5f * 0.2f;
            const float depth = 0.05f;
            maskfield::ReliefMap flat1;
            flat1.w = 4;
            flat1.h = 4;
            flat1.gray.assign(16, 1.0f);
            maskfield::MaskFieldParams mpr;
            mpr.cellSize = 0.09f; // coarse: smoke speed
            mpr.reliefDepth = depth;
            mpr.reliefMap = &flat1;
            maskfield::MaskField reliefField(mpr, &maskNodes[0][0], 8, 8);
            // Deep inside the block the top moves up by exactly depth.
            const float upIn = reliefField.eval(glm::vec3(2.5f, hh + depth - 0.02f, 2.5f));
            const float upOut = reliefField.eval(glm::vec3(2.5f, hh + depth + 0.02f, 2.5f));
            // Same field with an all-zero map: the top sinks by depth.
            maskfield::ReliefMap flat0 = flat1;
            flat0.gray.assign(16, 0.0f);
            mpr.reliefMap = &flat0;
            maskfield::MaskField sinkField(mpr, &maskNodes[0][0], 8, 8);
            const float downIn = sinkField.eval(glm::vec3(2.5f, hh - depth - 0.02f, 2.5f));
            const float downOut = sinkField.eval(glm::vec3(2.5f, hh - depth + 0.02f, 2.5f));
            // Bilinear REPEAT probe: gradient map gray[i] = i/15, tiling 1.0.
            // At (1.625, 2.125): gx = 2.5 (fx 0.5), gz = 1.0 (fz 0) ->
            // sample = 0.5*(6/15 + 7/15), so the top sits at
            // hh + depth*(sample - 0.5)*2.
            maskfield::ReliefMap grad;
            grad.w = 4;
            grad.h = 4;
            grad.gray.resize(16);
            for (int i = 0; i < 16; ++i) {
                grad.gray[static_cast<size_t>(i)] = static_cast<float>(i) / 15.0f;
            }
            mpr.reliefTiling = 1.0f;
            mpr.reliefMap = &grad;
            maskfield::MaskField gradField(mpr, &maskNodes[0][0], 8, 8);
            const float gy = hh + depth * (0.5f * (6.0f / 15.0f + 7.0f / 15.0f) - 0.5f) * 2.0f;
            const float gradIn = gradField.eval(glm::vec3(1.625f, gy - 0.01f, 2.125f));
            const float gradOut = gradField.eval(glm::vec3(1.625f, gy + 0.01f, 2.125f));
            if (upIn >= 0.0f || upOut <= 0.0f ||
                downIn >= 0.0f || downOut <= 0.0f ||
                gradIn >= 0.0f || gradOut <= 0.0f) {
                spdlog::error(
                    "TEST FAIL TileShape: mask relief wrong (up {:.4f}/{:.4f}, down {:.4f}/{:.4f}, grad {:.4f}/{:.4f})",
                    upIn, upOut, downIn, downOut, gradIn, gradOut);
                return false;
            }
            // Watertight with the relief on: the uniform map shifts the
            // whole iso surface, the contract must hold.
            cliff::ScalarFieldView reliefView = reliefField.view();
            std::vector<float> reliefSamples;
            reliefField.sample(reliefSamples);
            cliff::RegularizeStats reliefReg;
            cliff::regularizeSigns(reliefView, reliefSamples, &reliefReg);
            const cliff::Mesh reliefMesh = cliff::extractSurfaceNets(reliefView, reliefSamples, nullptr);
            const cliff::WatertightReport reliefReport = cliff::checkWatertight(reliefMesh);
            if (reliefReg.remaining != 0 || !reliefReport.ok()) {
                spdlog::error(
                    "TEST FAIL TileShape: mask relief mesh broken ({} saddles left, {} bad of {} edges)",
                    reliefReg.remaining,
                    reliefReport.badEdges,
                    reliefReport.undirectedEdges);
                return false;
            }
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + flat atlas generator + mask field pipeline");
    return true;
}
