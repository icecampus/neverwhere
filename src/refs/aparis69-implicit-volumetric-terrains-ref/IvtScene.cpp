#include "IvtScene.h"

#include "ivt_scenes.h"
#include "ttree.h"

#include <chrono>
#include <cmath>

namespace ivt_view {
namespace {

void fillBounds(IvtModel& model) {
    if (model.meshVertices.empty()) {
        return;
    }
    model.boundsMin = model.meshVertices.front();
    model.boundsMax = model.meshVertices.front();
    for (const Vec3& v : model.meshVertices) {
        model.boundsMin.x = std::min(model.boundsMin.x, v.x);
        model.boundsMin.y = std::min(model.boundsMin.y, v.y);
        model.boundsMin.z = std::min(model.boundsMin.z, v.z);
        model.boundsMax.x = std::max(model.boundsMax.x, v.x);
        model.boundsMax.y = std::max(model.boundsMax.y, v.y);
        model.boundsMax.z = std::max(model.boundsMax.z, v.z);
    }
}

void fillModelFromMesh(IvtModel& model, const MarchingCubeMesh& mesh) {
    const int vertexCount = (int)mesh.positions.size() / 3;
    model.meshVertices.resize((std::size_t)vertexCount);
    model.meshNormals.resize((std::size_t)vertexCount);
    for (int i = 0; i < vertexCount; i++) {
        model.meshVertices[(std::size_t)i] = {
            mesh.positions[(std::size_t)i * 3 + 0],
            mesh.positions[(std::size_t)i * 3 + 1],
            mesh.positions[(std::size_t)i * 3 + 2],
        };
        model.meshNormals[(std::size_t)i] = {
            mesh.normals[(std::size_t)i * 3 + 0],
            mesh.normals[(std::size_t)i * 3 + 1],
            mesh.normals[(std::size_t)i * 3 + 2],
        };
    }
    model.meshIndices = mesh.indices;
    model.vertexCount = vertexCount;
    model.triangleCount = (int)mesh.indices.size() / 3;
    fillBounds(model);
}

} // namespace

IvtScene::IvtScene() = default;

IvtScene::~IvtScene() {
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

int IvtScene::sceneCount() {
    return 3;
}

const char* IvtScene::sceneName(IvtSceneKind kind) {
    switch (kind) {
    case IvtSceneKind::Island: return "Floating islands";
    case IvtSceneKind::Sea: return "Sea erosion";
    case IvtSceneKind::Karst: return "Karst invasion-percolation";
    }
    return "Floating islands";
}

int IvtScene::defaultMcResolution(IvtSceneKind kind) {
    switch (kind) {
    case IvtSceneKind::Island: return 80;
    case IvtSceneKind::Sea: return 160;
    case IvtSceneKind::Karst: return 120;
    }
    return 80;
}

void IvtScene::sanitize(IvtSettings& settings) {
    const int kindIdx = (int)settings.scene;
    if (kindIdx < 0 || kindIdx > 2) {
        settings.scene = IvtSceneKind::Island;
    }
    settings.mcResolution = clampInt(settings.mcResolution, 24, 350);
}

const char* IvtScene::buildStage() const {
    switch (m_buildStage.load()) {
    case 1: return "building terrain tree";
    case 2: return "marching cubes";
    default: return "starting";
    }
}

double IvtScene::buildElapsedSeconds() const {
    if (!m_isBuilding.load()) {
        return m_buildFinalSeconds.load();
    }
    const int64_t startNs = m_buildStartSteadyNs.load();
    if (startNs == 0) {
        return 0.0;
    }
    const int64_t nowNs = (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double sec = double(nowNs - startNs) / 1e9;
    return sec < 0.0 ? 0.0 : sec;
}

void IvtScene::requestAsyncRebuild(const IvtSettings& settings) {
    if (m_worker.joinable()) {
        m_worker.join();
    }

    IvtSettings local = settings;
    sanitize(local);

    m_isBuilding.store(true);
    m_buildStage.store(0);
    m_buildStartSteadyNs.store((int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());

    m_worker = std::thread([this, local]() {
        IvtModel built;
        built.sceneKind = local.scene;
        built.mcResolution = local.mcResolution;

        const auto t0 = std::chrono::steady_clock::now();
        std::srand((unsigned)local.seed);

        try {
            m_buildStage.store(1);
            TTree* tree = nullptr;
            switch (local.scene) {
            case IvtSceneKind::Sea:
                tree = BuildSeaTerrainTree();
                break;
            case IvtSceneKind::Karst:
                tree = BuildKarstTerrainTree();
                break;
            case IvtSceneKind::Island:
            default:
                tree = BuildIslandTerrainTree();
                break;
            }

            m_buildStage.store(2);
            MarchingCubeMesh mesh;
            marching_cube_polygonize(tree, local.mcResolution, mesh);
            delete tree;

            fillModelFromMesh(built, mesh);
            if (built.triangleCount <= 0) {
                built.generationFailed = true;
                built.failureMessage = "Marching cubes produced an empty mesh";
            }
        } catch (const std::exception& ex) {
            built.generationFailed = true;
            built.failureMessage = ex.what();
        } catch (...) {
            built.generationFailed = true;
            built.failureMessage = "Unknown error during terrain build";
        }

        const auto t1 = std::chrono::steady_clock::now();
        built.buildSeconds = std::chrono::duration<double>(t1 - t0).count();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_model = std::move(built);
        }
        m_modelRevision.fetch_add(1, std::memory_order_release);
        m_buildFinalSeconds.store(built.buildSeconds);
        m_isBuilding.store(false);
        m_buildStage.store(0);
    });
}

} // namespace ivt_view
