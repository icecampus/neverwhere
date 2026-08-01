#include "PlaygroundSmokeTest.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>
#include <stone_gen/stone_field.h>
#include <topology_core/diamond_isometry.h>

#include "FlatAtlasGenerator.h"
#include "LandBrush.h"
#include "SceneStitch.h"

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
    }

    // --- Scene stitching CPU half: the contact-AO distance field and the
    // orthographic sun frame. Both feed the shaders, so a silent regression
    // here shows up only as "the shadow moved a bit" on a screenshot.
    {
        LandBrush b;
        b.reset(24, 24);
        for (int y = 10; y <= 13; ++y) {
            for (int x = 10; x <= 13; ++x) {
                b.setNode({x, y}, true);
            }
        }
        const LandBrush* brushes[1] = {&b};
        const ContactAoField ao = buildContactAoField(brushes, 1, 4, 4);
        if (ao.empty() || ao.width != (24 + 8) * 4 || ao.height != (24 + 8) * 4) {
            spdlog::error("TEST FAIL TileShape: AO field {}x{} (empty={})", ao.width, ao.height,
                ao.empty());
            return false;
        }
        // Inside the footprint the distance is zero and it grows monotonically
        // as we walk away from the near edge along +x.
        if (ao.distanceAt(11.5f, 11.5f) > 0.01f) {
            spdlog::error("TEST FAIL TileShape: AO distance inside the footprint is {:.3f}",
                ao.distanceAt(11.5f, 11.5f));
            return false;
        }
        // Half-lit cells count as half: cell (13, 11) has only its Left and Up
        // nodes on, so the far half of that same cell is already outside the
        // footprint. Whole-cell coverage would report zero across all of it and
        // the AO ring would trace cell borders instead of the wall.
        if (ao.distanceAt(13.25f, 11.5f) > 0.01f) {
            spdlog::error("TEST FAIL TileShape: solid half of a half-lit cell reads {:.3f}",
                ao.distanceAt(13.25f, 11.5f));
            return false;
        }
        const float emptyHalf = ao.distanceAt(13.75f, 11.5f);
        if (emptyHalf < 0.1f || emptyHalf > 0.8f) {
            spdlog::error("TEST FAIL TileShape: empty half of a half-lit cell reads {:.3f}, "
                          "expected a fraction of a cell", emptyHalf);
            return false;
        }
        float prev = -1.0f;
        for (float d = 0.0f; d <= 6.0f; d += 0.25f) {
            const float cur = ao.distanceAt(14.0f + d, 11.5f);
            if (cur < prev - 1e-4f) {
                spdlog::error("TEST FAIL TileShape: AO distance not monotonic at +{:.2f} "
                              "({:.3f} after {:.3f})", d, cur, prev);
                return false;
            }
            prev = cur;
        }
        if (prev < kAoMaxDistanceCells - 0.01f) {
            spdlog::error("TEST FAIL TileShape: AO distance saturates at {:.3f}, expected {:.3f}",
                prev, kAoMaxDistanceCells);
            return false;
        }
        // Empty brush -> empty field (the renderer falls back to "no AO").
        LandBrush blank;
        blank.reset(24, 24);
        const LandBrush* blankList[1] = {&blank};
        if (!buildContactAoField(blankList, 1, 4, 4).empty()) {
            spdlog::error("TEST FAIL TileShape: AO field of an empty brush is not empty");
            return false;
        }

        const glm::vec3 boxMin{-3.0f, 0.0f, -3.0f};
        const glm::vec3 boxMax{27.0f, 1.5f, 27.0f};
        const SunBasis basis = buildSunBasis(glm::normalize(glm::vec3{0.6f, 0.7f, -0.4f}),
            boxMin, boxMax);
        if (!basis.valid) {
            spdlog::error("TEST FAIL TileShape: sun basis is invalid");
            return false;
        }
        const float ortho = std::abs(glm::dot(basis.right, basis.up)) +
            std::abs(glm::dot(basis.right, basis.dir)) + std::abs(glm::dot(basis.up, basis.dir));
        const float unit = std::abs(glm::length(basis.right) - 1.0f) +
            std::abs(glm::length(basis.up) - 1.0f) + std::abs(glm::length(basis.dir) - 1.0f);
        if (ortho > 1e-4f || unit > 1e-4f) {
            spdlog::error("TEST FAIL TileShape: sun basis not orthonormal (ortho {:.6f}, unit {:.6f})",
                ortho, unit);
            return false;
        }
        // Every corner of the light volume has to land inside the shadow map.
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 c{
                (corner & 1) ? boxMax.x : boxMin.x,
                (corner & 2) ? boxMax.y : boxMin.y,
                (corner & 4) ? boxMax.z : boxMin.z};
            const glm::vec3 sc = basis.project(c);
            if (sc.x < -1e-4f || sc.x > 1.0f + 1e-4f || sc.y < -1e-4f || sc.y > 1.0f + 1e-4f ||
                sc.z < -1e-4f || sc.z > 1.0f + 1e-4f) {
                spdlog::error("TEST FAIL TileShape: light volume corner ({},{},{}) projects to "
                              "({:.3f},{:.3f},{:.3f})", c.x, c.y, c.z, sc.x, sc.y, sc.z);
                return false;
            }
        }
        // Raising a point towards the sun must bring it closer to the light.
        const glm::vec3 low = basis.project({12.0f, 0.0f, 12.0f});
        const glm::vec3 high = basis.project({12.0f, 1.5f, 12.0f});
        if (high.z >= low.z) {
            spdlog::error("TEST FAIL TileShape: shadow depth does not decrease with height "
                          "({:.4f} -> {:.4f})", low.z, high.z);
            return false;
        }
        if (isoHeightToWorld(64.0f, 32.0f, 96.0f) <= 0.0f) {
            spdlog::error("TEST FAIL TileShape: iso height scale is not positive");
            return false;
        }
    }

    spdlog::info("TEST PASS TileShape: LandBrush + flat atlas generator + cliff/stone field "
                 "pipelines + contact AO field + sun basis");
    return true;
}
