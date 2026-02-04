#include "game_data/map.h"

#include <fstream>

#include <magic_enum/magic_enum.hpp>
#include <nlohmann/json.hpp>

namespace game_data {
namespace fs = std::filesystem;

Map Map::load(const fs::path& mapJsonPath) {
    std::ifstream file(mapJsonPath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open map json: " + mapJsonPath.string());
    }

    nlohmann::json j;
    file >> j;

    Map map;
    for (const LayerType layerType : magic_enum::enum_values<LayerType>()) {
        const std::string layerName(magic_enum::enum_name(layerType));
        if (!j.contains(layerName) || !j[layerName].is_array()) {
            map.layers[layerType] = {};
            continue;
        }

        map.layers[layerType] = j[layerName].get<std::vector<GameObject>>();
    }
    return map;
}

const std::vector<GameObject>& Map::layer(LayerType type) const {
    static const std::vector<GameObject> empty;
    auto it = layers.find(type);
    if (it == layers.end()) {
        return empty;
    }
    return it->second;
}

} // namespace game_data

