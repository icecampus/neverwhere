#pragma once

#include "RenderTypes.h"
#include "rock_fracture/Blocks.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace render_playground {

enum class RockFractureKind : int {
    Equidimensional = 0,
    Rhombohedral    = 1,
    Polyhedral      = 2,
    Tabular         = 3,
};

struct RockFractureSettings {
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
};

class RockFractureScene {
public:
    RockFractureScene();
    ~RockFractureScene();

    RockFractureScene(const RockFractureScene&) = delete;
    RockFractureScene& operator=(const RockFractureScene&) = delete;

    // Synchronous rebuild (blocks the calling thread).
    void rebuild(const RockFractureSettings& settings);

    // Asynchronous rebuild: spawns a worker thread that calls rebuild().
    // Old worker (if any) is left to finish; the latest completed result wins.
    void requestAsyncRebuild(const RockFractureSettings& settings);

    bool isBuilding() const { return m_isBuilding.load(); }
    double buildElapsedSeconds() const;
    const char* buildStage() const;

    const RockFractureModel& model() const { return m_model; }
    RockFractureKind kind() const { return m_pendingKind; }
    int seed() const { return m_pendingSeed; }

    static int kindCount();
    static const char* kindName(RockFractureKind kind);
    static void sanitize(RockFractureSettings& settings);

private:
    std::mutex m_mutex;
    RockFractureModel m_model;
    RockFractureKind m_pendingKind = RockFractureKind::Equidimensional;
    int m_pendingSeed = 1234;
    std::atomic<bool> m_isBuilding{false};
    std::atomic<int64_t> m_buildStartSteadyNs{0};
    std::atomic<double> m_buildFinalSeconds{0.0};
    std::atomic<int> m_buildStage{0}; // 0..4, mapped to label by buildStage()
    std::thread m_worker;
};

} // namespace render_playground
