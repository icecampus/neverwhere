#include "RockFractureScene.h"

#include "rock_scene/CliffSceneBuilder.h"
#include "rock_scene/TileLibrary.h"
#include "TileBuild.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
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

RockFractureScene::RockFractureScene()
    : m_tileLibrary(std::make_unique<TileLibrary>()) {}

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
    case 5: return "building tile cache";
    case 6: return "composing scene field";
    case 7: return "marching cubes (scene)";
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

const char* RockFractureScene::modeName(GenerationMode mode) {
    switch (mode) {
    case GenerationMode::SingleTile: return "Single tile";
    case GenerationMode::CliffScene: return "Cliff scene";
    }
    return "Single tile";
}

void RockFractureScene::sanitize(RockFractureSettings& settings) {
    const int modeIdx = (int)settings.mode;
    if (modeIdx < 0 || modeIdx > 1) {
        settings.mode = GenerationMode::SingleTile;
    }

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
    sanitizeSceneSpec(settings.scene);
}

void RockFractureScene::rebuildSingleTile(const RockFractureSettings& settings) {
    RockFractureModel model;
    model.generationMode = GenerationMode::SingleTile;
    model.boundsMin = {-settings.tileSize * 0.5f, -settings.tileSize * 0.5f, -settings.tileSize * 0.5f};
    model.boundsMax = {settings.tileSize * 0.5f, settings.tileSize * 0.5f, settings.tileSize * 0.5f};

    if (settings.useOpenMP) {
#ifdef ROCK_FRACTURE_USE_OPENMP
        model.usedOpenMP = true;
#endif
    }

    m_buildStage.store(1);
    TileBuildResult tile = buildFractureTile(settings);
    model.usedTextureWarp = tile.usedTextureWarp;
    model.usedFallbackTexture = tile.usedFallbackTexture;

    m_buildStage.store(2);
    m_buildStage.store(3);
    m_buildStage.store(4);

    rock_fracture::MC::mcMesh mesh;
    if (tile.sdfRoot != nullptr) {
        mesh = rock_fracture::PolygonizeSDF(tile.sdfRoot->box, tile.sdfRoot, settings.mcResolution);
    }

    model.samples.reserve(tile.samples.Size());
    for (int i = 0; i < tile.samples.Size(); i++) {
        model.samples.push_back(toVec3(tile.samples.At(i)));
    }
    model.sampleCount = (int)model.samples.size();

    model.fractureCenters.reserve(tile.fractures.Size());
    for (int i = 0; i < tile.fractures.Size(); i++) {
        model.fractureCenters.push_back(toVec3(tile.fractures.At(i).Center()));
    }
    model.fractureCount = (int)model.fractureCenters.size();

    model.clusterCenters.reserve(tile.clusters.size());
    for (const rock_fracture::BlockCluster& cluster : tile.clusters) {
        rock_fracture::Vector3 sum(0);
        for (const rock_fracture::Vector3& p : cluster.pts) sum += p;
        const double inv = cluster.pts.empty() ? 0.0 : 1.0 / double(cluster.pts.size());
        model.clusterCenters.push_back(toVec3(sum * inv));
    }
    model.clusterCount = (int)model.clusterCenters.size();

    fillMeshFromMc(model, mesh);
    scanFieldRange(model, tile.sdfRoot, settings.mcResolution);
    releaseTileSdf(tile.sdfRoot);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_model = std::move(model);
    m_modelRevision.fetch_add(1, std::memory_order_release);
}

void RockFractureScene::rebuildCliffScene(const RockFractureSettings& settings) {
    m_buildStage.store(5);
    if (settings.enableBlockReplication) {
        m_buildStage.store(5);
    }
    m_buildStage.store(6);
    CliffBuildResult built = CliffSceneBuilder::build(settings, *m_tileLibrary);
    m_buildStage.store(7);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_model = std::move(built.model);
    m_modelRevision.fetch_add(1, std::memory_order_release);
}

void RockFractureScene::rebuild(const RockFractureSettings& settings) {
    spdlog::info("rebuildRockFractureScene: start mode={}", modeName(settings.mode));

    RockFractureSettings sanitized = settings;
    sanitize(sanitized);
    m_pendingKind = sanitized.kind;
    m_pendingSeed = sanitized.seed;

    const auto t0 = std::chrono::steady_clock::now();
    m_buildStartSteadyNs.store((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t0.time_since_epoch()).count());
    m_buildFinalSeconds.store(0.0);
    m_buildStage.store(0);

    try {
        if (sanitized.mode == GenerationMode::CliffScene) {
            rebuildCliffScene(sanitized);
        } else {
            rebuildSingleTile(sanitized);
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_model.generationFailed = true;
        m_model.failureMessage = e.what();
        spdlog::error("rebuildRockFractureScene: exception: {}", e.what());
    } catch (...) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_model.generationFailed = true;
        m_model.failureMessage = "unknown exception";
        spdlog::error("rebuildRockFractureScene: unknown exception");
    }

    const auto t1 = std::chrono::steady_clock::now();
    const double buildSec = std::chrono::duration<double>(t1 - t0).count();
    m_buildFinalSeconds.store(buildSec);
    m_buildStage.store(0);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_model.buildSeconds = buildSec;
    }

    int triCount = 0;
    bool failed = false;
    std::string failMsg;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        triCount = m_model.triangleCount;
        failed = m_model.generationFailed;
        failMsg = m_model.failureMessage;
    }

    spdlog::info(
        "rebuildRockFractureScene: done mode={} tri={} build={:.2f}s failed={}",
        modeName(sanitized.mode),
        triCount,
        buildSec,
        failed ? failMsg : std::string("no"));
}

void RockFractureScene::requestAsyncRebuild(const RockFractureSettings& settings) {
    if (m_isBuilding.load()) {
        spdlog::warn("requestAsyncRebuild: rebuild already in progress, ignoring");
        return;
    }
    m_isBuilding.store(true);
    m_buildStage.store(0);
    m_buildFinalSeconds.store(0.0);
    m_buildStartSteadyNs.store(0);

    if (m_worker.joinable()) {
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
