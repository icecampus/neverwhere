#include "RockFractureScenario.h"

#include "PlaygroundState.h"
#include "rock_fracture/Blocks.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <vector>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr int kKindCount = 4;

const char* kKindNames[kKindCount] = {
    "Equidimensional",
    "Rhombohedral",
    "Polyhedral",
    "Tabular",
};

rock_fracture::FractureType toRockType(RockFractureKind kind) {
    switch (kind) {
    case RockFractureKind::Equidimensional: return rock_fracture::Equidimensional;
    case RockFractureKind::Rhombohedral:    return rock_fracture::Rhombohedral;
    case RockFractureKind::Polyhedral:      return rock_fracture::Polyhedral;
    case RockFractureKind::Tabular:         return rock_fracture::Tabular;
    }
    return rock_fracture::Equidimensional;
}

Vec3 toVec3(const rock_fracture::Vector3& v) {
    return Vec3{(float)v.x, (float)v.y, (float)v.z};
}

bool fileExists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec) && !ec;
}

bool tryLoadWarpingTexture(RockFractureModel& model) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("src") / "apps" / "MeshGenerationPlayground" / "resources" / "textures" / "rock1.png";

    fs::path dir = fs::current_path();
    std::error_code ec;
    for (int i = 0; i < 16; i++) {
        const fs::path candidate = dir / relative;
        if (fileExists(candidate)) {
            const std::string pathStr = candidate.string();
            rock_fracture::LoadImageFileForWarping(pathStr.c_str(), 0.0, 1.0);
            spdlog::info("RockFracture: loaded warping texture from {}", pathStr);
            model.usedTextureWarp = true;
            return true;
        }
        fs::path parent = dir.parent_path();
        if (parent.empty() || parent == dir) break;
        dir = parent;
    }
    spdlog::warn("RockFracture: rock1.png not found, using Perlin-procedural warping field");
    rock_fracture::GenerateProceduralWarpingField(6.0, 4, 42);
    model.usedFallbackTexture = true;
    return false;
}

double triangleBarycenterY(const Vec3& a, const Vec3& b, const Vec3& c) {
    return (a.y + b.y + c.y) / 3.0f;
}

} // namespace

int rockFractureKindCount() {
    return kKindCount;
}

const char* rockFractureKindName(RockFractureKind kind) {
    const int idx = (int)kind;
    if (idx < 0 || idx >= kKindCount) return kKindNames[0];
    return kKindNames[idx];
}

void sanitizeSettings(RockFractureSettings& settings) {
    int idx = (int)settings.kind;
    if (idx < 0 || idx >= kKindCount) idx = 0;
    settings.kind = (RockFractureKind)idx;
    settings.tileSize = clampFloat(settings.tileSize, 2.0f, 80.0f);
    settings.poissonRadius = clampFloat(settings.poissonRadius, 0.05f, 5.0f);
    settings.poissonTries = clampInt(settings.poissonTries, 100, 100000);
    settings.fractureInflate = clampFloat(settings.fractureInflate, 0.0f, 12.0f);
    settings.mcResolution = clampInt(settings.mcResolution, 16, 240);
    settings.blockSmoothingRadius = clampFloat((float)settings.blockSmoothingRadius, 0.01f, 2.0f);
    settings.bvhTransitionRadius = clampFloat((float)settings.bvhTransitionRadius, 0.05f, 4.0f);
}

