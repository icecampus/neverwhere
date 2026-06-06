#pragma once

#include "TileBuild.h"
#include "RockFractureScene.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace render_playground {

struct TileCacheKey {
    RockFractureKind kind = RockFractureKind::Equidimensional;
    int seed = 0;
    float tileSize = 20.0f;
    float poissonRadius = 0.5f;
    int poissonTries = 10000;
    double blockSmoothingRadius = 0.25;
    double bvhTransitionRadius = 0.5;
    bool useTextureWarp = true;

    bool operator==(const TileCacheKey& other) const;
};

struct TileCacheEntry {
    TileBuildResult build;
    double buildSeconds = 0.0;
};

class TileLibrary {
public:
    ~TileLibrary();

    TileCacheEntry getOrBuild(const RockFractureSettings& settings);

private:
    std::mutex m_mutex;
    TileCacheKey m_cachedKey{};
    TileCacheEntry m_cachedEntry{};
    bool m_hasEntry = false;
};

std::string tileCacheKeyString(const TileCacheKey& key);

} // namespace render_playground
