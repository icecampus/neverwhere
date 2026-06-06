#include "TileLibrary.h"
#include "RockFractureScene.h"
#include "TileBuild.h"

#include <chrono>
#include <sstream>

#include <spdlog/spdlog.h>

namespace render_playground {

TileLibrary::~TileLibrary() {
    if (m_hasEntry) {
        releaseTileSdf(m_cachedEntry.build.sdfRoot);
        m_cachedEntry = {};
        m_hasEntry = false;
    }
}

bool TileCacheKey::operator==(const TileCacheKey& other) const {
    return kind == other.kind
        && seed == other.seed
        && tileSize == other.tileSize
        && poissonRadius == other.poissonRadius
        && poissonTries == other.poissonTries
        && blockSmoothingRadius == other.blockSmoothingRadius
        && bvhTransitionRadius == other.bvhTransitionRadius
        && useTextureWarp == other.useTextureWarp;
}

std::string tileCacheKeyString(const TileCacheKey& key) {
    std::ostringstream oss;
    oss << "kind=" << (int)key.kind
        << " seed=" << key.seed
        << " tile=" << key.tileSize
        << " poisson=" << key.poissonRadius << "/" << key.poissonTries;
    return oss.str();
}

TileCacheEntry TileLibrary::getOrBuild(const RockFractureSettings& settings) {
    RockFractureSettings sanitized = settings;
    RockFractureScene::sanitize(sanitized);

    TileCacheKey key{
        sanitized.kind,
        sanitized.seed,
        sanitized.tileSize,
        sanitized.poissonRadius,
        sanitized.poissonTries,
        sanitized.blockSmoothingRadius,
        sanitized.bvhTransitionRadius,
        sanitized.useTextureWarp,
    };

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_hasEntry && m_cachedKey == key && m_cachedEntry.build.sdfRoot != nullptr) {
            spdlog::info("TileLibrary: cache hit ({})", tileCacheKeyString(key));
            return m_cachedEntry;
        }

        if (m_hasEntry) {
            releaseTileSdf(m_cachedEntry.build.sdfRoot);
            m_cachedEntry = {};
            m_hasEntry = false;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    TileBuildResult build = buildFractureTile(sanitized);
    const auto t1 = std::chrono::steady_clock::now();

    TileCacheEntry entry;
    entry.build = std::move(build);
    entry.buildSeconds = std::chrono::duration<double>(t1 - t0).count();

    spdlog::info(
        "TileLibrary: built tile ({}) clusters={} build={:.2f}s",
        tileCacheKeyString(key),
        entry.build.clusters.size(),
        entry.buildSeconds);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_cachedKey = key;
    m_cachedEntry = entry;
    m_hasEntry = true;
    return m_cachedEntry;
}

} // namespace render_playground
