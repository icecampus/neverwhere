#pragma once

#include "game_data/types.h"

#include <filesystem>
#include <unordered_map>
#include <vector>

namespace game_data {

struct Map {
    std::unordered_map<LayerType, std::vector<GameObject>> layers;

    static Map load(const std::filesystem::path& mapJsonPath);

    const std::vector<GameObject>& layer(LayerType type) const;
};

} // namespace game_data

