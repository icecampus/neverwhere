#include "RockFractureScene.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <thread>

#include <spdlog/spdlog.h>

namespace render_playground {

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
    const fs::path relative = fs::path("src") / "apps" / "CliffsGenerationPlayground" / "resources" / "textures" / "rock1.png";

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

} // namespace

RockFractureScene::RockFractureScene() = default;

RockFractureScene::~RockFractureScene() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

const char* RockFractureScene::buildStage() const {
    switch (m_buildStage.load()) {
    case 1: return "Poisson sampling";
    case 2: return "generating fractures";
    case 3: return "clustering";
    case 4: return "marching cubes";
    default: return "starting";
    }
}

double RockFractureScene::buildElapsedSeconds() const {
    if (!m_isBuilding.load()) {
        return m_buildFinalSeconds.load();
    }
    const int64_t startNs = m_buildStartSteadyNs.load();
    if (startNs == 0) return 0.0;
    const int64_t nowNs = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double sec = double(nowNs - startNs) / 1e9;
    return sec < 0.0 ? 0.0 : sec;
}

int RockFractureScene::kindCount() {
    return kKindCount;
}

const char* RockFractureScene::kindName(RockFractureKind kind) {
    const int idx = (int)kind;
    if (idx < 0 || idx >= kKindCount) return kKindNames[0];
    return kKindNames[idx];
}

void RockFractureScene::sanitize(RockFractureSettings& settings) {
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

void RockFractureScene::rebuild(const RockFractureSettings& settings) {
    spdlog::info("rebuildRockFractureScene: start");

    RockFractureSettings sanitized = settings;
    sanitize(sanitized);
    m_pendingKind = sanitized.kind;
    m_pendingSeed = sanitized.seed;

    RockFractureModel model;
    const auto t0 = std::chrono::steady_clock::now();
    m_buildStartSteadyNs.store((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count());
    m_buildFinalSeconds.store(0.0);
    m_buildStage.store(0);

    try {
        std::srand(sanitized.seed);
        spdlog::info("rebuildRockFractureScene: seed={}, kind={}, tileSize={}, poisson(r={}, tries={}), mcRes={}, useOpenMP={}, useTexture={}",
            sanitized.seed, kindName(sanitized.kind), sanitized.tileSize,
            sanitized.poissonRadius, sanitized.poissonTries, sanitized.mcResolution,
            sanitized.useOpenMP, sanitized.useTextureWarp);

        if (sanitized.useOpenMP) {
#ifdef ROCK_FRACTURE_USE_OPENMP
            model.usedOpenMP = true;
#else
            model.usedOpenMP = false;
#endif
        }

        if (sanitized.useTextureWarp) {
            tryLoadWarpingTexture(model);
        } else {
            rock_fracture::GenerateProceduralWarpingField(6.0, 4, sanitized.seed);
            model.usedFallbackTexture = true;
        }

        const float halfTile = sanitized.tileSize * 0.5f;
        rock_fracture::Box tile(rock_fracture::Vector3(0), halfTile);

        m_buildStage.store(1);
        spdlog::info("rebuildRockFractureScene: stage 1/4 Poisson sampling");
        rock_fracture::PointSet3 samples = rock_fracture::PoissonSamplingBox(tile, sanitized.poissonRadius, sanitized.poissonTries);
        spdlog::info("rebuildRockFractureScene: samples={}", samples.Size());

        m_buildStage.store(2);
        spdlog::info("rebuildRockFractureScene: stage 2/4 generate fractures");
        rock_fracture::FractureSet fractures = rock_fracture::GenerateFractures(toRockType(sanitized.kind), tile, sanitized.fractureInflate);
        spdlog::info("rebuildRockFractureScene: fractures={}", fractures.Size());

        m_buildStage.store(3);
        spdlog::info("rebuildRockFractureScene: stage 3/4 cluster");
        std::vector<rock_fracture::BlockCluster> clusters = rock_fracture::ComputeBlockClusters(samples, fractures);
        spdlog::info("rebuildRockFractureScene: clusters={}", clusters.size());

        m_buildStage.store(4);
        spdlog::info("rebuildRockFractureScene: stage 4/4 implicit SDF + marching cubes (res={})", sanitized.mcResolution);
        rock_fracture::SDFNode* sdfRoot = rock_fracture::ComputeBlockSDF(clusters);
        if (sdfRoot == nullptr) {
            spdlog::warn("rebuildRockFractureScene: ComputeBlockSDF returned null (no closed convex blocks)");
        }

        rock_fracture::MC::mcMesh mesh;
        if (sdfRoot != nullptr) {
            mesh = rock_fracture::PolygonizeSDF(sdfRoot->box, sdfRoot, sanitized.mcResolution);
            spdlog::info("rebuildRockFractureScene: MC vertices={}, normals={}, indices={} ({} triangles)",
                mesh.vertices.size(), mesh.normals.size(), mesh.indices.size(), mesh.indices.size() / 3);
        }

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

        if (sdfRoot != nullptr) {
            const int scanRes = std::min(32, sanitized.mcResolution);
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
        spdlog::error("rebuildRockFractureScene: exception: {}", e.what());
    } catch (...) {
        model.generationFailed = true;
        model.failureMessage = "unknown exception";
        spdlog::error("rebuildRockFractureScene: unknown exception");
    }

    const auto t1 = std::chrono::steady_clock::now();
    model.buildSeconds = std::chrono::duration<double>(t1 - t0).count();
    m_buildFinalSeconds.store(model.buildSeconds);
    m_buildStage.store(0);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_model = std::move(model);
        m_modelRevision.fetch_add(1, std::memory_order_release);
    }

    spdlog::info(
        "rebuildRockFractureScene: done, samples={}, fractures={}, clusters={}, vertices={}, triangles={}, field=[{:.3f}, {:.3f}], build={:.3f}s, openMP={}",
        m_model.sampleCount, m_model.fractureCount, m_model.clusterCount,
        m_model.vertexCount, m_model.triangleCount, m_model.fieldMin, m_model.fieldMax,
        m_model.buildSeconds, m_model.usedOpenMP ? "yes" : "no");
    spdlog::info(
        "rebuildRockFractureScene: warp={}, failed={}",
        std::string(m_model.usedTextureWarp ? (m_model.usedFallbackTexture ? "perlin" : "rock1.png") : "disabled"),
        m_model.generationFailed ? (std::string("yes: ") + m_model.failureMessage) : std::string("no"));
}

void RockFractureScene::requestAsyncRebuild(const RockFractureSettings& settings) {
    if (m_isBuilding.load()) {
        spdlog::warn("requestAsyncRebuild: rebuild already in progress, ignoring (latest result will win)");
        return;
    }
    m_isBuilding.store(true);
    m_buildStage.store(0);
    m_buildFinalSeconds.store(0.0);
    m_buildStartSteadyNs.store(0); // will be set inside rebuild()

    if (m_worker.joinable()) {
        // The previous worker should have finished (we only set m_isBuilding=false on completion
        // after rebuilding), but be safe in case the worker was abandoned due to an exception
        // path: detach it so we don't block on join. The mutex on m_model keeps state consistent.
        m_worker.join();
    }

    m_worker = std::thread([this, settings]() {
        try {
            this->rebuild(settings);
        } catch (...) {
            spdlog::error("requestAsyncRebuild: worker thread caught an exception");
        }
        m_isBuilding.store(false);
    });
}

} // namespace render_playground