void rebuildRockFractureModel() {
    spdlog::info("rebuildRockFractureModel: start");

    RockFractureSettings settings = g_rockSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rockSettings = settings;
    }

    RockFractureModel model;
    const auto t0 = std::chrono::steady_clock::now();

    try {
        std::srand(settings.seed);
        spdlog::info("rebuildRockFractureModel: seed={}, kind={}, tileSize={}, poisson(r={}, tries={}), mcRes={}, useOpenMP={}, useTexture={}",
            settings.seed, rockFractureKindName(settings.kind), settings.tileSize,
            settings.poissonRadius, settings.poissonTries, settings.mcResolution,
            settings.useOpenMP, settings.useTextureWarp);

        if (settings.useOpenMP) {
#ifdef ROCK_FRACTURE_USE_OPENMP
            model.usedOpenMP = true;
#else
            model.usedOpenMP = false;
#endif
        }

        if (settings.useTextureWarp) {
            tryLoadWarpingTexture(model);
        } else {
            rock_fracture::GenerateProceduralWarpingField(6.0, 4, settings.seed);
            model.usedFallbackTexture = true;
        }

        const float halfTile = settings.tileSize * 0.5f;
        rock_fracture::Box tile(rock_fracture::Vector3(0), halfTile);

        spdlog::info("rebuildRockFractureModel: stage 1/4 Poisson sampling");
        rock_fracture::PointSet3 samples = rock_fracture::PoissonSamplingBox(tile, settings.poissonRadius, settings.poissonTries);
        spdlog::info("rebuildRockFractureModel: samples={}", samples.Size());

        spdlog::info("rebuildRockFractureModel: stage 2/4 generate fractures");
        rock_fracture::FractureSet fractures = rock_fracture::GenerateFractures(toRockType(settings.kind), tile, settings.fractureInflate);
        spdlog::info("rebuildRockFractureModel: fractures={}", fractures.Size());

        spdlog::info("rebuildRockFractureModel: stage 3/4 cluster");
        std::vector<rock_fracture::BlockCluster> clusters = rock_fracture::ComputeBlockClusters(samples, fractures);
        spdlog::info("rebuildRockFractureModel: clusters={}", clusters.size());

        spdlog::info("rebuildRockFractureModel: stage 4/4 implicit SDF + marching cubes (res={})", settings.mcResolution);
        rock_fracture::SDFNode* sdfRoot = rock_fracture::ComputeBlockSDF(clusters);
        if (sdfRoot == nullptr) {
            spdlog::warn("rebuildRockFractureModel: ComputeBlockSDF returned null (no closed convex blocks)");
        }

        rock_fracture::MC::mcMesh mesh;
        if (sdfRoot != nullptr) {
            mesh = rock_fracture::PolygonizeSDF(sdfRoot->box, sdfRoot, settings.mcResolution);
            spdlog::info("rebuildRockFractureModel: MC vertices={}, normals={}, indices={} ({} triangles)",
                mesh.vertices.size(), mesh.normals.size(), mesh.indices.size(), mesh.indices.size() / 3);
        }

        // Pack data for UI consumption.
        model.samples.reserve(samples.Size());
        for (int i = 0; i < samples.Size(); i++) {
            model.samples.push_back(toVec3(samples.At(i)));
        }
        model.sampleCount = (int)model.samples.size();

        model.fractureCenters.reserve(fractures.Size());
        for (int i = 0; i < fractures.Size(); i++) {
            const rock_fracture::Circle& c = fractures.At(i);
            model.fractureCenters.push_back(toVec3(c.Center()));
        }
        model.fractureCount = (int)model.fractureCenters.size();

        model.clusterCenters.reserve(clusters.size());
        for (const rock_fracture::BlockCluster& cluster : clusters) {
            rock_fracture::Vector3 sum(0);
            for (const rock_fracture::Vector3& p : cluster.pts) sum += p;
            const double inv = cluster.pts.empty() ? 0.0 : 1.0 / double(cluster.pts.size());
            model.clusterCenters.push_back(toVec3(sum * inv));
        }
        model.clusterCount = (int)model.clusterCenters.size();

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

        // Field min/max scan (rough, sampled at low res for stats).
        if (sdfRoot != nullptr) {
            const int scanRes = std::min(32, settings.mcResolution);
            rock_fracture::Box b = sdfRoot->box;
            rock_fracture::Vector3 d = b.Diagonal() / double(scanRes - 1);
            double fmin = 1e30, fmax = -1e30;
            for (int i = 0; i < scanRes; i++) {
                for (int j = 0; j < scanRes; j++) {
                    for (int k = 0; k < scanRes; k++) {
                        rock_fracture::Vector3 p(
                            b[0][0] + i * d[0],
                            b[0][1] + j * d[1],
                            b[0][2] + k * d[2]);
                        const double s = sdfRoot->Signed(p);
                        if (s < fmin) fmin = s;
                        if (s > fmax) fmax = s;
                    }
                }
            }
            model.fieldMin = fmin;
            model.fieldMax = fmax;
        }

        delete sdfRoot;
    } catch (const std::exception& e) {
        model.generationFailed = true;
        model.failureMessage = e.what();
        spdlog::error("rebuildRockFractureModel: exception: {}", e.what());
    } catch (...) {
        model.generationFailed = true;
        model.failureMessage = "unknown exception";
        spdlog::error("rebuildRockFractureModel: unknown exception");
    }

    const auto t1 = std::chrono::steady_clock::now();
    model.buildSeconds = std::chrono::duration<double>(t1 - t0).count();

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_rockModel = std::move(model);
    }

    spdlog::info(
        "rebuildRockFractureModel: done, samples={}, fractures={}, clusters={}, vertices={}, triangles={}, field=[{:.3f}, {:.3f}], build={:.3f}s, openMP={}",
        g_rockModel.sampleCount,
        g_rockModel.fractureCount,
        g_rockModel.clusterCount,
        g_rockModel.vertexCount,
        g_rockModel.triangleCount,
        g_rockModel.fieldMin,
        g_rockModel.fieldMax,
        g_rockModel.buildSeconds,
        g_rockModel.usedOpenMP ? "yes" : "no");
    spdlog::info(
        "rebuildRockFractureModel: warp={}, failed={}",
        std::string(g_rockModel.usedTextureWarp ? (g_rockModel.usedFallbackTexture ? "perlin" : "rock1.png") : "disabled"),
        g_rockModel.generationFailed ? (std::string("yes: ") + g_rockModel.failureMessage) : std::string("no"));
}

bool rockFractureModelValid() {
    std::lock_guard<std::mutex> lock(g_modelMutex);
    return !g_rockModel.generationFailed && g_rockModel.triangleCount > 0 && g_rockModel.vertexCount > 0;
}

} // namespace meshgen_playground
