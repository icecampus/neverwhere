#pragma once

#include "IvtScene.h"

namespace ivt_view {

struct WorldGridParams {
    float extent = 20.0f;
    float cellSize = 1.0f;
    int majorEvery = 5;
    float groundY = 0.0f;
};

WorldGridParams computeWorldGridParams(const IvtModel& model, const Vec3& center);

} // namespace ivt_view
