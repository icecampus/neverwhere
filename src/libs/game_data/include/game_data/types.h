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
    Fence = 5,
};

// Matches string keys in existing map.json ("Decoration", "BaseLandscape", ...)
enum class LayerType : int {
    Decoration = 0,
    BaseLandscape = 1,
    GameplayInteractive = 2,
    RaisedLandscape = 3,
    CliffLandscape = 4,
    CyclopeanLandscape = 5,
    StoneLandscape = 6,
    TextureLandscape = 7,
    TechLandscape = 8,
    MaskLandscape = 9,
    FenceLandscape = 10,
};

struct LandscapeData {
    std::size_t tileIndex{};
};

// One fence piece (fence3d asset, FenceLandscape layer): a post (kind 0,
// 1 cell) or a section (kind 1, `length` cells along (axisX,axisY)).
struct FenceData {
    int kind = 0;
    int axisX = 0;
    int axisY = 0;
    int length = 1;
};

struct GameObject {
    glm::ivec2 position{0, 0};
    std::string assetUuid; // UUID as a string (keep runtime No-Qt)
    GameObjectType type{GameObjectType::Tile2D};

    // For MVP we only need landscapeData, but we keep the shape open.
    std::optional<LandscapeData> landscapeData;
    std::optional<FenceData> fenceData;
};

inline void from_json(const nlohmann::json& j, LandscapeData& d) {
    j.at("tileIndex").get_to(d.tileIndex);
}

inline void from_json(const nlohmann::json& j, FenceData& d) {
    d.kind = j.value("kind", 0);
    d.axisX = j.value("axisX", 0);
    d.axisY = j.value("axisY", 0);
    d.length = j.value("length", 1);
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
    if (j.contains("fenceData") && !j["fenceData"].is_null()) {
        o.fenceData = j["fenceData"].get<FenceData>();
    } else {
        o.fenceData.reset();
    }
}

} // namespace game_data

