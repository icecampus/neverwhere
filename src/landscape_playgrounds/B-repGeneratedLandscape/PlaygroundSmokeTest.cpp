#include "pch.h"

#include "PlaygroundSmokeTest.h"

#include <cmath>
#include <filesystem>

#include <spdlog/spdlog.h>

#include <landscape_mesh/landscape_mesh.h>

#include "BrepRenderer.h"
#include "NodeField.h"

namespace {

std::vector<std::uint8_t> makeNodes(int nodesX, int nodesY) {
    return std::vector<std::uint8_t>(static_cast<std::size_t>(nodesX) * nodesY, 0);
}

void setNode(std::vector<std::uint8_t>& nodes, int nodesX, int x, int y) {
    nodes[static_cast<std::size_t>(y) * nodesX + x] = 1;
}

// The 6x6 on-node plateau block from the landscape test-suite
// (CyclopeanPipelineProducesSeamedPlateau), composed with the given style.
landscape_mesh::CompositionResult composePlateau(landscape_mesh::WallStyleId style) {
    auto nodes = makeNodes(12, 12);
    for (int y = 3; y <= 8; ++y) {
        for (int x = 3; x <= 8; ++x) {
            setNode(nodes, 12, x, y);
        }
    }

    landscape_mesh::MeshBuildSettings settings;
    settings.cellSize = 1.0f;
    settings.levelHeight = 3.0f;
    settings.wallStyle = style;
    settings.wallHorizontalSubdivisions = 16;
    settings.wallVerticalSubdivisions = 16;

    landscape_mesh::SolidMeshBuildRequest request;
    request.mask = landscape_mesh::solidMaskFromNodes(nodes.data(), 12, 12);
    request.baseHeight = 0.0f;
    request.topHeight = 3.0f;
    request.level = 1;
    request.maxLevel = 1;
    request.includeWalls = true;
    request.fadeWallDisplacementAtBottom = false;

    return landscape_mesh::composeSolidMaskMesh(request, settings);
}

bool findDefaultMatDir(std::filesystem::path& out) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::weakly_canonical(std::filesystem::current_path(), ec);
    if (dir.empty()) {
        dir = std::filesystem::current_path(ec);
    }
    // Default set: resources/textures/polyhaven/marble_cliff_01. The loader
    // consumes the diffuse jpg plus the PNG conversions of the EXR
    // normal/roughness maps.
    const char* files[3] = {
        "marble_cliff_01_diff_4k.jpg",
        "marble_cliff_01_nor_gl_4k.png",
        "marble_cliff_01_rough_4k.png",
    };
    for (int i = 0; i < 16; ++i) {
        const std::filesystem::path setDir =
            dir / "resources" / "textures" / "polyhaven" / "marble_cliff_01";
        bool allPresent = true;
        for (const char* file : files) {
            allPresent = allPresent && std::filesystem::exists(setDir / file, ec);
        }
        if (allPresent) {
            out = setDir;
            return true;
        }
        if (!dir.has_parent_path()) {
            break;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return false;
}

} // namespace

bool runBrepSmokeTest() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        if (ok) {
            spdlog::info("TEST PASS: {}", name);
        } else {
            spdlog::error("TEST FAIL: {}", name);
            ++failures;
        }
    };

    // --- Node field contract -------------------------------------------------
    {
        NodeField field;
        field.reset(25, 25);
        check(field.width == 25 && field.height == 25 && field.onNodeCount() == 0,
            "node field reset");

        paintNodeLine(field, {2, 3}, {9, 3}, true);
        bool lineOk = true;
        for (int x = 2; x <= 9; ++x) {
            lineOk = lineOk && field.isOn({x, 3});
        }
        check(lineOk && field.onNodeCount() == 8, "node line painting (horizontal)");

        // Diagonal stroke: one painted node per step of the long axis, no
        // holes (the "holey dashes" gotcha of per-event painting).
        NodeField diag;
        diag.reset(25, 25);
        paintNodeLine(diag, {2, 2}, {9, 9}, true);
        check(diag.onNodeCount() == 8, "node line painting (diagonal, no holes)");

        // setNode is idempotent and bumps the version only on real changes.
        NodeField versioned;
        versioned.reset(25, 25);
        const std::uint64_t v0 = versioned.version;
        versioned.setNode({5, 5}, true);
        versioned.setNode({5, 5}, true);
        check(versioned.version == v0 + 1, "node version bumps only on changes");
    }

    // --- Composer pipelines ----------------------------------------------------
    const landscape_mesh::CompositionResult cyclopean =
        composePlateau(landscape_mesh::WallStyleId::Cyclopean);
    check(!cyclopean.quads.empty(), "cyclopean compose produces quads");
    check(cyclopean.stats.topQuadCount > 0, "cyclopean top quads present");
    check(cyclopean.stats.cliffWallQuadCount > 0, "cyclopean wall quads present");
    check(cyclopean.seams.passed, "cyclopean seams pass");
    check(cyclopean.normalOrientation.outwardFailCount == 0,
        "cyclopean normals face outward");

    const landscape_mesh::CompositionResult blockCliff =
        composePlateau(landscape_mesh::WallStyleId::BlockCliff);
    check(!blockCliff.quads.empty() && blockCliff.seams.passed,
        "block-cliff compose produces a seamed plateau");

    // --- Vertex bake -------------------------------------------------------------
    // Default iso dims (128x64 cell), lift scale 96 px per world unit.
    {
        constexpr float kHalfW = 64.0f;
        constexpr float kHalfH = 32.0f;
        constexpr float kHeightScale = 96.0f;
        constexpr float kTopHeight = 3.0f;
        const glm::ivec2 origin{0, 0};

        // A plateau-top quad: all four corners at the top height.
        const landscape_mesh::MeshQuad* topQuad = nullptr;
        const landscape_mesh::MeshQuad* wallQuad = nullptr;
        for (const landscape_mesh::MeshQuad& quad : cyclopean.quads) {
            const float ys[4] = {quad.a.y, quad.b.y, quad.c.y, quad.d.y};
            const bool allTop = std::abs(ys[0] - kTopHeight) < 1e-4f &&
                std::abs(ys[1] - kTopHeight) < 1e-4f &&
                std::abs(ys[2] - kTopHeight) < 1e-4f &&
                std::abs(ys[3] - kTopHeight) < 1e-4f;
            if (!topQuad && !quad.cliffWall && allTop) {
                topQuad = &quad;
            }
            if (!wallQuad && quad.cliffWall) {
                wallQuad = &quad;
            }
        }
        check(topQuad != nullptr, "bake: plateau top quad found");
        check(wallQuad != nullptr, "bake: wall quad found");

        if (topQuad) {
            std::vector<BrepVertex> verts;
            appendBrepQuadVertices(*topQuad, origin, kHalfW, kHalfH, kHeightScale, verts);
            bool heightOk = verts.size() == 6;
            bool depthOk = true;
            bool bboxOk = true;
            for (const BrepVertex& v : verts) {
                heightOk = heightOk && std::abs(v.wy - kTopHeight) < 1e-4f;
                // The baked z matches the contract formula and the lifted
                // vertex is strictly closer than its ground anchor.
                const float fieldY = (v.wx + v.wz) * kHalfH;
                const float expectZ = brepBakedDepth(fieldY, kTopHeight * kHeightScale);
                depthOk = depthOk && std::abs(v.z - expectZ) < 1e-6f &&
                    v.z < brepBakedDepth(fieldY, 0.0f);
                // World coords stay inside the mask bbox (the composer ran on
                // the full 12x12 node grid = 11x11 cells; tops never bulge).
                bboxOk = bboxOk && v.wx >= -1e-3f && v.wx <= 11.0f + 1e-3f &&
                    v.wz >= -1e-3f && v.wz <= 11.0f + 1e-3f;
            }
            check(heightOk, "bake: top vertices sit at the plateau height");
            check(depthOk, "bake: lifted vertices are closer (smaller z)");
            check(bboxOk, "bake: world coords inside the mask bbox");
        }

        if (wallQuad) {
            std::vector<BrepVertex> verts;
            appendBrepQuadVertices(*wallQuad, origin, kHalfW, kHalfH, kHeightScale, verts);
            // litWallNormal: horizontal, unit length (mesh-authoritative
            // outward hint, not the displaced facet normal).
            bool normalOk = verts.size() == 6;
            for (const BrepVertex& v : verts) {
                const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
                normalOk = normalOk && std::abs(v.ny) < 1e-4f && std::abs(len - 1.0f) < 1e-3f;
            }
            check(normalOk, "bake: wall normals horizontal unit (litWallNormal)");
        }
    }

    // --- Material set ------------------------------------------------------------
    {
        std::filesystem::path matDir;
        check(findDefaultMatDir(matDir),
            "default material set (marble_cliff_01, incl. EXR->PNG conversions) present under resources/textures/");
        if (!matDir.empty()) {
            // The picker's filesystem probe sees color+normal+roughness (the
            // set ships no AO map).
            const int mask = probeMaterialMaps(matDir.string(), "marble_cliff_01");
            check((mask & 0xB) == 0xB,
                "probeMaterialMaps finds color+normal+roughness of marble_cliff_01");
        }
    }

    if (failures == 0) {
        spdlog::info("TEST PASS: B-repGeneratedLandscape smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: B-repGeneratedLandscape smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}
