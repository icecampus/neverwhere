#include "CliffSceneBuilder.h"

#include "rock_scene/ReplicationField.h"
#include "rock_scene/SceneComposer.h"
#include "rock_scene/TerrainField.h"
#include "TileBuild.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include <spdlog/spdlog.h>

namespace render_playground {

namespace {

Vec3 toVec3(const rock_fracture::Vector3& v) {
    return Vec3{(float)v.x, (float)v.y, (float)v.z};
}

void fillMeshFromMc(RockFractureModel& model, const rock_fracture::MC::mcMesh& mesh) {
    model.meshVertices.clear();
    model.meshNormals.clear();
    model.meshIndices.clear();

    model.meshVertices.reserve(mesh.vertices.size());
    for (const rock_fracture::MC::mcVec3f& v : mesh.vertices) {
        model.meshVertices.push_back({(float)v.x, (float)v.y, (float)v.z});
    }
    model.meshNormals.reserve(mesh.normals.size());
    for (const rock_fracture::MC::mcVec3f& n : mesh.normals) {
        model.meshNormals.push_back({(float)n.x, (float)n.y, (float)n.z});
    }
    model.meshIndices.reserve(mesh.indices.size());
    for (const rock_fracture::MC::muint idx : mesh.indices) {
        model.meshIndices.push_back((std::uint32_t)idx);
    }
    model.vertexCount = (int)model.meshVertices.size();
    model.triangleCount = (int)model.meshIndices.size() / 3;
}

void scanFieldRange(RockFractureModel& model, rock_fracture::SDFNode* root, int resolution) {
    if (root == nullptr) return;
    const int scanRes = std::min(32, resolution);
    const rock_fracture::Box b = root->box;
    const rock_fracture::Vector3 d = b.Diagonal() / double(scanRes - 1);
    double fmin = 1e30;
    double fmax = -1e30;
    for (int i = 0; i < scanRes; i++) {
        for (int j = 0; j < scanRes; j++) {
            for (int k = 0; k < scanRes; k++) {
                const rock_fracture::Vector3 p(
                    b[0][0] + i * d[0],
                    b[0][1] + j * d[1],
                    b[0][2] + k * d[2]);
                const double s = root->Signed(p);
                if (s < fmin) fmin = s;
                if (s > fmax) fmax = s;
            }
        }
    }
    model.fieldMin = fmin;
    model.fieldMax = fmax;
}

} // namespace

CliffBuildResult CliffSceneBuilder::build(const RockFractureSettings& settings, TileLibrary& tileLibrary) {
    CliffBuildResult result;
    RockFractureModel& model = result.model;

    RockFractureSettings sanitized = settings;
    RockFractureScene::sanitize(sanitized);
    applyPaperTilePreset(sanitized);
    SceneSpec sceneSpec = sanitized.scene;
    sanitizeSceneSpec(sceneSpec);

    const auto t0 = std::chrono::steady_clock::now();

    try {
        model.generationMode = GenerationMode::CliffScene;
        model.enableBlockReplication = sanitized.enableBlockReplication;
        model.cliffFace = sceneSpec.cliffFace;
        model.replicationMode = sceneSpec.replicationMode;
        SDFMacroBox terrain(sceneSpec);
        const rock_fracture::Box solidBox = terrain.solidBox();
        const rock_fracture::Box sceneBox = sceneBoundingBox(sceneSpec);

        model.boundsMin = {(float)solidBox[0][0], (float)solidBox[0][1], (float)solidBox[0][2]};
        model.boundsMax = {(float)solidBox[1][0], (float)solidBox[1][1], (float)solidBox[1][2]};
        model.plateauHeight = model.boundsMax.z;
        switch (sceneSpec.cliffFace) {
        case CliffFace::NegX:
            model.cliffInset = model.boundsMin.x;
            break;
        case CliffFace::PosX:
            model.cliffInset = model.boundsMax.x;
            break;
        case CliffFace::NegY:
            model.cliffInset = model.boundsMin.y;
            break;
        case CliffFace::PosY:
            model.cliffInset = model.boundsMax.y;
            break;
        }

        rock_fracture::SDFNode* tileRoot = nullptr;
        SDFReplicationField* replication = nullptr;

        if (sanitized.enableBlockReplication) {
            spdlog::info("CliffSceneBuilder: building tile cache...");
            TileCacheEntry tileEntry = tileLibrary.getOrBuild(sanitized);
            tileRoot = tileEntry.build.sdfRoot;
            model.usedTextureWarp = tileEntry.build.usedTextureWarp;
            model.usedFallbackTexture = tileEntry.build.usedFallbackTexture;

            model.sampleCount = tileEntry.build.samples.Size();
            model.fractureCount = tileEntry.build.fractures.Size();
            model.clusterCount = (int)tileEntry.build.clusters.size();

            replication = new SDFReplicationField(tileRoot, sceneSpec, sanitized.tileSize);
        }

        SDFSceneComposed composed(
            &terrain,
            replication,
            sceneBox,
            sceneSpec,
            sanitized.enableBlockReplication);

        const int mcRes = sceneSpec.mcResolution;
        spdlog::info("CliffSceneBuilder: marching cubes res={} replication={}...",
            mcRes, sanitized.enableBlockReplication ? "yes" : "no");
        rock_fracture::MC::mcMesh mesh = rock_fracture::PolygonizeSDF(sceneBox, &composed, mcRes);
        fillMeshFromMc(model, mesh);
        scanFieldRange(model, &composed, mcRes);

        delete replication;

        spdlog::info(
            "CliffSceneBuilder: mcRes={} replication={} vertices={} triangles={}",
            mcRes,
            sanitized.enableBlockReplication ? "yes" : "no",
            model.vertexCount,
            model.triangleCount);

        result.ok = model.triangleCount > 0;
    } catch (const std::exception& e) {
        model.generationFailed = true;
        model.failureMessage = e.what();
        spdlog::error("CliffSceneBuilder: exception: {}", e.what());
    } catch (...) {
        model.generationFailed = true;
        model.failureMessage = "unknown exception";
        spdlog::error("CliffSceneBuilder: unknown exception");
    }

    const auto t1 = std::chrono::steady_clock::now();
    model.buildSeconds = std::chrono::duration<double>(t1 - t0).count();
    return result;
}

bool CliffSceneBuilder::runTestScenario() {
    bool allPass = true;

    SceneSpec spec;
    spec.cubeSize = 20.0f;
    spec.replicationMode = CliffReplicationMode::AllVerticalFaces;
    sanitizeSceneSpec(spec);

    SDFMacroBox terrain(spec);
    const rock_fracture::Box solid = terrain.solidBox();
    const rock_fracture::Vector3 center = solid.Center();

    const double fInside = terrain.Signed(center);
    if (fInside >= 0.0) {
        spdlog::error("TEST FAIL: cube interior should be negative, got {}", fInside);
        allPass = false;
    } else {
        spdlog::info("TEST PASS: cube interior f={:.3f}", fInside);
    }

    const rock_fracture::Vector3 outside(0.1, 0.1, 0.1);
    const double fAir = terrain.Signed(outside);
    if (fAir <= 0.0) {
        spdlog::error("TEST FAIL: air outside cube should be positive, got {}", fAir);
        allPass = false;
    } else {
        spdlog::info("TEST PASS: air outside cube f={:.3f}", fAir);
    }

    const rock_fracture::Vector3 wallNegX(solid[0][0] + 0.05, center.y, center.z);
    const rock_fracture::Vector3 wallPosX(solid[1][0] - 0.05, center.y, center.z);
    const rock_fracture::Vector3 wallNegY(center.x, solid[0][1] + 0.05, center.z);
    const rock_fracture::Vector3 wallPosY(center.x, solid[1][1] - 0.05, center.z);
    const rock_fracture::Vector3 topFace(center.x, center.y, solid[1][2] - 0.05);

    if (!shouldApplyBlockReplication(wallNegX, spec, spec.surfaceBand)) {
        spdlog::error("TEST FAIL: NegX wall should accept block replication");
        allPass = false;
    } else {
        spdlog::info("TEST PASS: NegX wall in replication band");
    }
    if (!shouldApplyBlockReplication(wallPosX, spec, spec.surfaceBand)) {
        spdlog::error("TEST FAIL: PosX wall should accept block replication");
        allPass = false;
    } else {
        spdlog::info("TEST PASS: PosX wall in replication band");
    }
    if (!shouldApplyBlockReplication(wallNegY, spec, spec.surfaceBand)) {
        spdlog::error("TEST FAIL: NegY wall should accept block replication");
        allPass = false;
    } else {
        spdlog::info("TEST PASS: NegY wall in replication band");
    }
    if (!shouldApplyBlockReplication(wallPosY, spec, spec.surfaceBand)) {
        spdlog::error("TEST FAIL: PosY wall should accept block replication");
        allPass = false;
    } else {
        spdlog::info("TEST PASS: PosY wall in replication band");
    }
    if (shouldApplyBlockReplication(topFace, spec, spec.surfaceBand)) {
        spdlog::error("TEST FAIL: top face should not accept block replication");
        allPass = false;
    } else {
        spdlog::info("TEST PASS: top face excluded from replication");
    }

    RockFractureSettings settings;
    applyPaperTilePreset(settings);
    settings.mcResolution = 40;
    TileBuildResult tile = buildFractureTile(settings);
    if (tile.sdfRoot == nullptr) {
        spdlog::error("TEST FAIL: tile SDF root is null");
        allPass = false;
    } else {
        SDFReplicationField replication(tile.sdfRoot, spec, settings.tileSize);

        const rock_fracture::Vector3 pNegX(solid[0][0] + 1.0, solid[0][1] + 5.0, solid[0][2] + 5.0);
        const rock_fracture::Vector3 pNegXShift(
            solid[0][0] + 1.0, solid[0][1] + 5.0 + settings.tileSize, solid[0][2] + 5.0);
        const rock_fracture::Vector3 lNegX0 = worldToTileLocalForFace(pNegX, solid, CliffFace::NegX, settings.tileSize);
        const rock_fracture::Vector3 lNegX1 = worldToTileLocalForFace(pNegXShift, solid, CliffFace::NegX, settings.tileSize);
        if (std::abs(lNegX0[1] - lNegX1[1]) > 1e-4 || std::abs(lNegX0[2] - lNegX1[2]) > 1e-4) {
            spdlog::error("TEST FAIL: NegX tile-local Y/Z periodicity");
            allPass = false;
        } else {
            spdlog::info("TEST PASS: NegX tile-local periodicity");
        }

        const rock_fracture::Vector3 pNegY(solid[0][0] + 5.0, solid[0][1] + 1.0, solid[0][2] + 5.0);
        const rock_fracture::Vector3 pNegYShift(
            solid[0][0] + 5.0 + settings.tileSize, solid[0][1] + 1.0, solid[0][2] + 5.0);
        const rock_fracture::Vector3 lNegY0 = worldToTileLocalForFace(pNegY, solid, CliffFace::NegY, settings.tileSize);
        const rock_fracture::Vector3 lNegY1 = worldToTileLocalForFace(pNegYShift, solid, CliffFace::NegY, settings.tileSize);
        if (std::abs(lNegY0[0] - lNegY1[0]) > 1e-4 || std::abs(lNegY0[2] - lNegY1[2]) > 1e-4) {
            spdlog::error("TEST FAIL: NegY tile-local X/Z periodicity");
            allPass = false;
        } else {
            spdlog::info("TEST PASS: NegY tile-local periodicity");
        }

        SDFSceneComposed composedGap(&terrain, &replication, sceneBoundingBox(spec), spec, true);
        const rock_fracture::Vector3 corePoint = center;
        const double fCore = terrain.Signed(corePoint);
        const double feCore = composedGap.Signed(corePoint);
        if (fCore >= 0.0) {
            spdlog::error("TEST FAIL: corePoint should be inside cube, f={}", fCore);
            allPass = false;
        } else if (feCore > 0.0) {
            spdlog::error("TEST FAIL: fracture gap punched core hole fe={} (f={})", feCore, fCore);
            allPass = false;
        } else {
            spdlog::info("TEST PASS: no gap hole at cube core fe={:.4f} f={:.4f}", feCore, fCore);
        }

        const rock_fracture::Vector3 skinPoint(solid[0][0] + 0.08, center.y, center.z);
        const double fSkin = terrain.Signed(skinPoint);
        const double feSkin = composedGap.Signed(skinPoint);
        bool skinModified = std::abs(feSkin - fSkin) > 1e-3;
        if (!skinModified) {
            for (double dy = 1.0; dy < spec.cubeSize - 1.0; dy += 3.5) {
                for (double dz = 1.0; dz < spec.cubeSize - 1.0; dz += 3.5) {
                    const rock_fracture::Vector3 p(solid[0][0] + 0.05, solid[0][1] + dy, solid[0][2] + dz);
                    if (terrain.Signed(p) >= 0.0) {
                        continue;
                    }
                    if (std::abs(composedGap.Signed(p) - terrain.Signed(p)) > 1e-3) {
                        skinModified = true;
                        break;
                    }
                }
                if (skinModified) {
                    break;
                }
            }
        }
        if (!skinModified) {
            spdlog::error("TEST FAIL: no cliff skin SDF change on NegX wall scan");
            allPass = false;
        } else {
            spdlog::info("TEST PASS: cliff skin modified (sample fe={:.4f} f={:.4f})", feSkin, fSkin);
        }

        releaseTileSdf(tile.sdfRoot);
    }

    SDFSceneComposed composed(&terrain, nullptr, sceneBoundingBox(spec), spec, false);
    const double feCenter = composed.Signed(center);
    if (std::abs(feCenter - fInside) > 1e-4) {
        spdlog::error("TEST FAIL: cube interior fe should match f, fe={} f={}", feCenter, fInside);
        allPass = false;
    } else {
        spdlog::info("TEST PASS: cube interior fe matches f ({:.4f})", feCenter);
    }

    if (allPass) {
        spdlog::info("CliffSceneBuilder::runTestScenario: TEST PASS (all checks)");
    } else {
        spdlog::error("CliffSceneBuilder::runTestScenario: TEST FAIL");
    }
    return allPass;
}

bool CliffSceneBuilder::runCliffReplicationBuildTest() {
    spdlog::info("CliffSceneBuilder::runCliffReplicationBuildTest: start");

    RockFractureSettings settings;
    settings.mode = GenerationMode::CliffScene;
    settings.enableBlockReplication = true;
    settings.scene.replicationMode = CliffReplicationMode::AllVerticalFaces;
    settings.scene.mcResolution = 100;
    if (const char* mcEnv = std::getenv("CLIFF_MC_RES")) {
        settings.scene.mcResolution = std::atoi(mcEnv);
        spdlog::info("CliffSceneBuilder::runCliffReplicationBuildTest: MC res from env={}", settings.scene.mcResolution);
    }
    settings.useOpenMP = true;
    settings.useTextureWarp = true;
    applyPaperTilePreset(settings);
    if (const char* ompEnv = std::getenv("CLIFF_NO_OPENMP")) {
        (void)ompEnv;
        settings.useOpenMP = false;
        spdlog::info("CliffSceneBuilder::runCliffReplicationBuildTest: OpenMP disabled via CLIFF_NO_OPENMP");
    }

    TileLibrary tileLibrary;

    RockFractureSettings macroOnly = settings;
    macroOnly.enableBlockReplication = false;
    const CliffBuildResult macroResult = build(macroOnly, tileLibrary);
    if (macroResult.model.generationFailed || macroResult.model.triangleCount <= 0) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: macro-only baseline failed");
        return false;
    }

