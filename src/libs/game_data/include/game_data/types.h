#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace game_data {

// Matches numeric encoding in existing map.json ("type": 0/1/...)
enum class GameObjectType : int {
    Tile2D = 0,
    Landscape = 1,
    Resource = 2,
    Buildings = 3,
    Cloud = 4,
};

// Matches string keys in existing map.json ("Decoration", "BaseLandscape", "GameplayInteractive")
enum class LayerType : int {
    Decoration = 0,
    BaseLandscape = 1,
    GameplayInteractive = 2,
};

struct LandscapeData {
    std::size_t tileIndex{};
};

struct GameObject {
    glm::ivec2 position{0, 0};
    std::string assetUuid; // UUID as a string (keep runtime No-Qt)
    GameObjectType type{GameObjectType::Tile2D};

    // For MVP we only need landscapeData, but we keep the shape open.
    std::optional<LandscapeData> landscapeData;
};

inline void from_json(const nlohmann::json& j, LandscapeData& d) {
    j.at("tileIndex").get_to(d.tileIndex);
}

inline void from_json(const nlohmann::json& j, GameObject& o) {
    const auto& pos = j.at("position");
    pos.at("x").get_to(o.position.x);
    pos.at("y").get_to(o.position.y);
    j.at("assetUuid").get_to(o.assetUuid);

    int type_int = 0;
    j.at("type").get_to(type_int);
    o.type = static_cast<GameObjectType>(type_int);

    if (j.contains("landscapeData") && !j["landscapeData"].is_null()) {
        o.landscapeData = j["landscapeData"].get<LandscapeData>();
    } else {
        o.landscapeData.reset();
    }
}

} // namespace game_data

