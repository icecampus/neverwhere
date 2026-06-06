#pragma once

#include "RenderTypes.h"
#include "rock_fracture/Blocks.h"
#include "rock_scene/SceneSpec.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace render_playground {

class TileLibrary;

enum class RockFractureKind : int {
    Equidimensional = 0,
    Rhombohedral    = 1,
    Polyhedral      = 2,
    Tabular         = 3,
};

enum class GenerationMode : int {
    SingleTile = 0,
    CliffScene = 1,
};

struct RockFractureSettings {
    GenerationMode mode = GenerationMode::CliffScene;
    RockFractureKind kind = RockFractureKind::Equidimensional;
    int seed = 1234;
    float tileSize = 20.0f;
    float poissonRadius = 0.5f;
    int poissonTries = 10000;
    float fractureInflate = 3.0f;
    int mcResolution = 100;
    double blockSmoothingRadius = 0.25;
    double bvhTransitionRadius = 0.5;
    bool useOpenMP = true;
    bool useTextureWarp = true;
    bool enableBlockReplication = true;
    SceneSpec scene;
};

struct RockFractureModel {
    std::vector<Vec3> samples;
    std::vector<Vec3> fractureCenters;
    std::vector<Vec3> clusterCenters;
    std::vector<Vec3> meshVertices;
    std::vector<Vec3> meshNormals;
    std::vector<std::uint32_t> meshIndices;
    int sampleCount = 0;
    int fractureCount = 0;
    int clusterCount = 0;
    int triangleCount = 0;
    int vertexCount = 0;
    double buildSeconds = 0.0;
    double fieldMin = 0.0;
    double fieldMax = 0.0;
    bool usedOpenMP = false;
    bool usedTextureWarp = false;
    bool usedFallbackTexture = false;
    bool generationFailed = false;
    std::string failureMessage;
    GenerationMode generationMode = GenerationMode::SingleTile;
    bool enableBlockReplication = true;
    Vec3 boundsMin{0.0f, 0.0f, 0.0f};
    Vec3 boundsMax{20.0f, 20.0f, 20.0f};
    CliffFace cliffFace = CliffFace::NegX;
    CliffReplicationMode replicationMode = CliffReplicationMode::AllVerticalFaces;
    float cliffInset = 2.0f;
    float plateauHeight = 12.0f;
};

class RockFractureScene {
public:
    RockFractureScene();
    ~RockFractureScene();

    RockFractureScene(const RockFractureScene&) = delete;
    RockFractureScene& operator=(const RockFractureScene&) = delete;

    void rebuild(const RockFractureSettings& settings);
    void requestAsyncRebuild(const RockFractureSettings& settings);

    bool isBuilding() const { return m_isBuilding.load(); }
    double buildElapsedSeconds() const;
    const char* buildStage() const;

    template<typename Fn>
    void withModel(Fn&& fn) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        fn(m_model);
    }

    std::uint64_t modelRevision() const { return m_modelRevision.load(std::memory_order_acquire); }
    RockFractureKind kind() const { return m_pendingKind; }
    int seed() const { return m_pendingSeed; }

    static int kindCount();
    static const char* kindName(RockFractureKind kind);
    static const char* modeName(GenerationMode mode);
    static void sanitize(RockFractureSettings& settings);

private:
    void rebuildSingleTile(const RockFractureSettings& settings);
    void rebuildCliffScene(const RockFractureSettings& settings);

    mutable std::mutex m_mutex;
    RockFractureModel m_model;
    std::unique_ptr<TileLibrary> m_tileLibrary;
    std::atomic<std::uint64_t> m_modelRevision{0};
    RockFractureKind m_pendingKind = RockFractureKind::Equidimensional;
    int m_pendingSeed = 1234;
    std::atomic<bool> m_isBuilding{false};
    std::atomic<int64_t> m_buildStartSteadyNs{0};
    std::atomic<double> m_buildFinalSeconds{0.0};
    std::atomic<int> m_buildStage{0};
    std::thread m_worker;
};

} // namespace render_playground
