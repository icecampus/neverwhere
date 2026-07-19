#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "game_data/types.h"

namespace game_data {

struct SliceAssetData {
    std::string thumbnail;
    std::string atlas;
};

struct ImageAssetData {
    std::string imageFilename;
    float width = 1.0f; // width in map cells (screenWidth = cellWidth * width)
};

struct AssetData {
    std::filesystem::path indexPath;
    std::string uuid;
    LayerType layerType{LayerType::Decoration};
    glm::vec2 pivot{0.0f, 0.0f};

    std::optional<SliceAssetData> slice;
    std::optional<ImageAssetData> image;

    std::filesystem::path root() const { return indexPath.parent_path(); }
};

inline void from_json(const nlohmann::json& j, SliceAssetData& d) {
    j.at("thumbnail").get_to(d.thumbnail);
    j.at("atlas").get_to(d.atlas);
}

inline void from_json(const nlohmann::json& j, ImageAssetData& d) {
    j.at("imageFilename").get_to(d.imageFilename);
    j.at("width").get_to(d.width);
}

inline void from_json(const nlohmann::json& j, AssetData& a) {
    j.at("uuid").get_to(a.uuid);
    // pivot may be omitted in older assets; default (0,0)
    if (j.contains("pivot")) {
        j.at("pivot").at("x").get_to(a.pivot.x);
        j.at("pivot").at("y").get_to(a.pivot.y);
    }

    // layerType is a string, same as in base_data/assets.cpp
    std::string layerStr;
    j.at("layerType").get_to(layerStr);
    if (layerStr == "Decoration") a.layerType = LayerType::Decoration;
    else if (layerStr == "BaseLandscape") a.layerType = LayerType::BaseLandscape;
    else if (layerStr == "GameplayInteractive") a.layerType = LayerType::GameplayInteractive;

    if (j.contains("slice") && !j["slice"].is_null()) {
        a.slice = j["slice"].get<SliceAssetData>();
    }

    if (j.contains("image") && !j["image"].is_null()) {
        a.image = j["image"].get<ImageAssetData>();
    }
}

struct AssetIndexEntry {
    std::string uuid;
    LayerType layerType{LayerType::Decoration};
    glm::vec2 pivot{0.0f, 0.0f};

    // Slice (atlas) assets — landscape tiles.
    std::filesystem::path atlasPath;
    int cols = 4;
    int rows = 6;

    // Plain image assets — Tile2D sprites.
    std::filesystem::path imagePath;
    float widthCells = 1.0f;

    bool isSlice() const { return !atlasPath.empty(); }
    bool isImage() const { return !imagePath.empty(); }
};

struct AssetIndex {
    std::unordered_map<std::string, AssetIndexEntry> byUuid;

    static AssetIndex load(const std::filesystem::path& assetsRoot);
    const AssetIndexEntry* find(const std::string& uuid) const;
};

} // namespace game_data