    const CliffBuildResult result = build(settings, tileLibrary);

    if (result.model.generationFailed) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: {}",
            result.model.failureMessage);
        return false;
    }
    if (result.model.triangleCount <= 0) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: empty mesh");
        return false;
    }

    if (result.model.triangleCount <= macroResult.model.triangleCount) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: cliff tris={} <= macro tris={}",
            result.model.triangleCount, macroResult.model.triangleCount);
        return false;
    }
    if (result.model.replicationMode != CliffReplicationMode::AllVerticalFaces) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: expected all-vertical mode");
        return false;
    }
    if (!result.model.enableBlockReplication) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: replication flag not set on model");
        return false;
    }

    SceneSpec spec;
    spec.cubeSize = 20.0f;
    spec.replicationMode = CliffReplicationMode::AllVerticalFaces;
    sanitizeSceneSpec(spec);
    SDFMacroBox terrain(spec);
    const rock_fracture::Box solid = terrain.solidBox();
    const rock_fracture::Vector3 center = solid.Center();
    RockFractureSettings tileSettings;
    applyPaperTilePreset(tileSettings);
    tileSettings.mcResolution = 40;
    TileBuildResult tile = buildFractureTile(tileSettings);
    if (tile.sdfRoot == nullptr) {
        spdlog::error("CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: tile null");
        return false;
    }
    SDFReplicationField replication(tile.sdfRoot, spec, tileSettings.tileSize);
    SDFSceneComposed composed(&terrain, &replication, sceneBoundingBox(spec), spec, true);
    bool skinModified = false;
    for (double dy = 1.0; dy < spec.cubeSize - 1.0; dy += 3.5) {
        for (double dz = 1.0; dz < spec.cubeSize - 1.0; dz += 3.5) {
            const rock_fracture::Vector3 p(solid[0][0] + 0.05, solid[0][1] + dy, solid[0][2] + dz);
            if (terrain.Signed(p) >= 0.0) {
                continue;
            }
            if (std::abs(composed.Signed(p) - terrain.Signed(p)) > 1e-3) {
                skinModified = true;
                break;
            }
        }
        if (skinModified) {
            break;
        }
    }
    releaseTileSdf(tile.sdfRoot);
    if (!skinModified) {
        spdlog::error(
            "CliffSceneBuilder::runCliffReplicationBuildTest: TEST FAIL: no wall skin SDF change on NegX scan");
        return false;
    }

    spdlog::info(
        "CliffSceneBuilder::runCliffReplicationBuildTest: TEST PASS tri={} macroTri={} v={} build={:.2f}s",
        result.model.triangleCount,
        macroResult.model.triangleCount,
        result.model.vertexCount,
        result.model.buildSeconds);
    return true;
}

} // namespace render_playground
