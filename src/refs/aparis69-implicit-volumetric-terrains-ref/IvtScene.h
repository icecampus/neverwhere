#pragma once

#include "RenderTypes.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ivt_view {

enum class IvtSceneKind : int {
    Island = 0,
    Sea = 1,
    Karst = 2,
};

struct IvtSettings {
    IvtSceneKind scene = IvtSceneKind::Island;
    int mcResolution = 80;
    int seed = 1234;
};

struct IvtModel {
    std::vector<Vec3> meshVertices;
    std::vector<Vec3> meshNormals;
    std::vector<std::uint32_t> meshIndices;
    int vertexCount = 0;
    int triangleCount = 0;
    double buildSeconds = 0.0;
    bool generationFailed = false;
    std::string failureMessage;
    IvtSceneKind sceneKind = IvtSceneKind::Island;
    int mcResolution = 0;
    Vec3 boundsMin{};
    Vec3 boundsMax{};
};

class IvtScene {
public:
    IvtScene();
    ~IvtScene();

    IvtScene(const IvtScene&) = delete;
    IvtScene& operator=(const IvtScene&) = delete;

    void requestAsyncRebuild(const IvtSettings& settings);

    bool isBuilding() const { return m_isBuilding.load(); }
    double buildElapsedSeconds() const;
    const char* buildStage() const;

    template<typename Fn>
    void withModel(Fn&& fn) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        fn(m_model);
    }

    std::uint64_t modelRevision() const { return m_modelRevision.load(std::memory_order_acquire); }

    static int sceneCount();
    static const char* sceneName(IvtSceneKind kind);
    static int defaultMcResolution(IvtSceneKind kind);
    static void sanitize(IvtSettings& settings);

private:
    mutable std::mutex m_mutex;
    IvtModel m_model;
    std::atomic<std::uint64_t> m_modelRevision{0};
    std::atomic<bool> m_isBuilding{false};
    std::atomic<int64_t> m_buildStartSteadyNs{0};
    std::atomic<double> m_buildFinalSeconds{0.0};
    std::atomic<int> m_buildStage{0};
    std::thread m_worker;
};

} // namespace ivt_view
