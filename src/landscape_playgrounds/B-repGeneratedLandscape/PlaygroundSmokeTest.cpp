#include "pch.h"

#include "PlaygroundSmokeTest.h"

#include <cmath>
#include <filesystem>

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include "BrepMesh.h"

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
brepmesh::CompositionResult composePlateau(brepmesh::WallStyleId style) {
    auto nodes = makeNodes(12, 12);
    for (int y = 3; y <= 8; ++y) {
        for (int x = 3; x <= 8; ++x) {
            setNode(nodes, 12, x, y);
        }
    }

    brepmesh::MeshBuildSettings settings;
    settings.cellSize = 1.0f;
    settings.levelHeight = 3.0f;
    settings.wallStyle = style;
    settings.wallHorizontalSubdivisions = 16;
    settings.wallVerticalSubdivisions = 16;

    brepmesh::SolidMeshBuildRequest request;
    request.mask = brepmesh::solidMaskFromNodes(nodes.data(), 12, 12);
    request.baseHeight = 0.0f;
    request.topHeight = 3.0f;
    request.level = 1;
    request.maxLevel = 1;
    request.includeWalls = true;
    request.fadeWallDisplacementAtBottom = false;

    return brepmesh::composeSolidMaskMesh(request, settings);
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
    const brepmesh::CompositionResult cyclopean =
        composePlateau(brepmesh::WallStyleId::Cyclopean);
    check(!cyclopean.quads.empty(), "cyclopean compose produces quads");
    check(cyclopean.stats.topQuadCount > 0, "cyclopean top quads present");
    check(cyclopean.stats.cliffWallQuadCount > 0, "cyclopean wall quads present");
    check(cyclopean.seams.passed, "cyclopean seams pass");
    check(cyclopean.normalOrientation.outwardFailCount == 0,
        "cyclopean normals face outward");

    const brepmesh::CompositionResult blockCliff =
        composePlateau(brepmesh::WallStyleId::BlockCliff);
    check(!blockCliff.quads.empty() && blockCliff.seams.passed,
        "block-cliff compose produces a seamed plateau");

    // --- Multi-level compose -----------------------------------------------------
    {
        // 6x6 terrace at level 1 plus a 3x3 cap at level 2.
        auto nodes = makeNodes(12, 12);
        for (int y = 3; y <= 8; ++y) {
            for (int x = 3; x <= 8; ++x) {
                nodes[static_cast<std::size_t>(y) * 12 + x] = 1;
            }
        }
        for (int y = 5; y <= 7; ++y) {
            for (int x = 5; x <= 7; ++x) {
                nodes[static_cast<std::size_t>(y) * 12 + x] = 2;
            }
        }
        const auto composeLevel = [&](int level, int maxLevel) {
            auto levelNodes = makeNodes(12, 12);
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                levelNodes[i] = nodes[i] >= level ? std::uint8_t{1} : std::uint8_t{0};
            }
            brepmesh::MeshBuildSettings settings;
            settings.cellSize = 1.0f;
            settings.levelHeight = 3.0f;
            settings.wallStyle = brepmesh::WallStyleId::Cyclopean;
            settings.wallHorizontalSubdivisions = 16;
            settings.wallVerticalSubdivisions = 16;
            brepmesh::SolidMeshBuildRequest request;
            request.mask = brepmesh::solidMaskFromNodes(levelNodes.data(), 12, 12);
            request.baseHeight = static_cast<float>(level - 1) * 3.0f;
            request.topHeight = static_cast<float>(level) * 3.0f;
            request.level = static_cast<std::uint8_t>(level);
            request.maxLevel = static_cast<std::uint8_t>(maxLevel);
            request.includeWalls = true;
            request.fadeWallDisplacementAtBottom = false;
            return brepmesh::composeSolidMaskMesh(request, settings);
        };
        const brepmesh::CompositionResult level1 = composeLevel(1, 2);
        const brepmesh::CompositionResult level2 = composeLevel(2, 2);
        check(level1.seams.passed, "multi-level: level 1 seams pass");
        check(level2.seams.passed && !level2.quads.empty(), "multi-level: level 2 seams pass");
        // The level-2 band tops sit at 2H and its walls rise from 1H.
        bool capTop = false;
        bool capWall = false;
        for (const brepmesh::MeshQuad& quad : level2.quads) {
            const float ys[4] = {quad.a.y, quad.b.y, quad.c.y, quad.d.y};
            const float mn = std::min(std::min(ys[0], ys[1]), std::min(ys[2], ys[3]));
            const float mx = std::max(std::max(ys[0], ys[1]), std::max(ys[2], ys[3]));
            if (!quad.cliffWall && std::abs(mx - 6.0f) < 1e-3f && std::abs(mn - 6.0f) < 1e-3f) {
                capTop = true;
            }
            if (quad.cliffWall && mn >= 3.0f - 1e-3f) {
                capWall = true;
            }
        }
        check(capTop, "multi-level: level 2 top at 2H");
        check(capWall, "multi-level: level 2 walls rise from 1H");
    }

    // --- Wall profile (batter/flare/ledges) --------------------------------------
    {
        brepmesh::MeshBoundarySegment segment;
        segment.a = {0.0f, 0.0f, 0.0f};
        segment.b = {2.0f, 0.0f, 0.0f};
        segment.normal = {0.0f, 0.0f, -1.0f};
        segment.startNormal = segment.normal;
        segment.endNormal = segment.normal;

        brepmesh::MeshBuildSettings profileSettings;
        profileSettings.cellSize = 1.0f;
        profileSettings.levelHeight = 3.0f;
        profileSettings.rockEnabled = false; // isolate the macro-profile
        profileSettings.wallHorizontalSubdivisions = 8;
        profileSettings.wallVerticalSubdivisions = 8;
        profileSettings.wallBatter = 0.3f;

        const std::vector<brepmesh::MeshQuad> quads = brepmesh::buildWallQuadsFromBoundarySegment(
            segment, 0.0f, 3.0f, false, 0.0f, profileSettings);

        // Crest row pinned to the original line (z == 0 at y == 3); the foot
        // row pushed out along the outward normal by the batter (z == -0.3).
        bool crestPinned = !quads.empty();
        bool footBattered = !quads.empty();
        for (const brepmesh::MeshQuad& quad : quads) {
            const brepmesh::Vec3 corners[4] = {quad.a, quad.b, quad.c, quad.d};
            for (const brepmesh::Vec3& v : corners) {
                if (std::abs(v.y - 3.0f) < 1e-4f) {
                    crestPinned = crestPinned && std::abs(v.z) < 1e-4f;
                }
                if (std::abs(v.y) < 1e-4f) {
                    footBattered = footBattered && std::abs(v.z + 0.3f) < 1e-3f;
                }
            }
        }
        check(crestPinned, "profile: crest row pinned to the boundary line");
        check(footBattered, "profile: foot row pushed out by the batter");

        // Ledges: mid rows bulge, crest stays pinned.
        profileSettings.wallBatter = 0.0f;
        profileSettings.wallLedgeAmp = 0.1f;
        profileSettings.wallLedgeCount = 2;
        const std::vector<brepmesh::MeshQuad> ledgeQuads = brepmesh::buildWallQuadsFromBoundarySegment(
            segment, 0.0f, 3.0f, false, 0.0f, profileSettings);
        bool ledgeCrestPinned = !ledgeQuads.empty();
        bool anyLedge = false;
        for (const brepmesh::MeshQuad& quad : ledgeQuads) {
            const brepmesh::Vec3 corners[4] = {quad.a, quad.b, quad.c, quad.d};
            for (const brepmesh::Vec3& v : corners) {
                if (std::abs(v.y - 3.0f) < 1e-4f) {
                    ledgeCrestPinned = ledgeCrestPinned && std::abs(v.z) < 1e-4f;
                } else if (std::abs(v.z) > 1e-3f) {
                    anyLedge = true;
                }
            }
        }
        check(ledgeCrestPinned, "profile: ledges keep the crest pinned");
        check(anyLedge, "profile: ledges bulge the mid rows");

        // Talus apron: one quad per wall column; the top edge stitches to the
        // wall row (y == 0.75, z == 0), the bottom edge rests on the ground
        // pushed outward (y == 0, z == -0.5).
        profileSettings.wallLedgeAmp = 0.0f;
        profileSettings.talusWidth = 0.5f;
        profileSettings.talusHeightFrac = 0.25f;
        const std::vector<brepmesh::MeshQuad> talusQuads = brepmesh::buildWallQuadsFromBoundarySegment(
            segment, 0.0f, 3.0f, false, 0.0f, profileSettings);
        int apronCount = 0;
        int apronFlags = 0;
        bool apronStitched = true;
        for (const brepmesh::MeshQuad& quad : talusQuads) {
            if (quad.talus) {
                ++apronFlags;
            }
            const brepmesh::Vec3 corners[4] = {quad.a, quad.b, quad.c, quad.d};
            bool hasGroundFoot = false;
            bool hasWallEdge = false;
            for (const brepmesh::Vec3& v : corners) {
                if (std::abs(v.y) < 1e-4f && std::abs(v.z + 0.5f) < 1e-3f) {
                    hasGroundFoot = true;
                }
                if (std::abs(v.y - 0.75f) < 1e-3f && std::abs(v.z) < 1e-4f) {
                    hasWallEdge = true;
                }
            }
            if (hasGroundFoot) {
                ++apronCount;
                apronStitched = apronStitched && hasWallEdge;
            }
        }
        check(apronCount == 8, "talus: one apron quad per wall column");
        check(apronStitched, "talus: apron stitches to the wall row");
        check(apronFlags == 8, "talus: apron quads flagged for the bake channel");
    }

    // --- Vertex bake -------------------------------------------------------------
    // Default iso dims (128x64 cell), lift scale 96 px per world unit.
    {
        constexpr float kHalfW = 64.0f;
        constexpr float kHalfH = 32.0f;
        constexpr float kHeightScale = 96.0f;
        constexpr float kTopHeight = 3.0f;
        const glm::ivec2 origin{0, 0};

        // A plateau-top quad: all four corners at the top height.
        const brepmesh::MeshQuad* topQuad = nullptr;
        const brepmesh::MeshQuad* wallQuad = nullptr;
        for (const brepmesh::MeshQuad& quad : cyclopean.quads) {
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
            BrepDeformParams noDeform;
            noDeform.reliefAmp = 0.0f;
            noDeform.wobbleAmp = 0.0f;
            appendBrepQuadVertices(*topQuad, origin, kHalfW, kHalfH, kHeightScale, kTopHeight, noDeform, verts);
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
            BrepDeformParams noDeform;
            noDeform.reliefAmp = 0.0f;
            noDeform.wobbleAmp = 0.0f;
            appendBrepQuadVertices(*wallQuad, origin, kHalfW, kHalfH, kHeightScale, kTopHeight, noDeform, verts);
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

    // --- Bake deformation ------------------------------------------------------
    {
        const BrepDeformParams deform; // defaults: relief 0.25/4.5, wobble 0.2/4.0

        // Zero amps: identity (the pre-deformation bake contract).
        BrepDeformParams off;
        off.reliefAmp = 0.0f;
        off.wobbleAmp = 0.0f;
        const glm::vec3 p{4.25f, 3.0f, 7.5f};
        const glm::vec3 id = brepDeformVertex(p, 1.0f, off);
        check(id == p, "deform: zero amps are the identity");

        // Determinism: identical inputs displace identically (the seam-safety
        // contract is purity — no per-call state).
        const glm::vec3 d1 = brepDeformVertex(p, 1.0f, deform);
        const glm::vec3 d2 = brepDeformVertex(p, 1.0f, deform);
        check(d1 == d2, "deform: deterministic for shared vertices");

        // Grounded foot: heightFrac 0 pins the vertex even with amps on.
        const glm::vec3 foot = brepDeformVertex(p, 0.0f, deform);
        check(foot == p, "deform: heightFrac 0 keeps the foot grounded");

        // Amplitude bounds across a sample grid: |lift| <= reliefAmp (the two
        // octaves sum to <= 1), |wander| <= wobbleAmp, both scaled by hfrac.
        bool boundsOk = true;
        for (int i = -20; i <= 20 && boundsOk; ++i) {
            for (int j = -20; j <= 20; ++j) {
                const glm::vec3 q{0.37f * (float)i, 3.0f, 0.41f * (float)j};
                const glm::vec3 dq = brepDeformVertex(q, 0.5f, deform);
                boundsOk = boundsOk &&
                    std::abs(dq.y - q.y) <= deform.reliefAmp * 0.5f + 1e-4f &&
                    std::abs(dq.x - q.x) <= deform.wobbleAmp * 0.5f + 1e-4f &&
                    std::abs(dq.z - q.z) <= deform.wobbleAmp * 0.5f + 1e-4f;
            }
        }
        check(boundsOk, "deform: relief/wobble within amplitude bounds");

        // Relief actually lifts (not a constant zero field).
        bool anyLift = false;
        for (int i = -20; i <= 20 && !anyLift; ++i) {
            const glm::vec3 q{0.37f * (float)i, 3.0f, 0.19f * (float)i};
            anyLift = std::abs(brepDeformVertex(q, 1.0f, deform).y - q.y) > 1e-3f;
        }
        check(anyLift, "deform: relief is not flat");
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

    // --- Grass top texture -----------------------------------------------------
    {
        // Same walk-up as findDefaultMatDir: resources/textures/grass.png.
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::weakly_canonical(std::filesystem::current_path(), ec);
        if (dir.empty()) {
            dir = std::filesystem::current_path(ec);
        }
        std::filesystem::path grassPath;
        for (int i = 0; i < 16; ++i) {
            const std::filesystem::path candidate = dir / "resources" / "textures" / "grass.png";
            if (std::filesystem::exists(candidate, ec)) {
                grassPath = candidate;
                break;
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
        check(!grassPath.empty(), "default grass texture present under resources/textures/");
        if (!grassPath.empty()) {
            int w = 0, h = 0, comp = 0;
            check(stbi_info(grassPath.string().c_str(), &w, &h, &comp) == 1 && w > 0 && h > 0,
                "default grass texture decodable (stb)");
        }
    }

    if (failures == 0) {
        spdlog::info("TEST PASS: B-repGeneratedLandscape smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: B-repGeneratedLandscape smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}
